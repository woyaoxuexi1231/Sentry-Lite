#include "d2d_render.h"
#include "ui/theme.h"
#include <d2d1helper.h>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cwchar>

namespace hwmon {

namespace {

// 逻辑 px（96dpi 基准）
constexpr float kPadH = 10.f;        // 左右内距
constexpr float kBarH = 30.f;        // 条高
constexpr float kSepGap = 8.f;       // 分隔线两侧留白
constexpr float kLabelValueGap = 4.f;
constexpr float kSubGap = 4.f;       // 数值/温度之间
constexpr float kArrowGap = 1.f;

void FormatSpeed(float bps, wchar_t* out, size_t n) {
    if (bps < 0 || !std::isfinite(bps)) { swprintf(out, n, L"—"); return; }
    const wchar_t* unit = L"B/s";
    double v = bps;
    if (v >= 1024.0 * 1024 * 1024) { v /= 1024.0*1024*1024; unit = L"GB/s"; }
    else if (v >= 1024.0 * 1024)   { v /= 1024.0*1024;      unit = L"MB/s"; }
    else if (v >= 1024.0)          { v /= 1024.0;           unit = L"KB/s"; }
    if (v >= 100.0)     swprintf(out, n, L"%.0f%s", v, unit);
    else if (v >= 10.0) swprintf(out, n, L"%.1f%s", v, unit);
    else                swprintf(out, n, L"%.2f%s", v, unit);
}

} // namespace

BarRenderer::~BarRenderer() { Shutdown(); }

void BarRenderer::Shutdown() { ResetAll(); }

void BarRenderer::ResetAll() {
    if (rt_) rt_->Release();
    rt_ = nullptr;
    if (brush_) brush_->Release();
    brush_ = nullptr;
    if (label_base_) label_base_->Release();
    label_base_ = nullptr;
    if (mono_base_) mono_base_->Release();
    mono_base_ = nullptr;
    if (dwrite_) dwrite_->Release();
    dwrite_ = nullptr;
    if (factory_) factory_->Release();
    factory_ = nullptr;
    if (mem_dc_) DeleteDC(mem_dc_);
    mem_dc_ = nullptr;
    if (dib_) DeleteObject(dib_);
    dib_ = nullptr;
    bmp_w_ = bmp_h_ = 0;
}

IDWriteTextFormat* BarRenderer::LabelFormat(float px) {
    if (!label_base_) {
        HRESULT hr = dwrite_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            px, L"", &label_base_);
        if (SUCCEEDED(hr))
            label_base_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    return label_base_;
}

IDWriteTextFormat* BarRenderer::MonoFormat(float px) {
    if (!mono_base_) {
        const wchar_t* family = L"Fira Code";
        IDWriteFontCollection* sys = nullptr;
        if (SUCCEEDED(dwrite_->GetSystemFontCollection(&sys)) && sys) {
            UINT32 idx = 0; BOOL exists = FALSE;
            if (SUCCEEDED(sys->FindFamilyName(L"Fira Code", &idx, &exists)) && !exists)
                family = L"Consolas";
            sys->Release();
        }
        HRESULT hr = dwrite_->CreateTextFormat(
            family, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            px, L"", &mono_base_);
        if (SUCCEEDED(hr))
            mono_base_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    return mono_base_;
}

bool BarRenderer::Init(HWND hwnd) {
    hwnd_ = hwnd;
    D2D1_FACTORY_OPTIONS opt{};
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                   __uuidof(ID2D1Factory), &opt,
                                   reinterpret_cast<void**>(&factory_));
    if (FAILED(hr)) return false;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(&dwrite_));
    if (FAILED(hr)) return false;

    D2D1_RENDER_TARGET_PROPERTIES rtp{};
    rtp.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE; // 小面积 1Hz，软件路径最稳
    rtp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    rtp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    hr = factory_->CreateDCRenderTarget(&rtp, &rt_);
    if (FAILED(hr)) return false;

    rt_->CreateSolidColorBrush(theme::kTextPrimary, &brush_);
    return brush_ != nullptr;
}

