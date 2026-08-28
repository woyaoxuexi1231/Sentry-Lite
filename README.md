# Sentry-Lite

轻量 Windows 硬件监控仪表盘（WebView2 内嵌 UI）：CPU / GPU / 内存 / 网速 / 温度，1 秒采样并本地落盘。

**当前版本：1.4.0**

## 功能

- **便携包**：解压 zip 即用（`Sentry-Lite.exe` + LHM 模块 + `web/`）
- WebView2 内嵌 HTML 仪表盘（界面见 `design/ui-mockup-v2.html`）
- 实时卡片：CPU/GPU 占用 + 温度、内存、上下行网速，语义色分级
- **分指标健康条**：CPU / GPU / RAM 各自时段健康色条 + 健康占比；NET 时段流量柱 + ↓/↑ 总量
- **历史查看（并入仪表盘）**：30m 实时窗 / Day 选日看 24h、区间统计
- **温度（可选）**：CPU 与 [LiteMonitor](https://github.com/Diorser/LiteMonitor) 相同，经 `LibreHardwareMonitorLib 0.9.6` 读取；GPU 仍用 `nvml.dll`（NVIDIA）
- **系统托盘**：显示/隐藏、打开历史文件夹、**完全退出走托盘右键菜单**
- 历史数据每秒写入 `raw-YYYYMMDD.hwdb`（自定义二进制格式），保留策略自动管理
- 无 CSV 导出、无网络行为、静态链接 CRT；需 WebView2 Runtime（Win11 自带）与 .NET 8 Runtime

## 构建

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

产物：

- `build/Release/Sentry-Lite.exe` — 主程序（Visual Studio）或 `build/Sentry-Lite.exe`（Ninja）
- `build/.../Sentry-Lite-lhm.dll` — CPU 温度模块（LHM 0.9.6，hostfxr 进程内加载）
- `build/.../web/` — 内嵌仪表盘 HTML

需要 **.NET 8 Runtime**（x64）。

## 发布（GitHub Actions 自动）

推送版本标签后，Actions 自动编译并发布 **zip（完整便携包）**：

```powershell
git tag v1.4.0
git push origin v1.4.0
```

Release 资产：

- `Sentry-Lite-<ver>-x64.zip` — 解压即用

## 温度监控

1. **CPU 温度**：与 LiteMonitor 相同，依赖 [PawnIO](https://pawnio.eu) + `LibreHardwareMonitorLib 0.9.6`
2. **GPU 温度**：NVIDIA 需 `nvml.dll`；AMD/Intel 集显暂无
3. 读不到时对应温度列显示「—」
4. Manifest 与 LiteMonitor 相同：`requireAdministrator`（PawnIO 设备 ACL）

模块 blob 随 LHM 嵌入，无需手动放置 `pawnio/*.bin`。

## 数据目录

- 历史记录：`%APPDATA%\Sentry-Lite\history\`
- 配置：`%APPDATA%\Sentry-Lite\config.json`
- WebView2 缓存：`%LOCALAPPDATA%\Sentry-Lite\WebView2\`
- 便携模式：exe 同目录放置 `portable.marker`

## 许可证

MIT — 见 [LICENSE](LICENSE)

## 仓库

https://github.com/woyaoxuexi1231/Sentry-Lite

详细架构见 [DESIGN_v2.md](DESIGN_v2.md)。
