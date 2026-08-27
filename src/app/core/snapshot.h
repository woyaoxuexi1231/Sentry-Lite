// 一秒一帧的内存态快照：UI 显示与 Recorder 的共同输入
#pragma once
#include "shared/sample.h"

namespace hwmon {

struct Snapshot {
    float cpu_pct = -1.f;
    float gpu_pct = -1.f;
    float ram_pct = -1.f;
    float net_up_bps = 0.f;
    float net_down_bps = 0.f;
    // 温度由共享内存提供，无服务时保持哨兵
    uint8_t cpu_temp_c = kSentinel;
    uint8_t gpu_temp_c = kSentinel;
};

} // namespace hwmon
