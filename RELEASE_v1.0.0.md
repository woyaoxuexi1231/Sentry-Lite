# Release notes

## Sentry-Lite v1.0.0

首个公开发布版本。

### 功能

- 屏幕顶部悬浮条：CPU / GPU / 内存 / 网速实时显示
- 1 秒采样，历史数据本地落盘（`raw-YYYYMMDD.hwdb`）
- 右键菜单：置顶、鼠标穿透、打开历史数据文件夹、退出
- 任务栏图标与 Alt+Tab 入口
- 拖拽限制在当前显示器工作区内

### 下载

由 GitHub Actions 自动构建并附在本 Release 的 **Assets** 中（`Sentry-Lite-1.0.0-x64.exe`，单文件绿色版，无需安装）。

### 数据目录

- 默认：`%APPDATA%\Sentry-Lite\`
- 便携模式：exe 同目录放置 `portable.marker`

### 说明

历史图形查看器尚未包含在本版本（数据已在后台记录，后续版本提供）。
