#include "ui/float_bar.h"
#include <objbase.h>
#include <ctime>

namespace {

// 单实例：只允许一个写入者（DESIGN_v2.md §6.6）
bool AcquireSingleInstance() {
    HANDLE m = CreateMutexW(nullptr, TRUE, L"Local\\Sentry-Lite_single_instance");
    if (!m) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(m);
        return false;
    }
    return true; // 句柄刻意不关闭，随进程生命周期持有
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int show_cmd) {
    if (!AcquireSingleInstance()) return 0;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool com_ok = SUCCEEDED(hr);

    int exit_code = 0;
    {
        hwmon::AppContext ctx;
        ctx.config.Load();

        if (ctx.config.history.enabled) {
            std::wstring dir = hwmon::Config::HistoryDir(ctx.config.history);
            CreateDirectoryW(dir.c_str(), nullptr);
            time_t now = time(nullptr);
            // 启动即清一次过期 raw，再开始记录
            ctx.retention.RunOnce(dir, ctx.config.history.raw_retention_days,
                                  ctx.config.history.max_total_mb,
                                  static_cast<uint64_t>(now));
            ctx.recorder.Init(dir, static_cast<uint64_t>(now));
        }

        unsigned long nic = 0;
        if (ctx.config.nic != L"auto") nic = static_cast<unsigned long>(_wtoi(ctx.config.nic.c_str()));
        ctx.net.SetNicIndex(nic);

        // GPU PDH 初始化可能阻塞，放到悬浮条显示之后的 Tick 里做
        if (!hwmon::RunFloatBar(ctx, show_cmd)) exit_code = 1;
    }

    if (com_ok) CoUninitialize();
    return exit_code;
}
