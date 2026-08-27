# Sentry-Lite

轻量 Windows 硬件监控悬浮条：CPU / GPU / 内存 / 网速，1 秒采样并本地落盘。

**当前版本：1.0.0**

## 功能

- 屏幕顶部悬浮条，实时显示 CPU、GPU、内存、网速
- 右键菜单：置顶、鼠标穿透、打开历史数据文件夹、退出
- 历史数据每秒写入 `raw-YYYYMMDD.hwdb`（自定义二进制格式）
- 单文件绿色版，静态链接 CRT，无额外运行时依赖

## 构建

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

产物：`build/Release/Sentry-Lite.exe`

## 发布（GitHub Actions 自动）

**不需要本地手动上传 exe。** 推送版本标签后，GitHub Actions 会自动编译并创建 Release、附上可下载的安装包。

```powershell
# 1. 更新 VERSION / app.manifest 版本号并提交
# 2. 打标签并推送
git tag v1.0.0
git push origin v1.0.0
```

- 工作流：`.github/workflows/release.yml`（触发条件：`v*.*.*` 标签）
- 产物命名：`Sentry-Lite-<版本>-x64.exe`（例如 `Sentry-Lite-1.0.0-x64.exe`）
- Release 说明：优先读取仓库里的 `RELEASE_v<版本>.md`

若标签已推送但当时还没有工作流，可在 GitHub **Actions → Release → Run workflow**，输入已有标签（如 `v1.0.0`）重新触发。

## 数据目录

- 默认：`%APPDATA%\Sentry-Lite\`
  - `config.json` — 配置
  - `history\` — 历史原始数据
- 便携模式：在 exe 同目录放置 `portable.marker`，数据写在 exe 旁

## 历史记录

当前版本**已在后台持续记录**，但**图形化历史查看器尚未实现**（计划中的 M3 里程碑）。

查看原始数据：悬浮条 **右键 → 打开历史数据文件夹**，或直接打开上述 `history` 目录。文件为程序私有格式，后续版本将提供查看器与 CSV 导出。

## 许可证

MIT — 见 [LICENSE](LICENSE)

## 仓库

https://github.com/woyaoxuexi1231/Sentry-Lite
