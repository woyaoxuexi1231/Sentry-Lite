#pragma once
#include <windows.h>
#include "shared/shm.h"
#include "shared/sample.h"

namespace hwmon {

// 只读共享内存客户端；服务缺席/心跳超时 → 哨兵（DESIGN_v2.md §4）
class ShmClient {
public:
    // 尝试打开映射（失败安静返回 false，调用方按无温度处理）
    bool TryOpen();
    void Refresh();  // 每秒调用，取出当前温度（含有效性判断）
    uint8_t cpu_temp_c = kSentinel;
    uint8_t gpu_temp_c = kSentinel;

private:
    void* view_ = nullptr;
    uint64_t last_tick_ = 0;
    unsigned fail_ticks_ = 0;
};

} // namespace hwmon
