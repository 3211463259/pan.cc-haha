#pragma once

#include "database.h"
#include <string>
#include <vector>
#include <fstream>
#include <functional>
#include <memory>

namespace cloud {

// 分块大小: 5MB
constexpr int DEFAULT_CHUNK_SIZE = 5 * 1024 * 1024;

// 存储引擎: 管理物理文件存储、哈希计算、去重
class StorageEngine {
public:
    StorageEngine();
    ~StorageEngine();

    bool Initialize(const std::string& storage_root, Database* db);

    // === 文件写入 ===

    // 写入文件块 (用于断点续传)
    bool WriteChunk(const std::string& upload_id, int chunk_index,
                    const char* data, size_t len);

    // 合并分块为完整文件
    bool MergeChunks(const std::string& upload_id, int num_chunks,
                     const std::string& file_name, int64_t file_size);

    // 直接写入完整文件
    std::string WriteFile(const char* data, size_t len, int64_t parent_id,
                          const std::string& file_name, const std::string& hash_hint = "");

    // 写入文件流 (大文件)
    std::string WriteFileStream(std::istream& stream, size_t total_size,
                                int64_t parent_id, const std::string& file_name);

    // === 文件读取 ===

    // 获取文件存储路径
    std::string GetFilePath(const std::string& hash);

    // 读取文件到缓冲区
    std::vector<char> ReadFile(const std::string& hash);

    // 读取文件块 (断点续传下载)
    std::vector<char> ReadFileRange(const std::string& hash, int64_t offset, size_t length);

    // 打开文件流
    std::unique_ptr<std::ifstream> OpenFileStream(const std::string& hash);

    // === 哈希 ===

    // 计算 SHA256
    static std::string ComputeSHA256(const char* data, size_t len);
    static std::string ComputeSHA256(std::istream& stream, size_t total_size = 0);
    static std::string ComputeSHA256(const std::string& file_path);

    // === 去重 ===

    // 检查哈希是否已存在
    bool IsDuplicate(const std::string& hash);

    // 注册哈希 (引用计数+1)
    void RegisterHash(const std::string& hash, int64_t size, const std::string& storage_path);

    // === 上传会话文件管理 ===

    std::string GetChunkPath(const std::string& upload_id, int chunk_index);
    std::string GetUploadTempDir(const std::string& upload_id);
    void CleanupUpload(const std::string& upload_id);

    // === 工具 ===

    std::string GetStorageRoot() const { return storage_root_; }

    // 获取可用空间 (字节)
    static int64_t GetAvailableSpace(const std::string& path);

    // 格式化文件大小
    static std::string FormatSize(int64_t bytes);

private:
    std::string storage_root_;
    Database* db_ = nullptr;
    Mutex write_mutex_;

    // 存储路径: storage_root_/ab/cd/abcdef... (前4字符做二级目录)
    std::string HashToPath(const std::string& hash);
    std::string EnsureDir(const std::string& path);
};

} // namespace cloud
