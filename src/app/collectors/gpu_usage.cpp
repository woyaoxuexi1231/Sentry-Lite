#include "gpu_usage.h"
#include <pdhmsg.h>
#include <cstring>

namespace hwmon {

namespace {

// Fixed slots for common engtype labels — avoids std::map + wstring every tick.
struct EngAccum {
    wchar_t key[32];
    double sum;
};

int FindEng(EngAccum* slots, int n, const wchar_t* key) {
    for (int i = 0; i < n; ++i) {
        if (_wcsicmp(slots[i].key, key) == 0) return i;
    }
    return -1;
}

} // namespace

GpuUsageCollector::~GpuUsageCollector() { Teardown(); }

void GpuUsageCollector::Teardown() {
    if (query_) PdhCloseQuery(query_);
    query_ = nullptr;
    counter_ = nullptr;
    available_ = false;
    primed_ = false;
}

bool GpuUsageCollector::Init() {
    Teardown();
    if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS) return false;
    PDH_STATUS st = PdhAddEnglishCounterW(
        query_, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter_);
    if (st != ERROR_SUCCESS) {
        PdhCloseQuery(query_);
        query_ = nullptr;
        return false;
    }
    primed_ = false;
    available_ = true;
    return true;
}

float GpuUsageCollector::Tick() {
    if (!available_) {
        unsigned t = retry_tick_++;
        if (t != 0 && (t % 60) != 0) return -1.f;
        if (!Init()) return -1.f;
    }
    if (PdhCollectQueryData(query_) != ERROR_SUCCESS) {
        Teardown();
        return -1.f;
    }
    if (!primed_) {
        primed_ = true;
        return -1.f;
    }

    DWORD size = 0, count = 0;
    PdhGetFormattedCounterArrayW(counter_, PDH_FMT_DOUBLE, &size, &count, nullptr);
    if (size == 0) return -1.f;

    if (buf_.size() < size) buf_.resize(size);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf_.data());
    if (PdhGetFormattedCounterArrayW(counter_, PDH_FMT_DOUBLE, &size, &count,
                                     items) != ERROR_SUCCESS)
        return -1.f;

    EngAccum slots[16]{};
    int nSlots = 0;

    for (DWORD i = 0; i < count; ++i) {
        const wchar_t* inst = items[i].szName;
        const wchar_t* tag = wcsstr(inst, L"engtype_");
        if (!tag) continue;
        const wchar_t* key = tag + 8;
        double v = items[i].FmtValue.doubleValue;
        if (v < 0) v = 0;

        int idx = FindEng(slots, nSlots, key);
        if (idx < 0) {
            if (nSlots >= 16) continue;
            idx = nSlots++;
            wcsncpy_s(slots[idx].key, key, _TRUNCATE);
            slots[idx].sum = 0;
        }
        slots[idx].sum += v;
    }

    double max_sum = 0;
    for (int i = 0; i < nSlots; ++i)
        if (slots[i].sum > max_sum) max_sum = slots[i].sum;

    float pct = static_cast<float>(max_sum);
    if (pct > 100.f) pct = 100.f;
    return pct;
}

} // namespace hwmon
