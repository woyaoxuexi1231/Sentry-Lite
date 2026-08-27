#include "temp_gpu_nvml.h"
#include <windows.h>

namespace hwmon {

namespace {
constexpr int kNvmlTempGpu = 0; // NVML_TEMPERATURE_GPU
}

bool GpuTempNvml::Init() {
    Shutdown();
    nvml_ = LoadLibraryW(L"nvml.dll");
    if (!nvml_) return false;
    fn_init_ = reinterpret_cast<nvmlInit_v2_fn>(GetProcAddress(nvml_, "nvmlInit_v2"));
    fn_shutdown_ = reinterpret_cast<nvmlShutdown_fn>(GetProcAddress(nvml_, "nvmlShutdown"));
    fn_get_dev_ = reinterpret_cast<nvmlDeviceGetHandleByIndex_v2_fn>(
        GetProcAddress(nvml_, "nvmlDeviceGetHandleByIndex_v2"));
    fn_get_temp_ = reinterpret_cast<nvmlDeviceGetTemperature_fn>(
        GetProcAddress(nvml_, "nvmlDeviceGetTemperature"));
    if (!fn_init_ || !fn_shutdown_ || !fn_get_dev_ || !fn_get_temp_) {
        Shutdown();
        return false;
    }
    if (fn_init_() != 0) {
        Shutdown();
        return false;
    }
    void* dev = nullptr;
    if (fn_get_dev_(0, &dev) != 0) {
        fn_shutdown_();
        Shutdown();
        return false;
    }
    device_ = dev;
    return true;
}

void GpuTempNvml::Shutdown() {
    if (fn_shutdown_ && nvml_) fn_shutdown_();
    device_ = nullptr;
    if (nvml_) FreeLibrary(nvml_);
    nvml_ = nullptr;
    fn_init_ = nullptr;
    fn_shutdown_ = nullptr;
    fn_get_dev_ = nullptr;
    fn_get_temp_ = nullptr;
}

float GpuTempNvml::ReadC() {
    if (!device_ || !fn_get_temp_) return NAN;
    unsigned t = 0;
    if (fn_get_temp_(device_, kNvmlTempGpu, &t) != 0) return NAN;
    if (t > 150) return NAN;
    return static_cast<float>(t);
}

} // namespace hwmon
