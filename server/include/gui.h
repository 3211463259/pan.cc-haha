#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <functional>
#include <vector>

namespace cloud {

struct GuiConfig {
    std::string host = "0.0.0.0";
    int port = 8080;
    std::string storage_root = "./data";
};

class GuiManager {
public:
    using StartCallback = std::function<void(const GuiConfig&)>;
    using StopCallback = std::function<void()>;

    GuiManager();
    ~GuiManager();

    bool Create(HINSTANCE hInstance, int nCmdShow);
    int Run();  // 消息循环

    void AppendLog(const std::string& msg);
    void UpdateStatus(const std::string& status, bool running);

    void SetStartCallback(StartCallback cb) { start_cb_ = cb; }
    void SetStopCallback(StopCallback cb) { stop_cb_ = cb; }

    GuiConfig GetConfig() const { return config_; }

    static GuiManager* Instance() { return instance_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateControls();
    void OnStartStop();
    void LayoutControls();

    HWND hwnd_ = nullptr;
    HWND hwnd_btn_start_ = nullptr;
    HWND hwnd_btn_stop_ = nullptr;
    HWND hwnd_edit_host_ = nullptr;
    HWND hwnd_edit_port_ = nullptr;
    HWND hwnd_edit_storage_ = nullptr;
    HWND hwnd_status_ = nullptr;
    HWND hwnd_log_ = nullptr;

    HINSTANCE hinstance_ = nullptr;
    HFONT font_ = nullptr;

    GuiConfig config_;
    bool running_ = false;
    std::vector<std::string> log_lines_;

    StartCallback start_cb_;
    StopCallback stop_cb_;

    static GuiManager* instance_;
    static const int ID_BTN_START = 1001;
    static const int ID_BTN_STOP = 1002;
    static const int ID_EDIT_HOST = 1011;
    static const int ID_EDIT_PORT = 1012;
    static const int ID_EDIT_STORAGE = 1013;
};

} // namespace cloud
