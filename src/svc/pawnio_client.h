#pragma once
#include <windows.h>
#include <cstdint>
#include <string>

namespace hwmon {

// 动态加载 PawnIOLib.dll，加载官方签名模块 blob（IntelMSR.bin / AMDFamily17.bin）
class PawnIoClient {
public:
    bool Open(std::wstring& err);
    bool LoadModule(const wchar_t* blob_name, std::wstring& err);
    bool Execute(const char* fn, const uint64_t* in, size_t in_n,
                 uint64_t* out, size_t out_n, std::wstring& err);
    void Close();
    explicit operator bool() const { return handle_ != nullptr; }

private:
    using open_fn = long(__stdcall*)(void**);
    using load_fn = long(__stdcall*)(void*, const unsigned char*, size_t);
    using exec_fn = long(__stdcall*)(void*, const char*, const uint64_t*, size_t,
                                     uint64_t*, size_t, size_t*);
    using close_fn = long(__stdcall*)(void*);

    HMODULE dll_ = nullptr;
    void* handle_ = nullptr;
    open_fn fn_open_ = nullptr;
    load_fn fn_load_ = nullptr;
    exec_fn fn_exec_ = nullptr;
    close_fn fn_close_ = nullptr;

    static std::wstring FindDll();
    static std::wstring FindBlob(const wchar_t* name);
};

} // namespace hwmon
