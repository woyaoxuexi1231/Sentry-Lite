# Sentry-Lite

轻量 Windows 硬件监控仪表盘（单 EXE + WebView2 内嵌 UI）：CPU / GPU / 内存 / 网速 / 温度，1 秒采样并本地落盘。

**当前版本：1.1.0**

## 功能

- **单个 `Sentry-Lite.exe`**：WebView2 内嵌 HTML 仪表盘（界面见 `design/ui-mockup.html`）
- 实时卡片：CPU/GPU 占用 + 温度、内存、上下行网速，语义色分级
- **Uptime 时段健康条**：所选区间逐段健康分 + 悬停明细
- **历史查看（并入仪表盘）**：15m/1h/6h/24h/7d 区间切换、区间统计
- **温度（可选）**：`PawnIO`（Intel/AMD CPU）+ 动态加载 `nvml.dll`（NVIDIA GPU）；**未安装 PawnIO 则温度显示「—」，其余照常**
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
- `build/.../web/` — 内嵌仪表盘 HTML（构建时自动复制，随 exe 一起分发）
- `build/.../pawnio/` — CPU 温度模块（`IntelMSR.bin` + `AMDFamily17.bin`，构建时自动复制）

## 发布（GitHub Actions 自动）

推送版本标签后，GitHub Actions 自动编译并把 `Sentry-Lite.exe` + `web/` + `pawnio/` 打包成 zip Release：

```powershell
git tag v1.1.0
git push origin v1.1.0
```

- 构建使用 **Ninja + MSVC**（`ilammy/msvc-dev-cmd`）

## 温度监控

1. 安装 [PawnIO](https://pawnio.eu) 驱动（若需 CPU 温度）
2. **发布包已自带** `pawnio/IntelMSR.bin`（Intel）和 `pawnio/AMDFamily17.bin`（AMD），程序按 CPU 厂商自动选用，**用户无需再手动下载模块**
3. 未安装 PawnIO / NVIDIA 显卡无 NVML 时，对应温度列显示「—」，其余指标正常
4. CPU 温度依赖 PawnIO 驱动；默认仅管理员可打开设备句柄（与 LiteMonitor / LHM 相同）。普通用户若读不到温度，可右键「以管理员身份运行」，或接受 CPU 温度显示「—」

PawnIO 模块来自 [PawnIO.Modules](https://github.com/namazso/PawnIO.Modules)（LGPL-2.1），见 `third_party/pawnio-modules/COPYING`。

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