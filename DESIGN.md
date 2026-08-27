# 轻量硬件监控工具 · 详细设计 v0.1（草案，未开始实现）

一句话定位：**只做 4 项指标（CPU 占用+温度、GPU 占用+温度、内存、网速）的无依赖原生小工具，默认普通权限运行，温度作为可选的特权增强。**

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
- **LHM 自 v0.9.5 起把内核驱动从 WinRing0 换成了 PawnIO**（Release note: "Swap WinRing0 to PawnIO"）。原因：WinRing0 在微软"易受攻击驱动阻止列表"（Vulnerable Driver Blocklist）里，开启内核隔离/阻止列表的机器加载失败，且常年被杀软误报。
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
2. 因此架构上把温度隔离到**一个可选的小型特权服务进程**，UI 永远普通权限；不装驱动/不给管理员 = 其余全部照常，温度显示"—"。这比 TrafficMonitor（整进程提权）和 LiteMonitor（管理员计划任务跑整个 UI）都干净。
3. 不引入 LibreHardwareMonitorLib（否则 .NET 运行时 + 全硬件树轮询 + ~100MB 内存，"轻量"目标作废）；4 个指标的采集代码全部自写，温度解析参考 LHM 实现（MPL-2.0，文件级注明移植来源即可）。

---

## 2. 目标与非目标

**目标**

- 指标只有：CPU 占用+温度、GPU 占用+温度、内存占用、上传/下载网速。
- 轻量硬指标：UI 进程私有内存 < 15MB、CPU < 0.3%；含温度服务总内存 < 25MB；单 exe < 1MB；除配置外零磁盘写入、零网络行为（自动更新都不做）。
- 默认普通权限运行；温度功能一次性 UAC 安装，之后无人值守。
- 支持 Windows 10 1809+ / Windows 11，x64（你这台 26100 正好在支持范围）。

**非目标（明确不做）**

- 风扇/电压/频率/功耗控制、进程列表、磁盘、电池、主板、FPS、流量统计、天气插件、皮肤市场、网页版、自动更新、历史曲线。
- Windows on ARM、Win10 1709 及以下（无 GPU Engine 计数器）。

---

## 3. 指标数据源设计（核心）

| 指标 | 实现 | API | 权限 | 失败降级 |
|---|---|---|---|---|
| CPU 占用 | 总负载差分：`1 − Δidle/(Δidle+Δkernel+Δuser)` | `GetSystemTimes` 每 1s 差分 | 无 | `NtQuerySystemInformation(SystemProcessorPerformanceInformation)`（可拿每核） |
| 内存占用 | `dwMemoryLoad`；悬停显示 已用/总量 | `GlobalMemoryStatusEx` | 无 | — |
| 网速 | 默认路由网卡的收发字节差分；速率自动换算 B/s→KB/s→MB/s | `GetIfTable2`（64 位计数器，`MIB_IF_ROW2.InOctets/OutOctets`）+ `GetBestInterface` 判默认网卡 | 无 | 手动选网卡；`GetIfTable2` 失败回退 `GetIfTable` |
| GPU 占用 | 按物理 GPU（`phys_N`）聚合所有进程实例，各引擎类型（3D/Copy/Video…）分别求和后取最大值——任务管理器同口径 | PDH `\GPU Engine(*)\Utilization Percentage` | 无 | 计数器不存在（老系统/无驱动）显示"—" |
| CPU 温度 | Intel：`TjMax − ΔT`，MSR `IA32_TEMPERATURE_TARGET(0x1A2)` 取 TjMax，`IA32_PACKAGE_THERM_STATUS(0x1B1)`（或每核 `0x19C` 取最大）显示 package 温度。AMD Zen：SMN `0x59800` 读 Tctl，按 CPU 型号减偏移得 Tdie（偏移表移植自 LHM） | PawnIO 模块 `IntelMSR` / `AMDFamily17` | 服务进程（驱动） | 无 PawnIO/不支持的型号 → "—"；顺带机会性读 `IntelPCHThermal` |
| GPU 温度 | NVIDIA：`nvmlDeviceGetTemperature`（`System32\nvml.dll` 随驱动分发，动态加载，无需 SDK）。AMD：`atiadlxx.dll` ADL/ADLX 温度接口。Intel 核显：`level_zero.dll` `zesDeviceGetTemperature`（多数 iGPU 无传感器） | NVML / ADL / Level Zero，全部动态加载 | **放在服务进程内调用**（NVIDIA R530+ 起非管理员拿不到性能/温度类指标） | 传感器不可得 → "—" |

