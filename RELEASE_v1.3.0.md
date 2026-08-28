# Release notes

## Sentry-Lite v1.3.0

内存与性能清理：去掉调试残留，收紧 LHM / 采集 / 历史查询路径；发布仅提供 zip 便携包。

### 下载

| 文件 | 说明 |
|------|------|
| **`Sentry-Lite-1.3.0-x64.zip`** | 解压即用（含 exe、LHM 模块、依赖 DLL、`web/`） |

### 使用

1. 下载 zip，解压到任意目录  
2. 双击 `Sentry-Lite.exe`（需管理员权限以读取 CPU 温度）  
3. 需已安装 [PawnIO](https://pawnio.eu) 与 [.NET 8 Runtime](https://dotnet.microsoft.com/download/dotnet/8.0)（x64）；Win11 通常已带 WebView2

### 变更

- **LHM**：仅启用 CPU 传感器（关闭 GPU/存储/网卡等无关硬件树）
- 删除调试日志、ExecuteScript 探测、本机 diag `fetch` 等屎山残留
- 仪表盘改用本地 `InterVariable.woff2`，不再外连 Google Fonts
- 网速改 `GetIfEntry2`；GPU PDH 复用缓冲；历史区间二分定位 + 分块读取
- 去掉无用 30 分钟内存环形缓冲；WebView2 用户数据目录迁到 `%LOCALAPPDATA%`
- **发布**：仅上传 zip，不再附带独立 exe 资产
