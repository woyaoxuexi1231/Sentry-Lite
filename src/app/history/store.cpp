#include "store.h"
#include <windows.h>
#include <shlwapi.h>
#include <cstdio>
#include <ctime>

#pragma comment(lib, "shlwapi.lib")

namespace hwmon {

namespace {

uint32_t DayOf(uint64_t ts) {
    time_t t = static_cast<time_t>(ts);
    tm utc{};
    gmtime_s(&utc, &t);
    return static_cast<uint32_t>((utc.tm_year + 1900) * 10000u +
                                 (utc.tm_mon + 1) * 100u + utc.tm_mday);
}

std::wstring DayPath(const std::wstring& dir, uint64_t ts) {
    time_t t = static_cast<time_t>(ts);
    tm utc{};
    gmtime_s(&utc, &t);
    wchar_t name[64];
    swprintf(name, 64, L"raw-%04d%02d%02d.hwdb",
             utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday);
    return dir + L"\\" + name;
}

struct FileHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t record_size;
    uint32_t reserved;
    uint64_t start_ts;
    uint64_t count;
};
static_assert(sizeof(FileHeader) == 32);

} // namespace

bool Store::Open(const std::wstring& dir, uint64_t ts_unix) {
    Close();
    dir_ = dir;
    return OpenFile(ts_unix);
}

bool Store::OpenFile(uint64_t ts_unix) {
    Close();

    CreateDirectoryW(dir_.c_str(), nullptr);
    path_ = DayPath(dir_, ts_unix);
    open_day_ = DayOf(ts_unix);

    BOOL existed = PathFileExistsW(path_.c_str());
    HANDLE h = CreateFileW(path_.c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);

    if (!existed || size.QuadPart == 0) {
        FileHeader hdr{kHwdbMagic, kHwdbVersion, sizeof(RawSample), 0, ts_unix, 0};
        DWORD written = 0;
        WriteFile(h, &hdr, sizeof(hdr), &written, nullptr);
        count_ = 0;
    } else {
        // 校验头并容忍尾部半条记录
        FileHeader hdr{};
        DWORD read = 0;
        SetFilePointer(h, 0, nullptr, FILE_BEGIN);
        ReadFile(h, &hdr, sizeof(hdr), &read, nullptr);
        if (hdr.magic != kHwdbMagic || hdr.record_size != sizeof(RawSample)) {
            CloseHandle(h);
            return false;
        }
        long long payload = size.QuadPart - static_cast<long long>(sizeof(hdr));
        if (payload < 0 || payload % static_cast<long long>(sizeof(RawSample)) != 0) {
            long long aligned = sizeof(hdr) +
                (payload > 0 ? payload / sizeof(RawSample) * sizeof(RawSample) : 0);
            SetFilePointer(h, static_cast<long>(aligned), nullptr, FILE_BEGIN);
            SetEndOfFile(h);
        } else {
            SetFilePointer(h, 0, nullptr, FILE_END);
        }
        count_ = hdr.count; // 允许与实际条数短暂不一致，仅在读取端以实际对齐为准
    }

    LARGE_INTEGER end{};
    GetFileSizeEx(h, &end);
    SetFilePointer(h, end.QuadPart, nullptr, FILE_BEGIN);

    handle_ = h;
    return true;
}

bool Store::Append(const std::vector<RawSample>& batch, uint64_t ts_unix) {
    if (batch.empty()) return true;

    uint32_t day = DayOf(ts_unix);
    if (!*this || day != open_day_) {
        if (!OpenFile(ts_unix)) return false;
    }

    DWORD written = 0;
    if (!WriteFile(handle_, batch.data(),
                   static_cast<DWORD>(batch.size() * sizeof(RawSample)),
                   &written, nullptr))
        return false;

    count_ += batch.size();
    return true;
}

void Store::Close() {
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;
}

} // namespace hwmon
