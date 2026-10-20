#include "http_server.h"
#include <algorithm>
#include <cctype>

#pragma comment(lib, "ws2_32.lib")

namespace cloud {

extern HttpServer* g_current_server;  // 定义在文件后部, AcceptLoop 需前置引用

HttpServer::HttpServer() {
    WSADATA wsa;
    winsock_initialized_ = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
}

HttpServer::~HttpServer() {
    Stop();
    if (winsock_initialized_) {
        WSACleanup();
    }
}

bool HttpServer::Start(const std::string& host, int port) {
    if (!winsock_initialized_) return false;

    listen_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket_ == INVALID_SOCKET) return false;

    int opt = 1;
    setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = inet_addr(host.c_str());

    if (bind(listen_socket_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(listen_socket_);
        return false;
    }

    if (listen(listen_socket_, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(listen_socket_);
        return false;
    }

    running_ = true;
    accept_thread_ = CreateThread(nullptr, 0,
        [](LPVOID param) -> DWORD {
            ((HttpServer*)param)->AcceptLoop();
            return 0;
        }, this, 0, nullptr);

    return true;
}

void HttpServer::Stop() {
    running_ = false;
    if (listen_socket_ != INVALID_SOCKET) {
        closesocket(listen_socket_);
        listen_socket_ = INVALID_SOCKET;
    }
    if (accept_thread_) {
        WaitForSingleObject(accept_thread_, 5000);
        CloseHandle(accept_thread_);
        accept_thread_ = nullptr;
    }
}

void HttpServer::AcceptLoop() {
    g_current_server = this;  // 修复: 工作线程回调在 HandleClient 之前就检查此指针,
                              // 原先只在 HandleClient 开头赋值, 首个请求必然为 null
                              // 导致连接被直接关闭 (客户端表现为 Connection reset)
    while (running_) {
        sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);
        SOCKET client = accept(listen_socket_, (sockaddr*)&client_addr, &addr_len);
        if (client == INVALID_SOCKET) continue;

        // 每个连接用独立线程处理
        SOCKET* client_ptr = new SOCKET(client);
        CreateThread(nullptr, 0,
            [](LPVOID param) -> DWORD {
                SOCKET client = *(SOCKET*)param;
                delete (SOCKET*)param;

                // 获取调用者
                extern HttpServer* g_current_server;
                if (g_current_server) {
                    g_current_server->HandleClient(client);
                }
                closesocket(client);
                return 0;
            }, client_ptr, 0, nullptr);
    }
}

void HttpServer::Get(const std::string& path, Handler handler) {
    LockGuard<Mutex> lock(mutex_);
    if (path.find(".*") != std::string::npos) {
        std::string prefix = path.substr(0, path.find(".*"));
        wildcard_routes_.push_back({"GET", prefix, handler});
    } else {
        routes_.push_back({"GET", path, handler});
    }
}

void HttpServer::Post(const std::string& path, Handler handler) {
    LockGuard<Mutex> lock(mutex_);
    if (path.find(".*") != std::string::npos) {
        std::string prefix = path.substr(0, path.find(".*"));
        wildcard_routes_.push_back({"POST", prefix, handler});
    } else {
        routes_.push_back({"POST", path, handler});
    }
}

void HttpServer::Put(const std::string& path, Handler handler) {
    LockGuard<Mutex> lock(mutex_);
    routes_.push_back({"PUT", path, handler});
}

void HttpServer::Delete(const std::string& path, Handler handler) {
    LockGuard<Mutex> lock(mutex_);
    if (path.find(".*") != std::string::npos) {
        std::string prefix = path.substr(0, path.find(".*"));
        wildcard_routes_.push_back({"DELETE", prefix, handler});
    } else {
        routes_.push_back({"DELETE", path, handler});
    }
}

void HttpServer::SetStaticRoot(const std::string& path) {
    static_root_ = path;
}

// 全局指针用于线程回调
HttpServer* g_current_server = nullptr;

void HttpServer::HandleClient(SOCKET client) {
    g_current_server = this;

    // 临时诊断
    FILE* dbg = fopen("http_debug.txt", "a");
    if (dbg) { fprintf(dbg, "[1] enter\n"); fclose(dbg); }

    auto req = ParseRequest(client);

    if (dbg) { dbg = fopen("http_debug.txt", "a"); if (dbg) { fprintf(dbg, "[2] parsed method=%s path=%s\n", req.method.c_str(), req.path.c_str()); fclose(dbg); } }

    // 剥离 /api 前缀: 前端与客户端 API 统一走 /api/*, 服务端路由按根路径注册
    if (req.path.rfind("/api/", 0) == 0) {
        req.path = req.path.substr(4);
    } else if (req.path == "/api") {
        req.path = "/";
    }

    // CORS preflight
    if (req.method == "OPTIONS") {
        HttpResponse res;
        res.SetHeader("Access-Control-Allow-Origin", "*");
        res.SetHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.SetHeader("Access-Control-Allow-Headers", "Content-Type, X-Upload-Id, X-Chunk-Index, X-File-Hash");
        res.SetHeader("Access-Control-Max-Age", "86400");
        res.status = 204;
        SendResponse(client, res);
        return;
    }

    HttpResponse res;
    res.SetHeader("Access-Control-Allow-Origin", "*");

    // 查找路由
    Handler* handler = nullptr;
    {
        LockGuard<Mutex> lock(mutex_);

        // 精确匹配
        for (auto& r : routes_) {
            if (r.method == req.method && r.path == req.path) {
                handler = &r.handler;
                break;
            }
        }

        // 通配符匹配
        if (!handler) {
            for (auto& r : wildcard_routes_) {
                if (r.method == req.method && req.path.find(r.prefix) == 0) {
                    handler = &r.handler;
                    break;
                }
            }
        }
    }

    if (handler) {
        (*handler)(req, res);
    } else if (req.method == "GET" && !static_root_.empty()) {
        SendStaticFile(client, req.path);
        return;
    } else {
        res.status = 404;
        res.SetJson("{\"error\":\"Not Found\"}");
    }

    if (dbg) { dbg = fopen("http_debug.txt", "a"); if (dbg) { fprintf(dbg, "[3] handler done, status=%d\n", res.status); fclose(dbg); } }

    SendResponse(client, res);

    if (dbg) { dbg = fopen("http_debug.txt", "a"); if (dbg) { fprintf(dbg, "[4] response sent\n"); fclose(dbg); } }
}

// URL 解码
static std::string UrlDecode(const std::string& s) {
    std::string result;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            char hex[3] = {s[i+1], s[i+2], 0};
            result += (char)strtol(hex, nullptr, 16);
            i += 2;
        } else if (s[i] == '+') {
            result += ' ';
        } else {
            result += s[i];
        }
    }
    return result;
}

