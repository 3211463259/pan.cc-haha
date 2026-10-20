#define WIN32_LEAN_AND_MEAN
// 启用 Vista+ API (IFileOpenDialog 等); MinGW 8.1 默认 XP 级别
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif
#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <sstream>
#include <ctime>
#include <algorithm>

#include "gui.h"
#include "server.h"

// 需要链接的库
#pragma comment(lib, "comctl32.lib")

// ============ 高 DPI 支持 ============
// MinGW 8.1 头文件未声明以下 Win10 API, 动态加载
typedef BOOL (WINAPI *PFN_SetProcessDpiAwarenessContext)(HANDLE);
typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
typedef UINT (WINAPI *PFN_GetDpiForSystem)(void);

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif
#ifndef WM_GETDPISCALEDSIZE
#define WM_GETDPISCALEDSIZE 0x02E8
#endif
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#endif

// MinGW 8.1 未提供这些 GUID 的链接定义, 手动声明 (标准值)
static const CLSID kCLSID_FileOpenDialog =
    {0xDC1C5A9C,0xE88A,0x4DDE,{0xA5,0xA1,0x60,0xF8,0x2A,0x20,0x8A,0xE7}};
static const IID kIID_IFileOpenDialog =
    {0xD57C7288,0xD4AD,0x4768,{0xBE,0x02,0x9D,0x96,0x95,0x32,0xD9,0x60}};

// UTF-8 转 GBK (中文 Windows ANSI)
std::string Utf8ToGbk(const std::string& utf8) {
    if (utf8.empty()) return "";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return utf8;
    std::vector<wchar_t> wbuf(wlen);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wbuf.data(), wlen);
    int alen = WideCharToMultiByte(CP_ACP, 0, wbuf.data(), -1, nullptr, 0, nullptr, nullptr);
    if (alen <= 0) return utf8;
    std::vector<char> abuf(alen);
    WideCharToMultiByte(CP_ACP, 0, wbuf.data(), -1, abuf.data(), alen, nullptr, nullptr);
    return std::string(abuf.data());
}

// UTF-8 转 UTF-16 (供 COM 宽字符接口使用)
std::wstring Utf8ToUtf16(const std::string& utf8) {
    std::wstring w;
    if (utf8.empty()) return w;
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (n <= 0) return w;
    w.resize(n);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], n);
    if (!w.empty()) w.pop_back();
    return w;
}

using namespace cloud;

// 全局服务器实例
CloudServer* g_server = nullptr;
HANDLE g_server_thread = nullptr;
CRITICAL_SECTION g_log_cs;

// 窗口类名 (仅 ASCII)
const wchar_t* CLASS_NAME = L"CloudServerWindow";
// 标题用 UTF-8 窄字符串保存, 运行时经 Utf8ToUtf16 转换
// (MinGW 对 UTF-8 源码中的 L"中文" 宽字面量会按 GBK 误读, 编译期转换结果是错的)
const char* WINDOW_TITLE_UTF8 = "网盘服务器 - Cloud Server";

// GUI 控件ID
enum {
    ID_BTN_START = 1001,
    ID_BTN_STOP,
    ID_BTN_BROWSE,
    ID_EDIT_HOST,
    ID_EDIT_PORT,
    ID_EDIT_STORAGE,
    ID_EDIT_RECYCLE,
    ID_TIMER_CHECK,
};

// 全局窗口句柄
HWND g_hwnd = nullptr;
HWND g_lbl_host = nullptr;
HWND g_lbl_port = nullptr;
HWND g_lbl_storage = nullptr;
HWND g_lbl_recycle = nullptr;
HWND g_btn_start = nullptr;
HWND g_btn_stop = nullptr;
HWND g_btn_browse = nullptr;
HWND g_edit_host = nullptr;
HWND g_edit_port = nullptr;
HWND g_edit_storage = nullptr;
HWND g_edit_recycle = nullptr;
HWND g_status = nullptr;
HWND g_log = nullptr;
HFONT g_font = nullptr;

