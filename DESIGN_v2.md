# 轻量硬件监控工具 · 详细设计 v0.2.1（第二版，未开始实现）

一句话定位：**只做 4 项指标（CPU 占用+温度、GPU 占用+温度、内存、网速）的无依赖原生小工具，默认普通权限运行，温度作为可选的特权增强；v2 新增：全部指标持续落盘，可回看任意历史时刻的详细数据。**

## v0.1 → v0.2 变更摘要

1. **新增**：历史记录系统（§6）——全部指标 1s 采样持久化，两级降采样 + 保留策略，自定义二进制格式，磁盘空间可控。
2. **新增**：历史查看器窗口（§7）——时间范围切换、缩放平移、十字光标逐时刻读数、统计、CSV 导出。
3. **变更**：UI 进程新增 Recorder/History 模块；里程碑调整——M1 就内置记录器（从第一天开始积累数据），查看器放 M3。
4. **不变**：数据源选型、双进程架构、温度走 PawnIO 服务、轻量预算（CPU/内存不变，新增磁盘预算一节）。
5. **v2.1 新增**：UI 视觉规范整体对齐 port-manager——浅色工作台风、`#E2E4E8` 细边框、4px 小圆角、`#2D2D2D` 深色选中态、绿/琥珀/红语义色、数值全等宽字体（详见 §7 视觉规范），悬浮条与历史查看器按此重绘。

---

## 1. 调研结论：它们是怎么拿到数据的

### 1.1 TrafficMonitor（C++ / MFC）

| 指标 | 实现 | 权限 |
|---|---|---|
| 网速 | `GetAdaptersInfo` 枚举网卡 + `GetIfTable`（iphlpapi）字节计数器每秒差分 | 无需管理员 |
| CPU 占用 | PDH 性能计数器，失败回退 `GetSystemTimes`（源码 `PdhHardwareQuery/CPUUsage.cpp`） | 无需管理员 |
| GPU 占用 | PDH `\GPU Engine(*)\Utilization Percentage`，各引擎类型取最大值——与任务管理器同款算法（`GpuUsage.cpp`） | 无需管理员 |
| 内存 | `GlobalMemoryStatusEx` | 无需管理员 |
| 温度 | 独立工程 `OpenHardwareMonitorApi`：C++/CLI 包装 **LibreHardwareMonitorLib**，读 CPU/GPU/硬盘/主板温度 | **整进程需管理员**（读 MSR） |
| 任务栏显示 | `SetParent` 把窗口塞进任务栏（对 Win11 新任务栏较脆弱） | — |

### 1.2 LiteMonitor（C# / .NET 8 / WinForms）

- 全部传感器来自 **LibreHardwareMonitorLib 0.9.6**（`LiteMonitor.csproj` 唯一实质依赖），另有 PDH/PerformanceCounter 和网速差分。
- **LHM 自 v0.9.5 起把内核驱动从 WinRing0 换成了 PawnIO**（Release note: "Swap WinRing0 to PawnIO"）。原因：WinRing0 在微软"易受攻击驱动阻止列表"里，开启内核隔离/阻止列表的机器加载失败，且常年被杀软误报。
- LiteMonitor 的 `DriverInstaller` 在启动时检查 **PawnIO ≥ 2.2.0**，缺失则从 Gitee/官网/GitHub 下载 `driver.zip`（内含官方 `PawnIO_setup.exe`）安装；并用**管理员级计划任务**实现开机自启——即整个 UI 进程提权跑。
- "重"的来源：.NET 8 运行时 + LHM 每次更新轮询全部硬件树（主板/存储/电池/PSU/控制器）+ Updater 子进程 + 插件/主题/i18n/测速/历史等全家桶。

### 1.3 PawnIO（namazso 开发，GPLv2 + IOCTL 链接例外，官网 pawnio.eu）

