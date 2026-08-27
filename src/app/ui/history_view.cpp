#include "history_view.h"
#include "history/query.h"
#include "ui/theme.h"
#include <d2d1.h>
#include <dwrite.h>
#include <commdlg.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "comdlg32.lib")

namespace hwmon {

namespace {

constexpr wchar_t kClass[] = L"SentryLite_history";
constexpr wchar_t kTitle[] = L"历史记录 — Sentry-Lite";
constexpr int kToolbarH = 44;
constexpr int kStatsH = 28;
constexpr int kStatusH = 28;
constexpr int kCrosshairH = 36;
constexpr int kLaneCount = 4;
constexpr int kLanePad = 4;

enum RangePreset { R15m, R1h, R6h, R24h, R7d, R30d, RCount };

struct ViewState {
    Config config;
    const Recorder* recorder = nullptr;
    HWND hwnd = nullptr;

    ID2D1Factory* factory = nullptr;
    ID2D1HwndRenderTarget* rt = nullptr;
    IDWriteFactory* dwrite = nullptr;
    IDWriteTextFormat* fmt_label = nullptr;
    IDWriteTextFormat* fmt_mono = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;

    std::vector<RawSample> samples;
    HistoryStats stats{};
    RangePreset preset = R1h;
    uint64_t view_start = 0;
    uint64_t view_end = 0;
    int cursor_x = -1;
    RawSample cursor_sample{};
    bool cursor_valid = false;
    bool cursor_exact = false;
    bool loading = false;
};

ViewState* St(HWND h) {
    return reinterpret_cast<ViewState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
}

uint64_t NowUnix() { return static_cast<uint64_t>(time(nullptr)); }

uint64_t PresetSeconds(RangePreset p) {
    switch (p) {
    case R15m: return 15 * 60;
    case R1h:  return 3600;
    case R6h:  return 6 * 3600;
    case R24h: return 24 * 3600;
    case R7d:  return 7 * 86400;
    case R30d: return 30 * 86400;
    default:   return 3600;
    }
}

const wchar_t* PresetLabel(RangePreset p) {
    static const wchar_t* labels[] = {L"15分钟", L"1小时", L"6小时", L"24小时", L"7天", L"30天"};
    return labels[p];
}

void FormatTs(uint64_t ts, wchar_t* buf, size_t n, bool with_sec = true) {
    time_t t = static_cast<time_t>(ts);
    tm local{};
    localtime_s(&local, &t);
    if (with_sec)
        swprintf(buf, n, L"%04d-%02d-%02d %02d:%02d:%02d",
                 local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                 local.tm_hour, local.tm_min, local.tm_sec);
    else
        swprintf(buf, n, L"%02d:%02d", local.tm_hour, local.tm_min);
}

void FormatSpeed(float bps, wchar_t* out, size_t n) {
    if (bps < 0 || !std::isfinite(bps)) { swprintf(out, n, L"—"); return; }
    const wchar_t* unit = L"B/s";
    double v = bps;
    if (v >= 1024.0 * 1024) { v /= 1024.0 * 1024; unit = L"MB/s"; }
    else if (v >= 1024.0)   { v /= 1024.0; unit = L"KB/s"; }
    swprintf(out, n, L"%.1f%s", v, unit);
}

void FormatTemp(uint8_t t, wchar_t* buf, size_t n) {
    if (t == kSentinel) swprintf(buf, n, L"—");
    else swprintf(buf, n, L"%u°C", t);
}

void FormatPct(uint8_t v, wchar_t* buf, size_t n) {
    if (!valid_pct(v)) swprintf(buf, n, L"—");
    else swprintf(buf, n, L"%u%%", v);
}

void ResetD2D(ViewState* s) {
    if (s->brush) { s->brush->Release(); s->brush = nullptr; }
    if (s->rt) { s->rt->Release(); s->rt = nullptr; }
    if (s->fmt_label) { s->fmt_label->Release(); s->fmt_label = nullptr; }
    if (s->fmt_mono) { s->fmt_mono->Release(); s->fmt_mono = nullptr; }
    if (s->dwrite) { s->dwrite->Release(); s->dwrite = nullptr; }
    if (s->factory) { s->factory->Release(); s->factory = nullptr; }
}

bool InitD2D(ViewState* s) {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &s->factory))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&s->dwrite))))
        return false;
    RECT rc{};
    GetClientRect(s->hwnd, &rc);
    if (FAILED(s->factory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(s->hwnd,
                D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)),
            &s->rt)))
        return false;
    s->dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                12.f, L"zh-cn", &s->fmt_label);
    s->dwrite->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                11.f, L"", &s->fmt_mono);
    s->rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &s->brush);
    return true;
}