bool g_running = false;

// 当前 DPI (96 = 100%), 所有像素尺寸经 S() 缩放
UINT g_dpi = 96;

// 设计像素值 -> 当前 DPI 像素值
int S(int v) { return MulDiv(v, (int)g_dpi, 96); }

// 获取窗口当前 DPI (优先 GetDpiForWindow, 回退系统 DC)
UINT GetWindowDpi(HWND hwnd) {
    PFN_GetDpiForWindow pGet = (PFN_GetDpiForWindow)
        (void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (pGet && hwnd) return pGet(hwnd);
    HDC hdc = GetDC(hwnd);
    UINT dpi = (UINT)GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(hwnd, hdc);
    return dpi ? dpi : 96;
}

// 按 DPI 重建字体并应用到全部子控件
BOOL CALLBACK SetFontEnumProc(HWND h, LPARAM lp) {
    SendMessage(h, WM_SETFONT, (WPARAM)(HFONT)lp, TRUE);
    return TRUE;
}

void ApplyFontToAll() {
    HFONT old = g_font;
    g_font = CreateFontA(S(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "MS Shell Dlg 2");
    if (g_hwnd && g_font) EnumChildWindows(g_hwnd, SetFontEnumProc, (LPARAM)g_font);
    if (old) DeleteObject(old);
}

// ============ 自适应布局 ============
// 设计稿(96dpi): 两行配置 + 状态栏 + 日志区
//   行1: y=18  [监听地址: 80][输入框 150][端口: 50][输入框 80][启动按钮 120x30]
//   行2: y=53  [存储路径: 80][输入框 310]          [停止按钮 120x30]
//   状态: y=90  全宽
//   日志: y=120 填充剩余高度
// 输入框/日志宽度按客户区宽度动态计算, 右侧控件贴右边缘
void LayoutControls(HWND hwnd) {
    if (!g_log) return;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    int margin   = S(20);   // 外边距
    int lblW     = S(80);   // 标签宽
    int editX    = S(110);  // 输入框起始 X
    int portLblW = S(50);   // "端口:" 标签宽
    int portW    = S(80);   // 端口输入框宽
    int btnW     = S(120);  // 按钮宽
    int btnH     = S(30);   // 按钮高
    int rowH     = S(25);   // 控件高
    int row1Y    = S(18);
    int row2Y    = S(53);
    int row3Y    = S(88);
    int statusY  = S(125);
    int logY     = S(155);

    int R        = W - margin;              // 右边缘
    int btnX     = R - btnW;                // 按钮列 X
    int portX    = btnX - S(20) - portW;    // 端口输入框 X
    int portLblX = portX - S(10) - portLblW;// 端口标签 X
    int hostW    = portLblX - S(10) - editX;// 地址输入框自适应宽
    int browseW  = S(75);                   // 浏览按钮宽
    int browseX  = btnX - browseW - S(6);   // 浏览按钮 X (与停止按钮同行左侧)
    int storW    = browseX - S(6) - editX;  // 存储输入框自适应宽
    if (hostW < S(60)) hostW = S(60);
    if (storW < S(60)) storW = S(60);

    MoveWindow(g_lbl_host,    margin,  row1Y + S(2), lblW,     rowH, TRUE);
    MoveWindow(g_edit_host,   editX,   row1Y,        hostW,    rowH, TRUE);
    MoveWindow(g_lbl_port,    portLblX,row1Y + S(2), portLblW, rowH, TRUE);
    MoveWindow(g_edit_port,   portX,   row1Y,        portW,    rowH, TRUE);
    MoveWindow(g_btn_start,   btnX,    row1Y,        btnW,     btnH, TRUE);
    MoveWindow(g_lbl_storage, margin,  row2Y + S(2), lblW,     rowH, TRUE);
    MoveWindow(g_edit_storage,editX,   row2Y,        storW,    rowH, TRUE);
    MoveWindow(g_btn_browse,  browseX, row2Y,        browseW,  btnH, TRUE);
    MoveWindow(g_btn_stop,    btnX,    row2Y,        btnW,     btnH, TRUE);
    MoveWindow(g_lbl_recycle, margin,  row3Y + S(2), S(115),   rowH, TRUE);
    MoveWindow(g_edit_recycle,editX,   row3Y,        S(60),    rowH, TRUE);
    MoveWindow(g_status,      margin,  statusY,      W - 2 * margin, rowH, TRUE);
    MoveWindow(g_log,         margin,  logY,         W - 2 * margin, H - logY - margin, TRUE);
}

// DPI 变化处理: 更新 g_dpi -> 重建字体 -> 重排布局
void OnDpiChanged(HWND hwnd, UINT newDpi) {
    g_dpi = newDpi;
    ApplyFontToAll();
    LayoutControls(hwnd);
}

// 日志输出: 线程安全, 任意线程可调用
// 工作线程直接 SendMessage 写控件会阻塞等待 GUI 线程, 若 GUI 线程
// 同时在等日志锁则死锁。故改为: 锁内入队 + PostMessage 异步刷写
#define WM_APP_APPEND_LOG (WM_APP + 1)
static std::vector<std::string> g_pending_logs;

void AppendLog(const std::string& msg) {
    EnterCriticalSection(&g_log_cs);
    g_pending_logs.push_back(msg);
    LeaveCriticalSection(&g_log_cs);
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_APPEND_LOG, 0, 0);
}

// 仅 GUI 线程调用: 取出队列并写入日志控件
void FlushPendingLogs() {
    std::vector<std::string> batch;
    EnterCriticalSection(&g_log_cs);
    batch.swap(g_pending_logs);
    LeaveCriticalSection(&g_log_cs);
    if (!g_log) return;

    for (auto& msg : batch) {
        // 获取当前时间
        time_t now = time(nullptr);
        struct tm t;
        localtime_s(&t, &now);
        char time_buf[32];
        strftime(time_buf, sizeof(time_buf), "[%H:%M:%S] ", &t);

        std::string line = std::string(time_buf) + msg + "\r\n";

        // 全量读出拼接后重写: EDIT 控件对 DBCS(GBK) 文本的
        // GetWindowTextLengthA 与 EM_SETSEL 定位不一致, 逐条追加会
        // 覆盖前文尾部, 故不使用 EM_SETSEL 定位
        int len = GetWindowTextLengthA(g_log);
        std::string all(len + 1, '\0');
        GetWindowTextA(g_log, &all[0], len + 1);
        all.resize(len);
        all += line;
        SetWindowTextA(g_log, all.c_str());
    }
}

// 获取编辑框文本
std::string GetEditText(HWND hwnd) {
    int len = GetWindowTextLengthA(hwnd);
    if (len == 0) return "";
    std::string buf(len + 1, '\0');
    GetWindowTextA(hwnd, buf.data(), len + 1);
    buf.resize(len);  // 关键: 去掉填充的 '\0', 否则字符串内嵌 '\0'
                      // 会导致 sqlite3_open 提前截断路径、消息拼接被截断
    return buf;
}

// 备用: 老式文件夹浏览对话框 (SHBrowseForFolder, 兼容性最好)
std::string PickFolderLegacy(HWND owner) {
    BROWSEINFOA bi = {};
    bi.hwndOwner = owner;
    bi.lpszTitle = Utf8ToGbk("选择存储文件夹").c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (!pidl) return "";
    char path[MAX_PATH] = {0};
    std::string result;
    if (SHGetPathFromIDListA(pidl, path)) result = path;
    CoTaskMemFree(pidl);
    return result;
}

// HRESULT 转日志文本
std::string HrToStr(HRESULT hr) {
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%08lX", (unsigned long)hr);
    return buf;
}

// 弹出文件夹选择对话框 (IFileOpenDialog, Win10 现代样式)
// 返回 ANSI (GBK) 路径; 空串 = 取消或失败
std::string PickFolder(HWND owner) {
    std::string result;

    IFileOpenDialog* dlg = nullptr;
    HRESULT hr = CoCreateInstance(kCLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  kIID_IFileOpenDialog, (void**)&dlg);
    if (FAILED(hr) || !dlg) {
        AppendLog(Utf8ToGbk("现代对话框不可用(" + HrToStr(hr) + "), 使用备用对话框"));
        return PickFolderLegacy(owner);
    }

    DWORD opts = 0;
    dlg->GetOptions(&opts);
    hr = dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    if (FAILED(hr)) AppendLog(Utf8ToGbk("SetOptions 失败: " + HrToStr(hr)));
    dlg->SetTitle(Utf8ToUtf16("选择存储文件夹").c_str());

    hr = dlg->Show(owner);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        // 用户取消, 静默返回
        dlg->Release();
        return result;
    }
    if (FAILED(hr)) {
        AppendLog(Utf8ToGbk("现代对话框打开失败(" + HrToStr(hr) + "), 使用备用对话框"));
        dlg->Release();
        return PickFolderLegacy(owner);
    }

    IShellItem* item = nullptr;
    if (SUCCEEDED(dlg->GetResult(&item)) && item) {
        PWSTR path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
            // UTF-16 转 ANSI(GBK), 与输入框编码一致
            int alen = WideCharToMultiByte(CP_ACP, 0, path, -1,
                                           nullptr, 0, nullptr, nullptr);
            if (alen > 0) {
                std::vector<char> abuf(alen);
                WideCharToMultiByte(CP_ACP, 0, path, -1,
                                    abuf.data(), alen, nullptr, nullptr);
                result.assign(abuf.data());
            }
            CoTaskMemFree(path);
        }
        item->Release();
    }
    dlg->Release();
    return result;
}