- 一个正规 EV 签名的内核驱动 + 模块加载器：驱动本身不提供任意 ring0 读写，而是按名字加载**单独签名的功能模块**，应用再通过 IOCTL 调用模块函数。攻击面远小于 WinRing0，不在阻止列表，杀软友好。HWiNFO / LibreHardwareMonitor / FanControl 均已支持。
- 官方已签名模块（`namazso/PawnIO.Modules`）里**读温度需要的全都有**：
  - `IntelMSR`（Intel MSR 读写 → CPU 温度）
  - `AMDFamily17`（Zen 架构 SMN 访问 → CPU 温度）、`RyzenSMU`
  - `Nvidia`（N 卡寄存器访问）
  - `IntelPCHThermal`、`LpcACPIEC`（EC）、各 SuperIO/SMBus 模块
- **约束**：模块必须由 PawnIO 作者签名（自己写模块要上游发 PR/签名），所以我们**只用现成官方模块**，不改驱动。
- **许可证**：GPL 例外条款明确允许"仅通过 device IO control 接口与 PawnIO 通信的独立模块"保持自己的许可证 → 我们的程序通过 IOCTL 通信即可用任意许可证（MIT 等）；随包分发官方安装器属聚合分发，无传染。

### 1.4 关键结论（本设计的立足点）

1. **4 项指标里有 3.5 项完全不需要任何特权**：CPU 占用、GPU 占用、内存、网速全部是普通 Win32 API（TrafficMonitor 已在产品级证明）。唯一需要特权的是**温度**（CPU 温度在 MSR/SMN 必须 ring0；NVIDIA 新驱动对 NVML 性能指标加了管理员门槛）。
2. 因此架构上把温度隔离到**一个可选的小型特权服务进程**，UI 永远普通权限；不装驱动/不给管理员 = 其余全部照常，温度显示"—"。
3. 不引入 LibreHardwareMonitorLib（否则 .NET 运行时 + 全硬件树轮询 + ~100MB 内存，"轻量"目标作废）；4 个指标的采集代码全部自写，温度解析参考 LHM 实现（MPL-2.0，文件级注明移植来源即可）。
4. **v2**：历史记录同样只落在 UI 进程（它每秒已拿齐全部数据），服务与存储无关，架构复杂度不增加。

---

## 2. 目标与非目标

**目标**

- 指标只有：CPU 占用+温度、GPU 占用+温度、内存占用、上传/下载网速。
- **v2：全部指标以 1s 分辨率持久化，可回看任意历史时刻的全部数值；磁盘占用可控（默认策略下长期 < 100MB，上限可配置）。**
- 轻量硬指标：UI 进程私有内存 < 20MB（含历史缓冲）、CPU < 0.3%；含温度服务总内存 < 30MB；单 exe < 1.5MB；除历史数据文件与配置外零磁盘写入、零网络行为（自动更新都不做，历史数据只在本地）。
- 支持 Windows 10 1809+ / Windows 11，x64（你这台 26100 正好在支持范围）。

**非目标（明确不做）**

- 风扇/电压/频率/功耗控制、进程列表、磁盘、电池、主板、FPS、天气插件、皮肤市场、网页版、自动更新。
- 历史数据的云同步/上报/在线面板（CSV 导出属于本地能力，做）。
- Windows on ARM、Win10 1709 及以下（无 GPU Engine 计数器）。

---

## 3. 指标数据源设计（核心，与 v1 相同）

| 指标 | 实现 | API | 权限 | 失败降级 |
|---|---|---|---|---|
| CPU 占用 | 总负载差分：`1 − Δidle/(Δidle+Δkernel+Δuser)` | `GetSystemTimes` 每 1s 差分 | 无 | `NtQuerySystemInformation`（可拿每核） |
| 内存占用 | `dwMemoryLoad`；悬停显示 已用/总量 | `GlobalMemoryStatusEx` | 无 | — |
| 网速 | 默认路由网卡的收发字节差分；速率自动换算 B/s→KB/s→MB/s | `GetIfTable2`（64 位计数器）+ `GetBestInterface` 判默认网卡 | 无 | 手动选网卡；失败回退 `GetIfTable` |
| GPU 占用 | 按物理 GPU（`phys_N`）聚合所有进程实例，各引擎类型分别求和后取最大值——任务管理器同口径 | PDH `\GPU Engine(*)\Utilization Percentage` | 无 | 计数器不存在显示"—" |
| CPU 温度 | Intel：`TjMax − ΔT`（MSR `0x1A2` 取 TjMax，`0x1B1` package 温度，或每核 `0x19C` 取最大）。AMD Zen：SMN `0x59800` 读 Tctl，按型号减偏移得 Tdie（偏移表移植自 LHM） | PawnIO 模块 `IntelMSR` / `AMDFamily17` | 服务进程（驱动） | 无 PawnIO → "—"；机会性读 `IntelPCHThermal` |
| GPU 温度 | NVIDIA：`nvmlDeviceGetTemperature`（`nvml.dll` 随驱动分发，动态加载）。AMD：`atiadlxx.dll` ADL/ADLX。Intel 核显：`level_zero.dll`（多数 iGPU 无传感器） | NVML / ADL / Level Zero | **放服务进程内调用**（NVIDIA R530+ 起非管理员拿不到性能类指标） | 传感器不可得 → "—" |

