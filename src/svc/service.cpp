#include "service.h"
#include "shm_publisher.h"
#include "pawnio_client.h"
#include "temp_cpu.h"
#include "temp_gpu_nvml.h"
#include <windows.h>

namespace hwmon {

namespace {

SERVICE_STATUS status_{};
SERVICE_STATUS_HANDLE status_handle_ = nullptr;
HANDLE stop_event_ = nullptr;

void SetState(DWORD state, DWORD win32 = NO_ERROR, DWORD checkpoint = 0) {
    status_.dwCurrentState = state;
    status_.dwWin32ExitCode = win32;
    status_.dwCheckPoint = checkpoint;
    if (status_handle_) SetServiceStatus(status_handle_, &status_);
}

void RunLoop() {
    ShmPublisher shm;
    if (!shm.Init()) return;

    PawnIoClient pio;
    std::wstring err;
    CpuTempReader cpu;
    bool cpu_ok = pio.Open(err) && cpu.Init(pio, err);

    GpuTempNvml gpu;
    bool gpu_ok = gpu.Init();

    while (WaitForSingleObject(stop_event_, 1000) == WAIT_TIMEOUT) {
        float ct = cpu_ok ? cpu.ReadC() : NAN;
        float gt = gpu_ok ? gpu.ReadC() : NAN;
        shm.Publish(ct, gt);
    }

    gpu.Shutdown();
    pio.Close();
    shm.Shutdown();
}

void WINAPI ServiceCtrlHandler(DWORD ctrl) {
    if (ctrl == SERVICE_CONTROL_STOP && stop_event_)
        SetEvent(stop_event_);
}

void WINAPI ServiceMain(DWORD, LPWSTR*) {
    status_handle_ = RegisterServiceCtrlHandlerW(kServiceName, ServiceCtrlHandler);
    if (!status_handle_) return;

    status_.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status_.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    status_.dwWin32ExitCode = NO_ERROR;
    status_.dwServiceSpecificExitCode = 0;
    status_.dwCheckPoint = 0;
    status_.dwWaitHint = 0;
    SetState(SERVICE_START_PENDING);

    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_event_) {
        SetState(SERVICE_STOPPED, GetLastError());
        return;
    }

    SetState(SERVICE_RUNNING);
    RunLoop();
    SetState(SERVICE_STOPPED);
    CloseHandle(stop_event_);
    stop_event_ = nullptr;
}

std::wstring SvcExePath() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return exe;
}

} // namespace

bool InstallService() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm) return false;

    std::wstring path = L"\"" + SvcExePath() + L"\"";
    SC_HANDLE svc = CreateServiceW(
        scm, kServiceName, kServiceDisplay,
        SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL, path.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr);

    if (!svc) {
        if (GetLastError() == ERROR_SERVICE_EXISTS) {
            svc = OpenServiceW(scm, kServiceName, SERVICE_CHANGE_CONFIG);
            if (svc) {
                ChangeServiceConfigW(svc, SERVICE_NO_CHANGE, SERVICE_AUTO_START,
                                     SERVICE_NO_CHANGE, path.c_str(),
                                     nullptr, nullptr, nullptr, nullptr, nullptr, kServiceDisplay);
            }
        }
    }

    bool ok = svc != nullptr;
    if (svc) {
        SERVICE_DESCRIPTIONW desc{};
        desc.lpDescription = const_cast<LPWSTR>(
            L"Sentry-Lite 温度采集服务（PawnIO + NVML），供 UI 进程通过共享内存读取。");
        ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &desc);
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
    return ok;
}

bool UninstallService() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, kServiceName, DELETE | SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!svc) {
        CloseServiceHandle(scm);
        return GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST;
    }

    SERVICE_STATUS st{};
    ControlService(svc, SERVICE_CONTROL_STOP, &st);
    for (int i = 0; i < 30; ++i) {
        if (QueryServiceStatus(svc, &st) && st.dwCurrentState == SERVICE_STOPPED) break;
        Sleep(200);
    }

    BOOL ok = DeleteService(svc);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok != FALSE;
}

int RunServiceMain() {
    SERVICE_TABLE_ENTRYW table[] = {
        {const_cast<LPWSTR>(kServiceName), ServiceMain},
        {nullptr, nullptr},
    };
    if (!StartServiceCtrlDispatcherW(table))
        return GetLastError();
    return 0;
}

} // namespace hwmon