细节备注：

- GPU 占用与温度的"哪块卡"对齐：v1 用名称匹配（DXGI `AdapterDesc` ↔ NVML `DeviceName`）启发式；单卡场景（绝大多数）直接取第一块。
- PDH 每秒采一次约 1ms 开销，可忽略；`GPU Engine` 实例名格式 `pid_X_luid_0x…_phys_N_eng_M_engtype_T`，按 `phys_N`+`engtype` 两个维度聚合。
- CPU 温度每核 MSR 需要核间切换亲和性读取（LHM 同款做法）；显示用 package 值可避开混合架构（E/P 核）差异，首版先做 package。

---

## 4. 总体架构

双进程 + 共享内存，UI 与特权彻底解耦：

```
┌────────────────────────────────────────┐
│ hwmon.exe  （普通权限，常驻，开机自启 HKCU Run） │
│  UI：悬浮条（D2D/DWrite 绘制）、拖动/菜单/配置     │
│  采集：CPU% / GPU% / RAM / 网速（Win32 API）     │
└───────┬────────────────────┬───────────┘
        │ 每秒直接调用          │ 读共享内存拿温度
        │                     ▼
        │        ┌─────────────────────────────┐
        │        │ 共享内存 Local\hwmon_shm + 命名事件│
        │        └────────────▲────────────────┘
        │                     │ 每秒写入（含心跳 tick）
        │        ┌────────────┴────────────────┐
        │        │ hwmon-svc.exe（可选，Windows 服务，│
        │        │  SYSTEM，auto-delayed 启动）       │
        │        │  PawnIO 加载 IntelMSR/AMDFamily17  │
        │        │  NVML/ADL/LevelZero 读温度          │
        │        └─────────────────────────────┘
```

- 服务每秒写共享内存并更新心跳；UI 发现心跳 >5s 未更新或服务不存在 → 温度列显示"—"，**其余指标不受影响**。
- 服务崩溃自动被 SCM 重启（failure action），驱动异常只影响温度。
- UI 不直接碰驱动/PawnIO 设备（其设备接口的模块加载要求管理员上下文，全部收敛在服务内）。

共享内存布局（v1）：

```c
struct HwmonShm {
    uint32_t magic;            // 'HWM1'
    uint32_t size;             // 结构大小，便于演进
    uint64_t tick;             // 心跳（GetTickCount64）
    float    cpu_temp_c;       // NaN = 无效
    float    gpu_temp_c;
    float    pch_temp_c;       // 可选
    wchar_t  cpu_name[48];
    wchar_t  gpu_name[48];
};
```

---

## 5. 温度采集专项设计

### 5.1 为什么选 PawnIO 而不是别的

| 方案 | 结论 |
|---|---|
| LibreHardwareMonitorLib | ❌ .NET 运行时 + 全硬件树轮询，内存/体积直接违背轻量目标 |
| WinRing0（OHM/LHM 老方案） | ❌ 在微软易受攻击驱动阻止列表，内核隔离机器加载失败，杀软误报重灾区 |
| 自写内核驱动 | ❌ 需要 EV 代码签名 + WHQL，个人项目成本极高 |
| **PawnIO** | ✅ 正规 EV 签名驱动 + 官方签名模块（IntelMSR/AMDFamily17/Nvidia 现成），LHM/HWiNFO/FanControl 已切换，许可允许 IOCTL 侧自由使用 |

### 5.2 接入方式

- 参考 `namazso/PawnIOLib`（官方 C# 包装）与 PawnIO.Modules Wiki 的 IOCTL 协议，实现一个 ~200 行的 C++ 客户端：打开设备 → 请求加载模块（按名字）→ 按索引调用模块导出函数（读 MSR / SMN）。
- 服务启动流程：检测 CPU 厂商（CPUID）→ 加载对应模块 → 失败则记录并停用 CPU 温度，不重试轰炸。
- GPU 温度与驱动无关（NVML/ADL 是用户态 DLL），即使不用 PawnIO 也可用，但统一放服务里（NVML 权限门槛 + 统一通道）。

