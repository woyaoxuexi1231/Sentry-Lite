#pragma once
#include "core/snapshot.h"
#include "store.h"
#include <cstdint>
#include <vector>

namespace hwmon {

// 每秒采样进入缓冲，每 10s 批量追加落盘
class Recorder {
public:
    bool Init(const std::wstring& dir, uint64_t now_ts);
    void Record(const Snapshot& snap, uint64_t ts_unix);
    void FlushNow(uint64_t ts_unix);
    void ClearAll(const std::wstring& dir, uint64_t now_ts);
    void Shutdown();

    bool disk_error() const { return disk_error_; }

private:
    Store store_;
    std::vector<RawSample> pending_;
    uint64_t last_ts_ = 0;
    uint64_t last_flush_ts_ = 0;
    bool has_last_ = false;
    bool disk_error_ = false;
};

} // namespace hwmon
