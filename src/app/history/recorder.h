#pragma once
#include "core/snapshot.h"
#include "store.h"
#include <cstdint>
#include <deque>
#include <vector>

namespace hwmon {

// 每秒采样进入缓冲，每 10s 批量追加落盘（DESIGN_v2.md §6.5）
class Recorder {
public:
    bool Init(const std::wstring& dir, uint64_t now_ts);
    void Record(const Snapshot& snap, uint64_t ts_unix);
    void FlushNow(uint64_t ts_unix);
    void Shutdown();

    // 供后续查看器（M3）：最近样本环形缓冲
    const std::deque<RawSample>& recent() const { return recent_; }
    bool disk_error() const { return disk_error_; }

private:
    Store store_;
    std::vector<RawSample> pending_;
    std::deque<RawSample> recent_;
    uint64_t last_ts_ = 0;
    uint64_t last_flush_ts_ = 0;
    bool has_last_ = false;
    bool disk_error_ = false;

    static constexpr size_t kRecentCap = 1800;   // 最近 30 分钟
};

} // namespace hwmon
