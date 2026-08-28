# Sentry-Lite · 设计文档 v0.3

一句话定位：**单 EXE 嵌入式硬件监控仪表盘**——CPU / GPU / 内存 / 网速 / 温度（可选），1 秒采样，本地落盘，可回看任意历史时刻；UI 为内嵌 HTML 仪表盘，进程接管系统托盘。

## 相对 v0.2 的架构变更（推翻旧设计）

| 项 | v0.2（旧） | v0.3（当前，已实现） |
|---|---|---|
| 可执行文件 | `Sentry-Lite.exe`（UI）+ `Sentry-Lite-svc.exe`（温度服务）双进程 + 共享内存 | **单个 `Sentry-Lite.exe`**，温度采集进程内完成 |
| UI 形态 | 悬浮条（D2D/DWrite） | **WebView2 嵌入 HTML 仪表盘**，UI 按 `design/ui-mockup.html` 设计 |
| 温度权限 | 独立 SYSTEM 服务，用 UAC 安装 | 普通权限进程内直接读；**未装 PawnIO 则不检测温度**（降级为「—」），无需提权 |
| 窗口控制 | 悬浮条右键菜单 | **系统托盘图标**，右键菜单：显示/隐藏、打开历史文件夹、退出（完全退出走托盘） |
| CSV 导出 | 查看器内建 | **移除**，不做 |
| 历史查看器 | 独立 D2D 窗口 | 并入仪表盘 HTML（实时 + 区间热力条 + 统计） |

动机：避免整进程提权去读温度，又不引入第二个进程；用 WebView2 + HTML 降低 UI 开发与维护成本；用托盘承载全局交互。

---

## 1. 指标数据源

| 指标 | 实现 | API | 权限 | 失败降级 |
|---|---|---|---|---|
| CPU 占用 | 总负载差分 `1 − Δidle/(Δidle+Δkernel+Δuser)` | `GetSystemTimes` 每 1s 差分 | 无 | `NtQuerySystemInformation` |
| 内存占用 | `dwMemoryLoad` + 已用/总量 | `GlobalMemoryStatusEx` | 无 | — |
| 网速 | 默认路由网卡收发字节差分 | `GetIfTable2` + `GetBestInterface` | 无 | 手动选网卡；回退 `GetIfTable` |
| GPU 占用 | 按物理 GPU 聚合并引擎求和取最大值 | PDH `\GPU Engine(*)\Utilization Percentage` | 无 | 计数器不存在显示「—」 |
| CPU 温度 | Intel：MSR `0x1A2`/`0x1B1`；AMD Zen：SMN `0x59800`（Tctl→Tdie 偏移表移植自 LHM） | **PawnIO** 模块 `IntelMSR`/`AMDFamily17` | 无（驱动已装即普通进程可读） | **无 PawnIO → 不检测，显示「—」** |
| GPU 温度 | `nvmlDeviceGetTemperature`（`nvml.dll` 动态加载） | NVML | 无 | 传感器不可得 → 「—」 |

温度两路都封装为「读不到即 NaN/哨兵」：不装 PawnIO 或驱动不支持时，CPU/GPU 温度列显示「—」，其余指标与历史照常。

参考移植来源：温度解析逻辑参考 LibreHardwareMonitor（MPL-2.0，在 `src/app/temp/` 已有文件级注明）。

---

## 2. 总体架构

单进程、WebView2 承载 UI、托盘承载全局控制：

```
┌──────────────────────────────────────────────────────────┐
│ Sentry-Lite.exe（普通权限，常驻，HKCU Run 自启，单实例互斥体）      │
│                                                          │
│  ┌─────────── 系统托盘 ──────────┐                        │
│  │ 右键：显示/隐藏 │ 打开历史文件夹 │ 退出         │             │
│  └──────────────▲────────────────┘                        │
│                 │ 托盘消息（WM_APP+1）                        │
│  ┌──────────────┴───────────────────────────────────────┐ │
│  │ Win32 主窗口 + WebView2 控件（file:// 加载 dashboard）  │ │
│  │  Native ⇄ JS 桥：1s 推快照 / 按范围查历史 / 前端指令    │ │
│  └───────────────────────┬──────────────────────────────┘ │
│                          │                                │
│  1s 定时器 OnTick()                                       │
│   ├─ CollectSnapshot()：CPU/GPU/RAM/网速 + PawnIO/NVML 温度 │
│   ├─ Recorder 落盘（内存环形缓冲 → SPSC 队列 → 落盘线程）      │
│   └─ PushJson() 推送实时快照给前端                          │
└──────────────────────────────────────────────────────────┘
      │
      ▼
 %APPDATA%\Sentry-Lite\history\
   raw-YYYYMMDD.hwdb   （1s 原始，保留 N 天）
   min-YYYYMM.hwdb     （1min 聚合，长期保留）
```

