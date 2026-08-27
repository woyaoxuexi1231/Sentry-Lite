#pragma once
#include <string>

namespace hwmon {

// 从 UI 进程触发温度服务安装/卸载（需 UAC）
bool InstallTempService(std::wstring& err);
bool UninstallTempService(std::wstring& err);
bool IsTempServiceInstalled();
std::wstring SvcExePath();

} // namespace hwmon
