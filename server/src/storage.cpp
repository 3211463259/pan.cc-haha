#include "storage.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <vector>
#include <cerrno>
#include <chrono>
#include <direct.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>

// OpenSSL for SHA256
#include <openssl/evp.h>

namespace cloud {

// 文件操作辅助函数 (兼容 GCC 8.1, 避免 std::filesystem bug)
inline bool fs_exists(const std::string& p) {
    struct _stat st;
    return _stat(p.c_str(), &st) == 0;
}

inline bool fs_create_dir(const std::string& p) {
    return _mkdir(p.c_str()) == 0 || errno == EEXIST;
}

inline void fs_create_dirs(const std::string& p) {
    size_t pos = 0;
    while ((pos = p.find_first_of("/\\", pos)) != std::string::npos) {
        std::string dir = p.substr(0, pos++);
        if (!dir.empty()) _mkdir(dir.c_str());
    }
    _mkdir(p.c_str());
}

inline bool fs_remove(const std::string& p) {
    return ::remove(p.c_str()) == 0;
}

inline bool fs_rename(const std::string& from, const std::string& to) {
    return ::rename(from.c_str(), to.c_str()) == 0;
}

inline bool fs_copy(const std::string& from, const std::string& to) {
    std::ifstream src(from, std::ios::binary);
    std::ofstream dst(to, std::ios::binary);
    if (!src || !dst) return false;
    dst << src.rdbuf();
    return true;
}

// path 辅助
inline std::string fs_path_join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (a.back() == '/' || a.back() == '\\') return a + b;
    return a + "\\" + b;
}

inline std::string fs_path_join3(const std::string& a, const std::string& b, const std::string& c) {
    return fs_path_join(fs_path_join(a, b), c);
}

inline std::string fs_parent_path(const std::string& p) {
    auto pos = p.find_last_of("/\\");
    if (pos == std::string::npos) return "";
    return p.substr(0, pos);
}

struct fs_space_info {
    int64_t capacity;
    int64_t available;
};

inline fs_space_info fs_space(const std::string& p) {
    ULARGE_INTEGER free_bytes, total_bytes;
    GetDiskFreeSpaceExA(p.c_str(), &free_bytes, &total_bytes, nullptr);
    return {(int64_t)total_bytes.QuadPart, (int64_t)free_bytes.QuadPart};
}

StorageEngine::StorageEngine() = default;
StorageEngine::~StorageEngine() = default;

bool StorageEngine::Initialize(const std::string& storage_root, Database* db) {
    storage_root_ = storage_root;
    db_ = db;

    if (!fs_exists(storage_root_)) {
        fs_create_dirs(storage_root_);
    }
    fs_create_dirs(fs_path_join(storage_root_, "chunks"));
    fs_create_dirs(fs_path_join(storage_root_, "files"));
    return true;
}

std::string StorageEngine::HashToPath(const std::string& hash) {
    if (hash.length() < 4) return fs_path_join3(storage_root_, "files", hash);
    return fs_path_join3(storage_root_, "files",
                         fs_path_join(hash.substr(0, 2),
                                      fs_path_join(hash.substr(2, 2), hash)));
}

std::string StorageEngine::EnsureDir(const std::string& path) {
    fs_create_dirs(fs_parent_path(path));
    return path;
}

// === SHA256 计算 ===

