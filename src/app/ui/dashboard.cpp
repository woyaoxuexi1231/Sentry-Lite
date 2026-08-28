#include "dashboard.h"
#include <wrl/client.h>
#include <wrl/implements.h>
#include <wrl/event.h>
#include <WebView2.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <objbase.h>
#include <ctime>
#include <thread>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include "history/query.h"

namespace hwmon {

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;

namespace {

constexpr wchar_t kClassName[] = L"SentryLite_dashboard";
constexpr wchar_t kAppTitle[] = L"Sentry-Lite";
constexpr UINT kMsgTray  = WM_APP + 1;
constexpr UINT kMsgHisto = WM_APP + 2;
constexpr UINT kTimerSample = 1;
constexpr UINT kTrayId = 1;

constexpr int kMenuShow = 4001;
constexpr int kMenuOpenHistory = 4002;
constexpr int kMenuExit = 4099;

inline uint8_t TempToSentinel(float c) {
    return (std::isfinite(c) && c >= 0.f && c <= 200.f) ? static_cast<uint8_t>(c + 0.5f) : kSentinel;
}

std::wstring ExeDir() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    size_t sl = dir.find_last_of(L'\\');
    return sl == std::wstring::npos ? L"." : dir.substr(0, sl + 1);
}

std::wstring EscapeJson(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 8);
    for (wchar_t c : s) {
        if (c == L'"') out += L"\\\"";
        else if (c == L'\\') out += L"\\\\";
        else if (c == L'\n') out += L"\\n";
        else if (c == L'\r') out += L"\\r";
        else if (c == L'\t') out += L"\\t";
        else if (c < 0x20) { wchar_t b[8]; swprintf_s(b, L"\\u%04x", c); out += b; }
        else if (c < 0x80) out += c;
        else { wchar_t b[8]; swprintf_s(b, L"\\u%04x", c); out += b; }
    }
    return out;
}

ComPtr<ICoreWebView2Environment> g_env;
ComPtr<ICoreWebView2Controller>  g_ctrl;
ComPtr<ICoreWebView2>            g_web;

std::wstring Num1(float v) { wchar_t b[24]; swprintf_s(b, L"%.1f", v); return b; }
std::wstring Num0(float v) { wchar_t b[24]; swprintf_s(b, L"%.0f", v); return b; }

HICON LoadAppIconSize(HINSTANCE inst, int cx, int cy) {
    // LoadImage picks the best match from the multi-size .ico (no comctl32 v6 import)
    return (HICON)LoadImageW(inst, MAKEINTRESOURCE(1), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
}

void IconSizesForDpi(UINT dpi, int* cx_big, int* cy_big, int* cx_sm, int* cy_sm) {
    *cx_big = GetSystemMetricsForDpi(SM_CXICON, dpi);
    *cy_big = GetSystemMetricsForDpi(SM_CYICON, dpi);
    *cx_sm = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
    *cy_sm = GetSystemMetricsForDpi(SM_CYSMICON, dpi);
}

// WebView2 CSS viewport target (logical px). Dashboard content ~608px tall at width >= 910px.
constexpr int kCssClientW = 1008;
constexpr int kCssClientH = 620;

int PhysicalClientDim(int cssPx, UINT dpi) {
    return MulDiv(cssPx, dpi, 96);
}

// mockup 时段健康分：100 − 占用超出扣分 − 温度扣分
int BucketScore(float cpuA, float gpuA, float ramA, float cpuTA, float gpuTA) {
    float score = 100.f;
    score -= std::max(0.f, cpuA - 55.f) * 0.8f;
    score -= std::max(0.f, gpuA - 45.f) * 0.5f;
    score -= std::max(0.f, ramA - 90.f) * 2.f;
    if (cpuTA >= 85.f) score -= 12.f; else if (cpuTA >= 75.f) score -= 8.f;
    if (gpuTA >= 85.f) score -= 8.f;  else if (gpuTA >= 75.f) score -= 5.f;
    if (score > 100.f) score = 100.f;
    if (score < 0.f) score = 0.f;
    return static_cast<int>(score);
}

int BucketLevel(float score) { return score < 65.f ? 2 : (score < 85.f ? 1 : 0); }

std::wstring g_log_diag = L"C:\\Users\\15434\\AppData\\Local\\Temp\\sl_push.log";
void LogDiagnostic(const wchar_t* tag, const wchar_t* s) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, g_log_diag.c_str(), L"a") == 0 && f) {
        fprintf(f, "ts=%lld DIAG[%ls] %ls\n", (long long)time(nullptr), tag, s);
        fclose(f);
    }
}

} // namespace

