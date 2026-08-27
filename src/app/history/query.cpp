#include "query.h"
#include <windows.h>
#include <algorithm>
#include <ctime>

namespace hwmon {

namespace {

struct FileHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t record_size;
    uint32_t reserved;
    uint64_t start_ts;
    uint64_t count;
};
static_assert(sizeof(FileHeader) == 32);

uint32_t DayOf(uint64_t ts) {
    time_t t = static_cast<time_t>(ts);
    tm utc{};
    gmtime_s(&utc, &t);
    return static_cast<uint32_t>((utc.tm_year + 1900) * 10000u +
                                 (utc.tm_mon + 1) * 100u + utc.tm_mday);
}

std::wstring DayPath(const std::wstring& dir, uint32_t day) {
    int y = static_cast<int>(day / 10000u);
    int m = static_cast<int>((day / 100u) % 100u);
    int d = static_cast<int>(day % 100u);
    wchar_t name[64];
    swprintf(name, 64, L"raw-%04d%02d%02d.hwdb", y, m, d);
    return dir + L"\\" + name;
}

uint32_t NextDay(uint32_t day) {
    int y = static_cast<int>(day / 10000u);
    int m = static_cast<int>((day / 100u) % 100u);
    int d = static_cast<int>(day % 100u);
    tm utc{};
    utc.tm_year = y - 1900;
    utc.tm_mon = m - 1;
    utc.tm_mday = d;
    time_t t = _mkgmtime(&utc);
    t += 86400;
    gmtime_s(&utc, &t);
    return static_cast<uint32_t>((utc.tm_year + 1900) * 10000u +
                                 (utc.tm_mon + 1) * 100u + utc.tm_mday);
}

bool ReadFileRange(const std::wstring& path, uint64_t start_ts, uint64_t end_ts,
                   std::vector<RawSample>& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart < static_cast<long long>(sizeof(FileHeader))) {
        CloseHandle(h);
        return false;
    }

    FileHeader hdr{};
    DWORD read = 0;
    ReadFile(h, &hdr, sizeof(hdr), &read, nullptr);
    if (hdr.magic != kHwdbMagic || hdr.record_size != sizeof(RawSample)) {
        CloseHandle(h);
        return false;
    }

    long long payload = size.QuadPart - static_cast<long long>(sizeof(hdr));
    if (payload <= 0 || payload % static_cast<long long>(sizeof(RawSample)) != 0) {
        CloseHandle(h);
        return false;
    }
    size_t n = static_cast<size_t>(payload / sizeof(RawSample));
    std::vector<RawSample> all(n);
    ReadFile(h, all.data(), static_cast<DWORD>(payload), &read, nullptr);
    CloseHandle(h);

    if (all.empty()) return true;

    auto it = std::lower_bound(all.begin(), all.end(), start_ts,
                               [](const RawSample& s, uint64_t t) { return s.ts_unix < t; });
    for (; it != all.end() && it->ts_unix <= end_ts; ++it)
        out.push_back(*it);
    return true;
}

} // namespace

bool QueryRawRange(const std::wstring& dir, uint64_t start_ts, uint64_t end_ts,
                   std::vector<RawSample>& out) {
    out.clear();
    if (end_ts < start_ts) return false;

    uint32_t day = DayOf(start_ts);
    uint32_t end_day = DayOf(end_ts);
    while (day <= end_day) {
        ReadFileRange(DayPath(dir, day), start_ts, end_ts, out);
        if (day == end_day) break;
        day = NextDay(day);
    }
    return true;
}

bool QueryAt(const std::wstring& dir, uint64_t ts, RawSample& out, bool& exact) {
    exact = false;
    std::vector<RawSample> window;
    uint64_t lo = ts > 120 ? ts - 120 : 0;
    if (!QueryRawRange(dir, lo, ts + 120, window) || window.empty())
        return false;

    auto it = std::lower_bound(window.begin(), window.end(), ts,
                               [](const RawSample& s, uint64_t t) { return s.ts_unix < t; });
    if (it != window.end() && it->ts_unix == ts) {
        out = *it;
        exact = true;
        return true;
    }
    if (it != window.begin()) {
        --it;
        out = *it;
        return true;
    }
    if (!window.empty()) {
        out = window.front();
        return true;
    }
    return false;
}

void ComputeStats(const std::vector<RawSample>& samples, HistoryStats& stats) {
    if (samples.empty()) return;
    uint64_t cpu_sum = 0, gpu_sum = 0, ram_sum = 0;
    size_t cpu_n = 0, gpu_n = 0, ram_n = 0;
    for (const auto& s : samples) {
        if (valid_pct(s.cpu_pct)) {
            stats.cpu_max = std::max(stats.cpu_max, s.cpu_pct);
            cpu_sum += s.cpu_pct;
            ++cpu_n;
        }
        if (valid_pct(s.gpu_pct)) {
            stats.gpu_max = std::max(stats.gpu_max, s.gpu_pct);
            gpu_sum += s.gpu_pct;
            ++gpu_n;
        }
        if (valid_pct(s.ram_pct)) {
            stats.ram_max = std::max(stats.ram_max, s.ram_pct);
            ram_sum += s.ram_pct;
            ++ram_n;
        }
    }
    if (cpu_n) stats.cpu_avg = static_cast<uint8_t>((cpu_sum + cpu_n / 2) / cpu_n);
    if (gpu_n) stats.gpu_avg = static_cast<uint8_t>((gpu_sum + gpu_n / 2) / gpu_n);
    if (ram_n) stats.ram_avg = static_cast<uint8_t>((ram_sum + ram_n / 2) / ram_n);
}

} // namespace hwmon