备注：GPU 占用与温度的"哪块卡"对齐用名称匹配（DXGI `AdapterDesc` ↔ NVML `DeviceName`）启发式；温度不在时用 `0xFF`/NaN 哨兵值，与历史存储格式共用同一约定。

---

## 4. 总体架构

双进程 + 共享内存 + 本地历史存储（历史存储在 UI 进程内）：

```
┌────────────────────────────────────────────────────┐
│ hwmon.exe（普通权限，常驻，HKCU Run 自启，单实例互斥体）      │
│  UI：悬浮条 + 历史查看器窗口（D2D/DWrite）                   │
│  采集：CPU% / GPU% / RAM / 网速（Win32 API）               │
│  v2 Recorder：内存环形缓冲 → 批量落盘线程 → 分级数据文件       │
│      │ 每秒读共享内存拿温度                          │
└──┬────────────────────────┬────────────────────────┘
   │ 无特权 API                │ Local\hwmon_shm（温度+心跳）
   │                         ▼
   │            ┌─────────────────────────────┐
   │            │ hwmon-svc.exe（可选，Windows 服务，│
   │            │  SYSTEM，auto-delayed）           │
   │            │  PawnIO IntelMSR/AMDFamily17     │
   │            │  NVML/ADL/LevelZero 读温度        │
   │            └─────────────────────────────┘
   ▼
 %APPDATA%\hwmon\history\
   raw-YYYYMMDD.hwdb    （1s 原始，保留 N 天）
   min-YYYYMM.hwdb      （1min 聚合，长期保留）
```

- 服务每秒写共享内存并更新心跳；UI 发现心跳 >5s 未更新 → 温度列显示"—"，其余指标与**历史记录照常**（温度记为无效哨兵）。
- 服务崩溃由 SCM 自动重启；驱动异常只影响温度。
- 历史记录完全属于 UI 进程：数据已在手，追加写本地文件即可，无需服务、无需额外进程。

---

## 5. 温度采集专项设计（与 v1 相同）

### 5.1 为什么选 PawnIO

| 方案 | 结论 |
|---|---|
| LibreHardwareMonitorLib | ❌ .NET 运行时 + 全硬件树轮询，违背轻量目标 |
| WinRing0（OHM/LHM 老方案） | ❌ 在微软易受攻击驱动阻止列表，杀软误报重灾区 |
| 自写内核驱动 | ❌ 需要 EV 代码签名 + WHQL，成本极高 |
| **PawnIO** | ✅ 正规 EV 签名驱动 + 官方签名模块现成，LHM/HWiNFO/FanControl 已切换，许可允许 IOCTL 侧自由使用 |

### 5.2 接入方式

- 参考 `namazso/PawnIOLib` 与 PawnIO.Modules Wiki 的 IOCTL 协议，实现 ~200 行 C++ 客户端：打开设备 → 按名字加载模块 → 按索引调用模块函数（读 MSR / SMN）。
- 服务启动：CPUID 判厂商 → 加载对应模块 → 失败则记录并停用 CPU 温度，不重试轰炸。

### 5.3 待实测确认（设计按保守假设）

1. NVIDIA R530+ 对 NVML 的管理员限制——放服务内已规避。
2. PawnIO 官方安装器是否支持静默参数——不支持就引导用户跑一次官方安装器。

---

## 6. 历史记录系统（v2 新增，核心）

### 6.1 需求口径