void Dashboard::PushJson(const std::wstring& json) {
    if (!g_web) return;
    BSTR b = SysAllocString(json.c_str());
    if (b) {
        HRESULT hr = g_web->PostWebMessageAsJson(b);
        SysFreeString(b);
        // TEMP 诊断日志（调试后删除）
        FILE* f = nullptr;
        if (_wfopen_s(&f, L"C:\\Users\\15434\\AppData\\Local\\Temp\\sl_push.log", L"a") == 0 && f) {
            fprintf(f, "ts=%lld hr=0x%08X msg=%.60ls\n",
                    (long long)time(nullptr), (unsigned long)hr, json.c_str());
            fclose(f);
        }
    }
}

void Dashboard::PushInit() {
    std::wstring json = std::wstring(L"{\"t\":\"init\"") +
        L",\"cpuTOk\":" + (pub_cpu_t_ ? L"true" : L"false") +
        L",\"gpuTOk\":" + (pub_gpu_t_ ? L"true" : L"false") +
        L",\"cpuTMsg\":\"" + EscapeJson(temp_cpu_msg_) + L"\"" +
        L",\"gpuTMsg\":\"" + EscapeJson(temp_gpu_msg_) + L"\"}";
    PushJson(json);
}

Dashboard::~Dashboard() {
    if (tray_added_) {
        NOTIFYICONDATAW nid{}; nid.cbSize = sizeof(nid); nid.uID = kTrayId; nid.hWnd = hwnd_;
        Shell_NotifyIconW(NIM_DELETE, &nid);
    }
    recorder_.Shutdown();
    if (g_ctrl) g_ctrl->Close();
    temp_pio_.Close();
    temp_gpu_.Shutdown();
}

bool Dashboard::Init() {
    config_.Load();
    history_dir_ = Config::HistoryDir(config_.history);
    if (config_.history.enabled) {
        CreateDirectoryW(history_dir_.c_str(), nullptr);
        time_t now = time(nullptr);
        retention_.RunOnce(history_dir_, config_.history.raw_retention_days,
                           config_.history.max_total_mb, (uint64_t)now);
        recorder_.Init(history_dir_, (uint64_t)now);
    }

    unsigned long nic = 0;
    if (config_.nic != L"auto") nic = (unsigned long)_wtoi(config_.nic.c_str());
    net_.SetNicIndex(nic);
    gpu_.Init();
    InitTemperature();

    MEMORYSTATUSEX ms{sizeof(ms)};
    if (GlobalMemoryStatusEx(&ms)) ram_total_bytes_ = ms.ullTotalPhys;

    if (!CreateWindow_()) return false;
    AddTrayIcon();
    InitMenu();

    // 每秒采集 + 落盘 + 推 live
    SetTimer(hwnd_, kTimerSample, 1000u, nullptr);

    if (!InitWebView())
        SetWindowTextW(hwnd_, L"Sentry-Lite (WebView2 Runtime or web folder missing)");
    return true;
}

void Dashboard::InitTemperature() {
    std::wstring err;
    if (temp_pio_.Open(err)) {
        if (temp_cpu_.Init(temp_pio_, err)) pub_cpu_t_ = true;
        else temp_cpu_msg_ = err;
    } else {
        temp_cpu_msg_ = err.empty() ? L"PawnIO not installed" : err;
    }
    if (temp_gpu_.Init()) pub_gpu_t_ = true;
    else temp_gpu_msg_ = L"No NVIDIA GPU detected";
}

