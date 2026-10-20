#include "database.h"
#include <sstream>
#include <cstring>
#include <algorithm>
#include <direct.h>
#include <io.h>
#include <sys/stat.h>

namespace cloud {

// 简单的文件操作辅助 (兼容 GCC 8.1)
inline bool path_exists(const std::string& p) {
    struct _stat st;
    return _stat(p.c_str(), &st) == 0;
}

inline bool create_dir(const std::string& p) {
    return _mkdir(p.c_str()) == 0 || errno == EEXIST;
}

inline bool create_dirs(const std::string& p) {
    // 递归创建目录
    size_t pos = 0;
    std::string dir;
    while ((pos = p.find_first_of("/\\", pos)) != std::string::npos) {
        dir = p.substr(0, pos++);
        if (!dir.empty() && !create_dir(dir)) {
            if (errno != EEXIST) return false;
        }
    }
    return create_dir(p);
}

inline bool remove_file(const std::string& p) {
    return ::remove(p.c_str()) == 0;
}

Database::Database() = default;

Database::~Database() {
    Close();
}

bool Database::Initialize(const std::string& db_path) {
    LockGuard<Mutex> lock(mutex_);

    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) return false;

    // 启用 WAL 模式提升并发性能
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);

    CreateTables();
    return true;
}

void Database::Close() {
    LockGuard<Mutex> lock(mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void Database::CreateTables() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS fs_entries (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            parent_id INTEGER NOT NULL DEFAULT 0,
            name TEXT NOT NULL,
            type TEXT NOT NULL CHECK(type IN ('file','dir')),
            size INTEGER NOT NULL DEFAULT 0,
            hash TEXT DEFAULT '',
            created_at INTEGER NOT NULL,
            modified_at INTEGER NOT NULL,
            deleted_at INTEGER NOT NULL DEFAULT 0,
            ref_count INTEGER NOT NULL DEFAULT 1,
            UNIQUE(parent_id, name, deleted_at)
        );

        CREATE INDEX IF NOT EXISTS idx_parent ON fs_entries(parent_id, deleted_at);
        CREATE INDEX IF NOT EXISTS idx_hash ON fs_entries(hash) WHERE hash != '';
        CREATE INDEX IF NOT EXISTS idx_name ON fs_entries(name);
        CREATE INDEX IF NOT EXISTS idx_deleted ON fs_entries(deleted_at) WHERE deleted_at > 0;
        CREATE INDEX IF NOT EXISTS idx_type ON fs_entries(type);

        CREATE TABLE IF NOT EXISTS hash_store (
            hash TEXT PRIMARY KEY,
            size INTEGER NOT NULL,
            ref_count INTEGER NOT NULL DEFAULT 1,
            storage_path TEXT NOT NULL,
            created_at INTEGER NOT NULL
        );

        CREATE TABLE IF NOT EXISTS upload_sessions (
            upload_id TEXT PRIMARY KEY,
            parent_id INTEGER NOT NULL,
            file_name TEXT NOT NULL,
            file_size INTEGER NOT NULL,
            uploaded_size INTEGER NOT NULL DEFAULT 0,
            chunk_size INTEGER NOT NULL DEFAULT 0,
            chunks_received TEXT DEFAULT '',
            hash TEXT DEFAULT '',
            created_at INTEGER NOT NULL,
            expires_at INTEGER NOT NULL
        );

        -- 根目录
        INSERT OR IGNORE INTO fs_entries (id, parent_id, name, type, created_at, modified_at)
        VALUES (0, -1, '/', 'dir', 0, 0);
    )";

    sqlite3_exec(db_, sql, nullptr, nullptr, nullptr);
}

FileEntry Database::RowToFileEntry(sqlite3_stmt* stmt) {
    FileEntry e;
    e.id = sqlite3_column_int64(stmt, 0);
    e.parent_id = sqlite3_column_int64(stmt, 1);
    e.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    e.type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    e.size = sqlite3_column_int64(stmt, 4);
    const char* h = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    e.hash = h ? h : "";
    e.created_at = sqlite3_column_int64(stmt, 6);
    e.modified_at = sqlite3_column_int64(stmt, 7);
    e.deleted_at = sqlite3_column_int64(stmt, 8);
    e.ref_count = sqlite3_column_int(stmt, 9);
    return e;
}