void LoadData(ViewState* s) {
    s->loading = true;
    uint64_t now = NowUnix();
    s->view_end = now;
    s->view_start = now > PresetSeconds(s->preset) ? now - PresetSeconds(s->preset) : 0;

    // 优先用环形缓冲（最近 30 分钟秒开）
    if (s->preset == R15m && s->recorder && !s->recorder->recent().empty()) {
        s->samples.assign(s->recorder->recent().begin(), s->recorder->recent().end());
        s->samples.erase(
            std::remove_if(s->samples.begin(), s->samples.end(),
                           [&](const RawSample& r) {
                               return r.ts_unix < s->view_start || r.ts_unix > s->view_end;
                           }),
            s->samples.end());
    } else {
        std::wstring dir = Config::HistoryDir(s->config.history);
        QueryRawRange(dir, s->view_start, s->view_end, s->samples);
    }
    ComputeStats(s->samples, s->stats);
    s->loading = false;
    s->cursor_x = -1;
    s->cursor_valid = false;
}

float SampleValue(const RawSample& s, int lane, bool temp_axis) {
    switch (lane) {
    case 0: return temp_axis ? (valid_pct(s.cpu_temp_c) ? s.cpu_temp_c : NAN) : (valid_pct(s.cpu_pct) ? s.cpu_pct : NAN);
    case 1: return temp_axis ? (valid_pct(s.gpu_temp_c) ? s.gpu_temp_c : NAN) : (valid_pct(s.gpu_pct) ? s.gpu_pct : NAN);
    case 2: return valid_pct(s.ram_pct) ? s.ram_pct : NAN;
    case 3: return temp_axis ? s.net_down_bps : s.net_up_bps;
    default: return NAN;
    }
}

void DrawLane(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush,
              IDWriteTextFormat* label_fmt, const D2D1_RECT_F& lane_rc,
              const wchar_t* title, const std::vector<RawSample>& samples,
              uint64_t t0, uint64_t t1, int lane, D2D1_COLOR_F line_color,
              bool second_line = false, D2D1_COLOR_F line2_color = {}) {
    brush->SetColor(theme::kBgPanel);
    rt->FillRectangle(lane_rc, brush);
    brush->SetColor(theme::kBorder);
    rt->DrawRectangle(lane_rc, brush);

    D2D1_RECT_F label_rc{lane_rc.left + 4, lane_rc.top + 2, lane_rc.left + 48, lane_rc.top + 18};
    brush->SetColor(theme::kTextSecond);
    rt->DrawTextW(title, static_cast<UINT32>(wcslen(title)), label_fmt, label_rc, brush);

    if (samples.size() < 2 || t1 <= t0) return;

    float chart_l = lane_rc.left + 52;
    float chart_r = lane_rc.right - 4;
    float chart_t = lane_rc.top + 4;
    float chart_b = lane_rc.bottom - 4;
    float w = chart_r - chart_l;
    float h = chart_b - chart_t;

    // 网格
    brush->SetColor(theme::kBgInset);
    for (int g = 0; g <= 4; ++g) {
        float y = chart_t + h * g / 4.f;
        rt->DrawLine({chart_l, y}, {chart_r, y}, brush);
    }

    auto draw_series = [&](bool temp_axis, D2D1_COLOR_F col) {
        std::vector<D2D1_POINT_2F> pts;
        pts.reserve(samples.size());
        float vmin = 1e9f, vmax = -1e9f;
        for (const auto& s : samples) {
            float v = SampleValue(s, lane, temp_axis);
            if (std::isfinite(v)) {
                vmin = std::min(vmin, v);
                vmax = std::max(vmax, v);
            }
        }
        if (lane <= 2) { vmin = 0; vmax = 100; }
        else if (!std::isfinite(vmin)) return;
        if (vmax - vmin < 1.f) vmax = vmin + 1.f;

        size_t stride = std::max<size_t>(1, samples.size() / static_cast<size_t>(w));
        for (size_t i = 0; i < samples.size(); i += stride) {
            float v = SampleValue(samples[i], lane, temp_axis);
            if (!std::isfinite(v)) continue;
            float tx = static_cast<float>(samples[i].ts_unix - t0) / static_cast<float>(t1 - t0);
            float ty = 1.f - (v - vmin) / (vmax - vmin);
            pts.push_back({chart_l + tx * w, chart_t + ty * h});
        }
        if (pts.size() < 2) return;
        brush->SetColor(col);
        for (size_t i = 1; i < pts.size(); ++i)
            rt->DrawLine(pts[i - 1], pts[i], brush, 1.5f);
    };

    draw_series(false, line_color);
    if (second_line) draw_series(true, line2_color);
    else if (lane == 3) draw_series(true, theme::kOk); // 下载线
}

