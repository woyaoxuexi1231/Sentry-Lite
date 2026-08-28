# Release notes

## Sentry-Lite v1.2.0

CPU 温度改为与 [LiteMonitor](https://github.com/Diorser/LiteMonitor) 相同的 **LibreHardwareMonitorLib 0.9.6** 进程内读取；修复 255°C 显示与启动崩溃。

### 下载

| 文件 | 说明 |
|------|------|
| **`Sentry-Lite-1.2.0-x64.zip`** | **推荐**：解压即用（含 exe、LHM 模块、依赖 DLL、`web/`） |
| `Sentry-Lite-1.2.0-x64.exe` | 仅主程序；完整功能请用 zip |

### 使用

1. 下载 **zip**，解压到任意目录  
2. 双击 `Sentry-Lite.exe`（需管理员权限以读取 CPU 温度，与 LiteMonitor 相同）  
3. 需已安装 [PawnIO](https://pawnio.eu) 与 [.NET 8 Runtime](https://dotnet.microsoft.com/download/dotnet/8.0)（x64）；Win11 通常已带 WebView2

### 变更

- **CPU 温度**：自研 PawnIO 客户端 → 进程内 LHM 0.9.6（hostfxr 加载 `Sentry-Lite-lhm.dll`）
- 修复无效温度显示为 **255°C**（改为「—」）
- 修复 hostfxr 错误委托导致的启动崩溃
- 先显示窗口再初始化温度，避免长时间无响应
- Manifest 与 LiteMonitor 对齐：`requireAdministrator`