int64_t Database::CreateFolder(int64_t parent_id, const std::string& name, int64_t now) {
    LockGuard<Mutex> lock(mutex_);
    const char* sql = "INSERT INTO fs_entries (parent_id, name, type, created_at, modified_at) VALUES (?, ?, 'dir', ?, ?)";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return -1;
    sqlite3_bind_int64(stmt, 1, parent_id);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, now);
    sqlite3_bind_int64(stmt, 4, now);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return -1;
    }
    int64_t id = sqlite3_last_insert_rowid(db_);
    sqlite3_finalize(stmt);
    return id;
}

int64_t Database::AddFile(int64_t parent_id, const std::string& name,
                          int64_t size, const std::string& hash, int64_t now) {
    LockGuard<Mutex> lock(mutex_);
    const char* sql = "INSERT INTO fs_entries (parent_id, name, type, size, hash, created_at, modified_at) VALUES (?, ?, 'file', ?, ?, ?, ?)";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return -1;
    sqlite3_bind_int64(stmt, 1, parent_id);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, size);
    sqlite3_bind_text(stmt, 4, hash.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 5, now);
    sqlite3_bind_int64(stmt, 6, now);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return -1;
    }
    int64_t id = sqlite3_last_insert_rowid(db_);
    sqlite3_finalize(stmt);
    return id;
}

std::optional<FileEntry> Database::GetEntry(int64_t id) {
    LockGuard<Mutex> lock(mutex_);
    const char* sql = "SELECT id, parent_id, name, type, size, hash, created_at, modified_at, deleted_at, ref_count FROM fs_entries WHERE id=?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_int64(stmt, 1, id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        auto e = RowToFileEntry(stmt);
        sqlite3_finalize(stmt);
        return e;
    }
    sqlite3_finalize(stmt);
    return std::nullopt;
}

std::optional<FileEntry> Database::GetChildByName(int64_t parent_id, const std::string& name) {
    LockGuard<Mutex> lock(mutex_);
    const char* sql = "SELECT id, parent_id, name, type, size, hash, created_at, modified_at, deleted_at, ref_count FROM fs_entries WHERE parent_id=? AND name=? AND deleted_at=0";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_int64(stmt, 1, parent_id);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        auto e = RowToFileEntry(stmt);
        sqlite3_finalize(stmt);
        return e;
    }
    sqlite3_finalize(stmt);
    return std::nullopt;
}

std::vector<FileEntry> Database::ListDirectory(int64_t parent_id, bool include_deleted) {
    return ListDirectorySorted(parent_id, SortBy::NAME, SortOrder::ASC, true);
}

std::vector<FileEntry> Database::ListDirectorySorted(int64_t parent_id, SortBy sort_by,
                                                     SortOrder sort_order, bool dirs_first) {
    LockGuard<Mutex> lock(mutex_);
    std::ostringstream sql;
    sql << "SELECT id, parent_id, name, type, size, hash, created_at, modified_at, deleted_at, ref_count FROM fs_entries WHERE parent_id=? AND deleted_at=0";

    if (dirs_first) {
        sql << " ORDER BY type DESC"; // dir > file alphabetically
    }

    std::string order_col;
    switch (sort_by) {
        case SortBy::NAME: order_col = "name"; break;
        case SortBy::SIZE: order_col = "size"; break;
        case SortBy::CREATED_TIME: order_col = "created_at"; break;
        case SortBy::MODIFIED_TIME: order_col = "modified_at"; break;
    }
    std::string dir_str = (sort_order == SortOrder::ASC) ? "ASC" : "DESC";
    sql << (dirs_first ? ", " : " ORDER BY ") << order_col << " " << dir_str;

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql.str().c_str(), -1, &stmt, nullptr) != SQLITE_OK) return {};
    sqlite3_bind_int64(stmt, 1, parent_id);

    std::vector<FileEntry> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(RowToFileEntry(stmt));
    }
    sqlite3_finalize(stmt);
    return results;
}

