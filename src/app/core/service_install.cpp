#include "service_install.h"
#include <windows.h>
#include <shellapi.h>

namespace hwmon {

namespace {

constexpr wchar_t kServiceName[] = L"SentryLiteTemp";

std::wstring ExeDir() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    size_t slash = dir.find_last_of(L'\\');
    return slash == std::wstring::npos ? L"." : dir.substr(0, slash + 1);
}

} // namespace

std::wstring SvcExePath() {
    return ExeDir() + L"Sentry-Lite-svc.exe";
}

bool IsTempServiceInstalled() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, kServiceName, SERVICE_QUERY_STATUS);
    bool exists = svc != nullptr;
    if (svc) CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return exists;
}

bool InstallTempService(std::wstring& err) {
    std::wstring svc = SvcExePath();
    if (GetFileAttributesW(svc.c_str()) == INVALID_FILE_ATTRIBUTES) {
        err = L"找不到 Sentry-Lite-svc.exe，请与主程序放在同一目录。";
        return false;
    }
    SHELLEXECUTEINFOW sei{sizeof(sei)};
    sei.lpVerb = L"runas";
    sei.lpFile = svc.c_str();
    sei.lpParameters = L"--install-service";
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) {
        err = L"需要管理员权限才能安装温度服务。";
        return false;
    }
    // 安装后尝试启动
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm) {
        SC_HANDLE s = OpenServiceW(scm, kServiceName, SERVICE_START);
        if (s) {
            StartServiceW(s, 0, nullptr);
            CloseServiceHandle(s);
        }
        CloseServiceHandle(scm);
    }
    return true;
}

bool UninstallTempService(std::wstring& err) {
    std::wstring svc = SvcExePath();
    if (GetFileAttributesW(svc.c_str()) == INVALID_FILE_ATTRIBUTES) {
        err = L"找不到 Sentry-Lite-svc.exe";
        return false;
    }
    SHELLEXECUTEINFOW sei{sizeof(sei)};
    sei.lpVerb = L"runas";
    sei.lpFile = svc.c_str();
    sei.lpParameters = L"--uninstall-service";
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) {
        err = L"需要管理员权限才能卸载温度服务。";
        return false;
    }
    return true;
}

} // namespace hwmon
