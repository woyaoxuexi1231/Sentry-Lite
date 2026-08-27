#pragma once

namespace hwmon {

class MemUsageCollector {
public:
    // 0..100，失败返回 -1
    float Tick();
};

} // namespace hwmon
