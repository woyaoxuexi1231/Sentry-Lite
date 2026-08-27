// port-manager 视觉 token（DESIGN_v2.md §7）
#pragma once
#include <d2d1.h>

namespace hwmon::theme {

inline const D2D1_COLOR_F kBgApp       = {0xF9/255.f, 0xFA/255.f, 0xFB/255.f, 1.f};
inline const D2D1_COLOR_F kBgPanel     = {1.f, 1.f, 1.f, 0.95f};   // 悬浮条 95% 白
inline const D2D1_COLOR_F kBgSubtle    = {0xF8/255.f, 0xF9/255.f, 0xFA/255.f, 1.f};
inline const D2D1_COLOR_F kBgInset     = {0xF0/255.f, 0xF1/255.f, 0xF3/255.f, 1.f};
inline const D2D1_COLOR_F kBorder      = {0xE2/255.f, 0xE4/255.f, 0xE8/255.f, 1.f};
inline const D2D1_COLOR_F kTextPrimary = {0x1A/255.f, 0x1C/255.f, 0x20/255.f, 1.f};
inline const D2D1_COLOR_F kTextSecond  = {0x6B/255.f, 0x72/255.f, 0x80/255.f, 1.f};
inline const D2D1_COLOR_F kOk          = {0x16/255.f, 0xA3/255.f, 0x4A/255.f, 1.f};
inline const D2D1_COLOR_F kWarn        = {0xD9/255.f, 0x77/255.f, 0x06/255.f, 1.f};
inline const D2D1_COLOR_F kCrit        = {0xDC/255.f, 0x26/255.f, 0x26/255.f, 1.f};

} // namespace hwmon::theme