bool Dashboard::CreateWindow_() {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    UINT dpi = GetDpiForSystem();
    int cx_big = 0, cy_big = 0, cx_sm = 0, cy_sm = 0;
    IconSizesForDpi(dpi, &cx_big, &cy_big, &cx_sm, &cy_sm);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = &Dashboard::WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kClassName;
    wc.hIcon = LoadAppIconSize(inst, cx_big, cy_big);
    wc.hIconSm = LoadAppIconSize(inst, cx_sm, cy_sm);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    if (!RegisterClassExW(&wc)) return false;

    // Scale physical client area by DPI so WebView2 CSS viewport stays at kCssClientW × kCssClientH.
    const int clientW = PhysicalClientDim(kCssClientW, dpi);
    const int clientH = PhysicalClientDim(kCssClientH, dpi);
    RECT rc{0, 0, clientW, clientH};
    AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int x = (sw - w) / 2, y = std::max(20, (sh - h) / 2);

    hwnd_ = CreateWindowExW(0, kClassName, kAppTitle, WS_OVERLAPPEDWINDOW,
                            x, y, w, h, nullptr, nullptr, wc.hInstance, this);
    if (!hwnd_) return false;
    if (wc.hIcon) SendMessageW(hwnd_, WM_SETICON, ICON_BIG, (LPARAM)wc.hIcon);
    if (wc.hIconSm) SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, (LPARAM)wc.hIconSm);
    return true;
}

bool Dashboard::AddTrayIcon() {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    UINT dpi = hwnd_ ? GetDpiForWindow(hwnd_) : GetDpiForSystem();
    int cx_big = 0, cy_big = 0, cx_sm = 0, cy_sm = 0;
    IconSizesForDpi(dpi, &cx_big, &cy_big, &cx_sm, &cy_sm);

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = kTrayId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = kMsgTray;
    nid.hIcon = LoadAppIconSize(inst, cx_sm, cy_sm);
    if (!nid.hIcon)
        nid.hIcon = LoadIconW(inst, MAKEINTRESOURCE(1));
    lstrcpynW(nid.szTip, kAppTitle, 64);
    tray_added_ = !!Shell_NotifyIconW(NIM_ADD, &nid);
    return tray_added_;
}

void Dashboard::InitMenu() {
    menu_ = CreatePopupMenu();
    AppendMenuW(menu_, MF_STRING, kMenuShow, L"Show / Hide Window");
    AppendMenuW(menu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu_, MF_STRING, kMenuOpenHistory, L"Open History Folder");
    AppendMenuW(menu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu_, MF_STRING, kMenuExit, L"Exit");
}

void Dashboard::ProcessMenu(int id) {
    if (id == kMenuShow) {
        ShowWindow(hwnd_, IsWindowVisible(hwnd_) ? SW_HIDE : SW_SHOW);
        if (IsWindowVisible(hwnd_)) SetForegroundWindow(hwnd_);
    } else if (id == kMenuOpenHistory) {
        OpenHistoryFolder();
    } else if (id == kMenuExit) {
        quitting_ = true;
        DestroyWindow(hwnd_);
    }
}

void Dashboard::OpenHistoryFolder() {
    CreateDirectoryW(history_dir_.c_str(), nullptr);
    ShellExecuteW(hwnd_, L"open", history_dir_.c_str(), nullptr, nullptr, SW_SHOW);
}

void Dashboard::ClearHistoryData() {
    uint64_t now = (uint64_t)time(nullptr);
    if (config_.history.enabled)
        recorder_.ClearAll(history_dir_, now);
    PushJson(L"{\"t\":\"cleared\"}");
}

