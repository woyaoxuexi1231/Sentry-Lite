# Release notes

## Sentry-Lite v1.3.1

前端对齐最新 mockup；修复 Inter 字体未打进发布包的问题。

### 下载

| 文件 | 说明 |
|------|------|
| **`Sentry-Lite-1.3.1-x64.zip`** | 解压即用（含 exe、LHM、依赖、`web/` + Inter 字体） |

### 变更

- Uptime 范围：**15m / 1h → 合并为 30m**（默认 **30m / 24h / 7d**）
- 颜色：与最新 `design/ui-mockup.html` 对齐
- **字体**：把 `web/fonts/InterVariable.woff2` 纳入仓库与 zip（v1.3.0 改本地字体时漏提交了该文件）；并保留 Google Fonts 作为回退
- **功能未裁剪**：实时卡片、Uptime、历史、打开文件夹 / 清空数据、CPU/GPU 温度均保留。约 15MB 内存是清理调试残留与收紧 LHM 后的正常水平，不是功能缺失