void Paint(ViewState* s) {
    if (!s->rt) return;
    s->rt->BeginDraw();
    s->rt->Clear(theme::kBgApp);

    RECT cr{};
    GetClientRect(s->hwnd, &cr);
    float W = static_cast<float>(cr.right);
    float H = static_cast<float>(cr.bottom);

    // 工具栏底
    D2D1_RECT_F toolbar{0, 0, W, static_cast<float>(kToolbarH)};
    s->brush->SetColor(theme::kBgPanel);
    s->rt->FillRectangle(toolbar, s->brush);
    s->brush->SetColor(theme::kBorder);
    s->rt->DrawLine({0, static_cast<float>(kToolbarH)}, {W, static_cast<float>(kToolbarH)}, s->brush);

    s->brush->SetColor(theme::kTextPrimary);
    D2D1_RECT_F title_rc{12, 8, 200, 32};
    s->rt->DrawTextW(L"历史记录", 4, s->fmt_label, title_rc, s->brush);

    // 统计行
    wchar_t stats_buf[256];
    swprintf(stats_buf, 256,
             L"CPU max %u%% avg %u%%  │  GPU max %u%% avg %u%%  │  RAM max %u%% avg %u%%",
             s->stats.cpu_max, s->stats.cpu_avg,
             s->stats.gpu_max, s->stats.gpu_avg,
             s->stats.ram_max, s->stats.ram_avg);
    D2D1_RECT_F stats_rc{12, static_cast<float>(kToolbarH + 4),
                         W - 12, static_cast<float>(kToolbarH + kStatsH)};
    s->brush->SetColor(theme::kTextSecond);
    s->rt->DrawTextW(stats_buf, static_cast<UINT32>(wcslen(stats_buf)),
                     s->fmt_mono, stats_rc, s->brush);

    float chart_top = static_cast<float>(kToolbarH + kStatsH + 4);
    float chart_bot = H - static_cast<float>(kCrosshairH + kStatusH);
    float lane_h = (chart_bot - chart_top - kLanePad * (kLaneCount - 1)) / kLaneCount;

    static const wchar_t* lane_names[] = {L"CPU", L"GPU", L"RAM", L"网络"};
    for (int i = 0; i < kLaneCount; ++i) {
        D2D1_RECT_F lr{8, chart_top + i * (lane_h + kLanePad), W - 8,
                       chart_top + i * (lane_h + kLanePad) + lane_h};
        bool dual = (i <= 1);
        bool net = (i == 3);
        DrawLane(s->rt, s->brush, s->fmt_label, lr, lane_names[i], s->samples,
                 s->view_start, s->view_end, i, theme::kTextPrimary,
                 dual || net, dual ? theme::kWarn : theme::kOk);
    }

    // 十字光标
    if (s->cursor_x >= 0) {
        float cx = static_cast<float>(s->cursor_x);
        s->brush->SetColor(D2D1::ColorF(0.04f, 0.08f, 0.18f, 0.35f));
        s->rt->DrawLine({cx, chart_top}, {cx, chart_bot}, s->brush, 1.f);
    }

    // 十字光标读数条
    D2D1_RECT_F cross_rc{0, chart_bot, W, chart_bot + kCrosshairH};
    s->brush->SetColor(theme::kBgSubtle);
    s->rt->FillRectangle(cross_rc, s->brush);
    s->brush->SetColor(theme::kBorder);
    s->rt->DrawLine({0, chart_bot}, {W, chart_bot}, s->brush);

    wchar_t cross_buf[512] = L"移动鼠标查看逐秒读数";
    if (s->cursor_valid) {
        wchar_t ts[32], ct[16], gt[16], cp[16], gp[16], rp[16], up[16], dn[16];
        FormatTs(s->cursor_sample.ts_unix, ts, 32);
        FormatPct(s->cursor_sample.cpu_pct, cp, 16);
        FormatTemp(s->cursor_sample.cpu_temp_c, ct, 16);
        FormatPct(s->cursor_sample.gpu_pct, gp, 16);
        FormatTemp(s->cursor_sample.gpu_temp_c, gt, 16);
        FormatPct(s->cursor_sample.ram_pct, rp, 16);
        FormatSpeed(s->cursor_sample.net_up_bps, up, 16);
        FormatSpeed(s->cursor_sample.net_down_bps, dn, 16);
        swprintf(cross_buf, 512,
                 L"%s%s  CPU %s %s │ GPU %s %s │ RAM %s │ ↑ %s ↓ %s",
                 ts, s->cursor_exact ? L"" : L" (近似)",
                 cp, ct, gp, gt, rp, up, dn);
    }
    D2D1_RECT_F cross_txt{12, chart_bot + 6, W - 12, chart_bot + kCrosshairH - 4};
    s->brush->SetColor(theme::kTextPrimary);
    s->rt->DrawTextW(cross_buf, static_cast<UINT32>(wcslen(cross_buf)),
                     s->fmt_mono, cross_txt, s->brush);

    // 状态栏
    D2D1_RECT_F status_rc{0, H - kStatusH, W, H};
    s->brush->SetColor(theme::kBgSubtle);
    s->rt->FillRectangle(status_rc, s->brush);
    wchar_t status[128];
    swprintf(status, 128, L"%zu 条样本", s->samples.size());
    D2D1_RECT_F st_txt{12, H - kStatusH + 4, W - 12, H - 4};
    s->brush->SetColor(theme::kTextSecond);
    s->rt->DrawTextW(status, static_cast<UINT32>(wcslen(status)), s->fmt_label, st_txt, s->brush);

    s->rt->EndDraw();
}

