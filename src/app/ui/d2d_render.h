#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include "core/snapshot.h"

namespace hwmon {

// 悬浮条绘制：分层窗口（per-pixel alpha）+ D2D 软件路径，port-manager 视觉。
// 每帧自适应内容尺寸；返回值无，内部完成 ULW 提交。
class BarRenderer {
public:
    ~BarRenderer();
    bool Init(HWND hwnd);
    void Shutdown();
    // 建议每秒一次；内部按需调整窗口大小并提交分层更新
    void Render(const Snapshot& snap, float dpi_scale,
                float temp_warn_c, float temp_crit_c, bool disk_error);

private:
    struct Piece {
        const wchar_t* text;
        IDWriteTextFormat* fmt;
        D2D1_COLOR_F color;
        float gap_after;      // 逻辑 px
        bool is_separator;    // 竖分隔线而非文本
    };

    void ResetAll();
    bool EnsureBitmap(UINT w, UINT h);
    IDWriteTextFormat* LabelFormat(float px);
    IDWriteTextFormat* MonoFormat(float px);
    void FillBrushes();

    HWND hwnd_ = nullptr;
    HDC mem_dc_ = nullptr;
    HBITMAP dib_ = nullptr;
    UINT bmp_w_ = 0, bmp_h_ = 0;

    ID2D1Factory* factory_ = nullptr;
    ID2D1DCRenderTarget* rt_ = nullptr;
    IDWriteFactory* dwrite_ = nullptr;
    IDWriteTextFormat* label_base_ = nullptr;  // 会按字号重建，缓存单例足够
    IDWriteTextFormat* mono_base_ = nullptr;
    ID2D1SolidColorBrush* brush_ = nullptr;    // 复用换色（DrawTextW 前设色）

    wchar_t buf_cpu_[16], buf_gpu_[16], buf_ram_[16],
            buf_up_[16], buf_down_[16], buf_ct_[16], buf_gt_[16];
};

} // namespace hwmon

