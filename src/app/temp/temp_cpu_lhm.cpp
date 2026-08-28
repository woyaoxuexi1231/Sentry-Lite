#include "temp_cpu_lhm.h"
#include <windows.h>
#include <cmath>
#include <string>

namespace hwmon {

namespace {

using init_fn = int(__stdcall*)();
using read_fn = int(__stdcall*)(void*);
using shutdown_fn = void(__stdcall*)();

using hostfxr_initialize_for_runtime_config_fn = int(__stdcall*)(const wchar_t*, const void*, void**);
using hostfxr_close_fn = int(__stdcall*)(void*);
using hostfxr_get_runtime_delegate_fn = int(__stdcall*)(void*, int, void**);
using load_assembly_and_get_function_pointer_fn = int(__stdcall*)(
    const wchar_t*, const wchar_t*, const wchar_t*, const wchar_t*, void*, void**);

HMODULE hostfxr_ = nullptr;
void* hostctx_ = nullptr;
init_fn init_ = nullptr;
read_fn read_ = nullptr;
shutdown_fn shutdown_ = nullptr;

std::wstring ExeDir() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    const size_t slash = dir.find_last_of(L'\\');
    return slash == std::wstring::npos ? L"." : dir.substr(0, slash + 1);
}

bool FileExists(const std::wstring& p) {
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring FindHostFxrPath() {
    const std::wstring root = L"C:\\Program Files\\dotnet\\host\\fxr\\";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((root + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return L"";

    std::wstring best;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        if (fd.cFileName[0] == L'.') continue;
        const std::wstring candidate = root + fd.cFileName + L"\\hostfxr.dll";
        if (FileExists(candidate) && candidate > best) best = candidate;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return best;
}

bool BindFn(load_assembly_and_get_function_pointer_fn load_fn, const wchar_t* assembly,
            const wchar_t* method, const wchar_t* delegate_type, void** out) {
    return load_fn(assembly,
                     L"SentryLite.LhmBridge.CpuTempApi, Sentry-Lite-lhm",
                     method, delegate_type, nullptr, out) == 0 && *out;
}

bool LoadHostFxr(std::wstring& err) {
    if (init_) return true;

    const std::wstring hostfxr_path = FindHostFxrPath();
    if (hostfxr_path.empty()) {
        err = L".NET 8 hostfxr not found (install .NET 8 Desktop Runtime)";
        return false;
    }

    hostfxr_ = LoadLibraryW(hostfxr_path.c_str());
    if (!hostfxr_) {
        err = L"Failed to load hostfxr.dll";
        return false;
    }

    auto init_cfg = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
        GetProcAddress(hostfxr_, "hostfxr_initialize_for_runtime_config"));
    auto get_delegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
        GetProcAddress(hostfxr_, "hostfxr_get_runtime_delegate"));
    auto close_fn = reinterpret_cast<hostfxr_close_fn>(
        GetProcAddress(hostfxr_, "hostfxr_close"));
    if (!init_cfg || !get_delegate || !close_fn) {
        err = L"hostfxr exports missing";
        return false;
    }

    const std::wstring cfg = ExeDir() + L"Sentry-Lite-lhm.runtimeconfig.json";
    const std::wstring dll_path = ExeDir() + L"Sentry-Lite-lhm.dll";
    if (!FileExists(cfg)) {
        err = L"Sentry-Lite-lhm.runtimeconfig.json not found next to Sentry-Lite.exe";
        return false;
    }
    if (!FileExists(dll_path)) {
        err = L"Sentry-Lite-lhm.dll not found (LibreHardwareMonitor bridge)";
        return false;
    }

    if (init_cfg(cfg.c_str(), nullptr, &hostctx_) != 0 || !hostctx_) {
        err = L"hostfxr_initialize_for_runtime_config failed";
        return false;
    }

    void* load_ptr = nullptr;
    // hostfxr_delegate_type::hdt_load_assembly_and_get_function_pointer == 5
    // (1 is hdt_load_in_memory_assembly — wrong API, causes "Failed to locate managed application")
    constexpr int hdt_load_assembly_and_get_function_pointer = 5;
    if (get_delegate(hostctx_, hdt_load_assembly_and_get_function_pointer, &load_ptr) != 0 || !load_ptr) {
        err = L"hostfxr_get_runtime_delegate failed";
        close_fn(hostctx_);
        hostctx_ = nullptr;
        return false;
    }

    auto load_fn = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(load_ptr);
    const wchar_t* assembly = dll_path.c_str();

    void* p = nullptr;
    if (!BindFn(load_fn, assembly, L"InitNative",
                L"SentryLite.LhmBridge.CpuTempInitDelegate, Sentry-Lite-lhm", &p)) {
        err = L"Failed to bind CpuTempApi.InitNative";
        close_fn(hostctx_);
        hostctx_ = nullptr;
        return false;
    }
    init_ = reinterpret_cast<init_fn>(p);

    p = nullptr;
    if (!BindFn(load_fn, assembly, L"ReadNative",
                L"SentryLite.LhmBridge.CpuTempReadDelegate, Sentry-Lite-lhm", &p)) {
        err = L"Failed to bind CpuTempApi.ReadNative";
        close_fn(hostctx_);
        hostctx_ = nullptr;
        return false;
    }
    read_ = reinterpret_cast<read_fn>(p);

    p = nullptr;
    if (!BindFn(load_fn, assembly, L"ShutdownNative",
                L"SentryLite.LhmBridge.CpuTempShutdownDelegate, Sentry-Lite-lhm", &p)) {
        err = L"Failed to bind CpuTempApi.ShutdownNative";
        close_fn(hostctx_);
        hostctx_ = nullptr;
        return false;
    }
    shutdown_ = reinterpret_cast<shutdown_fn>(p);
    return true;
}

void UnloadHostFxr() {
    if (shutdown_) shutdown_();
    shutdown_ = nullptr;
    read_ = nullptr;
    init_ = nullptr;
    if (hostctx_) {
        auto close_fn = reinterpret_cast<hostfxr_close_fn>(GetProcAddress(hostfxr_, "hostfxr_close"));
        if (close_fn) close_fn(hostctx_);
        hostctx_ = nullptr;
    }
    if (hostfxr_) {
        FreeLibrary(hostfxr_);
        hostfxr_ = nullptr;
    }
}

} // namespace

bool CpuTempReader::Init(std::wstring& err) {
    Shutdown();
    if (!LoadHostFxr(err)) return false;
    if (init_() != 0) {
        err = L"LHM init failed";
        UnloadHostFxr();
        return false;
    }
    // Do not block startup waiting for the first sample — ReadC() on the 1s tick.
    ready_ = false;
    return true;
}

float CpuTempReader::ReadC() {
    if (!read_) return NAN;
    float t = NAN;
    if (read_(&t) == 0) return NAN;
    if (std::isnan(t) || t < -20.f || t > 125.f) return NAN;
    ready_ = true;
    return t;
}

std::wstring CpuTempReader::LastMessage() const {
    return ready_ ? L"" : L"CPU temperature unavailable";
}

bool CpuTempReader::BridgeAlive() const {
    return read_ != nullptr;
}

void CpuTempReader::Shutdown() {
    UnloadHostFxr();
    ready_ = false;
}

} // namespace hwmon
