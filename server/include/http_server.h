/**
 * @file http_server.h
 * @brief 轻量级 HTTP 服务器 (基于 Winsock2)
 *
 * 不依赖 cpp-httplib, 兼容 MinGW 8.1 win32 线程模型
 * 支持: GET, POST, PUT, DELETE, OPTIONS
 * 支持: multipart 上传, Range 请求, 流式响应, 静态文件服务
 */

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <string>
#include <map>
#include <functional>
#include <vector>
#include <sstream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include "thread_safe.h"

namespace cloud {

// HTTP 请求
struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> query_params;
    std::map<std::string, std::string> path_params;

    // multipart 文件
    struct UploadedFile {
        std::string name;
        std::string filename;
        std::string content_type;
        std::string data;
    };
    std::vector<UploadedFile> files;

    std::string GetHeader(const std::string& key) const {
        auto it = headers.find(key);
        if (it != headers.end()) return it->second;
        // 尝试小写
        for (auto& h : headers) {
            std::string lower = h.first;
            for (auto& c : lower) c = tolower(c);
            std::string lower_key = key;
            for (auto& c : lower_key) c = tolower(c);
            if (lower == lower_key) return h.second;
        }
        return "";
    }

    std::string GetParam(const std::string& key) const {
        auto it = query_params.find(key);
        if (it != query_params.end()) return it->second;
        return "";
    }
};

// HTTP 响应
struct HttpResponse {
    int status = 200;
    std::map<std::string, std::string> headers;
    std::string body;

    // 流式内容提供者
    using ContentProvider = std::function<void(size_t offset, std::vector<char>& buffer, size_t& bytes_provided)>;
    ContentProvider content_provider;
    size_t content_length = 0;
    bool is_streaming = false;

    void SetHeader(const std::string& key, const std::string& value) {
        headers[key] = value;
    }

    void SetJson(const std::string& json) {
        headers["Content-Type"] = "application/json; charset=utf-8";
        body = json;
    }

    void SetText(const std::string& text, const std::string& content_type = "text/plain") {
        headers["Content-Type"] = content_type;
        body = text;
    }
};

// 路由处理器类型
using Handler = std::function<void(const HttpRequest&, HttpResponse&)>;

class HttpServer {
public:
    HttpServer();
    ~HttpServer();

    bool Start(const std::string& host, int port);
    void Stop();
    bool IsRunning() const { return running_; }

    // 注册路由
    void Get(const std::string& path, Handler handler);
    void Post(const std::string& path, Handler handler);
    void Put(const std::string& path, Handler handler);
    void Delete(const std::string& path, Handler handler);

    // 静态文件服务
    void SetStaticRoot(const std::string& path);

    // 日志回调
    using LogCallback = std::function<void(const std::string&)>;
    void SetLogCallback(LogCallback cb) { log_cb_ = cb; }

private:
    void AcceptLoop();
    void HandleClient(SOCKET client);
    HttpRequest ParseRequest(SOCKET client);
    void SendResponse(SOCKET client, const HttpResponse& res);
    void SendStaticFile(SOCKET client, const std::string& path);

    // 路由匹配
    struct Route {
        std::string method;
        std::string path;
        Handler handler;
    };
    std::vector<Route> routes_;

    // 通配符路由 (如 /api/files/.*)
    struct WildcardRoute {
        std::string method;
        std::string prefix;
        Handler handler;
    };
    std::vector<WildcardRoute> wildcard_routes_;

    SOCKET listen_socket_ = INVALID_SOCKET;
    volatile bool running_ = false;
    HANDLE accept_thread_ = nullptr;
    std::string static_root_;
    LogCallback log_cb_;

    Mutex mutex_;
    bool winsock_initialized_ = false;

    static constexpr int BUFFER_SIZE = 8192;
    static constexpr int MAX_REQUEST_SIZE = 10 * 1024 * 1024; // 10MB max body
};

} // namespace cloud
