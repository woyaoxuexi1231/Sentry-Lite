#pragma once
#include <pdh.h>
#include <vector>

namespace hwmon {

// GPU 占用：PDH \GPU Engine(*)\Utilization Percentage，
// 按 engtype 求和取最大（任务管理器同口径）
class GpuUsageCollector {
public:
    ~GpuUsageCollector();
    bool Init();
    float Tick();

private:
    void Teardown();
    bool available_ = false;
    unsigned retry_tick_ = 0;
    PDH_HQUERY query_ = nullptr;
    PDH_HCOUNTER counter_ = nullptr;
    bool primed_ = false;
    std::vector<BYTE> buf_;   // reused each tick to avoid realloc
};

} // namespace hwmon