void Dashboard::RequestQuitFromWeb() {
    quitting_ = true;
    PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

LRESULT CALLBACK Dashboard::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Dashboard* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<Dashboard*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = reinterpret_cast<Dashboard*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMsg(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Dashboard::HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE:
        if (g_ctrl) { RECT rc{}; GetClientRect(hwnd, &rc); g_ctrl->put_Bounds(rc); }
        return 0;
    case WM_TIMER:
        if (wp == kTimerSample) OnTick();
        return 0;
    case kMsgTray: {
        const UINT trayMsg = LOWORD(lp);
        if (trayMsg == WM_RBUTTONUP || trayMsg == WM_CONTEXTMENU) {
            SetForegroundWindow(hwnd_);
            POINT pt{};
            GetCursorPos(&pt);
            const UINT cmd = TrackPopupMenu(
                menu_, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd_, nullptr);
            PostMessageW(hwnd_, WM_NULL, 0, 0);
            if (cmd) ProcessMenu(static_cast<int>(cmd));
        } else if (trayMsg == WM_LBUTTONUP || trayMsg == WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd_, IsWindowVisible(hwnd_) ? SW_HIDE : SW_SHOW);
            if (IsWindowVisible(hwnd_)) SetForegroundWindow(hwnd_);
        }
        return 0;
    }
    case WM_COMMAND:
        if (HIWORD(wp) == 0) ProcessMenu(LOWORD(wp));
        return 0;
    case kMsgHisto:
        OnHistoDone();
        return 0;
    case WM_CLOSE:
        if (quitting_) DestroyWindow(hwnd_);
        else ShowWindow(hwnd_, SW_HIDE);   // 关闭按钮 → 隐藏到托盘
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd_, kTimerSample);
        recorder_.FlushNow((uint64_t)time(nullptr));  // 退出前落盘
        PostQuitMessage(0);
        return 0;
    case WM_ERASEBKGND:
        return 1;   // WebView2 覆盖，避免闪烁
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------- WebView2 ----------------
bool Dashboard::InitWebView() {
    std::wstring html = ExeDir() + L"web";
    if (!PathFileExistsW((html + L"\\dashboard.html").c_str())) { webview_broken_ = true; return false; }
    // file:// URI 只接受正斜杠，反斜杠会导致 Chromium 导航失败（白屏）
    for (wchar_t& ch : html) if (ch == L'\\') ch = L'/';

    // 强制软件合成：部分环境上 WebView2 页面/JS 正常、消息互通，但 GPU 合成器不把内容呈现到窗口（白屏或旧帧）。
    // 必须在 CreateEnvironment 之前注入附加浏览器参数，才对浏览器进程生效。
    SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", L"--disable-gpu");

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, html](HRESULT res, ICoreWebView2Environment* environment) -> HRESULT {
                if (FAILED(res)) { webview_broken_ = true; return res; }
                g_env = environment;
                return g_env->CreateCoreWebView2Controller(
                    hwnd_,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this, html](HRESULT cres, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(cres)) { webview_broken_ = true; return cres; }
                            g_ctrl = controller;
                            g_ctrl->get_CoreWebView2(g_web.GetAddressOf());
                            if (!g_web) { webview_broken_ = true; return E_FAIL; }

                            // web → native 命令
                            g_web->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        LPWSTR s = nullptr;
                                        if (SUCCEEDED(args->TryGetWebMessageAsString(&s)) && s) {
                                            HandleWebMessage(s);
                                            CoTaskMemFree(s);
                                        }
                                        return S_OK;
                                    }).Get(), nullptr);

                            // 页面加载完成 → 推送 init（确保前端监听已就绪）
                            g_web->add_NavigationCompleted(
                                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                        BOOL ok = FALSE;
                                        args->get_IsSuccess(&ok);
                                        if (ok) {
                                            g_web->ExecuteScript(L"window.__slNavOk=true", nullptr);
                                            PushInit();
                                        }
                                        return S_OK;
                                    }).Get(), nullptr);

                            web_ready_ = true;
                            RECT rc{}; GetClientRect(hwnd_, &rc);
                            g_ctrl->put_Bounds(rc);
                            g_ctrl->put_IsVisible(TRUE);   // 默认不可见，必须显式显示控件

                            // 加载本地 dashboard.html（file:// 相对路径安全）
                            std::wstring uri = L"file:///" + html + L"/dashboard.html";
                            g_web->Navigate(uri.c_str());
                            ShowWindow(hwnd_, SW_SHOW);
                            return S_OK;
                        }).Get());
            }).Get());
    (void)hr;
    return true;
}

bool Dashboard::CollectSnapshot(Snapshot& snap) {
    snap.cpu_pct = cpu_.Tick();
    snap.gpu_pct = gpu_.Tick();
    snap.ram_pct = mem_.Tick();
    float up = 0.f, dn = 0.f;
    if (net_.Tick(up, dn)) { snap.net_up_bps = up; snap.net_down_bps = dn; }
    snap.cpu_temp_c = pub_cpu_t_ ? TempToSentinel(temp_cpu_.ReadC()) : kSentinel;
    snap.gpu_temp_c = pub_gpu_t_ ? TempToSentinel(temp_gpu_.ReadC()) : kSentinel;
    return true;
}

