#include "config.h"
#include <windows.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>

#pragma comment(lib, "shlwapi.lib")

namespace hwmon {

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring Config::DataDir() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring marker = exe;
    size_t slash = marker.find_last_of(L'\\');
    if (slash != std::wstring::npos) marker = marker.substr(0, slash + 1);
    marker += L"portable.marker";
    if (PathFileExistsW(marker.c_str())) {
        std::wstring dir = exe;
        slash = dir.find_last_of(L'\\');
        return slash == std::wstring::npos ? L"." : dir.substr(0, slash);
    }
    PWSTR appdata = nullptr;
    std::wstring base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata))) {
        base = appdata;
        CoTaskMemFree(appdata);
    }
    base += L"\\Sentry-Lite";
    CreateDirectoryW(base.c_str(), nullptr);
    return base;
}

std::wstring Config::HistoryDir(const HistoryConfig& history) {
    if (!history.dir.empty()) return history.dir;
    return DataDir() + L"\\history";
}

// ---- 迷你 JSON 解析 ----
namespace {

struct Scanner {
    const std::string& s;
    size_t i = 0;
    explicit Scanner(const std::string& str) : s(str) {}
    void SkipWs() { while (i < s.size() && (s[i]==' '||s[i]=='\t'||s[i]=='\r'||s[i]=='\n')) ++i; }
    bool Eat(char c) { SkipWs(); if (i < s.size() && s[i]==c) { ++i; return true; } return false; }
    char Peek() { SkipWs(); return i < s.size() ? s[i] : '\0'; }
    // 读一个裸值（字符串/数字/布尔），返回原始文本（去引号）
    std::string Value() {
        SkipWs();
        if (i >= s.size()) return {};
        if (s[i] == '"') {
            ++i; std::string out;
            while (i < s.size() && s[i] != '"') {
                if (s[i] == '\\' && i + 1 < s.size()) { ++i; out += s[i] == 'n' ? '\n' : s[i]; }
                else out += s[i];
                ++i;
            }
            if (i < s.size()) ++i; // closing quote
            return "\"" + out + "\""; // 标记为带引号
        }
        size_t start = i;
        while (i < s.size() && s[i]!=',' && s[i]!='}' && s[i]!=']') ++i;
        size_t end = i;
        while (end > start && (s[end-1]==' '||s[end-1]=='\t')) --end;
        return s.substr(start, end - start);
    }
};

} // namespace

void Config::Assign(const std::wstring& key, const std::string& raw) {
    auto unquote = [&]() -> std::wstring {
        if (raw.size() >= 2 && raw.front()=='"' && raw.back()=='"')
            return Utf8ToWide(raw.substr(1, raw.size()-2));
        return {};
    };
    if (key == L"interval_ms")   interval_ms = atoi(raw.c_str());
    else if (key == L"nic")      nic = unquote();
    else if (key == L"topmost")       topmost = raw == "true";
    else if (key == L"click_through") click_through = raw == "true";
    else if (key == L"position_x") position_x = atol(raw.c_str());
    else if (key == L"position_y") position_y = atol(raw.c_str());
    else if (key == L"temp_warn_c") temp_warn_c = (float)atof(raw.c_str());
    else if (key == L"temp_crit_c") temp_crit_c = (float)atof(raw.c_str());
    else if (key == L"history.enabled") history.enabled = raw == "true";
    else if (key == L"history.dir")     history.dir = unquote();
    else if (key == L"history.raw_retention_days") history.raw_retention_days = atoi(raw.c_str());
    else if (key == L"history.max_total_mb")       history.max_total_mb = _strtoui64(raw.c_str(), nullptr, 10);
}

bool Config::Load() {
    std::wstring path = DataDir() + L"\\config.json";
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    const std::string text = ss.str();

    Scanner sc(text);
    auto parseObject = [&](const std::wstring& prefix, auto&& self) -> void {
        if (!sc.Eat('{')) return;
        while (true) {
            if (sc.Peek() == '}') { sc.Eat('}'); break; }
            std::string keyRaw = sc.Value();
            if (keyRaw.empty()) break;
            std::wstring key = keyRaw.front()=='"'
                ? Utf8ToWide(keyRaw.substr(1, keyRaw.size()-2))
                : Utf8ToWide(keyRaw);
            if (!sc.Eat(':')) break;
            if (sc.Peek() == '{') {
                self(prefix.empty() ? key : prefix + L"." + key, self);
            } else {
                Assign(prefix.empty() ? key : prefix + L"." + key, sc.Value());
            }
            if (!sc.Eat(',')) { sc.Eat('}'); break; }
        }
    };
    parseObject(L"", parseObject);

    if (interval_ms < 500) interval_ms = 1000;
    return true;
}

bool Config::Save() const {
    std::wstring dir = DataDir();
    std::string json;
    json.reserve(512);
    json += "{\n";
    json += "  \"interval_ms\": " + std::to_string(interval_ms) + ",\n";
    json += "  \"nic\": \"" + WideToUtf8(nic) + "\",\n";
    json += "  \"topmost\": ";       json += topmost ? "true" : "false"; json += ",\n";
    json += "  \"click_through\": "; json += click_through ? "true" : "false"; json += ",\n";
    json += "  \"position_x\": " + std::to_string(position_x) + ",\n";
    json += "  \"position_y\": " + std::to_string(position_y) + ",\n";
    char buf[64];
    snprintf(buf, sizeof(buf), "%.1f", temp_warn_c);
    json += "  \"temp_warn_c\": " + std::string(buf) + ",\n";
    snprintf(buf, sizeof(buf), "%.1f", temp_crit_c);
    json += "  \"temp_crit_c\": " + std::string(buf) + ",\n";
    json += "  \"history\": {\n";
    json += "    \"enabled\": ";         json += history.enabled ? "true" : "false"; json += ",\n";
    json += "    \"dir\": \"" + WideToUtf8(history.dir) + "\",\n";
    json += "    \"raw_retention_days\": " + std::to_string(history.raw_retention_days) + ",\n";
    json += "    \"max_total_mb\": " + std::to_string(history.max_total_mb) + "\n";
    json += "  }\n}\n";

    std::wstring tmpPath = dir + L"\\config.json.tmp";
    std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(json.data(), (std::streamsize)json.size());
    f.close();
    std::wstring finalPath = dir + L"\\config.json";
    DeleteFileW(finalPath.c_str());
    return MoveFileW(tmpPath.c_str(), finalPath.c_str()) != 0;
}

} // namespace hwmon
