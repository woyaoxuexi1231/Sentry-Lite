// Dashboard 主窗口：WebView2 承载前端 + 系统托盘 + 1s 采集循环
//   native→web : postWebMessageAsJson({t:"init"|"live"|"histo"})
//   web→native : postMessage("action|...")   竖线分隔的简单文本协议
#pragma once
#include <windows.h>
#include <string>
#include <mutex>
#include "core/config.h"
#include "core/snapshot.h"
#include "collectors/cpu_usage.h"
#include "collectors/mem_usage.h"
#include "collectors/gpu_usage.h"
#include "collectors/net_speed.h"
#include "temp/temp_cpu_lhm.h"
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
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    bool CreateWindow_();
    bool AddTrayIcon();
    void InitMenu();
    void ProcessMenu(int id);

    void OnTick();
    void InitTemperature();
    bool CollectSnapshot(Snapshot& snap);
    void PushJson(const std::wstring& json);
    void PushInit();

    void HandleWebMessage(const std::wstring& text);
    void RequestHisto(const std::wstring& arg);
    void RequestQuitFromWeb();
    void OpenHistoryFolder();
    void ClearHistoryData();

    bool InitWebView();
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

    CpuTempReader temp_cpu_;
    GpuTempNvml temp_gpu_;
    bool pub_cpu_t_ = false;
    bool cpu_temp_ok_ = false;
    bool pub_gpu_t_ = false;
    std::wstring temp_cpu_msg_;
    std::wstring temp_gpu_msg_;
    uint32_t temp_retry_sec_ = 0;
    uint64_t ram_total_bytes_ = 0;

    HWND hwnd_ = nullptr;
    HMENU menu_ = nullptr;
    bool tray_added_ = false;
    bool quitting_ = false;
    uint64_t last_retention_day_ = 0;

    bool web_ready_ = false;
    bool webview_broken_ = false;
    bool ui_ready_ = false;

    std::mutex histo_mtx_;
    std::wstring histo_json_;
};

} // namespace hwmon