void Dashboard::OnTick() {
    Snapshot snap;
    CollectSnapshot(snap);

    time_t now = time(nullptr);
    recorder_.Record(snap, (uint64_t)now);

    // 每日保留策略
    uint64_t day = ((uint64_t)now) / 86400;
    if (day != last_retention_day_) {
        retention_.RunOnce(history_dir_, config_.history.raw_retention_days,
                           config_.history.max_total_mb, (uint64_t)now);
        last_retention_day_ = day;
    }

    if (!web_ready_) return;

    uint8_t cpu = snap.cpu_pct >= 0.f ? (uint8_t)(snap.cpu_pct + 0.5f) : 0xFF;
    uint8_t gpu = snap.gpu_pct >= 0.f ? (uint8_t)(snap.gpu_pct + 0.5f) : 0xFF;
    uint8_t ram = snap.ram_pct >= 0.f ? (uint8_t)(snap.ram_pct + 0.5f) : 0xFF;
    double used = ram_total_bytes_ ? (ram == 0xFF ? 0.0 : ram / 100.0 * (double)ram_total_bytes_) : -1.0;

    std::wstring json;
    json = L"{\"t\":\"live\"";
    json += L",\"cpu\":" + Num0((float)cpu) + L",\"gpu\":" + Num0((float)gpu) + L",\"ram\":" + Num0((float)ram);
    json += L",\"cpuT\":" + (pub_cpu_t_ ? Num1((float)snap.cpu_temp_c) : L"-1");
    json += L",\"gpuT\":" + (pub_gpu_t_ ? Num1(snap.gpu_temp_c) : L"-1");
    json += L",\"up\":" + Num0(snap.net_up_bps) + L",\"dn\":" + Num0(snap.net_down_bps);
    json += L",\"ramUsed\":";
    json += (used >= 0) ? std::to_wstring((long long)used) : L"-1";
    json += L",\"ramTotal\":" + std::to_wstring((long long)ram_total_bytes_);
    json += L"}";
    PushJson(json);

    // init 会影响前端切换加载遮罩；在未收到前端 ready 回执前反复重发，确保最终送达
    if (!ui_ready_) PushInit();

    // TEMP 诊断：每 5 秒用 ExecuteScript 探测页面真实状态（与 chrome.webview 无关，调试后删除）
    static int probe_ticks = 0;
    if ((++probe_ticks % 5 == 0) && g_web) {
        g_web->ExecuteScript(
            L"(function(){var L=document.getElementById('loading');"
            L"var A=document.getElementById('app');"
            L"return 'wv='+(!!(window.chrome&&window.chrome.webview))"
            L"+'|ld='+(L?L.hidden:'na')"
            L"+'|app='+(A?(!A.hidden):'na')"
            L"+'|msg='+(window.__msgN||0)"
            L"+'|init='+(window.__initN||0)"
            L"+'|t='+(window.__lastT||'-')"
            L"+'|pf='+(window.__pf||'-')"
            L"+'|err='+(window.__err||'-')"
            L"+'|body='+document.body.innerHTML.length;})()",
            Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                [](HRESULT, LPCWSTR r) -> HRESULT {
                    LogDiagnostic(L"PROBE", r ? r : L"(null)");
                    return S_OK;
                }).Get());
    }
}

// ---------------- web → native ----------------
void Dashboard::HandleWebMessage(const std::wstring& text) {
    // 协议："action|arg1|arg2|arg3"
    size_t p1 = text.find(L'|');
    std::wstring action = (p1 == std::wstring::npos) ? text : text.substr(0, p1);
    std::wstring arg = (p1 == std::wstring::npos) ? std::wstring() : text.substr(p1 + 1);
    if (action == L"histo") RequestHisto(arg);
    else if (action == L"quit") RequestQuitFromWeb();
    else if (action == L"ready") ui_ready_ = true;
    else if (action == L"openHistory") OpenHistoryFolder();
    else if (action == L"clearHistory") ClearHistoryData();
}