- "过去详细的每个时候的数据" = **1 秒分辨率的原始采样**，任意时刻可查全部 7 个数值（CPU%、CPU温、GPU%、GPU温、RAM%、↑、↓）。
- 回看必须是低摩擦的：打开查看器即得最近数据；任意日期/区间可查；光标停在哪，那一秒的数值全列出来。

### 6.2 存储选型：自定义追加式二进制文件（不用 SQLite）

| | 自定义定长记录文件（选定） | SQLite |
|---|---|---|
| 体积/依赖 | 零依赖，代码 ~400 行 | 静态链入 +~1MB，WAL/PRAGMA 调优 |
| 写路径 | 纯顺序追加，天然适合 1s 采样 | 事务/页管理开销，写放大 |
| 查询 | 定长+时间单调 → 二分定位 + 顺序扫描，本场景只需要区间扫描 | SQL 富查询（本场景用不上） |
| 数据可携性 | 查看器内建 CSV 导出 | 通用工具可打开（优势，但非刚需） |

结论：本场景是"单写者、顺序追加、区间读取"的最简单时间序列形态，自定义格式最轻；未来若要插件化/多维查询再引入 SQLite，格式版本号已预留演进空间。

### 6.3 数据布局：两级分辨率 + 保留策略

**原始层（raw）**：1s 一条，定长 24B：

```c
struct RawSample {            // 24 bytes
    uint64 ts_unix;           // UTC 秒
    uint8  cpu_pct;           // 0..100，0xFF=无效
    uint8  cpu_temp_c;        // 0xFF=无效（无温度服务期间）
    uint8  gpu_pct;
    uint8  gpu_temp_c;
    uint8  ram_pct;
    uint8  flags;             // bit0=采样缺口标记（见 6.6）
    float  net_up_bps;        // B/s（float 足够：网速显示精度 3 位有效数字）
    float  net_down_bps;
};
```

**分钟聚合层（min）**：对已完结的自然日做一次聚合，每指标存 avg/min/max（网速只存 avg/max）：

```c
struct MinSample {            // ~40 bytes
    uint64 ts_unix;           // 分钟起点
    uint8  cpu_avg, cpu_min, cpu_max;
    uint8  cpu_t_avg, cpu_t_min, cpu_t_max;   // 0xFF=无效
    uint8  gpu_avg, gpu_min, gpu_max;
    uint8  gpu_t_avg, gpu_t_min, gpu_t_max;
    uint8  ram_avg, ram_min, ram_max;
    float  net_up_avg, net_up_max;
    float  net_down_avg, net_down_max;
};
```

**保留策略（默认，可配置）**：

| 层 | 分辨率 | 保留 | 体积估算 |
|---|---|---|---|
| raw | 1s | 14 天 | 24B×86400 ≈ **2.1MB/天**，14 天 ≈ 29MB |
| min | 60s | 永久（或 2 年） | 40B×1440/天 ≈ 57KB/天，**≈21MB/年** |

默认稳态磁盘占用 **≈ 50MB/年 + 29MB 原始滚动**；总上限保护：`max_total_mb`（默认 300MB）超出时从最老的 raw 开始删。百分比类用 uint8 存储（显示精度 1% 已够；温度同理），网速用 float32。

### 6.4 文件格式与查询

- 每天一个 raw 文件 `raw-YYYYMMDD.hwdb`，每月一个 min 文件 `min-YYYYMM.hwdb`（按 UTC 日期切分，查看时转本地时区显示）。
- 文件头：`{magic 'HWD1', version, record_size, start_ts, count}` + 记录数组。记录定长且时间单调 → **区间查询 = 对时间戳二分定位起始偏移，顺序读**，O(log n)。
- 缺失天数/缺记录不补零，直接没有记录——渲染时表现为断线。

### 6.5 写入管线（UI 进程内，三个组件）

```
采样tick(1s) ──► 内存环形缓冲(最近30min，供查看器默认视图秒开)
            └──► SPSC队列 ──► 落盘线程：缓冲写、每10s一次顺序append（~240B）
                              ├─ 跨自然日 → 关闭旧文件、开新文件
                              ├─ 触发对"昨日"的min聚合任务（幂等，带水位）
                              └─ 启动时/每日跑保留策略清扫
```