void UpdateCursor(ViewState* s, int x) {
    RECT cr{};
    GetClientRect(s->hwnd, &cr);
    float chart_l = 60.f;
    float chart_r = static_cast<float>(cr.right) - 8.f;
    if (x < chart_l || x > chart_r || s->samples.empty()) {
        s->cursor_x = -1;
        s->cursor_valid = false;
        return;
    }
    s->cursor_x = x;
    float frac = (x - chart_l) / (chart_r - chart_l);
    uint64_t ts = s->view_start +
        static_cast<uint64_t>(frac * static_cast<double>(s->view_end - s->view_start));

    std::wstring dir = Config::HistoryDir(s->config.history);
    s->cursor_valid = QueryAt(dir, ts, s->cursor_sample, s->cursor_exact);
    if (!s->cursor_valid && !s->samples.empty()) {
        // 从已加载样本中找最近
        auto it = std::lower_bound(s->samples.begin(), s->samples.end(), ts,
                                   [](const RawSample& r, uint64_t t) { return r.ts_unix < t; });
        if (it != s->samples.end() && it->ts_unix == ts) {
            s->cursor_sample = *it;
            s->cursor_valid = true;
            s->cursor_exact = true;
        } else if (it != s->samples.begin()) {
            s->cursor_sample = *(--it);
            s->cursor_valid = true;
            s->cursor_exact = false;
        }
    }
}

