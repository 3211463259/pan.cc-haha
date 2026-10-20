/**
 * @file client_api.h
 * @brief 客户端接口预留定义 (Android / Windows 客户端)
 *
 * 本文件定义了 Android 客户端和 Windows 客户端与服务端通信的 API 接口。
 * 当前仅做接口定义预留，具体客户端代码后续实现。
 *
 * 所有客户端通过 HTTP REST API 与服务端通信。
 * 认证方式: Token (预留，当前版本无认证)
 */

#pragma once

namespace cloud {
namespace client_api {

// ============================================================
// API 版本信息
// ============================================================
// GET /api/version
// 返回: { "version": "10.0.0", "api_version": 1, "platform": "server" }

// ============================================================
// 认证接口 (预留)
// ============================================================
// POST /api/auth/login
// 请求: { "username": "string", "password": "string" }
// 返回: { "token": "string", "expires_at": 1234567890 }
//
// POST /api/auth/logout
// 请求头: Authorization: Bearer <token>
//
// POST /api/auth/refresh
// 请求: { "refresh_token": "string" }
// 返回: { "token": "string", "expires_at": 1234567890 }

// ============================================================
// 文件操作接口 (客户端共用)
// ============================================================

// --- 文件列表 ---
// GET /api/files?parent_id=0&sort_by=name&sort_order=asc
// 返回: [{ id, parent_id, name, type, size, hash, created_at, modified_at }]

// --- 创建文件夹 ---
// POST /api/folder
// 请求: { "parent_id": 0, "name": "新建文件夹" }

// --- 上传文件 ---
// POST /api/upload?parent_id=0  (multipart/form-data, file field)
// 返回: { "id": 123, "dedup": false }

// --- 分块上传 (大文件) ---
// POST /api/upload/session
// 请求: { "parent_id": 0, "file_name": "xxx.zip", "file_size": 12345, "chunk_size": 5242880 }
// 返回: { "upload_id": "up_xxx" }
//
// POST /api/upload/chunk  (multipart/form-data, chunk field)
// 请求头: X-Upload-Id: xxx, X-Chunk-Index: 0
//
// POST /api/upload/complete
// 请求: { "upload_id": "up_xxx", "parent_id": 0, "file_name": "xxx.zip", "file_size": 12345 }

// --- 下载文件 ---
// GET /api/download/{file_id}
// 支持 Range 请求头实现断点续传
// 响应头: Accept-Ranges: bytes, Content-Range: bytes 0-999/1000

// --- 删除文件 ---
// DELETE /api/files/{file_id}
// 移入回收站

// --- 重命名 ---
// PUT /api/rename
// 请求: { "id": 123, "name": "新名称.txt" }

// --- 移动 ---
// PUT /api/move
// 请求: { "id": 123, "target_parent_id": 456 }

// --- 复制 ---
// POST /api/copy
// 请求: { "id": 123, "target_parent_id": 456 }

// --- 搜索 ---
// GET /api/search?q=keyword&type=file&min_size=0&max_size=1000000

// --- 文件详情 ---
// GET /api/info/{file_id}

// --- 文件夹详情 ---
// GET /api/folder_info?id=0
// 返回: { "total_size": 12345, "folder_count": 3, "file_count": 10, "formatted_size": "12.1 KB" }

// --- 存储信息 ---
// GET /api/storage
// 返回: { "total_space": ..., "used_space": ..., "free_space": ..., "formatted_*" }

// ============================================================
// 回收站接口
// ============================================================
// GET /api/recycle
// POST /api/recycle/restore  { "id": 123, "target_parent_id": 0 }
// DELETE /api/recycle/{file_id}

// ============================================================
// 分享接口 (预留)
// ============================================================
// POST /api/share/create
// 请求: { "file_ids": [1,2,3], "expire_days": 7, "extract_code": "abcd" }
// 返回: { "share_id": "xxx", "share_url": "http://host/share/xxx" }
//
// GET /api/share/{share_id}
// GET /api/share/{share_id}/download?code=abcd
// DELETE /api/share/{share_id}

// ============================================================
// 同步接口 (预留)
// ============================================================
// POST /api/sync/config
// 请求: { "local_path": "D:\\CloudSync", "remote_folder_id": 0, "direction": "upload" }
//
// GET /api/sync/status
// POST /api/sync/pause
// POST /api/sync/resume

// ============================================================
// WebDAV 接口 (预留)
// ============================================================
// GET /webdav/           - PROPFIND
// PUT /webdav/file       - 上传
// GET /webdav/file       - 下载
// DELETE /webdav/file    - 删除
// MKCOL /webdav/folder   - 创建文件夹
// PROPFIND /webdav/      - 列出目录

// ============================================================
// URL 直链接口 (预留)
// ============================================================
// POST /api/direct_link/enable
// 请求: { "folder_id": 123, "enable_auth": true, "secret_key": "xxx" }
//
// GET /api/direct_link/file/{file_id}?token=xxx

// ============================================================
// 离线下载接口 (预留)
// ============================================================
// POST /api/offline_download/http
// 请求: { "url": "http://example.com/file.zip", "target_folder_id": 0 }
//
// POST /api/offline_download/bt
// 请求: { "torrent": "base64...", "target_folder_id": 0 }
//
// GET /api/offline_download/tasks
// DELETE /api/offline_download/tasks/{task_id}

// ============================================================
// 视频转码接口 (预留)
// ============================================================
// POST /api/video/transcode
// 请求: { "file_id": 123, "format": "m3u8", "quality": "720p" }
//
// GET /api/video/transcode/status/{task_id}

// ============================================================
// 批量操作接口
// ============================================================
// POST /api/batch
// 请求: {
//   "action": "delete|move|copy",
//   "ids": [1,2,3],
//   "target_parent_id": 0
// }

} // namespace client_api
} // namespace cloud
