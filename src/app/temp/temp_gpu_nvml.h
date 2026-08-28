#pragma once
#include <windows.h>
#include <cmath>

namespace hwmon {

// 动态加载 nvml.dll 读 GPU 温度（NVIDIA）
class GpuTempNvml {
public:
    bool Init();
    void Shutdown();
    float ReadC(); // NaN = 无效

private:
    HMODULE nvml_ = nullptr;
    void* device_ = nullptr;
    using nvmlInit_v2_fn = int(*)(void);
    using nvmlShutdown_fn = int(*)(void);
    using nvmlDeviceGetHandleByIndex_v2_fn = int(*)(unsigned, void**);
    using nvmlDeviceGetTemperature_fn = int(*)(void*, int, unsigned*);
    nvmlInit_v2_fn fn_init_ = nullptr;
    nvmlShutdown_fn fn_shutdown_ = nullptr;
    nvmlDeviceGetHandleByIndex_v2_fn fn_get_dev_ = nullptr;
    nvmlDeviceGetTemperature_fn fn_get_temp_ = nullptr;
};

} // namespace hwmon
