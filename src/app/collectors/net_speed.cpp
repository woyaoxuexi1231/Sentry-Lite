#include "net_speed.h"

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

    // Single-row lookup — avoids allocating the full IF_TABLE2 every second.
    MIB_IF_ROW2 row{};
    row.InterfaceIndex = target;
    if (GetIfEntry2(&row) != NO_ERROR) {
        has_prev_ = false;
        return false;
    }
    if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.OperStatus != IfOperStatusUp) {
        has_prev_ = false;
        return false;
    }

    const unsigned long long in_octets = row.InOctets;
    const unsigned long long out_octets = row.OutOctets;

    if (resolved_ != target) {
        resolved_ = target;
        has_prev_ = false;
    }

    unsigned long long now_ms = GetTickCount64();
    if (!has_prev_) {
        prev_in_octets_ = in_octets;
        prev_out_octets_ = out_octets;
        prev_tick_ms_ = now_ms;
        has_prev_ = true;
        return true;
    }

    unsigned long long dt_ms = now_ms - prev_tick_ms_;
    if (dt_ms < 200) return true;
    double dt_s = static_cast<double>(dt_ms) / 1000.0;

    double in_bps = 0, out_bps = 0;
    if (in_octets >= prev_in_octets_)
        in_bps = static_cast<double>(in_octets - prev_in_octets_) / dt_s;
    if (out_octets >= prev_out_octets_)
        out_bps = static_cast<double>(out_octets - prev_out_octets_) / dt_s;

    prev_in_octets_ = in_octets;
    prev_out_octets_ = out_octets;
    prev_tick_ms_ = now_ms;

    up_bps = static_cast<float>(out_bps);
    down_bps = static_cast<float>(in_bps);
    return true;
}

} // namespace hwmon