// 启动服务器
void StartServer() {
    if (g_running) return;

    ServerConfig config;
    config.host = GetEditText(g_edit_host);
    config.port = std::stoi(GetEditText(g_edit_port));
    config.storage_root = GetEditText(g_edit_storage);
    config.recycle_expire_days = std::max(1, std::stoi(GetEditText(g_edit_recycle)));

    g_server = new CloudServer();
    g_server->SetLogCallback([](const std::string& msg) {
        AppendLog(Utf8ToGbk(msg));  // 服务端消息为 UTF-8, 统一转 GBK
    });

    // 复制 config 用于线程
    ServerConfig* cfg = new ServerConfig(config);
    g_server_thread = CreateThread(nullptr, 0,
        [](LPVOID param) -> DWORD {
            ServerConfig* c = (ServerConfig*)param;
            g_server->Start(*c);
            delete c;
            return 0;
        }, cfg, 0, nullptr);

    g_running = true;
    EnableWindow(g_btn_start, FALSE);
    EnableWindow(g_btn_stop, TRUE);
    EnableWindow(g_btn_browse, FALSE);
    EnableWindow(g_edit_host, FALSE);
    EnableWindow(g_edit_port, FALSE);
    EnableWindow(g_edit_storage, FALSE);
    EnableWindow(g_edit_recycle, FALSE);

    AppendLog(Utf8ToGbk("服务器已启动: http://" + config.host + ":" + std::to_string(config.port)));
    AppendLog(Utf8ToGbk("回收站保留 " + std::to_string(config.recycle_expire_days) + " 天, 过期自动清理"));
    SetWindowTextA(g_status, Utf8ToGbk("状态: 运行中").c_str());
}

