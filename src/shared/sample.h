// 历史采样记录与无效值约定（DESIGN_v2.md §6.3）
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>

namespace hwmon {

constexpr uint32_t kHwdbMagic = 0x31445748u; // 'HWD1' little-endian
constexpr uint32_t kHwdbVersion = 1;
constexpr uint32_t kSentinel = 0xFF;         // uint8 指标无效（温度、占用等）

// 24 字节定长原始采样，写入前 memcpy，读出后按字段取用
struct RawSample {
    uint64_t ts_unix;      // UTC 秒
    uint8_t  cpu_pct;
    uint8_t  cpu_temp_c;
    uint8_t  gpu_pct;
    uint8_t  gpu_temp_c;
    uint8_t  ram_pct;
    uint8_t  flags;        // bit0 = gap（与上一条存在采样中断）
    float    net_up_bps;
    float    net_down_bps;
};
static_assert(sizeof(RawSample) == 24, "RawSample must stay exactly 24 bytes");

inline void store_pct(uint8_t& dst, float v) {
    if (!std::isfinite(v) || v < 0.f || v > 100.f) dst = kSentinel;
    else dst = static_cast<uint8_t>(v + 0.5f);
}
inline bool valid_pct(uint8_t v) { return v != kSentinel; }

} // namespace hwmon
