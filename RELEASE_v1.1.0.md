# Release notes

## Sentry-Lite v1.1.0

M2 温度服务 + M3 历史查看器。

### 新增

- **历史查看器**：时间范围（15 分钟～30 天）、CPU/GPU/RAM/网络四泳道曲线、十字光标逐秒读数、区间统计、CSV 导出
- **温度服务** `Sentry-Lite-svc.exe`：PawnIO 读 Intel/AMD CPU 温度，NVML 读 NVIDIA GPU 温度，经共享内存供 UI 显示并写入历史
- 右键菜单：**历史记录…**、**启用温度监控…** / **卸载温度服务**
- CI / Release 改用 Ninja + MSVC，修复 GitHub Actions 上 Visual Studio 生成器不可用的问题

### 下载

- `Sentry-Lite-1.1.0-x64.exe` — 主程序
- `Sentry-Lite-svc-1.1.0-x64.exe` — 温度服务（可选，与主程序同目录）

### 说明

- CPU 温度需安装 PawnIO 及对应 `.bin` 模块（见 README）
- 保留策略图形化编辑将在后续版本完善；当前可在 `config.json` 的 `history` 段修改
