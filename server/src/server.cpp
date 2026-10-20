#include "server.h"
#include <sstream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <direct.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>

namespace cloud {

// === 简易 JSON 构建器 ===
class JsonBuilder {
public:
    static std::string Object(const std::map<std::string, std::string>& fields) {
        std::ostringstream oss;
        oss << "{";
        bool first = true;
        for (auto& [k, v] : fields) {
            if (!first) oss << ",";
            oss << "\"" << Escape(k) << "\":" << v;
            first = false;
        }
        oss << "}";
        return oss.str();
    }

    static std::string Array(const std::vector<std::string>& items) {
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < items.size(); i++) {
            if (i > 0) oss << ",";
            oss << items[i];
        }
        oss << "]";
        return oss.str();
    }

    static std::string String(const std::string& s) {
        return "\"" + Escape(s) + "\"";
    }

    static std::string Number(int64_t n) { return std::to_string(n); }
    static std::string Bool(bool b) { return b ? "true" : "false"; }

private:
public:
    static std::string Escape(const std::string& s) {
        std::ostringstream oss;
        for (char c : s) {
            switch (c) {
                case '"': oss << "\\\""; break;
                case '\\': oss << "\\\\"; break;
                case '\n': oss << "\\n"; break;
                case '\r': oss << "\\r"; break;
                case '\t': oss << "\\t"; break;
                default: oss << c;
            }
        }
        return oss.str();
    }
};

// 路径辅助
inline std::string path_join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (a.back() == '/' || a.back() == '\\') return a + b;
    return a + "\\" + b;
}
inline std::string path_join3(const std::string& a, const std::string& b, const std::string& c) {
    return path_join(path_join(a, b), c);
}
inline bool path_exists(const std::string& p) {
    struct _stat st;
    return _stat(p.c_str(), &st) == 0;
}
inline void create_dirs(const std::string& p) {
    size_t pos = 0;
    while ((pos = p.find_first_of("/\\", pos)) != std::string::npos) {
        _mkdir(p.substr(0, pos++).c_str());
    }
    _mkdir(p.c_str());
}

// === 服务器实现 ===

CloudServer::CloudServer() = default;
CloudServer::~CloudServer() { Stop(); }

bool CloudServer::Start(const ServerConfig& config) {
    config_ = config;

    // 初始化数据库
    std::string db_path = path_join(config.storage_root, "cloud.db");
    create_dirs(config.storage_root);
    if (!db_.Initialize(db_path)) {
        if (log_cb_) log_cb_("数据库初始化失败: " + db_path);
        return false;
    }

    // 初始化存储引擎
    if (!storage_.Initialize(config.storage_root, &db_)) {
        if (log_cb_) log_cb_("存储引擎初始化失败");
        return false;
    }

    // 设置路由
    SetupRoutes();
    SetupStaticFiles();

    running_ = true;

    // 启动清理线程
    cleanup_thread_ = CreateThread(nullptr, 0,
        [](LPVOID param) -> DWORD {
            auto* server = (CloudServer*)param;
            while (server->running_) {
                server->CleanupTask();
                for (int i = 0; i < 3600 && server->running_; i++) {
                    Sleep(1000);
                }
            }
            return 0;
        }, this, 0, nullptr);

    bool ok = server_.Start(config.host, config.port);
    if (log_cb_) {
        if (ok) {
            log_cb_("服务器启动: http://" + config.host + ":" + std::to_string(config.port));
        } else {
            log_cb_("端口监听失败: " + config.host + ":" + std::to_string(config.port)
                    + " (端口可能被占用)");
        }
    }
    return ok;
}

void CloudServer::Stop() {
    running_ = false;
    server_.Stop();
    if (cleanup_thread_) {
        WaitForSingleObject(cleanup_thread_, 5000);
        CloseHandle(cleanup_thread_);
        cleanup_thread_ = nullptr;
    }
}

// === 路由设置 ===