// 解析查询参数
static std::map<std::string, std::string> ParseQuery(const std::string& query) {
    std::map<std::string, std::string> params;
    std::istringstream iss(query);
    std::string pair;
    while (std::getline(iss, pair, '&')) {
        auto eq = pair.find('=');
        if (eq != std::string::npos) {
            params[UrlDecode(pair.substr(0, eq))] = UrlDecode(pair.substr(eq + 1));
        }
    }
    return params;
}

// 查找子串 (二进制安全)
static const char* FindSubstr(const char* data, size_t len, const char* pattern) {
    size_t pat_len = strlen(pattern);
    if (pat_len > len) return nullptr;
    for (size_t i = 0; i <= len - pat_len; i++) {
        if (memcmp(data + i, pattern, pat_len) == 0) {
            return data + i;
        }
    }
    return nullptr;
}

HttpRequest HttpServer::ParseRequest(SOCKET client) {
    HttpRequest req;

    // 读取请求头
    std::vector<char> header_buf;
    char buf[BUFFER_SIZE];
    bool header_done = false;
    int content_length = 0;
    std::string header_data;

    while (!header_done) {
        int received = recv(client, buf, BUFFER_SIZE, 0);
        if (received <= 0) break;

        header_buf.insert(header_buf.end(), buf, buf + received);
        header_data.assign(header_buf.data(), header_buf.size());

        auto end = header_data.find("\r\n\r\n");
        if (end != std::string::npos) {
            header_done = true;

            // 解析 Content-Length
            auto cl_pos = header_data.find("Content-Length:");
            if (cl_pos == std::string::npos) cl_pos = header_data.find("content-length:");
            if (cl_pos != std::string::npos) {
                auto cl_end = header_data.find("\r\n", cl_pos);
                std::string cl_str = header_data.substr(cl_pos + 15, cl_end - cl_pos - 15);
                // 去空格
                while (!cl_str.empty() && cl_str[0] == ' ') cl_str.erase(0, 1);
                content_length = std::stoi(cl_str);
            }
        }
    }

    if (header_buf.empty()) return req;

    // 解析请求行
    std::string header_str(header_buf.data(), header_buf.size());
    auto first_line_end = header_str.find("\r\n");
    if (first_line_end == std::string::npos) return req;

    std::string request_line = header_str.substr(0, first_line_end);
    auto space1 = request_line.find(' ');
    auto space2 = request_line.find(' ', space1 + 1);

    if (space1 == std::string::npos || space2 == std::string::npos) return req;

    req.method = request_line.substr(0, space1);
    std::string full_path = request_line.substr(space1 + 1, space2 - space1 - 1);

    // 分离路径和查询参数
    auto qmark = full_path.find('?');
    if (qmark != std::string::npos) {
        req.path = full_path.substr(0, qmark);
        req.query_params = ParseQuery(full_path.substr(qmark + 1));
    } else {
        req.path = full_path;
    }

    // 解析请求头
    size_t pos = first_line_end + 2;
    while (pos < header_str.size()) {
        auto line_end = header_str.find("\r\n", pos);
        if (line_end == std::string::npos || line_end == pos) break;
        std::string line = header_str.substr(pos, line_end - pos);
        auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            while (!value.empty() && value[0] == ' ') value.erase(0, 1);
            req.headers[key] = value;
        }
        pos = line_end + 2;
    }

    // 读取请求体
    if (content_length > 0) {
        // 检查是否已有部分 body 在 header_buf 中
        auto header_end = header_str.find("\r\n\r\n");
        size_t body_already_read = 0;
        if (header_end != std::string::npos && header_end + 4 < header_buf.size()) {
            body_already_read = header_buf.size() - (header_end + 4);
        }

        req.body.resize(content_length);
        if (body_already_read > 0) {
            size_t copy_len = std::min(body_already_read, (size_t)content_length);
            memcpy(&req.body[0], header_buf.data() + header_buf.size() - body_already_read, copy_len);
        }

        size_t total_read = body_already_read;
        while (total_read < (size_t)content_length) {
            int to_read = std::min(BUFFER_SIZE, (int)(content_length - total_read));
            int received = recv(client, &req.body[total_read], to_read, 0);
            if (received <= 0) break;
            total_read += received;
        }
        req.body.resize(total_read);

        // 解析 multipart
        std::string content_type = req.GetHeader("Content-Type");
        if (content_type.find("multipart/form-data") != std::string::npos) {
            // 提取 boundary
            auto bpos = content_type.find("boundary=");
            if (bpos != std::string::npos) {
                std::string boundary = content_type.substr(bpos + 9);
                // 去掉可能的引号
                if (boundary.front() == '"') boundary = boundary.substr(1);
                if (boundary.back() == '"') boundary.pop_back();
                boundary = "--" + boundary;

                // 分割 multipart
                size_t pos = 0;
                while (pos < req.body.size()) {
                    auto part_start = req.body.find(boundary, pos);
                    if (part_start == std::string::npos) break;
                    part_start += boundary.size() + 2; // skip boundary + \r\n

                    auto part_end = req.body.find(boundary, part_start);
                    if (part_end == std::string::npos) break;

                    std::string part = req.body.substr(part_start, part_end - part_start - 2); // -2 for \r\n

                    // 解析 part 头
                    auto header_end = part.find("\r\n\r\n");
                    if (header_end == std::string::npos) break;

                    std::string part_headers = part.substr(0, header_end);
                    std::string part_body = part.substr(header_end + 4);

                    HttpRequest::UploadedFile file;

                    // 提取 name 和 filename
                    auto disp_pos = part_headers.find("Content-Disposition:");
                    if (disp_pos != std::string::npos) {
                        auto disp_end = part_headers.find("\r\n", disp_pos);
                        std::string disp = part_headers.substr(disp_pos, disp_end - disp_pos);

                        auto name_pos = disp.find("name=\"");
                        if (name_pos != std::string::npos) {
                            auto name_end = disp.find("\"", name_pos + 6);
                            file.name = disp.substr(name_pos + 6, name_end - name_pos - 6);
                        }

                        auto fname_pos = disp.find("filename=\"");
                        if (fname_pos != std::string::npos) {
                            auto fname_end = disp.find("\"", fname_pos + 10);
                            file.filename = disp.substr(fname_pos + 10, fname_end - fname_pos - 10);
                        }
                    }

                    // 提取 Content-Type
                    auto ct_pos = part_headers.find("Content-Type:");
                    if (ct_pos != std::string::npos) {
                        auto ct_end = part_headers.find("\r\n", ct_pos);
                        file.content_type = part_headers.substr(ct_pos + 13, ct_end - ct_pos - 13);
                        while (!file.content_type.empty() && file.content_type[0] == ' ')
                            file.content_type.erase(0, 1);
                    }

                    file.data = part_body;
                    req.files.push_back(file);

                    pos = part_end;
                }
            }
        }
    }

    return req;
}

