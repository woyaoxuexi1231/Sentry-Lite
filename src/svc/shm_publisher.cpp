#include "shm_publisher.h"
#include <cmath>
#include <cstring>

namespace hwmon {

bool ShmPublisher::Init() {
    map_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                              0, kShmSize, kShmName);
    if (!map_) return false;
    view_ = static_cast<ShmLayout*>(MapViewOfFile(map_, FILE_MAP_ALL_ACCESS, 0, 0, kShmSize));
    if (!view_) {
        CloseHandle(map_);
        map_ = nullptr;
        return false;
    }
    std::memset(view_, 0, sizeof(ShmLayout));
    view_->magic = kShmMagic;
    view_->size = kShmSize;
    return true;
}

void ShmPublisher::Publish(float cpu_temp_c, float gpu_temp_c) {
    if (!view_) return;
    view_->tick = GetTickCount64();
    view_->cpu_temp_c = std::isfinite(cpu_temp_c) ? cpu_temp_c : NAN;
    view_->gpu_temp_c = std::isfinite(gpu_temp_c) ? gpu_temp_c : NAN;
}

void ShmPublisher::Shutdown() {
    if (view_) UnmapViewOfFile(view_);
    view_ = nullptr;
    if (map_) CloseHandle(map_);
    map_ = nullptr;
}

} // namespace hwmon
