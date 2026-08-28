#pragma once
#include "pawnio_client.h"
#include <cmath>

namespace hwmon {

// Intel MSR / AMD SMN 读 CPU package 温度（需 PawnIO 模块）
class CpuTempReader {
public:
    bool Init(PawnIoClient& pio, std::wstring& err);
    float ReadC(); // NaN = 无效

    enum class Vendor { Unknown, Intel, Amd };

private:
    Vendor vendor_ = Vendor::Unknown;
    PawnIoClient* pio_ = nullptr;
    bool ReadMsr(uint32_t msr, uint64_t& val);
    bool ReadSmn(uint32_t addr, uint32_t& val);
};

} // namespace hwmon
