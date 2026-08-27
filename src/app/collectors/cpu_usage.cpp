#include "cpu_usage.h"
#include <windows.h>

namespace hwmon {

float CpuUsageCollector::Tick() {
    FILETIME ftIdle{}, ftKernel{}, ftUser{};
    if (!GetSystemTimes(&ftIdle, &ftKernel, &ftUser)) return -1.f;

    auto to_ull = [](const FILETIME& ft) {
        ULARGE_INTEGER u;
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        return u.QuadPart;
    };
    unsigned long long idle = to_ull(ftIdle);
    unsigned long long kernel = to_ull(ftKernel); // kernel 含 idle
    unsigned long long user = to_ull(ftUser);

    if (!has_prev_) {
        has_prev_ = true;
        prev_idle_ = idle; prev_kernel_ = kernel; prev_user_ = user;
        return -1.f;
    }

    unsigned long long dIdle = idle - prev_idle_;
    unsigned long long dTotal = (kernel - prev_kernel_) + (user - prev_user_);
    prev_idle_ = idle; prev_kernel_ = kernel; prev_user_ = user;

    if (dTotal == 0) return -1.f;
    float usage = 100.f * (1.f - static_cast<float>(static_cast<double>(dIdle) / static_cast<double>(dTotal)));
    if (usage < 0.f) usage = 0.f;
    if (usage > 100.f) usage = 100.f;
    return usage;
}

} // namespace hwmon
