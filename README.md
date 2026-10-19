# 局域网网盘 (Cloud Server)

一个运行在 Windows 上的局域网网盘服务器，支持通过网页浏览器进行文件上传、下载和管理。

## 功能特性

### 已实现
- ✅ 文件上传（支持拖拽、多选、批量上传，单次最多 1000 个文件）
- ✅ 文件下载（支持断点续传）
- ✅ 分块上传（大文件支持）
- ✅ 哈希去重（SHA256，重复文件只存一份）
- ✅ 新建文件夹（多层级目录）
- ✅ 文件排序（名称、大小、创建时间，文件夹置顶）
- ✅ 重命名与移动
- ✅ 文件复制（引用计数）
- ✅ 文件搜索（模糊搜索 + 筛选器）
- ✅ 回收站（100 天有效期，支持还原）
- ✅ 文件详情（总大小、子文件夹数、文件数）
- ✅ 存储信息展示
- ✅ Win32 GUI 服务端界面
- ✅ Vue.js 网页客户端

### 预留接口（未实现）
- 🔲 文件分享
- 🔲 视频在线播放
- 🔲 同步与备份
- 🔲 WebDAV 挂载
- 🔲 URL 直链下载
- 🔲 离线下载
- 🔲 视频转码
- 🔲 Android 客户端
- 🔲 Windows 客户端

## 项目结构

```
123pan.cc-haha/
├── cloud_server.exe          # 编译后的服务端程序
├── server/                   # C++ 服务端源码
│   ├── include/              # 头文件
│   │   ├── database.h        # SQLite 数据库层
│   │   ├── storage.h         # 文件存储引擎
│   │   ├── http_server.h     # HTTP 服务器
│   │   ├── server.h          # 网盘服务主类
│   │   ├── gui.h             # Win32 GUI
│   │   ├── thread_safe.h     # 线程安全 (Win32 临界区)
│   │   └── client_api.h      # 客户端接口预留
│   ├── src/                  # 源文件
│   │   ├── main.cpp          # 程序入口 (WinMain)
│   │   ├── database.cpp      # 数据库实现
│   │   ├── storage.cpp       # 存储引擎实现
│   │   ├── http_server.cpp   # HTTP 服务器实现
│   │   ├── server.cpp        # 网盘服务实现
│   │   └── gui.cpp           # GUI 实现 (未使用)
│   ├── lib/                  # 第三方库
│   │   ├── sqlite3.c         # SQLite 数据库
│   │   └── sqlite3.h
│   └── Makefile              # 构建脚本
├── web/                      # Vue.js 前端
│   ├── src/
│   │   ├── main.js           # 入口
│   │   ├── App.vue           # 主组件
│   │   ├── api.js            # API 封装
│   │   ├── style.css         # 全局样式
│   │   └── components/       # 组件
│   │       ├── FileManager.vue   # 文件列表
│   │       ├── Toolbar.vue       # 工具栏
│   │       ├── StatusBar.vue     # 状态栏
│   │       ├── UploadPanel.vue   # 上传面板
│   │       ├── RecycleBin.vue    # 回收站
│   │       └── SearchPanel.vue   # 搜索面板
│   ├── dist/                 # 构建产物
│   ├── package.json
│   └── vite.config.js
└── README.md
```

## 构建说明

### 环境要求
- MinGW-w64 (g++ 8.1+, gcc)
- OpenSSL 1.0.2+ 开发库
- zlib 开发库
- Node.js 16+ 和 npm

### 编译服务端

```bash
cd server
make
```

产物: `cloud_server.exe`

### 构建前端

```bash
cd web
npm install
npm run build
```

产物: `web/dist/`

## 使用方法

1. 运行 `cloud_server.exe`
2. 在 GUI 中配置监听地址（默认 0.0.0.0）和端口（默认 8080）
3. 设置存储路径（默认 ./data）
4. 点击"启动服务器"
5. 在浏览器中访问 `http://<服务器IP>:8080`

## API 接口

### 文件操作
| 方法 | 路径 | 说明 |
|------|------|------|
| GET | /files?parent_id=0 | 列出目录 |
| POST | /folder | 创建文件夹 |
| POST | /upload?parent_id=0 | 上传文件 |
| POST | /upload/chunk | 分块上传 |
| GET | /download/{id} | 下载文件 |
| DELETE | /files/{id} | 删除文件 |
| PUT | /rename | 重命名 |
| PUT | /move | 移动 |
| POST | /copy | 复制 |
| GET | /search?q=keyword | 搜索 |

### 回收站
| 方法 | 路径 | 说明 |
|------|------|------|
| GET | /recycle | 回收站列表 |
| POST | /recycle/restore | 还原 |
| DELETE | /recycle/{id} | 永久删除 |

### 信息
| 方法 | 路径 | 说明 |
|------|------|------|
| GET | /info/{id} | 文件详情 |
| GET | /folder_info?id=0 | 文件夹详情 |
| GET | /storage | 存储信息 |
| GET | /version | API 版本 |

## 技术架构

- **服务端**: C++17, Win32 API, Winsock2, OpenSSL, SQLite
- **前端**: Vue.js 3, Vite, Axios
- **存储**: 内容寻址存储 (CAS) + SHA256 去重 + 引用计数
- **传输**: HTTP/1.1, Range 请求 (断点续传), multipart 上传
