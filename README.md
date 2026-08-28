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

- `build/Release/Sentry-Lite.exe` — 主程序
- `build/Release/web/` — 内嵌仪表盘 HTML（构建时自动从 `src/app/web` 复制，随 exe 一起分发）

## 发布（GitHub Actions 自动）

推送版本标签后，GitHub Actions 自动编译并把 `Sentry-Lite.exe` + `web/` 打包成 zip Release：

```powershell
git tag v1.1.0
git push origin v1.1.0
```

- 构建使用 **Ninja + MSVC**（`ilammy/msvc-dev-cmd`）

## 温度监控

1. 安装 [PawnIO](https://pawnio.eu) 驱动（若需 CPU 温度）
2. 将官方模块 blob（`IntelMSR.bin` / `AMDFamily17.bin`）放到 `C:\Program Files\PawnIO\` 或 exe 同目录
3. 未安装 PawnIO / NVIDIA 显卡无 NVML 时，对应温度列显示「—」，其余指标正常
4. 无需管理员权限，无需安装服务

## 数据目录

- 历史记录：`%APPDATA%\Sentry-Lite\history\`
- 配置：`%APPDATA%\Sentry-Lite\config.json`
- 便携模式：exe 同目录放置 `portable.marker`

## 许可证

MIT — 见 [LICENSE](LICENSE)

## 仓库

https://github.com/woyaoxuexi1231/Sentry-Lite

详细架构见 [DESIGN_v2.md](DESIGN_v2.md)。