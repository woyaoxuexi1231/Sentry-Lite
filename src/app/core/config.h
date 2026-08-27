// 配置：%APPDATA%\Sentry-Lite\config.json；exe 旁有 portable.marker 时便携模式。
// 迷你 JSON：仅覆盖本程序配置形态（对象/字符串/数字/布尔/字符串数组），无转义兜底则忽略该项。
#pragma once
#include <string>
#include <vector>

namespace hwmon {

struct HistoryConfig {
    bool enabled = true;
    std::wstring dir;            // 空 = %APPDATA%\Sentry-Lite\history
    int raw_retention_days = 14; // 0 = 不留 raw
    uint64_t max_total_mb = 300;
};

struct Config {
    int interval_ms = 1000;
    std::wstring nic = L"auto";  // auto 或接口索引字符串
    bool topmost = true;
    bool click_through = false;
    long position_x = -1;        // -1 = 首次居中偏上
    long position_y = -1;
    float temp_warn_c = 85.f;
    float temp_crit_c = 95.f;
    HistoryConfig history;

    // 加载（不存在用默认值），返回读取是否成功
    bool Load();
    // 原子保存（临时文件 + ReplaceFile）
    bool Save() const;

    // 数据根目录（config.json 与 history 所在处）
    static std::wstring DataDir();
    static std::wstring HistoryDir(const HistoryConfig& history);

private:
    void Assign(const std::wstring& key, const std::string& val_json);
};

} // namespace hwmon
