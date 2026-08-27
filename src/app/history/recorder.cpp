#include "recorder.h"

namespace hwmon {

bool Recorder::Init(const std::wstring& dir, uint64_t now_ts) {
    last_flush_ts_ = now_ts;
    return store_.Open(dir, now_ts);
}

void Recorder::Record(const Snapshot& snap, uint64_t ts_unix) {
    if (ts_unix <= last_ts_) { // 单调性保护（时钟跳变）
        if (has_last_) return;
    }

    RawSample s{};
    s.ts_unix = ts_unix;
    store_pct(s.cpu_pct, snap.cpu_pct);
    s.cpu_temp_c = snap.cpu_temp_c;
    store_pct(s.gpu_pct, snap.gpu_pct);
    s.gpu_temp_c = snap.gpu_temp_c;
    store_pct(s.ram_pct, snap.ram_pct);
    if (has_last_ && ts_unix != last_ts_ + 1) s.flags |= 1; // gap 标记

    pending_.push_back(s);
    recent_.push_back(s);
    if (recent_.size() > kRecentCap) recent_.pop_front();
    last_ts_ = ts_unix;
    has_last_ = true;

    if (ts_unix - last_flush_ts_ >= 10) {
        if (!store_.Append(pending_, ts_unix)) {
            disk_error_ = true; // 写失败（磁盘满/文件被锁）→ 停写，UI 提示；重试由下次 Append 承担
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

void Recorder::Shutdown() {
    if (has_last_) FlushNow(last_ts_);
    store_.Close();
}

} // namespace hwmon