void CloudServer::SetupRoutes() {
    // 列出目录内容
    server_.Get("/files", [this](const HttpRequest& req, HttpResponse& res) {
        HandleListFiles(req, res);
    });

    // 创建文件夹
    server_.Post("/folder", [this](const HttpRequest& req, HttpResponse& res) {
        HandleCreateFolder(req, res);
    });

    // 上传文件
    server_.Post("/upload", [this](const HttpRequest& req, HttpResponse& res) {
        HandleUpload(req, res);
    });

    // 分块上传
    server_.Post("/upload/chunk", [this](const HttpRequest& req, HttpResponse& res) {
        HandleUploadChunk(req, res);
    });

    // 下载文件
    server_.Get("/download/.*", [this](const HttpRequest& req, HttpResponse& res) {
        HandleDownload(req, res);
    });

    // 删除文件/文件夹
    server_.Delete("/files/.*", [this](const HttpRequest& req, HttpResponse& res) {
        HandleDelete(req, res);
    });

    // 重命名
    server_.Put("/rename", [this](const HttpRequest& req, HttpResponse& res) {
        HandleRename(req, res);
    });

    // 移动
    server_.Put("/move", [this](const HttpRequest& req, HttpResponse& res) {
        HandleMove(req, res);
    });

    // 复制
    server_.Post("/copy", [this](const HttpRequest& req, HttpResponse& res) {
        HandleCopy(req, res);
    });

    // 搜索
    server_.Get("/search", [this](const HttpRequest& req, HttpResponse& res) {
        HandleSearch(req, res);
    });

    // 回收站列表
    server_.Get("/recycle", [this](const HttpRequest& req, HttpResponse& res) {
        HandleRecycleList(req, res);
    });

    // 回收站还原
    server_.Post("/recycle/restore", [this](const HttpRequest& req, HttpResponse& res) {
        HandleRecycleRestore(req, res);
    });

    // 永久删除
    server_.Delete("/recycle/.*", [this](const HttpRequest& req, HttpResponse& res) {
        HandleRecyclePurge(req, res);
    });

    // 文件详情
    server_.Get("/info/.*", [this](const HttpRequest& req, HttpResponse& res) {
        HandleFileInfo(req, res);
    });

    // 文件夹详情
    server_.Get("/folder_info", [this](const HttpRequest& req, HttpResponse& res) {
        HandleFolderInfo(req, res);
    });

    // 存储信息
    server_.Get("/storage", [this](const HttpRequest& req, HttpResponse& res) {
        HandleStorageInfo(req, res);
    });

    // API 版本
    server_.Get("/version", [](const HttpRequest&, HttpResponse& res) {
        res.SetJson("{\"version\":\"10.0.0\",\"api_version\":1,\"platform\":\"server\"}");
    });

    // 批量操作 (预留)
    server_.Post("/batch", [](const HttpRequest&, HttpResponse& res) {
        res.SetJson("{\"success\":true,\"message\":\"batch api reserved\"}");
    });

    // WebDAV 预留
    server_.Get("/webdav/.*", [](const HttpRequest&, HttpResponse& res) {
        res.status = 501;
        res.SetJson("{\"error\":\"WebDAV not implemented yet\"}");
    });
}

void CloudServer::SetupStaticFiles() {
    if (path_exists(config_.web_root)) {
        server_.SetStaticRoot(config_.web_root);
    }
}

// === 时间戳 ===

int64_t CloudServer::NowTimestamp() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
}

// === JSON 辅助 ===

void CloudServer::JsonSuccess(HttpResponse& res, const std::string& data) {
    res.SetJson("{\"success\":true,\"data\":" + data + "}");
}

void CloudServer::JsonError(HttpResponse& res, int code, const std::string& msg) {
    std::string json = "{\"success\":false,\"code\":" + std::to_string(code) +
                       ",\"message\":\"" + JsonBuilder::Escape(msg) + "\"}";
    res.SetJson(json);
    res.status = code;
}

std::string CloudServer::FileEntryToJson(const FileEntry& entry) {
    return JsonBuilder::Object({
        {"id", JsonBuilder::Number(entry.id)},
        {"parent_id", JsonBuilder::Number(entry.parent_id)},
        {"name", JsonBuilder::String(entry.name)},
        {"type", JsonBuilder::String(entry.type)},
        {"size", JsonBuilder::Number(entry.size)},
        {"hash", JsonBuilder::String(entry.hash)},
        {"created_at", JsonBuilder::Number(entry.created_at)},
        {"modified_at", JsonBuilder::Number(entry.modified_at)}
    });
}