- **绝不每条 fsync**：每 10s 批量 append，崩溃最多丢最后 <10s 数据，可接受（文件头 count 容忍尾部截断，读时丢弃半条记录）。
- 磁盘忙/被备份软件锁文件 → 退避重试，队列有界（满则丢弃最旧并计数），UI 永不因存储卡死。
- 磁盘满：Recorder 暂停写入 + 悬浮条角落显示一个小图标提示，其余功能不受影响。

### 6.6 边界情况

- **睡眠/休眠/关机**：无采样 → 时间戳自然断开；恢复后首条 `flags.gap=1`，查看器在该处断线不插值，tooltip 显示"中断 N 小时 N 分"。
- **系统时钟跳变**（NTP 校时/手动改）：ts 用 UTC 墙钟，若检测到新 ts ≤ 上一条，丢条并计数（单调性保护，保证二分成立）。
- **温度服务中途安装/卸载**：温度列 0xFF 哨兵自然表达"该时段无数据"，查看器显示灰段，不造数据。
- **多用户**：历史在各自 `%APPDATA%` 下，无冲突；便携模式则写 exe 旁 `history/`。
- **单实例**：命名互斥体保证只有一个写入者。

### 6.7 与查看器的读取接口

```
Query(start_ts, end_ts, max_points) → 序列(等距下采样或全量)
  窗口 ≤ 3 天 → 读 raw 层（stride = ceil(样本数/像素宽)）
  窗口 > 3 天 → 读 min 层
At(ts)  → 单条原始记录（十字光标读数用，raw 命中即回，否则最近一条+标注）
```

读取在后台线程流式进行，任何大区间查询 UI 不卡顿。

---

## 7. 历史查看器 UI（v2 新增；v2.1 视觉对齐 port-manager）

### 共用视觉规范（v2.1 新增，悬浮条与查看器共用，取自 port-manager）

**色板**（浅色为默认主题；深色主题降级为后续可选项）：

| Token | 色值 | 用途 |
|---|---|---|
| bg-app | `#F9FAFB` | 窗口/图表区底色 |
| bg-panel | `#FFFFFF` | 工具栏、卡片、悬浮条本体 |
| bg-subtle | `#F8F9FA` | 表头、行 hover、状态栏底 |
| bg-inset | `#F0F1F3` | 输入框底、图表网格线、细分隔线 |
| border | `#E2E4E8` | 主边框（一律 1px） |
| border-hover | `#C4C8CF` | 控件 hover 边框 |
| text-primary | `#1A1C20` | 主文本/数值 |
| text-secondary | `#6B7280` | 标签、次要文本 |
| text-hint | `#9CA3AF` | 占位、提示、小节标题 |
| accent-active | `#2D2D2D` | 分段控件选中态（配白字） |
| ok | `#16A34A`（浅底 `#F0FDF4`） | 正常状态、下载速率线 |
| warn | `#D97706`（浅底 `#FFF7EB`） | 温度 ≥85°C、上传速率线 |
| crit | `#DC2626` | 温度 ≥95°C、错误 |

**字体**：标题/正文 Outfit，中文回退 Noto Sans SC / Microsoft YaHei UI；**所有数值用 Fira Code 等宽**（回退 Consolas）——数值每秒刷新时宽度不抖动。字号 11/12/13/15 四档，标题 600、正文 400/500。内嵌 Outfit + Fira Code 拉丁子集约 +0.4MB（体积预算内）。

**形状与密度**：控件圆角 4px，弹窗/悬浮条 6~8px；紧凑密度（内距 12px、段间距 8px）；焦点态 2px `#0A152D` 外描边；阴影只用于浮层（`0 8px 32px rgba(0,0,0,0.08)`）。

**组件样式**（与 port-manager 同款）：

- 分段控件（时间范围等切换）：外框 `#E2E4E8`、底 `#F8F9FA`，选中项 `#2D2D2D` 白字，未选中 `#6B7280`。
- 按钮：白底 1px 边框，hover 边框 `#C4C8CF`、底 `#F3F4F6`。
- 窗口骨架：顶部工具栏（白底 + 下边框，标题 15px semibold + 副标题 12px）→ 内容区 `#F9FAFB` → 底部状态栏（28px 高，`#F8F9FA` 底 + 上边框，11px `#6B7280`，右侧时间）。
- 弹窗：遮罩 `#0A1B33`/25% + 1px 背景模糊，白底 8px 圆角轻阴影，标签 11px 大写字距加宽 `#9CA3AF`。

