#pragma once

#include "http_server.h"
#include "database.h"
#include "storage.h"
#include <string>
#include <atomic>
#include <functional>

namespace cloud {

struct ServerConfig {
    std::string host = "0.0.0.0";
    int port = 8080;
    std::string storage_root = "./data";
    std::string web_root = "../web/dist";
    int chunk_size = DEFAULT_CHUNK_SIZE;
    int recycle_expire_days = 100;
    int max_upload_batch = 1000;
};

class CloudServer {
public:
    using LogCallback = std::function<void(const std::string&)>;

    CloudServer();
    ~CloudServer();

    bool Start(const ServerConfig& config);
    void Stop();
    bool IsRunning() const { return running_; }

    void SetLogCallback(LogCallback cb) { log_cb_ = cb; }
    const ServerConfig& GetConfig() const { return config_; }

private:
    void SetupRoutes();
    void SetupStaticFiles();
    void CleanupTask();

    // API handlers
    void HandleListFiles(const HttpRequest& req, HttpResponse& res);
    void HandleCreateFolder(const HttpRequest& req, HttpResponse& res);
    void HandleUpload(const HttpRequest& req, HttpResponse& res);
    void HandleUploadChunk(const HttpRequest& req, HttpResponse& res);
    void HandleDownload(const HttpRequest& req, HttpResponse& res);
    void HandleDelete(const HttpRequest& req, HttpResponse& res);
    void HandleRename(const HttpRequest& req, HttpResponse& res);
    void HandleMove(const HttpRequest& req, HttpResponse& res);
    void HandleCopy(const HttpRequest& req, HttpResponse& res);
    void HandleSearch(const HttpRequest& req, HttpResponse& res);
    void HandleRecycleList(const HttpRequest& req, HttpResponse& res);
    void HandleRecycleRestore(const HttpRequest& req, HttpResponse& res);
    void HandleRecyclePurge(const HttpRequest& req, HttpResponse& res);
    void HandleFileInfo(const HttpRequest& req, HttpResponse& res);
    void HandleFolderInfo(const HttpRequest& req, HttpResponse& res);
    void HandleStorageInfo(const HttpRequest& req, HttpResponse& res);

    // 辅助
    int64_t NowTimestamp();
    void JsonSuccess(HttpResponse& res, const std::string& data = "{}");
    void JsonError(HttpResponse& res, int code, const std::string& msg);
    std::string FileEntryToJson(const FileEntry& entry);

    HttpServer server_;
    Database db_;
    StorageEngine storage_;
    ServerConfig config_;
    volatile bool running_ = false;
    HANDLE cleanup_thread_ = nullptr;
    LogCallback log_cb_;
};

} // namespace cloud