### 5.3 待实测确认的两个点（设计先按保守假设）

1. NVIDIA R530+ 对 NVML 性能/温度指标的管理员限制（调研时外网检索受限未在线复核）——放服务内已规避，无论限制与否都成立。
2. PawnIO 官方安装器是否支持静默参数（NSIS `/S`?）——不支持就引导用户跑一次官方安装器，或首次 `--install-service` 时弹出。

---

## 6. UI 设计

**默认形态：桌面悬浮条**（任务栏模式作为实验性选项，理由见风险 #5）

```
┌────────────────────────────────────────────┐
│ CPU 23% 45°C │ GPU 8% 51°C │ RAM 62% │ ↑3.1MB/s ↓256KB/s │
└────────────────────────────────────────────┘
```

- 无边框置顶小条；拖动移动；位置/DPI（per-monitor v2）记忆。
- 右键菜单：设置 / 置顶 / 鼠标穿透 / 靠边隐藏 / 开机自启 / 安装温度服务（仅当未装）/ 退出。
- 每项可独立开关与排序；温度 >85°C 橙色、>95°C 红色。
- 悬停 tooltip：CPU 名/GPU 名、内存已用/总量、网卡名。
- 主题仅深/浅两套 + 字号一档可调——不做主题系统。
- 渲染：Direct2D + DirectWrite，数据不变不重绘；无第三方 GUI 库。

---

## 7. 代码结构（C++20 / CMake）

```
device-monitor/
├── src/
│   ├── app/                      # hwmon.exe（UI 进程）
│   │   ├── main.cpp
│   │   ├── ui/                   # 窗口、D2D 绘制、拖动、DPI、菜单
│   │   ├── collectors/           # 无特权采集（每项一个类，统一 ISource 接口）
│   │   │   ├── cpu_usage.cpp     # GetSystemTimes 差分
│   │   │   ├── mem_usage.cpp     # GlobalMemoryStatusEx
│   │   │   ├── gpu_usage.cpp     # PDH GPU Engine 聚合
│   │   │   └── net_speed.cpp     # GetIfTable2 + GetBestInterface
│   │   └── core/                 # 配置、1s 调度、共享内存客户端
│   ├── svc/                      # hwmon-svc.exe（温度服务）
│   │   ├── service.cpp           # SCM 宿主、失败自动重启
│   │   ├── pawnio_client.cpp     # PawnIO IOCTL 客户端
│   │   ├── temp_cpu_intel.cpp    # MSR 0x1A2/0x1B1 解析（移植自 LHM，注明 MPL）
│   │   ├── temp_cpu_amd.cpp      # SMN 0x59800 + Tctl 偏移表
│   │   ├── temp_gpu_nvml.cpp     # nvml.dll 动态加载
│   │   ├── temp_gpu_adl.cpp      # atiadlxx.dll（M3）
│   │   ├── temp_gpu_l0.cpp       # level_zero.dll（M3，尽力而为）
│   │   └── shm_publisher.cpp
│   └── shared/                   # 共享内存结构、温度值约定（NaN）
├── installer/                    # --install-service / --uninstall-service、PawnIO 引导
└── DESIGN.md
```

---

## 8. 配置

单文件 JSON，默认 `%APPDATA%\hwmon\config.json`；exe 同目录存在 `portable.marker` 时改用便携模式（配置写旁边）。

```json
{
  "interval_ms": 1000,
  "items": ["cpu", "cputemp", "gpu", "gputemp", "ram", "net"],
  "nic": "auto",              // auto = 默认路由网卡，或网卡 GUID
  "theme": "auto",            // dark / light / auto
  "font_scale": 1.0,
  "topmost": true,
  "click_through": false,
  "auto_hide": false,
  "position": [-1, -1],       // -1 = 首次居中偏上
  "temp_warn_c": 85,
  "temp_crit_c": 95
}
```

---

## 9. 安装 / 分发 / 权限流