void Dashboard::RequestHisto(const std::wstring& arg) {
    size_t p1 = arg.find(L'|'), p2 = std::wstring::npos;
    if (p1 != std::wstring::npos) p2 = arg.find(L'|', p1 + 1);
    if (p1 == std::wstring::npos) return;
    std::wstring ss = arg.substr(0, p1);
    std::wstring se = (p2 == std::wstring::npos) ? arg.substr(p1 + 1) : arg.substr(p1 + 1, p2 - p1 - 1);
    std::wstring sb = (p2 == std::wstring::npos) ? L"48" : arg.substr(p2 + 1);
    uint64_t secs = (uint64_t)_wtoi64(ss.c_str());
    uint64_t end_ts = (uint64_t)_wtoi64(se.c_str());
    size_t B = (size_t)std::max(1, _wtoi(sb.c_str()));
    if (secs < 30 || end_ts == 0) return;
    if (secs > 90ull * 24 * 3600) secs = 90ull * 24 * 3600;

    std::wstring dir = history_dir_;
    std::thread([this, dir, end_ts, secs, B]() {
        std::wstring json = BuildHistoJson(dir, end_ts, secs, B);
        std::lock_guard<std::mutex> lk(histo_mtx_);
        histo_json_ = std::move(json);
        PostMessageW(hwnd_, kMsgHisto, 0, 0);
    }).detach();
}