// === 简单 JSON 解析辅助 ===

static int64_t FindInt64(const std::string& s, const std::string& key) {
    auto pos = s.find("\"" + key + "\"");
    if (pos == std::string::npos) return 0;
    pos = s.find(":", pos);
    if (pos == std::string::npos) return 0;
    pos++;
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) pos++;
    return std::stoll(s.substr(pos));
}

static std::string FindString(const std::string& s, const std::string& key) {
    auto pos = s.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    pos = s.find(":", pos);
    if (pos == std::string::npos) return "";
    pos = s.find("\"", pos + 1);
    if (pos == std::string::npos) return "";
    auto end = s.find("\"", pos + 1);
    if (end == std::string::npos) return "";
    return s.substr(pos + 1, end - pos - 1);
}

// === API 实现 ===

void CloudServer::HandleListFiles(const HttpRequest& req, HttpResponse& res) {
    int64_t parent_id = 0;
    std::string pid_str = req.GetParam("parent_id");
    if (!pid_str.empty()) parent_id = std::stoll(pid_str);

    std::string sort_by = req.GetParam("sort_by");
    std::string sort_order = req.GetParam("sort_order");

    Database::SortBy sb = Database::SortBy::NAME;
    if (sort_by == "size") sb = Database::SortBy::SIZE;
    else if (sort_by == "created") sb = Database::SortBy::CREATED_TIME;
    else if (sort_by == "modified") sb = Database::SortBy::MODIFIED_TIME;

    Database::SortOrder so = (sort_order == "desc") ? Database::SortOrder::DESC : Database::SortOrder::ASC;

    auto entries = db_.ListDirectorySorted(parent_id, sb, so);

    std::vector<std::string> items;
    for (auto& e : entries) {
        items.push_back(FileEntryToJson(e));
    }

    JsonSuccess(res, JsonBuilder::Array(items));
}

void CloudServer::HandleCreateFolder(const HttpRequest& req, HttpResponse& res) {
    int64_t parent_id = FindInt64(req.body, "parent_id");
    std::string name = FindString(req.body, "name");

    if (name.empty()) {
        JsonError(res, 400, "文件夹名称不能为空");
        return;
    }

    int64_t id = db_.CreateFolder(parent_id, name, NowTimestamp());
    if (id < 0) {
        JsonError(res, 500, "创建文件夹失败");
        return;
    }

    JsonSuccess(res, JsonBuilder::Object({{"id", JsonBuilder::Number(id)}}));
}

void CloudServer::HandleUpload(const HttpRequest& req, HttpResponse& res) {
    int64_t parent_id = 0;
    std::string pid_str = req.GetParam("parent_id");
    if (!pid_str.empty()) parent_id = std::stoll(pid_str);

    // 检查是否为分块上传
    std::string upload_id = req.GetHeader("X-Upload-Id");
    if (!upload_id.empty()) {
        HandleUploadChunk(req, res);
        return;
    }

    // 普通上传 (multipart)
    if (req.files.empty()) {
        JsonError(res, 400, "没有文件");
        return;
    }

    auto& file = req.files[0];
    std::string file_name = file.filename;
    if (file_name.empty()) {
        file_name = req.GetParam("filename");
        if (file_name.empty()) file_name = "unnamed";
    }

    const char* data = file.data.c_str();
    size_t len = file.data.size();

    // 计算哈希
    std::string hash = StorageEngine::ComputeSHA256(data, len);

    // 检查去重
    if (db_.HashExists(hash)) {
        db_.IncrementRef(hash);
        int64_t id = db_.AddFile(parent_id, file_name, len, hash, NowTimestamp());
        JsonSuccess(res, JsonBuilder::Object({
            {"id", JsonBuilder::Number(id)},
            {"dedup", JsonBuilder::Bool(true)}
        }));
        return;
    }

    // 写入存储
    std::string stored_hash = storage_.WriteFile(data, len, parent_id, file_name, hash);
    if (stored_hash.empty()) {
        JsonError(res, 500, "写入文件失败");
        return;
    }

    int64_t id = db_.AddFile(parent_id, file_name, len, stored_hash, NowTimestamp());
    if (id < 0) {
        JsonError(res, 500, "添加文件记录失败");
        return;
    }

    JsonSuccess(res, JsonBuilder::Object({
        {"id", JsonBuilder::Number(id)},
        {"dedup", JsonBuilder::Bool(false)}
    }));
}

