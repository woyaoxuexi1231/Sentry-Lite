#include "retention.h"
#include <windows.h>
#include <algorithm>
#include <ctime>
#include <string>
#include <vector>

namespace hwmon {

namespace {

struct FileEntry {
    std::wstring path;
    uint32_t day;        // yyyymmdd
    unsigned long long size;
};

bool ParseDay(const wchar_t* name, uint32_t& day) {
    int y = 0, m = 0, d = 0;
    if (swscanf(name, L"raw-%04d%02d%02d.hwdb", &y, &m, &d) != 3) return false;
    day = static_cast<uint32_t>(y * 10000u + m * 100u + d);
    return true;
}

} // namespace

void Retention::RunOnce(const std::wstring& dir, int raw_retention_days,
                        uint64_t max_total_mb, uint64_t now_ts) {
    std::wstring pattern = dir + L"\\raw-*.hwdb";
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return;

    time_t t = static_cast<time_t>(now_ts);
    tm utc{};
    gmtime_s(&utc, &t);
    uint32_t today = static_cast<uint32_t>((utc.tm_year + 1900) * 10000u +
                                           (utc.tm_mon + 1) * 100u + utc.tm_mday);

    uint32_t cutoff = today - static_cast<uint32_t>(raw_retention_days) * 10000u;

    std::vector<FileEntry> files;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        uint32_t day = 0;
        if (!ParseDay(fd.cFileName, day)) continue;
        ULARGE_INTEGER sz;
        sz.HighPart = fd.nFileSizeHigh;
        sz.LowPart = fd.nFileSizeLow;
        if (day == today) continue; // 永不删今天的文件
        if (day < cutoff)
            DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
        else
            files.push_back({dir + L"\\" + fd.cFileName, day, sz.QuadPart});
    } while (FindNextFileW(find, &fd));
    FindClose(find);

    // 容量上限：从最老的开始删（当天文件除外）
    const unsigned long long cap = max_total_mb * 1024ull * 1024ull;
    unsigned long long total = 0;
    for (auto& f : files) total += f.size;
    if (total > cap) {
        std::sort(files.begin(), files.end(),
                  [](const FileEntry& a, const FileEntry& b) { return a.day < b.day; });
        for (auto& f : files) {
            if (total <= cap) break;
            if (DeleteFileW(f.path.c_str())) total -= f.size;
        }
    }
}

} // namespace hwmon