bool BarRenderer::EnsureBitmap(UINT w, UINT h) {
    if (w == bmp_w_ && h == bmp_h_ && mem_dc_) return true;

    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = static_cast<LONG>(w);
    bi.bV5Height = -static_cast<LONG>(h); // top-down， premultiplied ARGB
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask   = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask  = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&bi),
                                   DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dib || !dc) {
        if (dib) DeleteObject(dib);
        if (dc) DeleteDC(dc);
        return false;
    }
    HGDIOBJ prev = SelectObject(dc, dib);
    if (prev == nullptr || prev == HGDI_ERROR) {
        DeleteDC(dc);
        DeleteObject(dib);
        return false;
    }

    if (mem_dc_) DeleteDC(mem_dc_); // 先释放旧 DC 再删旧位图
    if (dib_) DeleteObject(dib_);

    mem_dc_ = dc;
    dib_ = dib;
    bmp_w_ = w; bmp_h_ = h;
    return true;
}

void BarRenderer::Render(const Snapshot& snap, float dpi_scale,
                         float temp_warn_c, float temp_crit_c, bool disk_error) {
    if (!factory_ || !dwrite_ || !brush_) return;
    RECT client{};
    GetClientRect(hwnd_, &client);
    UINT cur_w = static_cast<UINT>(client.right - client.left);

    const bool c_v = snap.cpu_pct >= 0, g_v = snap.gpu_pct >= 0, r_v = snap.ram_pct >= 0;
    const bool ct_v = snap.cpu_temp_c != kSentinel, gt_v = snap.gpu_temp_c != kSentinel;

    auto pct = [](wchar_t* b, size_t n, float v) {
        if (v < 0) swprintf(b, n, L"—");
        else swprintf(b, n, L"%.0f%%", v + 0.5f);
    };
    pct(buf_cpu_, 16, snap.cpu_pct);
    pct(buf_gpu_, 16, snap.gpu_pct);
    pct(buf_ram_, 16, snap.ram_pct);
    FormatSpeed(snap.net_up_bps, buf_up_, 16);
    FormatSpeed(snap.net_down_bps, buf_down_, 16);
    if (ct_v) swprintf(buf_ct_, 16, L"%.0f°C", static_cast<float>(snap.cpu_temp_c));
    else wcscpy_s(buf_ct_, L"—");
    if (gt_v) swprintf(buf_gt_, 16, L"%.0f°C", static_cast<float>(snap.gpu_temp_c));
    else wcscpy_s(buf_gt_, L"—");

    const float s = dpi_scale;
    IDWriteTextFormat* f_label_s = LabelFormat(11.f * s);
    IDWriteTextFormat* f_mono_l = MonoFormat(13.f * s);
    if (!f_label_s || !f_mono_l) return;

    // 文本指针指向自持缓冲；两遍使用（测量、绘制）
    struct PieceRef {
        const wchar_t* text;
        IDWriteTextFormat* fmt;
        D2D1_COLOR_F color;
        float gap_after;
        bool is_separator;
    };
    std::vector<PieceRef> pieces;
    pieces.reserve(16);
    auto push_text = [&](const wchar_t* t, IDWriteTextFormat* f, D2D1_COLOR_F c, float gap) {
        pieces.push_back({t, f, c, gap, false});
    };
    auto push_sep = [&]() {
        pieces.push_back({nullptr, nullptr, theme::kBgInset, 0.f, true});
    };

    auto temp_col = [&](uint8_t t, bool valid) -> D2D1_COLOR_F {
        if (!valid) return theme::kTextSecond;
        if (static_cast<float>(t) >= temp_crit_c) return theme::kCrit;
        if (static_cast<float>(t) >= temp_warn_c) return theme::kWarn;
        return theme::kTextPrimary;
    };

    const D2D1_COLOR_F gray = theme::kTextSecond, black = theme::kTextPrimary;
    const D2D1_COLOR_F cpu_tc = temp_col(snap.cpu_temp_c, ct_v);
    const D2D1_COLOR_F gpu_tc = temp_col(snap.gpu_temp_c, gt_v);

    push_text(L"CPU", f_label_s, gray, kLabelValueGap);
    push_text(buf_cpu_, f_mono_l, c_v ? black : gray, kSubGap);
    push_text(buf_ct_, f_mono_l, cpu_tc, 0.f);
    push_sep();
    push_text(L"GPU", f_label_s, gray, kLabelValueGap);
    push_text(buf_gpu_, f_mono_l, g_v ? black : gray, kSubGap);
    push_text(buf_gt_, f_mono_l, gpu_tc, 0.f);
    push_sep();
    push_text(L"RAM", f_label_s, gray, kLabelValueGap);
    push_text(buf_ram_, f_mono_l, r_v ? black : gray, 0.f);
    push_sep();
    push_text(L"↑", f_label_s, theme::kWarn, kArrowGap);   // ↑琥珀 ↓绿
    push_text(buf_up_, f_mono_l, theme::kWarn, kLabelValueGap);
    push_text(L"↓", f_label_s, theme::kOk, kArrowGap);
    push_text(buf_down_, f_mono_l, theme::kOk, 0.f);

    // ---- 测量（字号已按 dpi 创建，宽度为物理像素；间距按逻辑 px * s）----
    float content_w = kPadH * 2 * s;
    std::vector<float> widths(pieces.size(), 0.f);
    for (size_t i = 0; i < pieces.size(); ++i) {
        const PieceRef& p = pieces[i];
        if (p.is_separator) {
            content_w += (kSepGap * 2 + 1.f) * s;
            continue;
        }
        IDWriteTextLayout* lay = nullptr;
        if (SUCCEEDED(dwrite_->CreateTextLayout(
                p.text, static_cast<UINT32>(wcslen(p.text)), p.fmt,
                10000.f, kBarH * s, &lay)) && lay) {
            DWRITE_TEXT_METRICS m{};
            lay->GetMetrics(&m);
            widths[i] = m.widthIncludingTrailingWhitespace;
            lay->Release();
        }
        content_w += widths[i] + p.gap_after * s;
    }
    if (disk_error) content_w += 6.f * s; // 右上角异常红点

    UINT need_w = static_cast<UINT>(content_w + 0.5f);
    UINT need_h = static_cast<UINT>(kBarH * s + 0.5f);
    if (need_w < 8 || need_h < 8) return;

    if (need_w != cur_w) {
        SetWindowPos(hwnd_, nullptr, 0, 0, need_w, need_h,
                     SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED |
                         SWP_NOREDRAW);
    }
    if (!EnsureBitmap(need_w, need_h)) return;

    // ---- 绘制 ----
    RECT bind{0, 0, static_cast<LONG>(need_w), static_cast<LONG>(need_h)};
    if (FAILED(rt_->BindDC(mem_dc_, &bind))) return;

    rt_->BeginDraw();
    rt_->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f)); // 圆角外透出桌面

    D2D1_ROUNDED_RECT panel{
        D2D1::RectF(0.5f, 0.5f, static_cast<float>(need_w) - 0.5f,
                    static_cast<float>(need_h) - 0.5f),
        6.f * s, 6.f * s};
    D2D1_COLOR_F bg = theme::kBgPanel; bg.a = 1.f;   // 分层窗口统一透明度，圆角内不透明
    brush_->SetColor(bg);
    rt_->FillRoundedRectangle(panel, brush_);
    brush_->SetColor(theme::kBorder);
    rt_->DrawRoundedRectangle(panel, brush_, s);

    float pen_y0 = 7.f * s, pen_y1 = (kBarH - 7.f) * s;
    float x = kPadH * s;
    for (size_t i = 0; i < pieces.size(); ++i) {
        const PieceRef& p = pieces[i];
        if (p.is_separator) {
            brush_->SetColor(theme::kBgInset);
            rt_->DrawLine(D2D1::Point2F(x + kSepGap * s, pen_y0),
                          D2D1::Point2F(x + kSepGap * s, pen_y1), brush_, s);
            x += (kSepGap * 2 + 1.f) * s;
            continue;
        }
        if (widths[i] <= 0.f) continue;
        D2D1_RECT_F rc{x, 0, x + widths[i] + s, static_cast<float>(need_h)};
        brush_->SetColor(p.color);
        rt_->DrawTextW(p.text, static_cast<UINT32>(wcslen(p.text)), p.fmt, rc, brush_);
        x += widths[i] + p.gap_after * s;
    }

    if (disk_error) { // Recorder 写盘失败提示点
        brush_->SetColor(theme::kCrit);
        rt_->FillEllipse(D2D1::Ellipse(
                             D2D1::Point2F(static_cast<float>(need_w) - 3.f * s, 3.f * s),
                             2.f * s, 2.f * s),
                         brush_);
    }

    if (FAILED(rt_->EndDraw())) return; // 少见：设备丢失，下一帧重来

    // ---- 提交分层更新 ----
    SIZE sz{static_cast<LONG>(need_w), static_cast<LONG>(need_h)};
    POINT src{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, nullptr, nullptr, &sz, mem_dc_, &src, 0, &blend, ULW_ALPHA);
}

} // namespace hwmon