std::string StorageEngine::ComputeSHA256(const char* data, size_t len) {
    EVP_MD_CTX ctx;
    EVP_MD_CTX_init(&ctx);

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;

    EVP_DigestInit_ex(&ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(&ctx, data, len);
    EVP_DigestFinal_ex(&ctx, hash, &hash_len);
    EVP_MD_CTX_cleanup(&ctx);

    std::ostringstream oss;
    for (unsigned int i = 0; i < hash_len; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return oss.str();
}

std::string StorageEngine::ComputeSHA256(std::istream& stream, size_t total_size) {
    EVP_MD_CTX ctx;
    EVP_MD_CTX_init(&ctx);

    EVP_DigestInit_ex(&ctx, EVP_sha256(), nullptr);

    const size_t buf_size = 64 * 1024;
    std::vector<char> buffer(buf_size);
    while (stream.good()) {
        stream.read(buffer.data(), buf_size);
        std::streamsize bytes_read = stream.gcount();
        if (bytes_read > 0) {
            EVP_DigestUpdate(&ctx, buffer.data(), bytes_read);
        }
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;
    EVP_DigestFinal_ex(&ctx, hash, &hash_len);
    EVP_MD_CTX_cleanup(&ctx);

    std::ostringstream oss;
    for (unsigned int i = 0; i < hash_len; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return oss.str();
}

std::string StorageEngine::ComputeSHA256(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file) return "";
    return ComputeSHA256(file);
}

// === 文件写入 ===

bool StorageEngine::WriteChunk(const std::string& upload_id, int chunk_index,
                               const char* data, size_t len) {
    std::string chunk_path = GetChunkPath(upload_id, chunk_index);
    EnsureDir(chunk_path);

    std::ofstream file(chunk_path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(data, len);
    return file.good();
}

bool StorageEngine::MergeChunks(const std::string& upload_id, int num_chunks,
                                const std::string& file_name, int64_t file_size) {
    std::string temp_path = fs_path_join3(storage_root_, "chunks", upload_id + "_merged");
    EnsureDir(temp_path);

    std::ofstream out(temp_path, std::ios::binary);
    if (!out) return false;

    for (int i = 0; i < num_chunks; i++) {
        std::string chunk_path = GetChunkPath(upload_id, i);
        std::ifstream chunk(chunk_path, std::ios::binary);
        if (!chunk) {
            out.close();
            fs_remove(temp_path);
            return false;
        }
        out << chunk.rdbuf();
    }
    out.close();

    // 计算哈希
    std::string hash = ComputeSHA256(temp_path);

    // 检查去重
    if (db_->HashExists(hash)) {
        // 秒传: 只需引用已有文件
        db_->IncrementRef(hash);
        fs_remove(temp_path);
        return true;
    }

    // 移动到最终存储位置
    std::string final_path = HashToPath(hash);
    EnsureDir(final_path);

    if (!fs_rename(temp_path, final_path)) {
        fs_copy(temp_path, final_path);
        fs_remove(temp_path);
    }

    // 注册哈希
    db_->IncrementRef(hash); // 先+1
    RegisterHash(hash, file_size, final_path);

    return true;
}

std::string StorageEngine::WriteFile(const char* data, size_t len, int64_t parent_id,
                                     const std::string& file_name, const std::string& hash_hint) {
    std::string hash = hash_hint.empty() ? ComputeSHA256(data, len) : hash_hint;

    // 去重检查
    if (db_->HashExists(hash)) {
        db_->IncrementRef(hash);
        return hash;
    }

    std::string path = HashToPath(hash);
    EnsureDir(path);

    std::ofstream file(path, std::ios::binary);
    if (!file) return "";
    file.write(data, len);
    file.close();

    RegisterHash(hash, len, path);
    return hash;
}

std::string StorageEngine::WriteFileStream(std::istream& stream, size_t total_size,
                                           int64_t parent_id, const std::string& file_name) {
    // 先写入临时文件再计算哈希
    std::string temp_path = fs_path_join3(storage_root_, "chunks", "stream_" + file_name);
    EnsureDir(temp_path);

    {
        std::ofstream out(temp_path, std::ios::binary);
        if (!out) return "";

        const size_t buf_size = 64 * 1024;
        std::vector<char> buffer(buf_size);
        size_t remaining = total_size;
        while (remaining > 0 && stream.good()) {
            size_t to_read = std::min(buf_size, remaining);
            stream.read(buffer.data(), to_read);
            std::streamsize bytes_read = stream.gcount();
            if (bytes_read > 0) {
                out.write(buffer.data(), bytes_read);
                remaining -= bytes_read;
            } else {
                break;
            }
        }
    }

    std::string hash = ComputeSHA256(temp_path);
    if (db_->HashExists(hash)) {
        db_->IncrementRef(hash);
        fs_remove(temp_path);
        return hash;
    }

    std::string final_path = HashToPath(hash);
    EnsureDir(final_path);
    if (!fs_rename(temp_path, final_path)) {
        fs_copy(temp_path, final_path);
        fs_remove(temp_path);
    }

    RegisterHash(hash, total_size, final_path);
    return hash;
}

// === 文件读取 ===

std::string StorageEngine::GetFilePath(const std::string& hash) {
    return db_->GetHashStoragePath(hash);
}

std::vector<char> StorageEngine::ReadFile(const std::string& hash) {
    std::string path = GetFilePath(hash);
    if (path.empty()) return {};

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};

    auto size = file.tellg();
    file.seekg(0);

    std::vector<char> data(size);
    file.read(data.data(), size);
    return data;
}

std::vector<char> StorageEngine::ReadFileRange(const std::string& hash, int64_t offset, size_t length) {
    std::string path = GetFilePath(hash);
    if (path.empty()) return {};

    std::ifstream file(path, std::ios::binary);
    if (!file) return {};

    file.seekg(offset);
    std::vector<char> data(length);
    file.read(data.data(), length);
    auto actual_read = file.gcount();
    data.resize(actual_read);
    return data;
}

std::unique_ptr<std::ifstream> StorageEngine::OpenFileStream(const std::string& hash) {
    std::string path = GetFilePath(hash);
    if (path.empty()) return nullptr;
    auto stream = std::make_unique<std::ifstream>(path, std::ios::binary);
    if (!stream->is_open()) return nullptr;
    return stream;
}

// === 去重 ===

bool StorageEngine::IsDuplicate(const std::string& hash) {
    return db_->HashExists(hash);
}

void StorageEngine::RegisterHash(const std::string& hash, int64_t size, const std::string& storage_path) {
    // 注意: 调用者已经调用了 db_->IncrementRef(hash)
    // 如果 hash 已存在, IncrementRef 已经处理了引用计数
    // 如果 hash 不存在, 需要插入新记录
    if (!db_->HashExists(hash)) {
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        db_->InsertHashRecord(hash, size, storage_path, now);
    }
}

// === 上传会话 ===

std::string StorageEngine::GetChunkPath(const std::string& upload_id, int chunk_index) {
    return fs_path_join3(storage_root_, "chunks", upload_id + "_" + std::to_string(chunk_index) + ".tmp");
}

std::string StorageEngine::GetUploadTempDir(const std::string& upload_id) {
    return fs_path_join(storage_root_, "chunks");
}

void StorageEngine::CleanupUpload(const std::string& upload_id) {
    std::string chunk_dir = fs_path_join(storage_root_, "chunks");
    for (int i = 0; i < 10000; i++) {
        std::string chunk_name = upload_id + "_" + std::to_string(i) + ".tmp";
        std::string chunk_path = fs_path_join(chunk_dir, chunk_name);
        if (fs_exists(chunk_path)) {
            fs_remove(chunk_path);
        } else {
            break;
        }
    }
    // 清理 merged 文件
    std::string merged = fs_path_join(chunk_dir, upload_id + "_merged");
    if (fs_exists(merged)) fs_remove(merged);
}

// === 工具 ===

int64_t StorageEngine::GetAvailableSpace(const std::string& path) {
    ULARGE_INTEGER free_bytes;
    if (GetDiskFreeSpaceExA(path.c_str(), &free_bytes, nullptr, nullptr)) {
        return (int64_t)free_bytes.QuadPart;
    }
    return -1;
}

std::string StorageEngine::FormatSize(int64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double size = (double)bytes;
    int unit = 0;
    while (size >= 1024 && unit < 4) {
        size /= 1024;
        unit++;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f %s", size, units[unit]);
    return buf;
}

} // namespace cloud
