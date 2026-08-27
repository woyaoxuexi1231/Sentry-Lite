#pragma once
#include "core/config.h"
#include "collectors/cpu_usage.h"
#include "collectors/mem_usage.h"
#include "collectors/gpu_usage.h"
#include "collectors/net_speed.h"
#include "core/shm_client.h"
#include "history/recorder.h"
#include "history/retention.h"
#include "ui/d2d_render.h"

namespace hwmon {

struct AppContext {
    Config config;
    Recorder recorder;
    Retention retention;
    BarRenderer renderer;
    ShmClient shm;
    CpuUsageCollector cpu;
    MemUsageCollector mem;
    GpuUsageCollector gpu;
    NetSpeedCollector net;
    unsigned last_retention_day = 0;
};

// 悬浮条窗口（唯一的 M1 UI），自持消息循环所需的一切
bool RunFloatBar(AppContext& ctx, int show_cmd);

} // namespace hwmon
