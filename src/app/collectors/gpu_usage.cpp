#include "gpu_usage.h"
#include <pdhmsg.h>
#include <string>
#include <vector>
#include <map>

namespace hwmon {

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
    // 用英文名：本地化系统（zh-CN 等）不接受 "\\GPU Engine(*)\\..." 原文路径
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
        // 首次立即尝试；之后每 60 拍重试（避免 PDH 卡死启动路径）
        unsigned t = retry_tick_++;
        if (t != 0 && (t % 60) != 0) return -1.f;
        if (!Init()) return -1.f;
    }
    if (PdhCollectQueryData(query_) != ERROR_SUCCESS) {
        Teardown();
        return -1.f;
    }
    if (!primed_) { // 第一次仅为建立基线
        primed_ = true;
        return -1.f;
    }

    DWORD size = 0, count = 0;
    // 首次传空缓冲取所需大小（返回 PDH_MORE_DATA）
    PdhGetFormattedCounterArrayW(counter_, PDH_FMT_DOUBLE, &size, &count, nullptr);
    if (size == 0) return -1.f;

    std::vector<BYTE> buf(size);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
    if (PdhGetFormattedCounterArrayW(counter_, PDH_FMT_DOUBLE, &size, &count,
                                     items) != ERROR_SUCCESS)
        return -1.f;

    // 按 engtype 汇总所有进程实例，取各引擎类型中的最大值
    std::map<std::wstring, double> by_engine;
    for (DWORD i = 0; i < count; ++i) {
        const wchar_t* inst = items[i].szName;
        const wchar_t* tag = wcsstr(inst, L"engtype_");
        if (!tag) continue;
        double v = items[i].FmtValue.doubleValue;
        if (v < 0) v = 0;
        by_engine[tag + 8] += v;
    }
    double max_sum = 0;
    for (auto& kv : by_engine)
        if (kv.second > max_sum) max_sum = kv.second;

    float pct = static_cast<float>(max_sum);
    if (pct > 100.f) pct = 100.f;
    return pct;
}

} // namespace hwmon
