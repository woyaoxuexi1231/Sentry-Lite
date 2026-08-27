#pragma once
#include "core/config.h"
#include "history/recorder.h"
#include <windows.h>

namespace hwmon {

// 打开历史查看器（模态，阻塞至关闭）
void ShowHistoryViewer(HWND owner, const Config& config, const Recorder& recorder);

} // namespace hwmon