- 分发：绿色 zip（`hwmon.exe` + `hwmon-svc.exe`），无安装程序。
- 首次运行：普通权限即可用（无温度）。
- 温度一键开通：菜单里"启用温度监控" → UAC 一次 →
  1. 创建 `hwmon-svc` 服务（SYSTEM，自动-延迟启动，失败重启）；
  2. 检测 PawnIO：未装则引导安装官方 `PawnIO_setup.exe`（静默参数待实测，失败弹官网）；
  3. 启动服务，温度立即可用，之后开机无人值守。
- 卸载：`hwmon.exe --uninstall-service`（UAC）删除服务；PawnIO 留给用户自行卸载（它可能被其他软件共用，不代删）。

---

## 10. 性能预算与验证方法

| 项 | 预算 | 验证 |
|---|---|---|
| UI CPU | < 0.3%（1s 采样 + 按需重绘） | 任务管理器/ETW 观察 10 分钟均值 |
| UI 内存 | 私有工作集 < 15MB | `Get-Process` |
| 服务 CPU/内存 | < 0.1% / < 8MB（MSR 读为微秒级） | 同上 |
| 磁盘/网络 | 除配置外 0 写入、0 外联 | Process Monitor / netstat |
| 体积 | exe < 1MB，包 < 1.5MB（PawnIO 按需下载不内置） | — |

与现有软件的量级对比（典型观测值，仅作参考）：TrafficMonitor ~30-60MB；LiteMonitor（.NET+LHM+Updater）~100-200MB。

---

## 11. 风险与对策

1. **PawnIO 模块对新硬件的覆盖**：模块必须官方签名，新平台不在支持列表 → 该项温度缺失显示"—"，功能不崩。Intel（Core 2 起 DTS）与 Zen 系覆盖是官方模块主力，主流机型风险低。
2. **NVIDIA R530+ 的 NVML 管理员门槛**（未能在线复核）：温度调用已全部放服务进程，无论门槛是否存在设计都成立。
3. **AMD Tctl 偏移表维护**：不同 Zen 型号偏移不同，从 LHM（MPL-2.0）移植偏移表并注明来源；未知型号显示 Tctl 原值并 tooltip 标注"可能含偏移"。
4. **Intel 混合架构（E/P 核）**：首版显示 package 温度（MSR 0x1B1），避开逐核口径差异。
5. **任务栏嵌入模式**：Win11 24H2 任务栏对 `SetParent` 黑科技不友好（Explorer 重启/DPI 切换可能吞窗口）→ 只作为实验选项，默认悬浮条。
6. **Intel 核显温度**：多数 iGPU 不暴露传感器 → 明确显示"—"，不做假数据。
7. **杀软误报**：正规 EV 签名驱动 + 不自写驱动，风险远低于 WinRing0 系方案；非零，保留 FAQ 说明。
8. **数值口径**：CPU/GPU 占用与任务管理器可能有 ±1-2% 差异（采样窗口不同），验收标准定为 ±2%（占用）/ ±3°C（温度，对齐 HWiNFO）。

---

## 12. 里程碑

| 阶段 | 内容 | 验收 |
|---|---|---|
| M1 无特权核心版 | 悬浮条 UI + CPU/GPU 占用、内存、网速 | 数值对齐任务管理器 ±2%；内存 <15MB；CPU <0.3%；此时已是 TrafficMonitor 的轻量子集替代品 |
| M2 温度服务 | 服务进程 + PawnIO + Intel/AMD CPU 温度 + NVIDIA GPU 温度 | 与 HWiNFO 对齐 ±3°C；拔掉服务后 UI 优雅降级 |
| M3 完善 | AMD/Intel GPU 温度、实验性任务栏模式、打磨安装引导 | — |

---

## 13. 待拍板的决策点

1. **技术栈**：推荐 C++20 + Win32 + D2D（零运行时、体积最小）；备选 Rust + windows-rs（同样轻，GUI 手写工作量相近）；C# 不推荐（WinForms/WPF 不支持 NativeAOT，带运行时就回不到 <25MB）。
2. **首发范围**：M1 先出无温度版，还是直接做到 M2？
3. **默认 UI 形态**：悬浮条（推荐，稳）还是任务栏模式优先？
4. **开源许可证**：MIT（推荐）？——温度解析有 LHM 移植文件需保留 MPL-2.0 文件级声明。
