#pragma once
#include "shared/sample.h"
#include <cstdint>
#include <string>
#include <vector>

namespace hwmon {

struct HistoryStats {
    uint8_t cpu_max = 0, cpu_avg = 0;
    uint8_t gpu_max = 0, gpu_avg = 0;
    uint8_t ram_max = 0, ram_avg = 0;
};

// 从 raw-YYYYMMDD.hwdb 读取 [start_ts, end_ts] 区间（UTC 秒）
bool QueryRawRange(const std::wstring& dir, uint64_t start_ts, uint64_t end_ts,
                   std::vector<RawSample>& out);

// 单点查询（十字光标）：精确命中或最近一条
bool QueryAt(const std::wstring& dir, uint64_t ts, RawSample& out, bool& exact);

void ComputeStats(const std::vector<RawSample>& samples, HistoryStats& stats);

} // namespace hwmon
