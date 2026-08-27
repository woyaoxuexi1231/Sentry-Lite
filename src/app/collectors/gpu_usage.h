#pragma once
#include <pdh.h>

namespace hwmon {

// GPU 占用：PDH \GPU Engine(*)\Utilization Percentage，
// 按 engtype 求和取最大（任务管理器同口径，DESIGN_v2.md §3）
class GpuUsageCollector {
public:
    ~GpuUsageCollector();
    bool Init();
    // 返回 0..100，不可用返回 -1；不可用后每 60s 重试初始化
    float Tick();

private:
    void Teardown();
    bool available_ = false;
    unsigned retry_tick_ = 0;   // 失败后下次允许重试的秒计数
    PDH_HQUERY query_ = nullptr;
    PDH_HCOUNTER counter_ = nullptr;
    bool primed_ = false;
};

} // namespace hwmon
