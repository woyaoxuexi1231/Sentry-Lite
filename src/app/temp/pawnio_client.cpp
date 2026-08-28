#include "pawnio_client.h"
#include <windows.h>
#include <vector>

namespace hwmon {

namespace {

std::wstring ExeDir() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    size_t slash = dir.find_last_of(L'\\');
    return slash == std::wstring::npos ? L"." : dir.substr(0, slash + 1);
}

bool FileExists(const std::wstring& p) {
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool ReadAll(const std::wstring& path, std::vector<uint8_t>& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 4 * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    out.resize(static_cast<size_t>(sz.QuadPart));
    DWORD read = 0;
    BOOL ok = ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &read, nullptr);
    CloseHandle(h);
    return ok && read == out.size();
}

} // namespace

std::wstring PawnIoClient::FindDll() {
    wchar_t buf[MAX_PATH]{};
    if (DWORD n = SearchPathW(nullptr, L"PawnIOLib.dll", nullptr, MAX_PATH, buf, nullptr);
        n > 0 && n < MAX_PATH)
        return buf;
    const wchar_t* fixed = L"C:\\Program Files\\PawnIO\\PawnIOLib.dll";
    return FileExists(fixed) ? fixed : L"";
}

std::wstring PawnIoClient::FindBlob(const wchar_t* name) {
    const std::wstring exe = ExeDir() + name;
    if (FileExists(exe)) return exe;
    const std::wstring pf = L"C:\\Program Files\\PawnIO\\" + std::wstring(name);
    if (FileExists(pf)) return pf;
    return L"";
}

bool PawnIoClient::Open(std::wstring& err) {
    Close();
    std::wstring dll = FindDll();
    if (dll.empty()) {
        err = L"PawnIOLib.dll not found — install PawnIO (https://pawnio.eu)";
        return false;
    }
    dll_ = LoadLibraryW(dll.c_str());
    if (!dll_) {
        err = L"Failed to load PawnIOLib.dll";
        return false;
    }
    fn_open_ = reinterpret_cast<open_fn>(GetProcAddress(dll_, "pawnio_open"));
    fn_load_ = reinterpret_cast<load_fn>(GetProcAddress(dll_, "pawnio_load"));
    fn_exec_ = reinterpret_cast<exec_fn>(GetProcAddress(dll_, "pawnio_execute"));
    fn_close_ = reinterpret_cast<close_fn>(GetProcAddress(dll_, "pawnio_close"));
    if (!fn_open_ || !fn_load_ || !fn_exec_ || !fn_close_) {
        err = L"PawnIOLib.dll missing required exports";
        Close();
        return false;
    }
    long hr = fn_open_(&handle_);
    if (hr < 0 || !handle_) {
        err = L"pawnio_open failed (admin rights and PawnIO driver required)";
        Close();
        return false;
    }
    return true;
}

bool PawnIoClient::LoadModule(const wchar_t* blob_name, std::wstring& err) {
    if (!handle_) {
        err = L"PawnIO not open";
        return false;
    }
    std::wstring path = FindBlob(blob_name);
    if (path.empty()) {
        err = std::wstring(L"Module not found: ") + blob_name + L" (get from PawnIO.Modules Releases)";
        return false;
    }
    std::vector<uint8_t> blob;
    if (!ReadAll(path, blob)) {
        err = L"Failed to read module " + path;
        return false;
    }
    long hr = fn_load_(handle_, blob.data(), blob.size());
    if (hr < 0) {
        err = L"pawnio_load failed";
        return false;
    }
    return true;
}

bool PawnIoClient::Execute(const char* fn, const uint64_t* in, size_t in_n,
                             uint64_t* out, size_t out_n, std::wstring& err) {
    if (!handle_) return false;
    size_t written = 0;
    long hr = fn_exec_(handle_, fn, in, in_n, out, out_n, &written);
    if (hr < 0) {
        err = L"pawnio_execute failed";
        return false;
    }
    return true;
}

void PawnIoClient::Close() {
    if (handle_ && fn_close_) fn_close_(handle_);
    handle_ = nullptr;
    if (dll_) FreeLibrary(dll_);
    dll_ = nullptr;
    fn_open_ = nullptr;
    fn_load_ = nullptr;
    fn_exec_ = nullptr;
    fn_close_ = nullptr;
}

} // namespace hwmon
