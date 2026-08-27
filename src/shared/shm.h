// 温度共享内存布局（DESIGN_v2.md §4）。hwmon-svc（M2）为唯一写者，UI 只读。
#pragma once
#include <cstdint>

namespace hwmon {

constexpr uint32_t kShmMagic = 0x314D5748u; // 'HWM1'
constexpr wchar_t kShmName[] = L"Local\\hwmon_shm";
constexpr uint32_t kShmSize = 256;

// 固定布局，两端共用；svc 写整个结构，tick 每秒更新作心跳
struct alignas(8) ShmLayout {
    uint32_t magic;        // kShmMagic
    uint32_t size;         // kShmSize
    uint64_t tick;         // GetTickCount64 心跳
    float    cpu_temp_c;   // NaN = 无效
    float    gpu_temp_c;
    float    pch_temp_c;
    wchar_t  cpu_name[48];
    wchar_t  gpu_name[48];
};
static_assert(sizeof(ShmLayout) <= kShmSize);

} // namespace hwmon
