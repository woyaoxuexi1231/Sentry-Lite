#include "recorder.h"

namespace hwmon {

bool Recorder::Init(const std::wstring& dir, uint64_t now_ts) {
    last_flush_ts_ = now_ts;
    pending_.reserve(16);
    return store_.Open(dir, now_ts);
}

void Recorder::Record(const Snapshot& snap, uint64_t ts_unix) {
    if (ts_unix <= last_ts_) {
        if (has_last_) return;
    }

    RawSample s{};
    s.ts_unix = ts_unix;
    store_pct(s.cpu_pct, snap.cpu_pct);
    s.cpu_temp_c = snap.cpu_temp_c;
    store_pct(s.gpu_pct, snap.gpu_pct);
    s.gpu_temp_c = snap.gpu_temp_c;
    store_pct(s.ram_pct, snap.ram_pct);
    s.net_up_bps = snap.net_up_bps;
    s.net_down_bps = snap.net_down_bps;
    if (has_last_ && ts_unix != last_ts_ + 1) s.flags |= 1;

    pending_.push_back(s);
    last_ts_ = ts_unix;
    has_last_ = true;

    if (ts_unix - last_flush_ts_ >= 10) {
        if (!store_.Append(pending_, ts_unix)) {
            disk_error_ = true;
            store_.Close();
        } else {
            disk_error_ = false;
        }
        pending_.clear();
        last_flush_ts_ = ts_unix;
    }
}

void Recorder::FlushNow(uint64_t ts_unix) {
    if (pending_.empty() && disk_error_) return;
    if (store_.Append(pending_, ts_unix)) disk_error_ = false;
    pending_.clear();
    last_flush_ts_ = ts_unix;
}

void Recorder::ClearAll(const std::wstring& dir, uint64_t now_ts) {
    pending_.clear();
    store_.Close();
    has_last_ = false;
    last_ts_ = 0;
    disk_error_ = false;

    for (const wchar_t* pat : {L"raw-*.hwdb", L"min-*.hwdb"}) {
        std::wstring pattern = dir + L"\\" + pat;
        WIN32_FIND_DATAW fd{};
        HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
        if (find == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }

    if (store_.Open(dir, now_ts)) {
        last_flush_ts_ = now_ts;
    } else {
        disk_error_ = true;
    }
}

void Recorder::Shutdown() {
    if (has_last_) FlushNow(last_ts_);
    store_.Close();
}

} // namespace hwmon