void CloudServer::HandleUploadChunk(const HttpRequest& req, HttpResponse& res) {
    std::string upload_id = req.GetHeader("X-Upload-Id");
    int chunk_index = 0;
    std::string ci_str = req.GetHeader("X-Chunk-Index");
    if (!ci_str.empty()) chunk_index = std::stoi(ci_str);

    if (upload_id.empty()) {
        JsonError(res, 400, "缺少 Upload-Id");
        return;
    }

    auto session = db_.GetUploadSession(upload_id);
    if (!session) {
        JsonError(res, 404, "上传会话不存在或已过期");
        return;
    }

    // 写入分块
    if (!storage_.WriteChunk(upload_id, chunk_index, req.body.c_str(), req.body.size())) {
        JsonError(res, 500, "写入分块失败");
        return;
    }

    // 更新进度
    int64_t uploaded_size = session->uploaded_size + req.body.size();
    db_.UpdateUploadProgress(upload_id, uploaded_size, chunk_index, "");

    // 检查是否全部上传完成
    int total_chunks = (session->file_size + session->chunk_size - 1) / session->chunk_size;
    auto updated_session = db_.GetUploadSession(upload_id);

    bool all_received = true;
    int received_count = 0;
    if (updated_session) {
        for (bool received : updated_session->received_chunks) {
            if (received) received_count++;
            else { all_received = false; break; }
        }
    }

    if (all_received && received_count >= total_chunks) {
        storage_.MergeChunks(upload_id, total_chunks, session->file_name, session->file_size);

        std::string merged_path = path_join3(config_.storage_root, "chunks", upload_id + "_merged");
        std::string hash;
        if (path_exists(merged_path)) {
            hash = StorageEngine::ComputeSHA256(merged_path);
        }

        int64_t id = db_.AddFile(session->parent_id, session->file_name,
                                  session->file_size, hash, NowTimestamp());

        db_.DeleteUploadSession(upload_id);
        storage_.CleanupUpload(upload_id);

        JsonSuccess(res, JsonBuilder::Object({
            {"id", JsonBuilder::Number(id)},
            {"completed", JsonBuilder::Bool(true)}
        }));
    } else {
        JsonSuccess(res, JsonBuilder::Object({
            {"chunk_index", JsonBuilder::Number(chunk_index)},
            {"received", JsonBuilder::Number(received_count)},
            {"total", JsonBuilder::Number(total_chunks)},
            {"completed", JsonBuilder::Bool(false)}
        }));
    }
}