void ExportCsv(ViewState* s) {
    wchar_t path[MAX_PATH] = L"Sentry-Lite-export.csv";
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = s->hwnd;
    ofn.lpstrFilter = L"CSV 文件\0*.csv\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = L"csv";
    if (!GetSaveFileNameW(&ofn)) return;

    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"wb") != 0 || !f) return;
    fprintf(f, "timestamp,cpu_pct,cpu_temp_c,gpu_pct,gpu_temp_c,ram_pct,net_up_bps,net_down_bps,flags\n");
    for (const auto& r : s->samples) {
        fprintf(f, "%llu,%u,%u,%u,%u,%u,%.2f,%.2f,%u\n",
                static_cast<unsigned long long>(r.ts_unix),
                r.cpu_pct, r.cpu_temp_c, r.gpu_pct, r.gpu_temp_c, r.ram_pct,
                r.net_up_bps, r.net_down_bps, r.flags);
    }
    fclose(f);
    MessageBoxW(s->hwnd, L"CSV 已导出。", kTitle, MB_OK | MB_ICONINFORMATION);
}

void ShowRetentionDialog(ViewState* s) {
    wchar_t msg[512];
    swprintf(msg, 512,
             L"当前保留策略：\n\n"
             L"原始数据（raw）：%d 天\n"
             L"总容量上限：%llu MB\n\n"
             L"修改 config.json 中 history 字段后重启生效。\n"
             L"（后续版本将提供图形化设置）",
             s->config.history.raw_retention_days,
             static_cast<unsigned long long>(s->config.history.max_total_mb));
    MessageBoxW(s->hwnd, msg, L"保留策略", MB_OK | MB_ICONINFORMATION);
}

INT_PTR CALLBACK RangeDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    if (msg == WM_INITDIALOG) return TRUE;
    if (msg == WM_COMMAND) {
        if (LOWORD(wp) >= 100 && LOWORD(wp) < 100 + RCount) {
            EndDialog(dlg, LOWORD(wp) - 100);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) { EndDialog(dlg, -1); return TRUE; }
    }
    return FALSE;
}

void OnCommand(ViewState* s, int id) {
    if (id >= 3000 && id < 3000 + RCount) {
        s->preset = static_cast<RangePreset>(id - 3000);
        LoadData(s);
        InvalidateRect(s->hwnd, nullptr, FALSE);
        return;
    }
    switch (id) {
    case 3100: ExportCsv(s); break;
    case 3101: ShowRetentionDialog(s); break;
    case IDCANCEL: DestroyWindow(s->hwnd); break;
    }
}

LRESULT CALLBACK HistoryProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ViewState* s = St(hwnd);
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        s = static_cast<ViewState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
        s->hwnd = hwnd;
        if (!InitD2D(s)) return -1;
        LoadData(s);

        HMENU bar = CreateMenu();
        HMENU range = CreateMenu();
        for (int i = 0; i < RCount; ++i)
            AppendMenuW(range, MF_STRING, 3000 + i, PresetLabel(static_cast<RangePreset>(i)));
        AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(range), L"时间范围");
        AppendMenuW(bar, MF_STRING, 3100, L"导出 CSV");
        AppendMenuW(bar, MF_STRING, 3101, L"保留策略…");
        SetMenu(hwnd, bar);
        return 0;
    }
    case WM_SIZE:
        if (s && s->rt) {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            s->rt->Resize(D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top));
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE:
        if (s) {
            UpdateCursor(s, GET_X_LPARAM(lp));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_PAINT:
        if (s) Paint(s);
        else {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
        }
        return 0;
    case WM_COMMAND:
        if (s) OnCommand(s, LOWORD(wp));
        return 0;
    case WM_DESTROY:
        ResetD2D(s);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

} // namespace

void ShowHistoryViewer(HWND owner, const Config& config, const Recorder& recorder) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = HistoryProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
        registered = true;
    }

    ViewState state{};
    state.config = config;
    state.recorder = &recorder;

    HWND hwnd = CreateWindowExW(
        0, kClass, kTitle,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 920, 640,
        owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (!hwnd) return;

    EnableWindow(owner, FALSE);
    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}

} // namespace hwmon
