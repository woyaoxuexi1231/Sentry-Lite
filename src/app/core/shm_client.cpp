#include "shm_client.h"
#include <cmath>
#include <cstring>

namespace hwmon {

bool ShmClient::TryOpen() {
    HANDLE map = OpenFileMappingW(FILE_MAP_READ, FALSE, kShmName);
    if (!map) return false;
    view_ = MapViewOfFile(map, FILE_MAP_READ, 0, 0, kShmSize);
    CloseHandle(map); // 映射节句柄可关，视图保持有效
    if (!view_) return false;

    const auto* shm = static_cast<const ShmLayout*>(view_);
    if (shm->magic != kShmMagic || shm->size != kShmSize) {
        UnmapViewOfFile(view_);
        view_ = nullptr;
        return false;
    }
    return true;
}

void ShmClient::Refresh() {
    if (!view_) {
        // 服务未就绪：每 5s 重试一次
        if (++fail_ticks_ < 5) return;
        fail_ticks_ = 0;
        if (!TryOpen()) {
            cpu_temp_c = gpu_temp_c = kSentinel;
            return;
        }
    }

    const auto* shm = static_cast<const ShmLayout*>(view_);
    uint64_t tick = shm->tick;
    float ct = shm->cpu_temp_c, gt = shm->gpu_temp_c;

    // 心跳 >5s 未更新 → 视为服务失效
    ULONGLONG now = GetTickCount64();
    if (tick == 0 || tick + 5000 < now) {
        cpu_temp_c = gpu_temp_c = kSentinel;
        UnmapViewOfFile(view_);
        view_ = nullptr;
        fail_ticks_ = 0;
        return;
    }
    if (tick == last_tick_) { // 同一拍：沿用
        return;
    }
    last_tick_ = tick;

    auto to_u8 = [](float v) -> uint8_t {
        return (std::isfinite(v) && v > -50.f && v < 150.f)
            ? static_cast<uint8_t>(v + 0.5f) : kSentinel;
    };
    cpu_temp_c = to_u8(ct);
    gpu_temp_c = to_u8(gt);
}

} // namespace hwmon
