#include "float_bar.h"
#include "ui/history_view.h"
#include "core/service_install.h"
#include <windowsx.h>
#include <shellapi.h>
#include <ctime>
#include "core/config.h"

namespace hwmon {

namespace {

constexpr wchar_t kAppTitle[] = L"Sentry-Lite";
constexpr wchar_t kClassName[] = L"SentryLite_bar";
constexpr UINT kTickTimer = 1;
constexpr int kIdTopmost = 2001;
constexpr int kIdClickThrough = 2002;
constexpr int kIdHistoryView = 2003;
constexpr int kIdOpenHistory = 2004;
constexpr int kIdInstallTempSvc = 2005;
constexpr int kIdUninstallTempSvc = 2006;
constexpr int kIdExit = 2099;

AppContext* g_ctx = nullptr;

void ApplyExStyles(HWND hwnd, const Config& cfg) {
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    ex |= WS_EX_LAYERED | WS_EX_APPWINDOW | WS_EX_NOACTIVATE;
    ex &= ~WS_EX_TOOLWINDOW; // TOOLWINDOW 会隐藏任务栏按钮
    if (cfg.topmost) ex |= WS_EX_TOPMOST;
    else ex &= ~WS_EX_TOPMOST;
    if (cfg.click_through) ex |= WS_EX_TRANSPARENT;
    else ex &= ~WS_EX_TRANSPARENT;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
    SetWindowPos(hwnd, cfg.topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

// 限制在当前显示器工作区内（顶部已有系统约束，这里补左右边界）
void ClampToWorkArea(HWND hwnd, RECT& rc) {
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(mon, &mi)) return;

    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    const RECT& work = mi.rcWork;

    if (rc.left < work.left) {
        rc.left = work.left;
        rc.right = rc.left + w;
    }
    if (rc.right > work.right) {
        rc.right = work.right;
        rc.left = rc.right - w;
    }
    if (rc.top < work.top) {
        rc.top = work.top;
        rc.bottom = rc.top + h;
    }
}

void PlaceInitial(HWND hwnd, const Config& cfg) {
    RECT rc{};
    GetWindowRect(hwnd, &rc);
    if (cfg.position_x >= 0 && cfg.position_y >= 0) {
        rc.left = cfg.position_x;
        rc.top = cfg.position_y;
        rc.right = rc.left + (rc.right - rc.left);
        rc.bottom = rc.top + (rc.bottom - rc.top);
    } else {
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(mon, &mi);
        const int w = rc.right - rc.left;
        rc.left = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
        rc.top = mi.rcWork.top + 24;
        rc.right = rc.left + w;
        rc.bottom = rc.top + (rc.bottom - rc.top);
    }
    ClampToWorkArea(hwnd, rc);
    SetWindowPos(hwnd, nullptr, rc.left, rc.top, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
}

void TickAll(AppContext& ctx, HWND hwnd) {
    time_t now_t = time(nullptr);
    uint64_t now = static_cast<uint64_t>(now_t);

    Snapshot snap;
    ctx.shm.Refresh();
    snap.cpu_temp_c = ctx.shm.cpu_temp_c;
    snap.gpu_temp_c = ctx.shm.gpu_temp_c;
    snap.cpu_pct = ctx.cpu.Tick();
    snap.ram_pct = ctx.mem.Tick();
    float up = 0, down = 0;
    ctx.net.Tick(up, down);
    snap.net_up_bps = up;
    snap.net_down_bps = down;
    snap.gpu_pct = ctx.gpu.Tick();

    ctx.recorder.Record(snap, now);

    tm utc{};
    gmtime_s(&utc, &now_t);
    unsigned day = static_cast<unsigned>((utc.tm_year + 1900) * 10000u +
                                         (utc.tm_mon + 1) * 100u + utc.tm_mday);
    if (ctx.config.history.enabled && ctx.last_retention_day != day) {
        ctx.last_retention_day = day;
        std::wstring dir = Config::HistoryDir(ctx.config.history);
        ctx.retention.RunOnce(dir, ctx.config.history.raw_retention_days,
                              ctx.config.history.max_total_mb, now);
    }

    float dpi_scale = static_cast<float>(GetDpiForWindow(hwnd)) / 96.f;
    ctx.renderer.Render(snap, dpi_scale, ctx.config.temp_warn_c,
                        ctx.config.temp_crit_c, ctx.recorder.disk_error());
}

void OpenHistoryFolder() {
    if (!g_ctx->config.history.enabled) {
        MessageBoxW(nullptr, L"历史记录已在配置中关闭。", kAppTitle, MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::wstring dir = Config::HistoryDir(g_ctx->config.history);
    CreateDirectoryW(dir.c_str(), nullptr);
    HINSTANCE r = ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(r) <= 32) {
        MessageBoxW(nullptr, L"无法打开历史数据文件夹。", kAppTitle, MB_OK | MB_ICONERROR);
    }
}

void ShowContextMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (g_ctx->config.topmost ? MF_CHECKED : 0),
                kIdTopmost, L"置顶");
    AppendMenuW(menu, MF_STRING | (g_ctx->config.click_through ? MF_CHECKED : 0),
                kIdClickThrough, L"鼠标穿透");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    if (g_ctx->config.history.enabled) {
        AppendMenuW(menu, MF_STRING, kIdHistoryView, L"历史记录…");
        AppendMenuW(menu, MF_STRING, kIdOpenHistory, L"打开历史数据文件夹");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    if (IsTempServiceInstalled())
        AppendMenuW(menu, MF_STRING, kIdUninstallTempSvc, L"卸载温度服务");
    else
        AppendMenuW(menu, MF_STRING, kIdInstallTempSvc, L"启用温度监控…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kIdExit, L"退出");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                             pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    switch (cmd) {
    case kIdTopmost:
        g_ctx->config.topmost = !g_ctx->config.topmost;
        ApplyExStyles(hwnd, g_ctx->config);
        g_ctx->config.Save();
        break;
    case kIdClickThrough:
        g_ctx->config.click_through = !g_ctx->config.click_through;
        ApplyExStyles(hwnd, g_ctx->config);
        g_ctx->config.Save();
        break;
    case kIdOpenHistory:
        OpenHistoryFolder();
        break;
    case kIdHistoryView:
        ShowHistoryViewer(hwnd, g_ctx->config, g_ctx->recorder);
        break;
    case kIdInstallTempSvc: {
        std::wstring err;
        if (InstallTempService(err))
            MessageBoxW(hwnd,
                L"温度服务已安装并尝试启动。\n\n"
                L"请确保已安装 PawnIO（https://pawnio.eu）及对应模块 blob。\n"
                L"若无 PawnIO，GPU 温度（NVIDIA）仍可能可用。",
                kAppTitle, MB_OK | MB_ICONINFORMATION);
        else
            MessageBoxW(hwnd, err.c_str(), kAppTitle, MB_OK | MB_ICONWARNING);
        break;
    }
    case kIdUninstallTempSvc: {
        std::wstring err;
        if (UninstallTempService(err))
            MessageBoxW(hwnd, L"温度服务已卸载。", kAppTitle, MB_OK | MB_ICONINFORMATION);
        else
            MessageBoxW(hwnd, err.c_str(), kAppTitle, MB_OK | MB_ICONWARNING);
        break;
    }
    case kIdExit:
        DestroyWindow(hwnd);
        break;
    default:
        break;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        auto* ctx = static_cast<AppContext*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
        ApplyExStyles(hwnd, ctx->config);
        if (!ctx->renderer.Init(hwnd)) return -1;
        SetTimer(hwnd, kTickTimer, static_cast<UINT>(ctx->config.interval_ms), nullptr);
        return 0;
    }
    case WM_TIMER:
        if (wp == kTickTimer && g_ctx) TickAll(*g_ctx, hwnd);
        return 0;
    case WM_NCHITTEST: {
        LRESULT base = DefWindowProcW(hwnd, msg, wp, lp);
        if (base == HTCLIENT) return HTCAPTION;
        return base;
    }
    case WM_MOVING: {
        auto* rc = reinterpret_cast<RECT*>(lp);
        ClampToWorkArea(hwnd, *rc);
        return TRUE;
    }
    case WM_NCRBUTTONUP:
    case WM_RBUTTONUP:
        ShowContextMenu(hwnd);
        return 0;
    case WM_DPICHANGED:
        if (g_ctx) TickAll(*g_ctx, hwnd);
        return 0;
    case WM_EXITSIZEMOVE: {
        RECT rc{};
        GetWindowRect(hwnd, &rc);
        ClampToWorkArea(hwnd, rc);
        SetWindowPos(hwnd, nullptr, rc.left, rc.top, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
        g_ctx->config.position_x = rc.left;
        g_ctx->config.position_y = rc.top;
        g_ctx->config.Save();
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, kTickTimer);
        if (g_ctx) {
            g_ctx->recorder.Shutdown();
            g_ctx->renderer.Shutdown();
            g_ctx->config.Save();
        }
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

} // namespace

bool RunFloatBar(AppContext& ctx, int /*show_cmd*/) {
    g_ctx = &ctx;

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!wc.hIconSm) wc.hIconSm = wc.hIcon;
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_APPWINDOW | WS_EX_NOACTIVATE |
            (ctx.config.topmost ? WS_EX_TOPMOST : 0),
        kClassName, kAppTitle, WS_POPUP, 0, 0, 120, 40,
        nullptr, nullptr, wc.hInstance, &ctx);
    if (!hwnd) return false;

    PlaceInitial(hwnd, ctx.config);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    {
        Snapshot boot;
        float dpi = static_cast<float>(GetDpiForWindow(hwnd)) / 96.f;
        ctx.renderer.Render(boot, dpi, ctx.config.temp_warn_c, ctx.config.temp_crit_c, false);
    }
    TickAll(ctx, hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_ctx = nullptr;
    return true;
}

} // namespace hwmon