void HttpServer::SendResponse(SOCKET client, const HttpResponse& res) {
    std::ostringstream oss;

    // 状态行
    const char* status_text = "OK";
    switch (res.status) {
        case 200: status_text = "OK"; break;
        case 204: status_text = "No Content"; break;
        case 206: status_text = "Partial Content"; break;
        case 400: status_text = "Bad Request"; break;
        case 404: status_text = "Not Found"; break;
        case 500: status_text = "Internal Server Error"; break;
        case 501: status_text = "Not Implemented"; break;
    }
    oss << "HTTP/1.1 " << res.status << " " << status_text << "\r\n";

    // 默认头
    if (res.headers.find("Content-Type") == res.headers.end()) {
        oss << "Content-Type: application/json; charset=utf-8\r\n";
    }

    // 流式响应
    if (res.is_streaming) {
        oss << "Content-Length: " << res.content_length << "\r\n";
        for (auto& h : res.headers) {
            oss << h.first << ": " << h.second << "\r\n";
        }
        oss << "\r\n";

        std::string header_str = oss.str();
        send(client, header_str.c_str(), (int)header_str.size(), 0);

        // 流式发送
        size_t offset = 0;
        std::vector<char> buffer(256 * 1024);
        while (offset < res.content_length) {
            size_t bytes_provided = 0;
            res.content_provider(offset, buffer, bytes_provided);
            if (bytes_provided == 0) break;
            send(client, buffer.data(), (int)bytes_provided, 0);
            offset += bytes_provided;
        }
    } else {
        // 普通响应
        std::string body = res.body;
        oss << "Content-Length: " << body.size() << "\r\n";

        for (auto& h : res.headers) {
            oss << h.first << ": " << h.second << "\r\n";
        }
        oss << "\r\n";
        oss << body;

        std::string response = oss.str();
        send(client, response.c_str(), (int)response.size(), 0);
    }
}

