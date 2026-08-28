#include "ui/dashboard.h"
#include <objbase.h>

namespace {

// 单实例：只允许一个进程运行
bool AcquireSingleInstance() {
    HANDLE m = CreateMutexW(nullptr, TRUE, L"Local\\Sentry-Lite_single_instance");
    if (!m) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(m);
        return false;
    }
    return true; // 句柄刻意不关闭，随进程生命周期持有
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    if (!AcquireSingleInstance()) return 0;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool com_ok = SUCCEEDED(hr);

    int exit_code = 0;
    {
        hwmon::Dashboard dashboard;
        if (!dashboard.Init()) {
            exit_code = 1;
        } else {
            exit_code = dashboard.Run();
        }
    }

    if (com_ok) CoUninitialize();
    return exit_code;
}