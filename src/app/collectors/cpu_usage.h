#pragma once

namespace hwmon {

// CPU 总占用：GetSystemTimes 每秒差分（DESIGN_v2.md §3）
class CpuUsageCollector {
public:
    // 返回 0..100，失败返回 -1
    float Tick();
private:
    bool has_prev_ = false;
    unsigned long long prev_idle_ = 0, prev_kernel_ = 0, prev_user_ = 0;
};

} // namespace hwmon