**图表配色**：网格线 `#F0F1F3`，刻度文字 11px 等宽 `#6B7280`；占用曲线 `#1A1C20`、温度曲线 `#D97706`（两色各泳道通用，无需图例）；网络泳道上传 `#D97706`、下载 `#16A34A`；断档区间用 `#9CA3AF` 细斜纹填充。

从悬浮条右键菜单"历史记录"打开的独立窗口（普通窗口，约 900×600 起步，可调；骨架按上方规范：白底工具栏 + 分段控件 + 图表区 + 底部状态栏）：

```
┌─ 历史记录 ────────────────────────────────────────────────┐
│ [15分钟][1小时][6小时][24小时][7天][30天][自定义▾]   [导出CSV] [⚙保留策略] │
│ ── 可见区间统计：CPU max 97% avg 23% │ GPU max 88% … ──────────│
│ CPU  ▲%  ────────曲线────────        ┊温度副轴 45-90°C        │
│ GPU  ▲%  ────────曲线────────        ┊温度副轴                │
│ RAM  ▲%  ────────曲线────────                                 │
│ 网络  ↑↓  ──双线──────────                                   │
│        08-26 00:00    08-26 12:00    08-27 00:00（时间轴）      │
├─ 十字光标读数 ─────────────────────────────────────────────┤
│ 2026-08-27 14:32:05  CPU 47% 48°C │ GPU 12% 51°C │ RAM 63% │  ↑ 2.1MB/s ↓ 318KB/s │
└────────────────────────────────────────────────────────────┘
```

- **四条泳道共享 X 轴**：CPU（占用线+温度线双轴）、GPU（同构）、RAM、网络（↑↓双线）。
- 交互：滚轮以光标为中心缩放，拖拽平移，双击复位；十字光标竖线 + 底部读数条显示**该秒全部 7 个数值**（超 raw 保留期则显示该分钟的 avg/max 并标注）。
- 断档（睡眠/关机/丢数据）渲染为断线，光标悬停显示中断时长。
- 导出 CSV：当前可见区间、raw 原始分辨率，一秒一列时间+7 值；大区间流式写出。
- 保留策略设置：raw 天数（7/14/30/60）、min 年限（1/2/永久）、总容量上限——都在设置对话框里，改完即时生效。
- 渲染与悬浮条共用 D2D 设施；泳道曲线绘制 ~1k 行内，不引第三方图表库。

---

## 8. 悬浮条 UI（v2.1 视觉对齐 port-manager；菜单加一项）

```
┌────────────────────────────────────────────┐
│ CPU 23% 45°C │ GPU 8% 51°C │ RAM 62% │ ↑3.1MB/s ↓256KB/s │
└────────────────────────────────────────────┘
```

- 白底 95% 不透明小条：1px `#E2E4E8` 边框、6px 圆角、浮层阴影；标签 11px `#6B7280`，数值 13px Fira Code `#1A1C20`，段间 1px `#F0F1F3` 竖分隔；无边框置顶、拖动移动、位置/DPI（per-monitor v2）记忆。
- 右键菜单：设置 / **历史记录（v2）** / 置顶 / 鼠标穿透 / 靠边隐藏 / 开机自启 / 安装温度服务 / 退出。
- 每项可独立开关与排序；温度 ≥85°C `#D97706`、≥95°C `#DC2626`；Recorder 异常（磁盘满等）时角落 `#DC2626` 小圆点提示。
- 悬停 tooltip：CPU/GPU 名称、内存已用/总量、网卡名。
- 主题默认浅色（§7 色板），深色为后续可选项；字号一档可调；Direct2D + DirectWrite，数据不变不重绘。
- 任务栏嵌入模式仍为实验性选项（见风险）。

---

## 9. 代码结构（C++20 / CMake）

