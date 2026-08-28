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
    // 温度由进程内 PawnIO(CPU)/NVML(GPU) 读取，不可用时保持哨兵
    uint8_t cpu_temp_c = kSentinel;
    uint8_t gpu_temp_c = kSentinel;
};

} // namespace hwmon
