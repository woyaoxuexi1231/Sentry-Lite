#pragma once
#include <cstdint>
#include <string>

namespace hwmon {

// 保留策略（DESIGN_v2.md §6.3）：raw 文件按天删除 + 总量上限
class Retention {
public:
    // dir = history 根目录；启动与每日各跑一次
    void RunOnce(const std::wstring& dir, int raw_retention_days,
                 uint64_t max_total_mb, uint64_t now_ts);
};

} // namespace hwmon