- 采样循环在主线程 `SetTimer(1000ms)` 内完成；历史查询在后台线程流式读取，完成后 `WM_APP+2` 回主线程推给前端。
- 完全退出只能通过托盘右键菜单；关闭主窗口仅隐藏（交回托盘），不退出进程。

---

## 3. UI：WebView2 内嵌仪表盘

实现文件：`src/app/web/dashboard.html`，视觉与交互遵循 `design/ui-mockup.html`。

布局：
- **头部**：品牌（状态点 + 标题）、时间范围切换（15m/1h/6h/24h/7d）。
- **实时卡片**（live）：CPU（占用 + 温度）、GPU（占用 + 温度）、RAM（占用 + 已用/总量）、NET（下/上），严重级别用绿/琥珀/红语义色。
- **Uptime 时段健康条**：把所选区间切成多个桶，按公式 `100 − CPU>55% 扣分 − GPU>45% 扣分 − RAM>90% 扣分 − 温度扣分` 计算时段分，>85 健康 / 65–85 警示 / <65 严重；悬停某桶弹出该时段明细 tooltip。
- **统计条**：CPU/GPU/RAM 在所选区间的 max/avg（含温度）与颜色规则说明。
- **底部**：样本数 + 本地时钟。

前端无任何外部网络资源（字体走系统回退），完全离线可用。

### Native ⇄ JS 桥协议（字符串，`|` 分隔）

- Native → JS：`{ "type": "live", ... }`（实时快照）；`{ "type": "histo", ... }`（区间桶数据）；`{ "type": "histoDone", ... }`。
- JS → Native：
  - `histo|<secs>|<end_ts>|<buckets>` —— 请求某区间的时段健康桶数据。
  - `quit` —— 请求退出（实际退出走托盘，前端仅兜底）。
  - （已移除 `export`，不做 CSV 导出。）

---

## 4. 系统托盘

- `Shell_NotifyIcon` 注册 `NIM_ADD`，图标来自 `resources/app.ico`。
- 左键单击：显示/隐藏主窗口。
- 右键菜单：`显示/隐藏`、`打开历史文件夹`（`ShellExecute` explorer）、`退出`（`PostMessage WM_CLOSE` → 完全退出）。
- 销毁回调清除托盘图标。

---

## 5. 温度采集（进程内，可选）

- `CpuTempReader`：打开 PawnIO 设备 → 按 CPU 厂商（CPUID）加载 `IntelMSR` 或 `AMDFamily17` → MSR/SMN 读 package 温度；任一步失败即停用，后续重试频率可控，绝不阻塞 1s 采样。
- `GpuTempNvml`：`LoadLibrary(nvml.dll)` → `nvmlDeviceGetHandleByIndex` → `nvmlDeviceGetTemperature`（NVIDIA GPU）。
- `InitTemperature()` 汇总初始化；任一不可用则该温标视为哨兵 `0xFF`，UI 显示「—」，历史记无效。

---

## 6. 历史记录系统

沿用 v0.2 的设计，未做改动：

- 两级分辨率：raw（1s，24B 定长，每天一个 `raw-YYYYMMDD.hwdb`，默认保留 14 天）+ min（60s 聚合，每月一个 `min-YYYYMM.hwdb`，长期保留）。
- 磁盘预算：raw ≈ 2.1MB/天，min ≈ 57KB/天，总上限 `max_total_mb`（默认 300MB）超出从最老 raw 删除。
- 写入：内存环形缓冲（最近 30min）→ SPSC 队列 → 落盘线程每 10s 批量 append，无逐条 fsync。
- 查询：定长 + 时间单调 → 二分定位 + 顺序扫描；窗口 ≤ 3 天读 raw，> 3 天读 min；后台线程流式读取，UI 不卡。
- 单实例互斥体保证单写者。