bool Database::Rename(int64_t id, const std::string& new_name, int64_t now) {
    LockGuard<Mutex> lock(mutex_);
    const char* sql = "UPDATE fs_entries SET name=?, modified_at=? WHERE id=?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, new_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, now);
    sqlite3_bind_int64(stmt, 3, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::Move(int64_t id, int64_t new_parent_id, int64_t now) {
    LockGuard<Mutex> lock(mutex_);
    const char* sql = "UPDATE fs_entries SET parent_id=?, modified_at=? WHERE id=?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, new_parent_id);
    sqlite3_bind_int64(stmt, 2, now);
    sqlite3_bind_int64(stmt, 3, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

int64_t Database::CopyFile(int64_t file_id, int64_t target_parent_id, int64_t now) {
    auto entry = GetEntry(file_id);
    if (!entry || entry->type != FILE_TYPE_FILE) return -1;

    int64_t new_id = AddFile(target_parent_id, entry->name, entry->size, entry->hash, now);
    if (new_id > 0 && !entry->hash.empty()) {
        IncrementRef(entry->hash);
    }
    return new_id;
}

int64_t Database::CopyFolder(int64_t folder_id, int64_t target_parent_id, int64_t now, int depth) {
    if (depth > 50) return -1; // 层级限制

    auto entry = GetEntry(folder_id);
    if (!entry || entry->type != FILE_TYPE_DIR) return -1;

    int64_t new_folder_id = CreateFolder(target_parent_id, entry->name, now);
    if (new_folder_id < 0) return -1;

    auto children = ListDirectory(folder_id);
    for (auto& child : children) {
        if (child.type == FILE_TYPE_DIR) {
            CopyFolder(child.id, new_folder_id, now, depth + 1);
        } else {
            CopyFile(child.id, new_folder_id, now);
        }
    }
    return new_folder_id;
}

bool Database::SoftDelete(int64_t id, int64_t now) {
    LockGuard<Mutex> lock(mutex_);
    // 递归标记删除
    std::function<void(int64_t)> mark_deleted = [&](int64_t fid) {
        const char* sql = "UPDATE fs_entries SET deleted_at=? WHERE id=?";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, now);
            sqlite3_bind_int64(stmt, 2, fid);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        // 递归子节点
        const char* child_sql = "SELECT id FROM fs_entries WHERE parent_id=? AND deleted_at=0";
        sqlite3_stmt* child_stmt;
        if (sqlite3_prepare_v2(db_, child_sql, -1, &child_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(child_stmt, 1, fid);
            while (sqlite3_step(child_stmt) == SQLITE_ROW) {
                mark_deleted(sqlite3_column_int64(child_stmt, 0));
            }
            sqlite3_finalize(child_stmt);
        }
    };
    mark_deleted(id);
    return true;
}

bool Database::Restore(int64_t id, int64_t target_parent_id, int64_t now) {
    LockGuard<Mutex> lock(mutex_);
    const char* sql = "UPDATE fs_entries SET deleted_at=0, parent_id=?, modified_at=? WHERE id=?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, target_parent_id);
    sqlite3_bind_int64(stmt, 2, now);
    sqlite3_bind_int64(stmt, 3, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::HardDelete(int64_t id) {
    LockGuard<Mutex> lock(mutex_);

    auto entry = GetEntry(id);
    if (!entry) return false;

    if (entry->type == FILE_TYPE_FILE && !entry->hash.empty()) {
        DecrementRef(entry->hash);
    }

    const char* sql = "DELETE FROM fs_entries WHERE id=?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<int64_t> Database::PurgeExpiredRecycleBin(int64_t now, int expire_days) {
    LockGuard<Mutex> lock(mutex_);
    int64_t threshold = now - (int64_t)expire_days * 86400;

    std::vector<int64_t> expired_ids;
    const char* sql = "SELECT id FROM fs_entries WHERE deleted_at > 0 AND deleted_at < ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, threshold);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            expired_ids.push_back(sqlite3_column_int64(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }

    for (int64_t id : expired_ids) {
        HardDelete(id);
    }
    return expired_ids;
}

std::vector<FileEntry> Database::ListRecycleBin() {
    LockGuard<Mutex> lock(mutex_);
    const char* sql = "SELECT id, parent_id, name, type, size, hash, created_at, modified_at, deleted_at, ref_count FROM fs_entries WHERE deleted_at > 0 ORDER BY deleted_at DESC";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return {};

    std::vector<FileEntry> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(RowToFileEntry(stmt));
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<FileEntry> Database::Search(const SearchFilter& filter, int limit) {
    LockGuard<Mutex> lock(mutex_);
    std::ostringstream sql;
    sql << "SELECT id, parent_id, name, type, size, hash, created_at, modified_at, deleted_at, ref_count FROM fs_entries WHERE deleted_at=0";

    if (!filter.keyword.empty()) {
        if (filter.exact_match) {
            sql << " AND name LIKE '%" << filter.keyword << "%'";
        } else {
            // 模糊搜索 - 分词
            std::istringstream iss(filter.keyword);
            std::string word;
            while (iss >> word) {
                sql << " AND name LIKE '%" << word << "%'";
            }
        }
    }

    if (!filter.type_filter.empty()) {
        sql << " AND type='" << filter.type_filter << "'";
    }

    if (filter.min_size >= 0) {
        sql << " AND size>=" << filter.min_size;
    }
    if (filter.max_size >= 0) {
        sql << " AND size<=" << filter.max_size;
    }
    if (filter.start_time >= 0) {
        sql << " AND created_at>=" << filter.start_time;
    }
    if (filter.end_time >= 0) {
        sql << " AND created_at<=" << filter.end_time;
    }

    sql << " LIMIT " << limit;

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql.str().c_str(), -1, &stmt, nullptr) != SQLITE_OK) return {};

    std::vector<FileEntry> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(RowToFileEntry(stmt));
    }
    sqlite3_finalize(stmt);
    return results;
}

Database::FolderInfo Database::GetFolderInfo(int64_t folder_id) {
    LockGuard<Mutex> lock(mutex_);
    FolderInfo info;

    const char* sql = R"(
        SELECT
            COALESCE(SUM(CASE WHEN type='file' THEN size ELSE 0 END), 0),
            COALESCE(SUM(CASE WHEN type='dir' THEN 1 ELSE 0 END), 0),
            COALESCE(SUM(CASE WHEN type='file' THEN 1 ELSE 0 END), 0)
        FROM fs_entries WHERE parent_id=? AND deleted_at=0
    )";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, folder_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            info.total_size = sqlite3_column_int64(stmt, 0);
            info.folder_count = sqlite3_column_int(stmt, 1);
            info.file_count = sqlite3_column_int(stmt, 2);
        }
        sqlite3_finalize(stmt);
    }
    return info;
}

// === 哈希去重 ===

bool Database::HashExists(const std::string& hash) {
    LockGuard<Mutex> lock(mutex_);
    const char* sql = "SELECT COUNT(*) FROM hash_store WHERE hash=?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_STATIC);
    bool exists = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        exists = sqlite3_column_int(stmt, 0) > 0;
    }
    sqlite3_finalize(stmt);
    return exists;
}

