# Sentry-Lite

轻量 Windows 硬件监控仪表盘（单 EXE + WebView2 内嵌 UI）：CPU / GPU / 内存 / 网速 / 温度，1 秒采样并本地落盘。

**当前版本：1.1.0**

## 功能

- **单个 `Sentry-Lite.exe`**：WebView2 内嵌 HTML 仪表盘（界面见 `design/ui-mockup.html`）
- 实时卡片：CPU/GPU 占用 + 温度、内存、上下行网速，语义色分级
- **Uptime 时段健康条**：所选区间逐段健康分 + 悬停明细
- **历史查看（并入仪表盘）**：15m/1h/6h/24h/7d 区间切换、区间统计
- **温度（可选）**：CPU 与 [LiteMonitor](https://github.com/Diorser/LiteMonitor) 相同，经 `LibreHardwareMonitorLib 0.9.6` 读取；GPU 仍用 `nvml.dll`（NVIDIA）
- **系统托盘**：显示/隐藏、打开历史文件夹、**完全退出走托盘右键菜单**
- 历史数据每秒写入 `raw-YYYYMMDD.hwdb`（自定义二进制格式），保留策略自动管理
- 无 CSV 导出、无网络行为、静态链接 CRT、无额外运行时依赖（需系统已装 WebView2 Runtime，Win11 自带）

## 构建

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

产物：

- `build/Release/Sentry-Lite.exe` — 主程序（Visual Studio）或 `build/Sentry-Lite.exe`（Ninja）
- `build/.../Sentry-Lite-lhm.dll` — CPU 温度模块（与 LiteMonitor 同款 LHM 0.9.6，由主进程内嵌加载，构建时 `dotnet publish` 自动输出）
- `build/.../web/` — 内嵌仪表盘 HTML（构建时自动复制，随 exe 一起分发）

需要 **.NET 8 Desktop Runtime**（与 LiteMonitor 相同；Win11 通常已带）。

## 发布（GitHub Actions 自动）

推送版本标签后，GitHub Actions 自动编译并把 `Sentry-Lite.exe` + `Sentry-Lite-lhm*` + `web/` 打包成 zip Release：

```powershell
git tag v1.1.0
git push origin v1.1.0
```

- 构建使用 **Ninja + MSVC**（`ilammy/msvc-dev-cmd`）

## 温度监控

1. **CPU 温度**：与 LiteMonitor 相同，依赖 [PawnIO](https://pawnio.eu) 驱动 + 内置 `LibreHardwareMonitorLib 0.9.6`（`Sentry-Lite-lhm.dll` 进程内加载）
2. **GPU 温度**：NVIDIA 显卡需系统有 `nvml.dll`；AMD/Intel 集显暂无温度
3. 驱动或 LHM 读不到时，对应温度列显示「—」，其余指标正常
4. LHM 与 LiteMonitor 相同：进程内加载、`requireAdministrator` manifest（PawnIO 设备 ACL）、WinForms STA、同款初始化

PawnIO 由 LHM 内部使用；模块 blob 随 LibreHardwareMonitorLib 嵌入，**无需**再手动放置 `pawnio/*.bin`。

## 数据目录

- 历史记录：`%APPDATA%\Sentry-Lite\history\`
- 配置：`%APPDATA%\Sentry-Lite\config.json`
- 便携模式：exe 同目录放置 `portable.marker`

## 许可证

MIT — 见 [LICENSE](LICENSE)

第三方：`third_party/pawnio-modules/` 中的 PawnIO 模块 blob 遵循 LGPL-2.1（见该目录下 `COPYING`）。

## 仓库

https://github.com/woyaoxuexi1231/Sentry-Lite

详细架构见 [DESIGN_v2.md](DESIGN_v2.md)。