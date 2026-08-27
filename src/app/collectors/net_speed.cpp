#include "net_speed.h"

// winsock2 须在 windows.h 之前，否则 INADDR_ANY / GetBestInterface 不可用
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

namespace hwmon {

void NetSpeedCollector::SetNicIndex(unsigned long if_index) {
    if (nic_ == if_index) return;
    nic_ = if_index;
    has_prev_ = false;
}

bool NetSpeedCollector::Tick(float& up_bps, float& down_bps) {
    up_bps = down_bps = 0.f;

    unsigned long target = nic_;
    if (target == 0) {
        if (GetBestInterface(INADDR_ANY, &target) != NO_ERROR) {
            has_prev_ = false;
            return false;
        }
    }

    MIB_IF_TABLE2* table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR) return false;

    bool found = false;
    unsigned long long in_octets = 0, out_octets = 0;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IF_ROW2& row = table->Table[i];
        if (row.InterfaceIndex != target) continue;
        if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.OperStatus != IfOperStatusUp) break;
        in_octets = row.InOctets;
        out_octets = row.OutOctets;
        found = true;
        break;
    }
    FreeMibTable(table);
    if (!found) { has_prev_ = false; return false; }

    if (resolved_ != target) { // 接口切换视为重新建立基线
        resolved_ = target;
        has_prev_ = false;
    }

    unsigned long long now_ms = GetTickCount64();
    if (!has_prev_) {
        prev_in_octets_ = in_octets; prev_out_octets_ = out_octets;
        prev_tick_ms_ = now_ms;
        has_prev_ = true;
        return true; // 首拍无速率
    }

    unsigned long long dt_ms = now_ms - prev_tick_ms_;
    if (dt_ms < 200) return true; // 时钟异常保护
    double dt_s = static_cast<double>(dt_ms) / 1000.0;

    // 计数器回绕/重置：新值更小则该秒不计
    double in_bps = 0, out_bps = 0;
    if (in_octets >= prev_in_octets_)
        in_bps = static_cast<double>(in_octets - prev_in_octets_) / dt_s;
    if (out_octets >= prev_out_octets_)
        out_bps = static_cast<double>(out_octets - prev_out_octets_) / dt_s;

    prev_in_octets_ = in_octets; prev_out_octets_ = out_octets;
    prev_tick_ms_ = now_ms;

    up_bps = static_cast<float>(out_bps);
    down_bps = static_cast<float>(in_bps);
    return true;
}

} // namespace hwmon