// 停止服务器
void StopServer() {
    if (!g_running) return;

    if (g_server) {
        g_server->Stop();
    }

    if (g_server_thread) {
        WaitForSingleObject(g_server_thread, 5000);
        CloseHandle(g_server_thread);
        g_server_thread = nullptr;
    }

    delete g_server;
    g_server = nullptr;

    g_running = false;
    EnableWindow(g_btn_start, TRUE);
    EnableWindow(g_btn_stop, FALSE);
    EnableWindow(g_btn_browse, TRUE);
    EnableWindow(g_edit_host, TRUE);
    EnableWindow(g_edit_port, TRUE);
    EnableWindow(g_edit_storage, TRUE);
    EnableWindow(g_edit_recycle, TRUE);

    AppendLog(Utf8ToGbk("服务器已停止"));
    SetWindowTextA(g_status, Utf8ToGbk("状态: 已停止").c_str());
}

// 创建控件 (先按 DPI 缩放创建, 再由 LayoutControls 精确排版)
void CreateControls(HWND hwnd) {
    ApplyFontToAll();  // 创建 DPI 缩放字体

    g_lbl_host = CreateWindowA("STATIC", Utf8ToGbk("监听地址:").c_str(),
                              WS_CHILD | WS_VISIBLE | SS_LEFT,
                              S(20), S(20), S(80), S(25), hwnd, nullptr, nullptr, nullptr);
    SendMessage(g_lbl_host, WM_SETFONT, (WPARAM)g_font, TRUE);

    g_edit_host = CreateWindowA("EDIT", "0.0.0.0",
                                 WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                 S(110), S(18), S(150), S(25), hwnd, (HMENU)ID_EDIT_HOST, nullptr, nullptr);
    SendMessage(g_edit_host, WM_SETFONT, (WPARAM)g_font, TRUE);

    g_lbl_port = CreateWindowA("STATIC", Utf8ToGbk("端口:").c_str(),
                        WS_CHILD | WS_VISIBLE | SS_LEFT,
                        S(280), S(20), S(50), S(25), hwnd, nullptr, nullptr, nullptr);
    SendMessage(g_lbl_port, WM_SETFONT, (WPARAM)g_font, TRUE);

    g_edit_port = CreateWindowA("EDIT", "8080",
                                 WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
                                 S(340), S(18), S(80), S(25), hwnd, (HMENU)ID_EDIT_PORT, nullptr, nullptr);
    SendMessage(g_edit_port, WM_SETFONT, (WPARAM)g_font, TRUE);

    g_lbl_storage = CreateWindowA("STATIC", Utf8ToGbk("存储路径:").c_str(),
                        WS_CHILD | WS_VISIBLE | SS_LEFT,
                        S(20), S(55), S(80), S(25), hwnd, nullptr, nullptr, nullptr);
    SendMessage(g_lbl_storage, WM_SETFONT, (WPARAM)g_font, TRUE);

    g_edit_storage = CreateWindowA("EDIT", "./data",
                                    WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                    S(110), S(53), S(310), S(25), hwnd, (HMENU)ID_EDIT_STORAGE, nullptr, nullptr);
    SendMessage(g_edit_storage, WM_SETFONT, (WPARAM)g_font, TRUE);

    g_btn_start = CreateWindowA("BUTTON", Utf8ToGbk("启动服务器").c_str(),
                                 WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                 S(440), S(18), S(120), S(30), hwnd, (HMENU)ID_BTN_START, nullptr, nullptr);
    SendMessage(g_btn_start, WM_SETFONT, (WPARAM)g_font, TRUE);

    g_btn_stop = CreateWindowA("BUTTON", Utf8ToGbk("停止服务器").c_str(),
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED,
                                S(440), S(53), S(120), S(30), hwnd, (HMENU)ID_BTN_STOP, nullptr, nullptr);
    SendMessage(g_btn_stop, WM_SETFONT, (WPARAM)g_font, TRUE);

    // 浏览按钮 (弹出文件夹选择对话框)
    g_btn_browse = CreateWindowA("BUTTON", Utf8ToGbk("浏览...").c_str(),
                                  WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                  S(360), S(53), S(75), S(30), hwnd, (HMENU)ID_BTN_BROWSE, nullptr, nullptr);
    SendMessage(g_btn_browse, WM_SETFONT, (WPARAM)g_font, TRUE);

    // 回收站保留天数 (自动清理过期项)
    g_lbl_recycle = CreateWindowA("STATIC", Utf8ToGbk("回收站保留(天):").c_str(),
                        WS_CHILD | WS_VISIBLE | SS_LEFT,
                        S(20), S(90), S(115), S(25), hwnd, nullptr, nullptr, nullptr);
    SendMessage(g_lbl_recycle, WM_SETFONT, (WPARAM)g_font, TRUE);

    g_edit_recycle = CreateWindowA("EDIT", "100",
                                    WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
                                    S(110), S(88), S(60), S(25), hwnd, (HMENU)ID_EDIT_RECYCLE, nullptr, nullptr);
    SendMessage(g_edit_recycle, WM_SETFONT, (WPARAM)g_font, TRUE);

    g_status = CreateWindowA("STATIC", Utf8ToGbk("状态: 已停止").c_str(),
                              WS_CHILD | WS_VISIBLE | SS_LEFT,
                              S(20), S(125), S(540), S(25), hwnd, nullptr, nullptr, nullptr);
    SendMessage(g_status, WM_SETFONT, (WPARAM)g_font, TRUE);

    g_log = CreateWindowA("EDIT", "",
                           WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE |
                           ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                           S(20), S(155), S(540), S(320), hwnd, nullptr, nullptr, nullptr);
    SendMessage(g_log, WM_SETFONT, (WPARAM)g_font, TRUE);

    LayoutControls(hwnd);  // 立即按客户区精确排版
}