int Database::GetRefCount(const std::string& hash) {
    LockGuard<Mutex> lock(mutex_);
    const char* sql = "SELECT ref_count FROM hash_store WHERE hash=?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_STATIC);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

void Database::IncrementRef(const std::string& hash) {
    LockGuard<Mutex> lock(mutex_);
    const char* sql = "UPDATE hash_store SET ref_count = ref_count + 1 WHERE hash=?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void Database::DecrementRef(const std::string& hash) {
    LockGuard<Mutex> lock(mutex_);
    const char* update_sql = "UPDATE hash_store SET ref_count = ref_count - 1 WHERE hash=?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, update_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // 如果引用计数为0，删除物理文件
    const char* check_sql = "SELECT ref_count, storage_path FROM hash_store WHERE hash=?";
    sqlite3_stmt* check_stmt;
    if (sqlite3_prepare_v2(db_, check_sql, -1, &check_stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(check_stmt, 1, hash.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(check_stmt) == SQLITE_ROW) {
            int ref = sqlite3_column_int(check_stmt, 0);
            const char* path = reinterpret_cast<const char*>(sqlite3_column_text(check_stmt, 1));
            if (ref <= 0) {
                // 删除记录和物理文件
                sqlite3_finalize(check_stmt);
                const char* del_sql = "DELETE FROM hash_store WHERE hash=?";
                sqlite3_stmt* del_stmt;
                if (sqlite3_prepare_v2(db_, del_sql, -1, &del_stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_text(del_stmt, 1, hash.c_str(), -1, SQLITE_STATIC);
                    sqlite3_step(del_stmt);
                    sqlite3_finalize(del_stmt);
                }
                if (path) {
                    remove_file(path);
                }
                return;
            }
        }
        sqlite3_finalize(check_stmt);
    }
}

std::string Database::GetHashStoragePath(const std::string& hash) {
    LockGuard<Mutex> lock(mutex_);
    const char* sql = "SELECT storage_path FROM hash_store WHERE hash=?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return "";
    sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_STATIC);
    std::string path;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (p) path = p;
    }
    sqlite3_finalize(stmt);
    return path;
}

void Database::InsertHashRecord(const std::string& hash, int64_t size, const std::string& storage_path, int64_t now) {
    LockGuard<Mutex> lock(mutex_);
    const char* sql = "INSERT OR IGNORE INTO hash_store (hash, size, ref_count, storage_path, created_at) VALUES (?, ?, 1, ?, ?)";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 2, size);
        sqlite3_bind_text(stmt, 3, storage_path.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 4, now);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

// === 断点续传 ===

std::string Database::CreateUploadSession(int64_t parent_id, const std::string& file_name,
                                          int64_t file_size, int chunk_size, int64_t now) {
    LockGuard<Mutex> lock(mutex_);
    // 生成 upload_id (简单实现)
    static int64_t counter = 0;
    std::string upload_id = "up_" + std::to_string(now) + "_" + std::to_string(++counter);

    int num_chunks = (file_size + chunk_size - 1) / chunk_size;

    const char* sql = "INSERT INTO upload_sessions (upload_id, parent_id, file_name, file_size, chunk_size, created_at, expires_at) VALUES (?, ?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return "";
    sqlite3_bind_text(stmt, 1, upload_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, parent_id);
    sqlite3_bind_text(stmt, 3, file_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 4, file_size);
    sqlite3_bind_int(stmt, 5, chunk_size);
    sqlite3_bind_int64(stmt, 6, now);
    sqlite3_bind_int64(stmt, 7, now + 86400); // 24小时过期

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return "";
    }
    sqlite3_finalize(stmt);
    return upload_id;
}

