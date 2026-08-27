#pragma once

namespace hwmon {

// 网速：默认路由网卡收发字节每秒差分（DESIGN_v2.md §3）
class NetSpeedCollector {
public:
    void SetNicIndex(unsigned long if_index); // 配置固定网卡时调用；0 = auto
    // 输出 B/s，不可得返回 false
    bool Tick(float& up_bps, float& down_bps);

private:
    bool has_prev_ = false;
    unsigned long nic_ = 0;        // 0 = 自动（默认路由）
    unsigned long resolved_ = 0;   // 实际使用的接口
    unsigned long long prev_in_octets_ = 0, prev_out_octets_ = 0;
    unsigned long long prev_tick_ms_ = 0;
};

} // namespace hwmon
