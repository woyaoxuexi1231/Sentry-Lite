# Sentry-Lite

轻量 Windows 硬件监控悬浮条：CPU / GPU / 内存 / 网速（含温度），1 秒采样并本地落盘。

**当前版本：1.1.0**

## 功能

- 屏幕顶部悬浮条，实时显示 CPU、GPU、内存、网速；安装温度服务后显示 CPU/GPU 温度
- **历史查看器**（M3）：时间范围切换、四泳道曲线、十字光标读数、统计、CSV 导出
- **温度服务**（M2）：`Sentry-Lite-svc.exe` + PawnIO（Intel/AMD CPU）+ NVML（NVIDIA GPU）
- 历史数据每秒写入 `raw-YYYYMMDD.hwdb`（自定义二进制格式）
- 静态链接 CRT，主程序无额外运行时依赖

## 构建

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

产物：

- `build/Release/Sentry-Lite.exe` — 主程序
- `build/Release/Sentry-Lite-svc.exe` — 温度服务（与主程序同目录）

## 发布（GitHub Actions 自动）

推送版本标签后，GitHub Actions 自动编译并创建 Release：

```powershell
git tag v1.1.0
git push origin v1.1.0
```

- CI / Release 使用 **Ninja + MSVC**（`ilammy/msvc-dev-cmd`），避免 VS 生成器在 runner 上不可用
- 产物：`Sentry-Lite-<版本>-x64.exe`、`Sentry-Lite-svc-<版本>-x64.exe`

## 温度监控

1. 安装 [PawnIO](https://pawnio.eu) 驱动
2. 将官方模块 blob（`IntelMSR.bin` / `AMDFamily17.bin`）放到 `C:\Program Files\PawnIO\` 或 exe 同目录
3. 悬浮条 **右键 → 启用温度监控…**（UAC 一次，安装 Windows 服务）
4. 服务未运行或 PawnIO 不可用时，温度列显示「—」，其余指标正常

## 历史记录

- **右键 → 历史记录…** 打开图形化查看器
- **右键 → 打开历史数据文件夹** 查看原始 `.hwdb` 文件
- 默认路径：`%APPDATA%\Sentry-Lite\history\`

## 数据目录

- 默认：`%APPDATA%\Sentry-Lite\`
- 便携模式：exe 同目录放置 `portable.marker`

## 许可证

MIT — 见 [LICENSE](LICENSE)

## 仓库

https://github.com/woyaoxuexi1231/Sentry-Lite
