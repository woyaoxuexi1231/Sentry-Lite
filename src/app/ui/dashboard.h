// Dashboard 主窗口：WebView2 承载前端 + 系统托盘 + 1s 采集循环
//   native→web : postWebMessageAsJson({t:"init"|"live"|"histo"})
//   web→native : postMessage("action|...")   竖线分隔的简单文本协议
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include "core/config.h"
#include "core/snapshot.h"
#include "collectors/cpu_usage.h"
#include "collectors/mem_usage.h"
#include "collectors/gpu_usage.h"
#include "collectors/net_speed.h"
#include "temp/pawnio_client.h"
#include "temp/temp_cpu.h"
#include "temp/temp_gpu_nvml.h"
#include "history/recorder.h"
#include "history/retention.h"

namespace hwmon {

class Dashboard {
public:
    ~Dashboard();
    bool Init();
    int Run();
    void RequestQuit() {
        quitting_ = true;
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }

private:
    // Win32
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    bool CreateWindow_();
    bool AddTrayIcon();
    void InitMenu();
    void ProcessMenu(int id);

    void OnTick();                 // 1s 采集 + 落盘 + 推送 live
    void InitTemperature();
    bool CollectSnapshot(Snapshot& snap);
    void PushJson(const std::wstring& json);   // 若 webview 就绪则 postWebMessageAsJson
    void PushInit();                            // 推送 init（持续重发，直到前端 ready）

    // web→native 命令解析
    void HandleWebMessage(const std::wstring& text);
    void RequestHisto(const std::wstring& arg);   // histo|<secs>|<end_ts>|<buckets>
    void RequestQuitFromWeb();
    void OpenHistoryFolder();

    // WebView2 初始化
    bool InitWebView();

    // 历史查询线程完成后回调（主线程上执行）
    void OnHistoDone();
    std::wstring BuildHistoJson(const std::wstring& dir, uint64_t end_ts, uint64_t secs, size_t B);

    Config config_;
    CpuUsageCollector cpu_;
    MemUsageCollector mem_;
    GpuUsageCollector gpu_;
    NetSpeedCollector net_;
    Recorder recorder_;
    Retention retention_;
    std::wstring history_dir_;

    // 温度
    PawnIoClient temp_pio_;
    CpuTempReader temp_cpu_;
    GpuTempNvml temp_gpu_;
    bool pub_cpu_t_ = false;       // 是否对外发布 CPU 温度
    bool pub_gpu_t_ = false;
    std::wstring temp_cpu_msg_;
    std::wstring temp_gpu_msg_;
    uint64_t ram_total_bytes_ = 0;

    HWND hwnd_ = nullptr;
    HMENU menu_ = nullptr;
    bool tray_added_ = false;
    bool quitting_ = false;
    uint64_t last_retention_day_ = 0;

    // WebView2（COM 内部静态持有；此处仅记录状态）
    bool web_ready_ = false;          // WebView2 已创建
    bool webview_broken_ = false;     // 创建失败（无 runtime 或缺 web 目录）
    bool ui_ready_ = false;           // 前端已注册监听并处理 init（收到 "ready" 回执）

    // 后台历史查询结果（工作线程构建 JSON 字符串，主线程 OnHistoDone 只推送给前端）
    std::mutex histo_mtx_;
    std::wstring histo_json_;
    std::string histo_err_;
};

} // namespace hwmon