```
device-monitor/
├── src/
│   ├── app/                      # hwmon.exe（UI 进程）
│   │   ├── main.cpp              # + 单实例互斥体
│   │   ├── ui/
│   │   │   ├── theme.h           # v2.1：port-manager 色板/字号/圆角 token（D2D 色值）
│   │   │   ├── float_bar/        # 悬浮条窗口、菜单、拖动、DPI
│   │   │   ├── history_view/     # v2：查看器窗口、泳道图表、十字光标、交互
│   │   │   └── d2d/              # D2D/DWrite 公共绘制设施 + 内嵌字体加载
│   │   ├── collectors/           # 无特权采集（统一 ISource 接口）
│   │   │   ├── cpu_usage.cpp     # GetSystemTimes 差分
│   │   │   ├── mem_usage.cpp     # GlobalMemoryStatusEx
│   │   │   ├── gpu_usage.cpp     # PDH GPU Engine 聚合
│   │   │   └── net_speed.cpp     # GetIfTable2 + GetBestInterface
│   │   ├── history/              # v2：历史记录子系统
│   │   │   ├── sample.h          # RawSample/MinSample/哨兵约定
│   │   │   ├── recorder.cpp      # 环形缓冲、SPSC 队列、落盘线程
│   │   │   ├── store.cpp         # 文件格式、追加写、二分区间读
│   │   │   ├── downsampler.cpp   # 日→分钟聚合（幂等）
│   │   │   ├── retention.cpp     # 保留策略与容量上限清扫
│   │   │   └── query.cpp         # Query()/At()，后台线程流式读取
│   │   └── core/                 # 配置、1s 调度、共享内存客户端
│   ├── svc/                      # hwmon-svc.exe（温度服务，不变）
│   │   ├── service.cpp           # SCM 宿主、失败自动重启
│   │   ├── pawnio_client.cpp     # PawnIO IOCTL 客户端
│   │   ├── temp_cpu_intel.cpp    # MSR 0x1A2/0x1B1（移植自 LHM，注明 MPL）
│   │   ├── temp_cpu_amd.cpp      # SMN 0x59800 + Tctl 偏移表
│   │   ├── temp_gpu_nvml.cpp     # nvml.dll 动态加载
│   │   ├── temp_gpu_adl.cpp      # atiadlxx.dll（M4）
│   │   ├── temp_gpu_l0.cpp       # level_zero.dll（M4，尽力而为）
│   │   └── shm_publisher.cpp
│   └── shared/                   # 共享内存结构、NaN/0xFF 哨兵约定
├── installer/                    # --install-service / --uninstall-service、PawnIO 引导
└── DESIGN.md / DESIGN_v2.md
```

---

## 10. 配置

单文件 JSON，默认 `%APPDATA%\hwmon\config.json`；exe 旁存在 `portable.marker` 时便携模式。

```json
{
  "interval_ms": 1000,
  "items": ["cpu", "cputemp", "gpu", "gputemp", "ram", "net"],
  "nic": "auto",
  "theme": "light",            // v2.1 默认浅色（port-manager 色板）；dark 为后续可选
  "font_scale": 1.0,
  "topmost": true,
  "click_through": false,
  "auto_hide": false,
  "position": [-1, -1],
  "temp_warn_c": 85,
  "temp_crit_c": 95,
  "history": {                   // v2
    "enabled": true,
    "dir": "",                   // 空 = 默认 %APPDATA%\hwmon\history
    "raw_retention_days": 14,    // 0 = 不留 raw（不建议）
    "min_retention_years": 0,    // 0 = 永久
    "max_total_mb": 300
  }
}
```

---

## 11. 安装 / 分发 / 权限流（不变，补充数据目录）