void CloudServer::HandleDownload(const HttpRequest& req, HttpResponse& res) {
    // 从路径提取文件ID: /download/{id}
    std::string path = req.path;
    auto pos = path.find("/download/");
    if (pos == std::string::npos) {
        JsonError(res, 400, "无效的下载路径");
        return;
    }

    int64_t file_id = std::stoll(path.substr(pos + 10));

    auto entry = db_.GetEntry(file_id);
    if (!entry || entry->type != FILE_TYPE_FILE) {
        JsonError(res, 404, "文件不存在");
        return;
    }

    std::string file_path = storage_.GetFilePath(entry->hash);
    if (file_path.empty() || !path_exists(file_path)) {
        JsonError(res, 404, "文件数据丢失");
        return;
    }

    // 获取文件大小
    struct _stat st;
    _stat(file_path.c_str(), &st);
    int64_t file_size = st.st_size;

    // 支持 Range 请求
    int64_t start = 0;
    int64_t end = file_size - 1;

    std::string range = req.GetHeader("Range");
    if (!range.empty()) {
        auto eq = range.find('=');
        auto dash = range.find('-');
        if (eq != std::string::npos && dash != std::string::npos) {
            start = std::stoll(range.substr(eq + 1, dash - eq - 1));
            std::string end_str = range.substr(dash + 1);
            if (!end_str.empty()) {
                end = std::stoll(end_str);
            }
        }
        res.status = 206;
    }

    int64_t content_length = end - start + 1;

    res.SetHeader("Content-Type", "application/octet-stream");
    res.SetHeader("Content-Disposition", "attachment; filename=\"" + entry->name + "\"");
    res.SetHeader("Accept-Ranges", "bytes");

    if (res.status == 206) {
        res.SetHeader("Content-Range", "bytes " + std::to_string(start) + "-" +
                      std::to_string(end) + "/" + std::to_string(file_size));
    }

    // 流式传输
    res.is_streaming = true;
    res.content_length = (size_t)content_length;

    std::ifstream* file_stream = new std::ifstream(file_path, std::ios::binary);
    file_stream->seekg(start);

    res.content_provider = [file_stream, content_length](size_t offset, std::vector<char>& buffer, size_t& bytes_provided) mutable {
        int64_t remaining = content_length - offset;
        if (remaining <= 0) {
            file_stream->close();
            delete file_stream;
            bytes_provided = 0;
            return;
        }
        size_t to_read = (size_t)std::min<int64_t>(remaining, buffer.size());
        file_stream->read(buffer.data(), to_read);
        bytes_provided = (size_t)file_stream->gcount();
        if (bytes_provided == 0) {
            file_stream->close();
            delete file_stream;
        }
    };
}

void CloudServer::HandleDelete(const HttpRequest& req, HttpResponse& res) {
    std::string path = req.path;
    auto pos = path.find("/files/");
    if (pos == std::string::npos) {
        JsonError(res, 400, "无效路径");
        return;
    }

    int64_t id = std::stoll(path.substr(pos + 7));
    if (!db_.SoftDelete(id, NowTimestamp())) {
        JsonError(res, 500, "删除失败");
        return;
    }

    JsonSuccess(res);
}

void CloudServer::HandleRename(const HttpRequest& req, HttpResponse& res) {
    int64_t id = FindInt64(req.body, "id");
    std::string new_name = FindString(req.body, "name");

    if (!db_.Rename(id, new_name, NowTimestamp())) {
        JsonError(res, 500, "重命名失败");
        return;
    }

    JsonSuccess(res);
}

void CloudServer::HandleMove(const HttpRequest& req, HttpResponse& res) {
    int64_t id = FindInt64(req.body, "id");
    int64_t target_parent_id = FindInt64(req.body, "target_parent_id");

    if (!db_.Move(id, target_parent_id, NowTimestamp())) {
        JsonError(res, 500, "移动失败");
        return;
    }

    JsonSuccess(res);
}

void CloudServer::HandleCopy(const HttpRequest& req, HttpResponse& res) {
    int64_t id = FindInt64(req.body, "id");
    int64_t target_parent_id = FindInt64(req.body, "target_parent_id");

    auto entry = db_.GetEntry(id);
    if (!entry) {
        JsonError(res, 404, "文件不存在");
        return;
    }

    int64_t new_id = -1;
    if (entry->type == FILE_TYPE_DIR) {
        new_id = db_.CopyFolder(id, target_parent_id, NowTimestamp());
    } else {
        new_id = db_.CopyFile(id, target_parent_id, NowTimestamp());
    }

    if (new_id < 0) {
        JsonError(res, 500, "复制失败");
        return;
    }

    JsonSuccess(res, JsonBuilder::Object({{"id", JsonBuilder::Number(new_id)}}));
}

