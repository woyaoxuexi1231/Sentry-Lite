#pragma once

namespace hwmon {

constexpr wchar_t kServiceName[] = L"SentryLiteTemp";
constexpr wchar_t kServiceDisplay[] = L"Sentry-Lite Temperature Service";

bool InstallService();
bool UninstallService();
int RunServiceMain();

} // namespace hwmon