std::wstring Dashboard::BuildHistoJson(const std::wstring& dir, uint64_t end_ts, uint64_t secs, size_t B) {
    uint64_t start = end_ts > secs ? end_ts - secs : 0;
    std::vector<RawSample> samples;
    QueryRawRange(dir, start, end_ts, samples);

    std::wstring out;
    out = L"{\"t\":\"histo\",\"sec\":" + Num0((float)secs) + L",";

    if (samples.empty()) {
        out += L"\"buckets\":[";
        for (size_t i = 0; i < B; ++i) {
            if (i) out += L",";
            out += L"{\"empty\":true}";
        }
        out += L"],\"stats\":{"
               L"\"cpu\":{\"max\":-1,\"avg\":-1},"
               L"\"cpuT\":{\"max\":-1,\"avg\":-1},"
               L"\"gpu\":{\"max\":-1,\"avg\":-1},"
               L"\"gpuT\":{\"max\":-1,\"avg\":-1},"
               L"\"ram\":{\"max\":-1,\"avg\":-1}"
               L"}}";
        return out;
    }

    // 每桶聚合（字段按 ui-mockup 命名：cpuA/cpuTA/.../score/level）
    struct Bucket {
        double cpuA=0, cpuTA=0, gpuA=0, gpuTA=0, ramA=0, dnA=0, upA=0;
        double score=100; int level=0;
    };

    auto idx = [&](uint64_t ts) -> size_t {   // 归一化到桶下标
        size_t k = (ts <= start) ? 0 : (size_t)((double)(ts - start) / (double)secs * (double)B);
        if (k >= B) k = B - 1;
        return k;
    };

    std::vector<Bucket> bk(B);
    std::vector<int> nSamples(B, 0), nCpu(B), nGpu(B), nRam(B), nCt(B), nGt(B), nNet(B);

    // 全局统计
    double csum=0, gs=0, rs=0, cts=0, gts=0;
    int cn=0, gn=0, rn=0, ctn=0, gtn=0;
    uint8_t bkMaxCpu=0, bkMaxGpu=0, bkMaxRam=0, bkMaxCt=0, bkMaxGt=0;

    for (const RawSample& s : samples) {
        size_t k = idx(s.ts_unix);
        nSamples[k]++;
        bool cpuOk = s.cpu_pct != kSentinel, gpuOk = s.gpu_pct != kSentinel;
        bool ramOk = s.ram_pct != kSentinel, ctOk = s.cpu_temp_c != kSentinel, gtOk = s.gpu_temp_c != kSentinel;
        if (cpuOk) { bk[k].cpuA += s.cpu_pct; nCpu[k]++; } else nCpu[k]++;
        if (gpuOk) { bk[k].gpuA += s.gpu_pct; nGpu[k]++; } else nGpu[k]++;
        if (ramOk) { bk[k].ramA += s.ram_pct; nRam[k]++; }
        bk[k].dnA += s.net_down_bps; bk[k].upA += s.net_up_bps; nNet[k]++;
        if (ctOk) { bk[k].cpuTA += s.cpu_temp_c; nCt[k]++; }
        if (gtOk) { bk[k].gpuTA += s.gpu_temp_c; nGt[k]++; }

        if (cpuOk) { csum += s.cpu_pct; if (s.cpu_pct > bkMaxCpu) bkMaxCpu = s.cpu_pct; cn++; }
        if (gpuOk) { gs += s.gpu_pct;    if (s.gpu_pct > bkMaxGpu) bkMaxGpu = s.gpu_pct; gn++; }
        if (ramOk) { rs += s.ram_pct;    if (s.ram_pct > bkMaxRam) bkMaxRam = s.ram_pct; rn++; }
        if (ctOk)  { cts += s.cpu_temp_c; if (s.cpu_temp_c > bkMaxCt) bkMaxCt = s.cpu_temp_c; ctn++; }
        if (gtOk)  { gts += s.gpu_temp_c; if (s.gpu_temp_c > bkMaxGt) bkMaxGt = s.gpu_temp_c; gtn++; }
    }

    // 桶 JSON
    out += L"\"buckets\":[";
    bool first = true;
    for (size_t i = 0; i < B; ++i) {
        if (!first) out += L",";
        first = false;
        if (nSamples[i] == 0) {
            out += L"{\"empty\":true}";
            continue;
        }
        const Bucket& b = bk[i];
        float cpuA = nCpu[i] ? (float)(b.cpuA / nCpu[i]) : 0.f;
        float gpuA = nGpu[i] ? (float)(b.gpuA / nGpu[i]) : 0.f;
        float ramA = nRam[i] ? (float)(b.ramA / nRam[i]) : 0.f;
        float ctA  = nCt[i]  ? (float)(b.cpuTA / nCt[i]) : -1.f;
        float gtA  = nGt[i]  ? (float)(b.gpuTA / nGt[i]) : -1.f;
        int score = BucketScore(cpuA, gpuA, ramA, ctA, gtA);
        out += L"{\"cpuA\":" + Num0(cpuA) + L",\"cpuTA\":" + Num1(ctA < 0 ? -1 : ctA) +
               L",\"gpuA\":" + Num0(gpuA) + L",\"gpuTA\":" + Num1(gtA < 0 ? -1 : gtA) +
               L",\"ramA\":" + Num0(ramA) +
               L",\"dnA\":" + Num0(nNet[i] ? (float)(b.dnA / nNet[i]) : 0.f) +
               L",\"upA\":" + Num0(nNet[i] ? (float)(b.upA / nNet[i]) : 0.f) +
               L",\"score\":" + Num0((float)score) + L",\"level\":" + std::to_wstring(BucketLevel((float)score)) + L"}";
    }
    out += L"],";

    // stats
    auto stat = [](double sum, long long max, int n, bool has)->std::wstring {
        if (!has || n == 0) return L"{\"max\":-1,\"avg\":-1}";
        wchar_t b[64];
        swprintf_s(b, L"{\"max\":%lld,\"avg\":%.1f}", max, sum / n);
        return b;
    };
    out += std::wstring(L"\"stats\":{")
        + L"\"cpu\":" + stat(csum, bkMaxCpu, cn, cn > 0) + L","
        + L"\"cpuT\":" + stat(cts, bkMaxCt, ctn, ctn > 0) + L","
        + L"\"gpu\":" + stat(gs, bkMaxGpu, gn, gn > 0) + L","
        + L"\"gpuT\":" + stat(gts, bkMaxGt, gtn, gtn > 0) + L","
        + L"\"ram\":" + stat(rs, bkMaxRam, rn, rn > 0)
        + L"}}";
    return out;
}

void Dashboard::OnHistoDone() {
    std::wstring json;
    { std::lock_guard<std::mutex> lk(histo_mtx_); json.swap(histo_json_); }
    if (!json.empty()) PushJson(json);
}

int Dashboard::Run() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

} // namespace hwmon