void CloudServer::HandleSearch(const HttpRequest& req, HttpResponse& res) {
    SearchFilter filter;
    std::string q = req.GetParam("q");
    if (!q.empty()) filter.keyword = q;

    std::string type_str = req.GetParam("type");
    if (!type_str.empty()) filter.type_filter = type_str;

    std::string ms = req.GetParam("min_size");
    if (!ms.empty()) filter.min_size = std::stoll(ms);

    std::string xs = req.GetParam("max_size");
    if (!xs.empty()) filter.max_size = std::stoll(xs);

    int limit = 1000;
    std::string lim_str = req.GetParam("limit");
    if (!lim_str.empty()) limit = std::stoi(lim_str);

    auto results = db_.Search(filter, limit);

    std::vector<std::string> items;
    for (auto& e : results) {
        items.push_back(FileEntryToJson(e));
    }

    JsonSuccess(res, JsonBuilder::Array(items));
}

void CloudServer::HandleRecycleList(const HttpRequest& req, HttpResponse& res) {
    auto items = db_.ListRecycleBin();
    std::vector<std::string> json_items;
    for (auto& e : items) {
        json_items.push_back(FileEntryToJson(e));
    }
    JsonSuccess(res, JsonBuilder::Array(json_items));
}

void CloudServer::HandleRecycleRestore(const HttpRequest& req, HttpResponse& res) {
    int64_t id = FindInt64(req.body, "id");
    int64_t target_parent_id = FindInt64(req.body, "target_parent_id");

    if (!db_.Restore(id, target_parent_id, NowTimestamp())) {
        JsonError(res, 500, "还原失败");
        return;
    }

    JsonSuccess(res);
}

void CloudServer::HandleRecyclePurge(const HttpRequest& req, HttpResponse& res) {
    std::string path = req.path;
    auto pos = path.find("/recycle/");
    if (pos != std::string::npos) {
        int64_t id = std::stoll(path.substr(pos + 9));
        db_.HardDelete(id);
    }
    JsonSuccess(res);
}

void CloudServer::HandleFileInfo(const HttpRequest& req, HttpResponse& res) {
    std::string path = req.path;
    auto pos = path.find("/info/");
    if (pos == std::string::npos) {
        JsonError(res, 400, "无效路径");
        return;
    }

    int64_t id = std::stoll(path.substr(pos + 6));
    auto entry = db_.GetEntry(id);

    if (!entry) {
        JsonError(res, 404, "文件不存在");
        return;
    }

    JsonSuccess(res, FileEntryToJson(*entry));
}

void CloudServer::HandleFolderInfo(const HttpRequest& req, HttpResponse& res) {
    int64_t folder_id = 0;
    std::string fid = req.GetParam("id");
    if (!fid.empty()) folder_id = std::stoll(fid);

    auto info = db_.GetFolderInfo(folder_id);

    JsonSuccess(res, JsonBuilder::Object({
        {"total_size", JsonBuilder::Number(info.total_size)},
        {"folder_count", JsonBuilder::Number(info.folder_count)},
        {"file_count", JsonBuilder::Number(info.file_count)},
        {"formatted_size", JsonBuilder::String(StorageEngine::FormatSize(info.total_size))}
    }));
}

void CloudServer::HandleStorageInfo(const HttpRequest& req, HttpResponse& res) {
    ULARGE_INTEGER total_bytes, free_bytes;
    GetDiskFreeSpaceExA(config_.storage_root.c_str(), &free_bytes, &total_bytes, nullptr);
    int64_t total_space = (int64_t)total_bytes.QuadPart;
    int64_t free_space = (int64_t)free_bytes.QuadPart;
    int64_t used_space = total_space - free_space;

    JsonSuccess(res, JsonBuilder::Object({
        {"total_space", JsonBuilder::Number(total_space)},
        {"used_space", JsonBuilder::Number(used_space)},
        {"free_space", JsonBuilder::Number(free_space)},
        {"formatted_total", JsonBuilder::String(StorageEngine::FormatSize(total_space))},
        {"formatted_used", JsonBuilder::String(StorageEngine::FormatSize(used_space))},
        {"formatted_free", JsonBuilder::String(StorageEngine::FormatSize(free_space))}
    }));
}

void CloudServer::CleanupTask() {
    auto purged = db_.PurgeExpiredRecycleBin(NowTimestamp(), config_.recycle_expire_days);
    if (!purged.empty() && log_cb_) {
        log_cb_("回收站自动清理 " + std::to_string(purged.size()) + " 个过期项目");
    }
}

} // namespace cloud
