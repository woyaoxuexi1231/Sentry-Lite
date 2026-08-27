#include "mem_usage.h"
#include <windows.h>

namespace hwmon {

float MemUsageCollector::Tick() {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return -1.f;
    return static_cast<float>(ms.dwMemoryLoad);
}

} // namespace hwmon