std::optional<Database::UploadSession> Database::GetUploadSession(const std::string& upload_id) {
    LockGuard<Mutex> lock(mutex_);
    const char* sql = "SELECT upload_id, parent_id, file_name, file_size, uploaded_size, chunk_size, chunks_received, hash, created_at FROM upload_sessions WHERE upload_id=?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, upload_id.c_str(), -1, SQLITE_STATIC);

    UploadSession session;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        session.upload_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        session.parent_id = sqlite3_column_int64(stmt, 1);
        session.file_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        session.file_size = sqlite3_column_int64(stmt, 3);
        session.uploaded_size = sqlite3_column_int64(stmt, 4);
        session.chunk_size = sqlite3_column_int(stmt, 5);

        const char* chunks = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        if (chunks) {
            std::string chunk_str(chunks);
            // 解析已收到的分块索引
            std::istringstream iss(chunk_str);
            std::string idx;
            int total = (session.file_size + session.chunk_size - 1) / session.chunk_size;
            session.received_chunks.resize(total, false);
            while (std::getline(iss, idx, ',')) {
                int i = std::stoi(idx);
                if (i >= 0 && i < total) session.received_chunks[i] = true;
            }
        }

        const char* h = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        session.hash = h ? h : "";
        session.created_at = sqlite3_column_int64(stmt, 8);

        sqlite3_finalize(stmt);
        return session;
    }
    sqlite3_finalize(stmt);
    return std::nullopt;
}

bool Database::UpdateUploadProgress(const std::string& upload_id, int64_t uploaded_size,
                                    int chunk_index, const std::string& chunk_hash) {
    LockGuard<Mutex> lock(mutex_);

    // 获取现有 chunks_received
    auto session = GetUploadSession(upload_id);
    if (!session) return false;

    if (chunk_index >= 0 && chunk_index < (int)session->received_chunks.size()) {
        session->received_chunks[chunk_index] = true;
    }

    // 序列化
    std::ostringstream oss;
    for (int i = 0; i < (int)session->received_chunks.size(); i++) {
        if (session->received_chunks[i]) {
            if (oss.tellp() > 0) oss << ",";
            oss << i;
        }
    }

    const char* sql = "UPDATE upload_sessions SET uploaded_size=?, chunks_received=? WHERE upload_id=?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(stmt, 1, uploaded_size);
    std::string chunks_str = oss.str();
    sqlite3_bind_text(stmt, 2, chunks_str.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, upload_id.c_str(), -1, SQLITE_STATIC);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

void Database::DeleteUploadSession(const std::string& upload_id) {
    LockGuard<Mutex> lock(mutex_);
    const char* sql = "DELETE FROM upload_sessions WHERE upload_id=?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, upload_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

} // namespace cloud
