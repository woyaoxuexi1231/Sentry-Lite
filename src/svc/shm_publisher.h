#pragma once
#include <windows.h>
#include "shared/shm.h"

namespace hwmon {

class ShmPublisher {
public:
    bool Init();
    void Publish(float cpu_temp_c, float gpu_temp_c);
    void Shutdown();

private:
    HANDLE map_ = nullptr;
    ShmLayout* view_ = nullptr;
};

} // namespace hwmon