---

## 7. 代码结构

```
src/
├── app/
│   ├── main.cpp               # 入口：COM 初始化 + 单实例 + Dashboard 主循环
│   ├── core/
│   │   ├── config.{cpp,h}     # JSON 配置（%APPDATA%\Sentry-Lite\config.json / 便携模式）
│   │   └── snapshot.h         # 每秒 Snapshot（UI 与 Recorder 共同输入）
│   ├── collectors/            # 无特权采集
│   │   ├── cpu_usage.{cpp,h}  # GetSystemTimes 差分
│   │   ├── mem_usage.{cpp,h}  # GlobalMemoryStatusEx
│   │   ├── gpu_usage.{cpp,h}  # PDH GPU Engine 聚合
│   │   └── net_speed.{cpp,h}  # GetIfTable2 + GetBestInterface
│   ├── history/               # 历史子系统（raw/min 两级 + 保留策略）
│   │   ├── recorder.{cpp,h}   # 环形缓冲、SPSC 队列、落盘线程
│   │   ├── store.{cpp,h}      # 文件格式、追加写、二分区间读
│   │   ├── retention.{cpp,h}  # 保留策略与容量上限清扫
│   │   └── query.{cpp,h}      # 区间/单点查询
│   ├── temp/                  # 温度采集（进程内，可选）
│   │   ├── pawnio_client.{cpp,h}
│   │   ├── temp_cpu.{cpp,h}   # PawnIO IntelMSR / AMDFamily17
│   │   └── temp_gpu_nvml.{cpp,h}
│   ├── ui/
│   │   ├── dashboard.{cpp,h}  # 主窗口、WebView2 生命周期、托盘、1s 采样循环、Native⇄JS 桥
│   │   └── web/dashboard.html # 内嵌仪表盘（构建后复制到 exe 旁 web/ 目录）
└── shared/
    └── sample.h               # RawSample/MinSample/哨兵（0xFF）约定
```

## 8. 配置

`%APPDATA%\Sentry-Lite\config.json`；exe 旁存在 `portable.marker` 时便携模式。

```json
{ "interval_ms": 1000, "nic": "auto", "temp_warn_c": 75, "temp_crit_c": 85,
  "history": { "enabled": true, "dir": "", "raw_retention_days": 14,
               "min_retention_years": 0, "max_total_mb": 300 } }
```

## 9. 构建 / 分发

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

- 产物 `build/Release/Sentry-Lite.exe`（静态链接 CRT，无额外运行时依赖）。
- 构建后把 `src/app/web` 复制到 exe 旁 `web/`，WebView2 `file://` 加载。
- release：GitHub Actions 打 tag 后打包 exe + web 为 zip（见 `.github/workflows/release.yml`）。

## 10. 非目标

- 风扇/电压/频率/功耗控制、进程列表、磁盘/电池/主板、FPS、自动更新、云同步、**CSV 导出**。
- Windows on ARM、Win10 1809 以下（依赖 GPU Engine 计数器与 WebView2）。

## 11. 风险与对策

1. **无 PawnIO / 驱动不支持**：CPU/GPU 温度显示「—」，功能不崩（降级即默认态）。
2. **WebView2 运行时缺失**：探测失败则避免崩溃，仅实时数据缺失，托盘仍可用（`webview_broken_` 兜底）。
3. **数值口径**：占用对齐任务管理器 ±2%，温度对齐 HWiNFO ±3°C。
4. **时钟跳变 / 睡眠**：时间戳单调性保护 + gap 标记，历史断档正确渲染。

## 12. 里程碑（当前状态）

| 阶段 | 内容 | 状态 |
|---|---|---|
| v0.3 核心 | 单 EXE、WebView2 仪表盘、托盘退出、1s 采集落盘、区间健康条、温度降级 | ✅ 已实现 |
| v0.3 后续 | 开机自启、通知、美化打磨 | 待定 |