#include "service.h"
#include "shm_publisher.h"
#include "pawnio_client.h"
#include "temp_cpu.h"
#include "temp_gpu_nvml.h"
#include <windows.h>
#include <cstdio>

namespace {

bool HasArg(int argc, wchar_t** argv, const wchar_t* flag) {
    for (int i = 1; i < argc; ++i)
        if (_wcsicmp(argv[i], flag) == 0) return true;
    return false;
}

void ConsoleLoop() {
    wprintf(L"Sentry-Lite-svc 控制台模式（Ctrl+C 退出）\n");
    hwmon::ShmPublisher shm;
    if (!shm.Init()) {
        wprintf(L"共享内存初始化失败\n");
        return;
    }
    hwmon::PawnIoClient pio;
    std::wstring err;
    hwmon::CpuTempReader cpu;
    bool cpu_ok = pio.Open(err) && cpu.Init(pio, err);
    if (!cpu_ok) wprintf(L"CPU 温度：%s\n", err.c_str());

    hwmon::GpuTempNvml gpu;
    bool gpu_ok = gpu.Init();
    if (!gpu_ok) wprintf(L"GPU 温度（NVML）不可用\n");

    while (true) {
        float ct = cpu_ok ? cpu.ReadC() : NAN;
        float gt = gpu_ok ? gpu.ReadC() : NAN;
        shm.Publish(ct, gt);
        wprintf(L"CPU %.1f°C  GPU %.1f°C\r", ct, gt);
        Sleep(1000);
    }
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (HasArg(argc, argv, L"--install-service"))
        return hwmon::InstallService() ? 0 : 1;
    if (HasArg(argc, argv, L"--uninstall-service"))
        return hwmon::UninstallService() ? 0 : 1;
    if (HasArg(argc, argv, L"--console")) {
        ConsoleLoop();
        return 0;
    }
    return hwmon::RunServiceMain();
}
