#pragma once
#include <windows.h>
#include <cmath>
#include <string>

namespace hwmon {

// CPU 温度：与 LiteMonitor 相同，委托 Sentry-Lite-lhm（LibreHardwareMonitorLib 0.9.6）读取。
class CpuTempReader {
public:
    bool Init(std::wstring& err);
    float ReadC(); // NaN = 无效
    std::wstring LastMessage() const;
    bool BridgeAlive() const;
    void Shutdown();

private:
    bool ready_ = false;
};

} // namespace hwmon