// 窗口过程
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            InitializeCriticalSection(&g_log_cs);
            CreateControls(hwnd);
            AppendLog(Utf8ToGbk("网盘服务器初始化完成"));
            AppendLog(Utf8ToGbk("配置监听地址和端口后点击启动"));
            PostMessageW(hwnd, WM_APP_APPEND_LOG, 0, 0);  // WM_CREATE 期间 g_hwnd 未就绪, 主动刷写
            return 0;

        case WM_APP_APPEND_LOG:
            FlushPendingLogs();
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_BTN_START:
                    StartServer();
                    break;
                case ID_BTN_STOP:
                    StopServer();
                    break;
                case ID_BTN_BROWSE: {
                    std::string path = PickFolder(hwnd);
                    if (!path.empty()) SetWindowTextA(g_edit_storage, path.c_str());
                    break;
                }
            }
            return 0;

        case WM_SIZE:
            LayoutControls(hwnd);
            return 0;

        case WM_DPICHANGED: {
            // 跨不同缩放比例的显示器拖动窗口时触发
            UINT newDpi = HIWORD(wParam);
            RECT* suggested = (RECT*)lParam;
            OnDpiChanged(hwnd, newDpi);
            SetWindowPos(hwnd, nullptr,
                         suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }

        case WM_GETDPISCALEDSIZE: {
            // 固定尺寸窗口: 按 DPI 比例缩放, 避免内容错位
            SIZE* size = (SIZE*)lParam;
            UINT newDpi = (UINT)wParam;
            size->cx = MulDiv(S(600), (int)newDpi, (int)g_dpi);
            size->cy = MulDiv(S(535), (int)newDpi, (int)g_dpi);
            return TRUE;
        }

        case WM_DESTROY:
            if (g_running) StopServer();
            if (g_font) DeleteObject(g_font);
            DeleteCriticalSection(&g_log_cs);
            PostQuitMessage(0);
            return 0;

        default:
            // 必须用 W 版本: 窗口类以 RegisterClassExW 注册 (Unicode 过程),
            // 若调用 DefWindowProcA, WM_SETTEXT 等含文本的消息会被按 ANSI(GBK)
            // 重新解释, 导致窗口标题乱码
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // 初始化 COM (STA), 文件夹选择对话框需要
    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // ============ DPI 感知备用声明 ============
    // 清单已声明 PerMonitorV2; 此处代码调用作为双保险
    // (清单缺失时仍能启用系统级 DPI 感知)
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    PFN_SetProcessDpiAwarenessContext pSetCtx = (PFN_SetProcessDpiAwarenessContext)
        (void*)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if (pSetCtx) {
        // Win10 1703+: Per-Monitor V2, 失败则逐级回退
        if (!pSetCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            pSetCtx((HANDLE)-3);  // PER_MONITOR_AWARE
            if (!pSetCtx((HANDLE)-3)) {
                pSetCtx((HANDLE)-2);  // SYSTEM_AWARE
            }
        }
    } else {
        // Win8.1 及更早: 系统级 DPI 感知
        HMODULE shcore = LoadLibraryW(L"shcore.dll");
        if (shcore) {
            typedef HRESULT (WINAPI *PFN_SetProcessDpiAwareness)(int);
            PFN_SetProcessDpiAwareness pSet = (PFN_SetProcessDpiAwareness)
                (void*)GetProcAddress(shcore, "SetProcessDpiAwareness");
            if (pSet) pSet(2);  // PROCESS_PER_MONITOR_DPI_AWARE
        } else {
            SetProcessDPIAware();  // Vista/XP 回退
        }
    }

    // 初始化通用控件
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    // 获取屏幕 DPI, 用于初始窗口尺寸缩放
    g_dpi = GetWindowDpi(nullptr);

    // 加载 DPI 匹配的图标
    // 多分辨率方案: 若添加自定义图标资源, 在 .rc 中包含 16/24/32/48 多个尺寸,
    // 用 LoadImage(hInst, MAKEINTRESOURCE(IDI_APP), IMAGE_ICON,
    //   GetSystemMetrics(SM_CXICON) 按比例, GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR)
    // 按当前 DPI 选择最合适尺寸; 系统图标 LoadImage 亦按此尺寸加载
    int iconCx = MulDiv(GetSystemMetrics(SM_CXICON), (int)g_dpi, 96);
    int iconCy = MulDiv(GetSystemMetrics(SM_CYICON), (int)g_dpi, 96);
    HICON hIcon = (HICON)LoadImage(nullptr, IDI_APPLICATION, IMAGE_ICON,
                                   iconCx, iconCy, LR_SHARED);

    // 注册窗口类
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = hIcon;
    wc.hIconSm = (HICON)LoadImage(nullptr, IDI_APPLICATION, IMAGE_ICON,
                                  MulDiv(GetSystemMetrics(SM_CXSMICON), (int)g_dpi, 96),
                                  MulDiv(GetSystemMetrics(SM_CYSMICON), (int)g_dpi, 96),
                                  LR_SHARED);

    if (!RegisterClassExW(&wc)) {
        MessageBoxA(nullptr, Utf8ToGbk("注册窗口类失败").c_str(),
                    Utf8ToGbk("错误").c_str(), MB_OK | MB_ICONERROR);
        return 1;
    }

    // 创建窗口 (尺寸按 DPI 缩放: 设计值 600x500)
    std::wstring title = Utf8ToUtf16(WINDOW_TITLE_UTF8);
    g_hwnd = CreateWindowExW(
        0, CLASS_NAME, title.c_str(),
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, // 固定大小
        CW_USEDEFAULT, CW_USEDEFAULT, S(600), S(535),
        nullptr, nullptr, hInstance, nullptr
    );

    if (!g_hwnd) {
        MessageBoxA(nullptr, Utf8ToGbk("创建窗口失败").c_str(),
                    Utf8ToGbk("错误").c_str(), MB_OK | MB_ICONERROR);
        return 1;
    }

    // 窗口实际所在显示器 DPI 可能与主屏不同, 校正一次
    UINT winDpi = GetWindowDpi(g_hwnd);
    if (winDpi != g_dpi) OnDpiChanged(g_hwnd, winDpi);

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    // 消息循环 (W 版本, 与 Unicode 窗口一致)
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (SUCCEEDED(hrCom)) CoUninitialize();
    return (int)msg.wParam;
}
