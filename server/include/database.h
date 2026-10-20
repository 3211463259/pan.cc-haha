#pragma once

#include <sqlite3.h>
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <optional>
#include <functional>
#include <sstream>
#include <memory>
#include "thread_safe.h"

namespace cloud {

// 文件类型
constexpr const char* FILE_TYPE_FILE = "file";
constexpr const char* FILE_TYPE_DIR = "dir";

// 文件元数据
struct FileEntry {
    int64_t id = 0;
    int64_t parent_id = 0;      // 0 = 根目录
    std::string name;
    std::string type;           // "file" or "dir"
    int64_t size = 0;
    std::string hash;           // SHA256, 仅文件
    int64_t created_at = 0;
    int64_t modified_at = 0;
    int64_t deleted_at = 0;     // 0 = 正常, >0 = 在回收站
    int ref_count = 1;          // 引用计数(去重用)
};

// 搜索筛选条件
struct SearchFilter {
    std::string keyword;
    std::string type_filter;        // 文件类型筛选
    int64_t min_size = -1;
    int64_t max_size = -1;
    int64_t start_time = -1;
    int64_t end_time = -1;
    std::string path_prefix;        // path: 语法
    bool exact_match = false;       // 引号固定词序
    std::string exclude_type;       // 减号排除
};

class Database {
public:
    Database();
    ~Database();

    bool Initialize(const std::string& db_path);
    void Close();

    // === 文件系统操作 ===

    // 创建文件夹
    int64_t CreateFolder(int64_t parent_id, const std::string& name, int64_t now);

    // 添加文件记录
    int64_t AddFile(int64_t parent_id, const std::string& name,
                    int64_t size, const std::string& hash, int64_t now);

    // 获取文件/文件夹信息
    std::optional<FileEntry> GetEntry(int64_t id);
    std::optional<FileEntry> GetChildByName(int64_t parent_id, const std::string& name);

    // 列出目录内容
    std::vector<FileEntry> ListDirectory(int64_t parent_id, bool include_deleted = false);

    // 获取路径上的所有条目 (用于路径解析)
    std::optional<FileEntry> ResolvePath(const std::string& path);

    // 重命名
    bool Rename(int64_t id, const std::string& new_name, int64_t now);

    // 移动
    bool Move(int64_t id, int64_t new_parent_id, int64_t now);

    // 复制文件 (引用计数+1)
    int64_t CopyFile(int64_t file_id, int64_t target_parent_id, int64_t now);

    // 复制文件夹 (递归)
    int64_t CopyFolder(int64_t folder_id, int64_t target_parent_id, int64_t now, int depth = 0);

    // 删除 (移入回收站)
    bool SoftDelete(int64_t id, int64_t now);

    // 还原
    bool Restore(int64_t id, int64_t target_parent_id, int64_t now);

    // 永久删除
    bool HardDelete(int64_t id);

    // 清空过期回收站 (超过 expire_days 天)
    std::vector<int64_t> PurgeExpiredRecycleBin(int64_t now, int expire_days);

    // 回收站列表
    std::vector<FileEntry> ListRecycleBin();

    // 搜索
    std::vector<FileEntry> Search(const SearchFilter& filter, int limit = 1000);

    // 文件夹详情 (总大小、子文件夹数、文件数)
    struct FolderInfo {
        int64_t total_size = 0;
        int folder_count = 0;
        int file_count = 0;
    };
    FolderInfo GetFolderInfo(int64_t folder_id);

    // === 哈希去重 ===

    // 查找已有哈希
    bool HashExists(const std::string& hash);
    int GetRefCount(const std::string& hash);
    void IncrementRef(const std::string& hash);
    void DecrementRef(const std::string& hash);

    // 获取哈希对应的存储路径
    std::string GetHashStoragePath(const std::string& hash);

    // 插入哈希记录 (直接操作 hash_store)
    void InsertHashRecord(const std::string& hash, int64_t size, const std::string& storage_path, int64_t now);

    // === 断点续传 ===

    struct UploadSession {
        std::string upload_id;
        int64_t parent_id = 0;
        std::string file_name;
        int64_t file_size = 0;
        int64_t uploaded_size = 0;
        std::string hash;
        int64_t created_at = 0;
        int chunk_size = 0;
        std::vector<bool> received_chunks;
    };

    std::string CreateUploadSession(int64_t parent_id, const std::string& file_name,
                                    int64_t file_size, int chunk_size, int64_t now);
    std::optional<UploadSession> GetUploadSession(const std::string& upload_id);
    bool UpdateUploadProgress(const std::string& upload_id, int64_t uploaded_size,
                              int chunk_index, const std::string& chunk_hash);
    void DeleteUploadSession(const std::string& upload_id);

    // === 排序 ===

    enum class SortBy { NAME, SIZE, CREATED_TIME, MODIFIED_TIME };
    enum class SortOrder { ASC, DESC };

    std::vector<FileEntry> ListDirectorySorted(int64_t parent_id, SortBy sort_by,
                                                SortOrder sort_order, bool dirs_first = true);

private:
    sqlite3* db_ = nullptr;
    Mutex mutex_;

    void CreateTables();
    FileEntry RowToFileEntry(sqlite3_stmt* stmt);
    int64_t CalculateFolderSize(int64_t folder_id);
};

} // namespace cloud
