#include "temp_cpu.h"
#include <windows.h>
#include <intrin.h>
#include <cstring>

namespace hwmon {

namespace {

CpuTempReader::Vendor DetectVendor() {
    int info[4]{};
    __cpuid(info, 0);
    char vendor[13]{};
    *reinterpret_cast<int*>(vendor) = info[1];
    *reinterpret_cast<int*>(vendor + 4) = info[3];
    *reinterpret_cast<int*>(vendor + 8) = info[2];
    if (strcmp(vendor, "GenuineIntel") == 0) return CpuTempReader::Vendor::Intel;
    if (strcmp(vendor, "AuthenticAMD") == 0) return CpuTempReader::Vendor::Amd;
    return CpuTempReader::Vendor::Unknown;
}

constexpr uint32_t kMsrTjMax = 0x1A2;
constexpr uint32_t kMsrPackageTherm = 0x1B1;
constexpr uint32_t kSmnTctl = 0x59800;

float AmdOffset(uint32_t family, uint32_t model) {
    if (family >= 0x19) return 0.f;
    if (family == 0x17) {
        if (model >= 0x71) return 0.f;
        return 10.f;
    }
    (void)model;
    return 0.f;
}

} // namespace

bool CpuTempReader::Init(PawnIoClient& pio, std::wstring& err) {
    pio_ = &pio;
    auto v = DetectVendor();
    if (v == CpuTempReader::Vendor::Unknown) {
        err = L"Unsupported CPU vendor";
        return false;
    }
    vendor_ = v;
    const wchar_t* mod = v == CpuTempReader::Vendor::Intel ? L"IntelMSR.bin" : L"AMDFamily17.bin";
    return pio.LoadModule(mod, err);
}

bool CpuTempReader::ReadMsr(uint32_t msr, uint64_t& val) {
    std::wstring err;
    uint64_t in = msr;
    uint64_t out = 0;
    if (!pio_->Execute("ioctl_read_msr", &in, 1, &out, 1, err)) return false;
    val = out;
    return true;
}

bool CpuTempReader::ReadSmn(uint32_t addr, uint32_t& val) {
    std::wstring err;
    uint64_t in = addr;
    uint64_t out = 0;
    if (!pio_->Execute("ioctl_read_smn", &in, 1, &out, 1, err)) return false;
    val = static_cast<uint32_t>(out);
    return true;
}

float CpuTempReader::ReadC() {
    if (!pio_) return NAN;

    if (vendor_ == CpuTempReader::Vendor::Intel) {
        uint64_t tj_raw = 0, pkg_raw = 0;
        if (!ReadMsr(kMsrTjMax, tj_raw) || !ReadMsr(kMsrPackageTherm, pkg_raw))
            return NAN;
        int tj_max = static_cast<int>((tj_raw >> 16) & 0xFF);
        if (tj_max <= 0 || tj_max > 150) tj_max = 100;
        if (!(pkg_raw & (1ull << 31))) return NAN;
        int dts = static_cast<int>((pkg_raw >> 16) & 0x7F);
        return static_cast<float>(tj_max - dts);
    }

    if (vendor_ == CpuTempReader::Vendor::Amd) {
        HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, L"Global\\Access_PCI");
        if (!mutex) mutex = CreateMutexW(nullptr, FALSE, L"Global\\Access_PCI");
        if (mutex) WaitForSingleObject(mutex, 2000);

        uint32_t raw = 0;
        bool ok = ReadSmn(kSmnTctl, raw);
        if (mutex) {
            ReleaseMutex(mutex);
            CloseHandle(mutex);
        }
        if (!ok) return NAN;
        int tctl = static_cast<int>((raw >> 21) & 0x7FF);
        if (tctl <= 0 || tctl > 255) return NAN;

        int info[4]{};
        __cpuid(info, 1);
        uint32_t family = ((info[0] >> 8) & 0xF) + ((info[0] >> 20) & 0xFF);
        uint32_t model = ((info[0] >> 4) & 0xF) | ((info[0] >> 12) & 0xF0);
        return static_cast<float>(tctl - AmdOffset(family, model));
    }

    return NAN;
}

} // namespace hwmon