void HttpServer::SendStaticFile(SOCKET client, const std::string& path) {
    std::string file_path = static_root_;
    if (path == "/" || path.empty()) {
        file_path += "/index.html";
    } else {
        file_path += path;
    }

    // 安全检查: 防止路径穿越
    if (file_path.find("..") != std::string::npos) {
        HttpResponse res;
        res.status = 403;
        res.SetJson("{\"error\":\"Forbidden\"}");
        SendResponse(client, res);
        return;
    }

    std::ifstream file(file_path, std::ios::binary | std::ios::ate);
    if (!file) {
        HttpResponse res;
        res.status = 404;
        res.SetJson("{\"error\":\"File not found\"}");
        SendResponse(client, res);
        return;
    }

    auto size = file.tellg();
    file.seekg(0);

    // 判断 Content-Type
    std::string content_type = "application/octet-stream";
    auto dot = file_path.rfind('.');
    if (dot != std::string::npos) {
        std::string ext = file_path.substr(dot);
        for (auto& c : ext) c = tolower(c);
        if (ext == ".html") content_type = "text/html; charset=utf-8";
        else if (ext == ".css") content_type = "text/css";
        else if (ext == ".js") content_type = "application/javascript";
        else if (ext == ".json") content_type = "application/json";
        else if (ext == ".png") content_type = "image/png";
        else if (ext == ".jpg" || ext == ".jpeg") content_type = "image/jpeg";
        else if (ext == ".svg") content_type = "image/svg+xml";
        else if (ext == ".ico") content_type = "image/x-icon";
        else if (ext == ".woff2") content_type = "font/woff2";
    }

    HttpResponse res;
    res.SetHeader("Content-Type", content_type);
    res.is_streaming = true;
    res.content_length = (size_t)size;

    std::ifstream* file_ptr = new std::ifstream(std::move(file));
    res.content_provider = [file_ptr](size_t offset, std::vector<char>& buffer, size_t& bytes_provided) {
        file_ptr->seekg(offset);
        file_ptr->read(buffer.data(), buffer.size());
        bytes_provided = (size_t)file_ptr->gcount();
        if (bytes_provided == 0) {
            file_ptr->close();
            delete file_ptr;
        }
    };

    SendResponse(client, res);
}

} // namespace cloud
