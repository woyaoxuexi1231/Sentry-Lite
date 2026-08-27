#pragma once
#include <windows.h>
#include "shared/sample.h"
#include <cstdint>
#include <string>
#include <vector>

namespace hwmon {

// raw-YYYYMMDD.hwdb 追加写（DESIGN_v2.md §6.4）
// 头部 32B：magic u32 | version u32 | record_size u32 | reserved u32 | start_ts u64 | count u64
class Store {
public:
    bool Open(const std::wstring& dir, uint64_t ts_unix); // 打开 ts 当天的文件
    // 批量追加；跨 UTC 日自动滚动开新文件
    bool Append(const std::vector<RawSample>& batch, uint64_t ts_unix);
    void Close();
    const std::wstring& current_path() const { return path_; }
    explicit operator bool() const { return handle_ != INVALID_HANDLE_VALUE; }

private:
    bool OpenFile(uint64_t ts_unix);

    HANDLE handle_ = INVALID_HANDLE_VALUE;
    std::wstring dir_;
    std::wstring path_;
    uint32_t open_day_ = 0;     // yyyymmdd
    uint64_t count_ = 0;
};

} // namespace hwmon