- 绿色 zip（`hwmon.exe` + `hwmon-svc.exe`），无安装程序。
- 首次运行即开始记录历史（普通权限，数据在 `%APPDATA%\hwmon\`）。
- 温度一键开通：菜单"启用温度监控" → UAC 一次 → 建 `hwmon-svc` 服务（SYSTEM，自动-延迟，失败重启）→ 检测/引导 PawnIO 官方安装器 → 温度即时并入显示与历史。
- 卸载：`--uninstall-service`（UAC）删服务；PawnIO 留给用户自行卸载（可能被其他软件共用）；历史数据目录在卸载时提示是否一并删除。

---

## 12. 性能预算与验证方法

| 项 | 预算 | 验证 |
|---|---|---|
| UI CPU | < 0.3%（1s 采样 + 按需重绘 + Recorder 均摊） | ETW 10 分钟均值 |
| UI 内存 | 私有工作集 < 20MB（含环形缓冲与查看器工作集） | `Get-Process` |
| 服务 CPU/内存 | < 0.1% / < 8MB | 同上 |
| 磁盘写 | 每 10s 一次顺序 append ≈ 240B，无 fsync 风暴 | Process Monitor |
| 磁盘空间 | 默认策略稳态 ≈ 50MB/年 + 29MB raw 滚动；硬上限 300MB 可配 | 目录大小 |
| 网络 | 0 外联 | netstat |
| 体积 | exe < 1.5MB（含 v2.1 内嵌字体 ~0.4MB），包 < 2MB | — |

与现有软件的量级对比（典型观测值，仅供参考）：TrafficMonitor ~30-60MB；LiteMonitor（.NET+LHM+Updater）~100-200MB。

---

## 13. 风险与对策

1. **PawnIO 模块对新硬件的覆盖**：不在支持列表 → 温度缺失显示"—"，历史记哨兵，功能不崩。
2. **NVIDIA R530+ 的 NVML 管理门槛**（未在线复核）：温度调用全在服务进程，无论门槛是否存在设计都成立。
3. **AMD Tctl 偏移表维护**：从 LHM（MPL-2.0）移植并注明；未知型号显示 Tctl 原值并 tooltip 标注。
4. **Intel 混合架构（E/P 核）**：首版显示 package 温度（MSR 0x1B1）。
5. **任务栏嵌入模式**：Win11 24H2 对 `SetParent` 黑科技不友好 → 实验性选项，默认悬浮条。
6. **Intel 核显温度**：多数 iGPU 不暴露传感器 → "—"，不造假数据。
7. **杀软误报**：正规 EV 签名驱动 + 不自写驱动，远低于 WinRing0 系风险；非零，保留 FAQ。
8. **数值口径**：占用对齐任务管理器 ±2%，温度对齐 HWiNFO ±3°C。
9. **v2 磁盘风险**：磁盘满/配额 → Recorder 暂停并提示，绝不影响监控本身；文件被锁 → 退避重试。
10. **v2 大区间查询**：>3 天自动切 min 层 + 流式读取 + 像素步长下采样，UI 不卡、内存不涨。
11. **v2 时钟跳变**：ts 单调性保护（非单调丢条计数），保证二分查找正确性。

---

## 14. 里程碑

| 阶段 | 内容 | 验收 |
|---|---|---|
| M1 无特权核心版 + **记录器** | 悬浮条 + CPU/GPU 占用、内存、网速 + **Recorder 落盘**（查看器未出，数据先积累） | 数值对齐任务管理器 ±2%；内存 <20MB；CPU <0.3%；磁盘写入符合预算；文件格式可被后续查看器读取 |
| M2 温度服务 | 服务进程 + PawnIO + Intel/AMD CPU 温度 + NVIDIA GPU 温度 | 与 HWiNFO 对齐 ±3°C；服务拔掉后 UI 优雅降级；温度并入历史 |
| M3 历史查看器 | 查看器窗口：区间切换/缩放平移/十字光标读数/统计/CSV 导出/保留策略设置 | 30 天区间查询 < 300ms；光标读数与原始记录一致；断档正确渲染 |
| M4 完善 | AMD/Intel GPU 温度、实验性任务栏模式、安装引导打磨 | — |

---

## 15. 待拍板的决策点

1. **技术栈**：推荐 C++20 + Win32 + D2D（零运行时、体积最小）；备选 Rust + windows-rs；C# 不推荐（WinForms/WPF 不支持 NativeAOT）。
2. **首发范围**：M1（无温度但已带记录）先出，还是直接做到 M2/M3？
3. **默认 UI 形态**：悬浮条（推荐，稳）还是任务栏模式优先？
4. **历史保留默认值**：raw 14 天 / min 永久 / 上限 300MB——按此默认，还是更保守（raw 7 天 / 上限 150MB）？
5. **开源许可证**：MIT（推荐）？温度解析含 LHM 移植文件需保留 MPL-2.0 文件级声明。
