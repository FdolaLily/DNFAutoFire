# Windows / DNF 原生连发引擎

## 审查结论

旧版每个按键创建一个 AHK 进程，另外启动修饰键释放和职业功能进程。每个脉冲都读取多项 INI 配置，按 `Down → Sleep(10) → Up → Sleep(10)` 执行；处理与唤醒开销累计到周期中。仅按键数量变化时校准相位，同数量换键、GUI 名与原始键名不一致也会失配。主循环只在进入持续按键循环前检查游戏窗口。

多个 AHK 键盘 hook 还会使 AHK 的 `SendInput` 自动退回 `SendEvent`；普通连发、原始阻塞热键、职业进程和修饰键释放进程同时发送同一键，无法保证单一输出波形。参见 [AHK v1 SendInput](https://www.autohotkey.com/docs/v1/lib/Send.htm#SendInputDetail)。

## 当前实现

- v0.1.5.0 的界面、配置、奔跑、连招和连发引擎均为 C++17 / Win32，直接静态编入同一个 EXE。旧 AHK 实现及其 DLL 装载、测试已在 v0.1.6.0 从工作区移除，保留在 Git 提交“snapshot: 清理前的完整工作区”中。`engine.cpp` 仍保留非 `DAF_STATIC_ENGINE` 的导出分支，但构建脚本不再生成 DLL。
- 一个 native 消息线程接收低级键盘 hook 与前台窗口事件。硬件状态按扫描码配对，所有 `LLKHF_INJECTED` 输入都排除；普通连发键在 DNF 前台只放行首次硬件 Down（与旧 AHK `$*scXX` 阻塞热键一致，聊天框只得到一个字符），系统重复 Down 被吞掉，连发脉冲由引擎唯一发送。
- 一个 native 调度线程处理所有普通键和职业触发，使用预先构造的 `vkFF + 扫描码` `INPUT`（旧 AHK `vkFFscXX`：游戏按扫描码识别，VK 0xFF 不会被 TranslateMessage 转成文字，游戏内打字不触发连发；连招步骤同样如此，奔跑方向键仍用扫描码映射的真实 VK）、QPC 单调微秒时间、固定数组，无配置读取、热路径分配或 AHK 解释执行。
- 高精度单次 waitable timer 等待下一截止点；按键变化和停止事件可立即打断。前台事件之外保留 20ms 看门狗，每次 Down 前再次核对前台窗口与输入请求。只匹配 `DNF.exe` 和三个原有 DNF 类名。
- 默认普通优先级、无忙等待、不绑定 CPU。不使用实时优先级、全局电源设置或驱动。
- 正常按绝对时间网格发送。迟到时维持至少半个设定 Down/Up 窗口，必要时跳周期；不补发积压事件。同数量换成员也重新定相，正在 Down 的键不会被相位调整延长。
- 默认 10+10ms 保留两/三键 0/8/16ms 相位；其他周期同比缩放，四键以上按周期的 80% 范围均分。变化时可能出现过渡长间隔，这是重新定相的一部分。
- 职业触发与普通连发同一输出键合并；剑宗保留首次硬件 Down，吞系统重复 Down，配置延迟后进入脉冲。战法非托管发射键保留实体事件直通，包括原先按住发射键再关 Num Lock 的操作。职业脉冲不再附带旧 AHK `Sleep,1` 的约 15.6ms 空窗，需游戏内复核。
- 奔跑与连招由另一个原生 hook/工作线程对执行。hook 只更新键态与有界事件队列，所有等待和发送交接在工作线程进行。连招暂停该键普通连发并等待其 Up，再按原有步骤间隔执行，完成后恢复。
- 奔跑/连招是可用虚拟时钟测试的状态机；首次确认使用用户设置的搓招保护，保留 350ms 方向序列保护、90ms 变向确认、斜轴继承和技能释放恢复。双方向起跑时，第二方向若在首方向确认或双击期间加入，会要求首方向重新完成连续的同方向双击，再将两轴标记为奔跑；交替斜向变向会清除旧的奔跑标记，按实际发出的方向按下顺序确认双击，水平反向优先完成自身双击。已确认奔跑后加入正交方向仍直接继承。Combo 触发键和奔跑方向键从普通连发候选中剔除。
- 所有注入输入按 `LLKHF_INJECTED` 排除，不依赖伪装输入标记。仅保存当前物理键态，不记录键盘文本，不联网，不读取或修改游戏内存。
- 停止时先结束奔跑/连招并释放其拥有的 Down，再停止连发引擎；线程未完成退出或 Up 发送失败时保留状态并报告，禁止悄悄重建控制器。焦点切换取消旧工作，离开 DNF 只允许补释放已拥有的按键。

只有普通托管键的硬件输入被抑制；职业专用输出的实体操作与方向键仍直通各自机制。切换/启动时已经按住的键，原生状态从下一次硬件事件开始建立；测试和操作时应先松开再启动或切方案。

## 服务模式与模块

v0.3.0.0 起原 .NET 服务 DNF Process Manager（`Worker.cs`、`LauncherMonitor.cs`、`PInvoke.cs`）以 C++ 重写并入同一个 EXE。`client_main.cpp` 按命令行分派：无参数为客户端界面，`--service` 为 Windows 服务（不创建窗口、不取单实例锁、不提权），`--self-test` / `--ui-self-test` 不变；`--replace "<已安装 EXE>"` 是确认更新后提权执行的替换，`--updated <原版本> <pid>` 是替换后重新打开的客户端（先等该 pid 退出再取单实例锁）。

| 模块 | 职责 |
| --- | --- |
| `json.h/.cpp` | 无依赖 JSON 文档模型：严格解析（可选注释 / 尾逗号，仅用于旧 appsettings.json）、有序对象、UTF-8 ↔ UTF-16、带行列号的错误 |
| `win_fs.h/.cpp` | 有界读取、临时文件 + `MoveFileEx` 原子替换、元数据变化戳、`Global\` 配置锁（SYSTEM / Administrators 共享的 SDDL） |
| `client_config.*`、`config_schema.h` | 统一 `config.json` 的 `Store`：文档缓存，只有文件大小或写入时间变化才重新解析；写入先重读再修改已知字段，未知字段原样保留 |
| `config_migrate.*` | 旧 `config.ini`（UTF-16/UTF-8/ANSI）与 `appsettings.json`（.NET，大小写不敏感键、注释）一次性迁移：写入 `.migrating`、用真实加载器校验、原子改名，然后才删除旧文件；未知 INI 字段进入 `legacyIni` |
| `service_host.*` | SCM 入口与服务线程：每秒检查配置变化（热重载），按最近截止时间与游戏进程句柄等待，无忙等 |
| `game_monitor.*` | 游戏生命周期：低频轮询发现游戏 → 随游戏启动 → 延迟后结束 / 限制进程 → 维持优先级 → 句柄信号感知退出（打不开句柄时每秒轮询）→ 游戏退出后关闭 |
| `launcher_monitor.*` | DNF 启动器：一分钟延迟关闭、五项身份复核、`AppData\config.json` 退出行为的最小文本替换（`ReplaceFile` 保留 ACL） |
| `process_util.*` | Toolhelp 快照（每轮最多一次，监视器共享）、进程名规范化、`WTSQueryUserToken` + 关联提升令牌 + `CreateProcessAsUserW` 跨会话启动、亲和性与 I/O 优先级 |
| `service_control.*` | 界面侧 SCM 操作：安装 / 更新（先停删旧 `DNFProcessManager`、`AutoManagerProcess`）、失败自动重启、启动 / 停止 / 重启 / 卸载、查询是否指向当前 EXE |
| `service_log.*` | `logs\service.log`，128 KB 滚动为 `.1`，共享读取 |
| `client_ui_picker.*` | 运行中进程选择浮层（Toolhelp 快照按名去重、完整路径、会话 0 标“后台”、搜索框实时过滤、回车手动添加）与 `IFileOpenDialog` 程序选择（经已导入的 ole32 / shell32，无新增 DLL），本程序目录内的路径转为相对路径 |
| `game_toolbox.*` | 原工具箱 8.0 的本机实现：17 个组件的禁用 / 恢复（`.dnf-toolbox-disabled` 备份与 `.blocked-by-dnf-toolbox` 标记与脚本一致，只删除带标记的占位）、日志缓存清理（保留 DNF.cfg）、用户缓存重置、游戏根目录识别（记住的目录 → 运行中 DNF → 启动器 → 注册表 → 常见安装位置） |
| `client_ui_toolbox.*` | “游戏工具箱”抽屉：目录识别 / 手动选择（`IFileOpenDialog` 选文件夹）、组件状态列表、二次确认的清理与修复，文件操作在后台线程执行 |
| `client_ui_host.h`、`ui_motion.h` | 抽屉模块共用的控件接口；按 key 缓动的过渡动画时钟（ease-out、首次出现不动画、遵循系统动画开关），有动画时才以 15ms 帧定时器重绘 |
| `app_update.*` | 原位更新：按版本资源（`LoadLibraryEx` 数据文件方式读取 `VS_VERSIONINFO`，不新增 VERSION.dll 导入）识别本程序与版本；目标取自服务注册的 EXE 或正在运行的客户端窗口，不写死路径；替换顺序为 SCM 停止服务 → 置位退出事件让客户端释放按键（5 秒后才 `TerminateProcess`）→ 同目录暂存副本 → 原文件改名（运行中的 EXE 也允许）→ 换入 → 打开新客户端 → `svcctl::install` 重启服务；失败时恢复原文件，成功后删除旧文件（仍被本进程占用时由新客户端 / 服务启动时清理） |
| （旧版组合） | `app_update` 同时识别 AHK 时代的连发（版本名 `DAF连发工具`，隐藏主窗口类 `AutoHotkey`）和旧服务 DNFProcessManager / AutoManagerProcess：目标取自旧服务 `appsettings.json` 的 AutoStart（默认同目录 `DNFAutoFire.exe`）或运行中的 AHK 版；替换前经 SCM 停止旧服务、向 AHK 主窗口发 `WM_CLOSE`（宽限后结束同映像的全部进程）；替换后 `svcctl::install` 删除旧服务，确认删除后 `removeLegacyFiles` 只删除已知的服务程序、内容匹配的两个 bat、`appsettings.Development.json`（`appsettings.json` 已迁移后）和 `logs\auto-manager*.log`；旧服务在其他目录时 `adoptLegacySettings` 把其设置（AutoStart 转绝对路径）移到连发旁边供迁移 |
| `update_dialog.*` | 双击新版本时的系统任务对话框：“更新已安装的程序 / 暂不更新”（降级默认暂不更新），以及替换过程的进度与结果 |
| `client_ui_service.*` | “服务管理”抽屉，以及主界面“启动连发”左侧的后台服务状态胶囊（`ServicePanel::summarize` 给出状态文字与绿 / 黄 / 红灯，点击打开抽屉）；通过 `UiHost` 接口复用 `client_ui.cpp` 的开关、步进器、文本框和命中测试，服务操作在后台线程执行并以 `kServiceDoneMessage` 回到界面线程 |

跨会话协作只用两个命名对象：服务启动客户端时用登录用户令牌（管理员在 UAC 下取关联的提升令牌，因此不弹 UAC；非管理员回退到普通令牌，由客户端自行 runas）；游戏退出时服务置位 `Global\DNFAutoFire.Client.Quit.{…}`，客户端监听线程向界面线程投递 `InputCommand::Exit`，走正常退出（停止连发、补释放按键、移除托盘），5 秒未退出才 `TerminateProcess`。服务自身与客户端同名，判断“是否已在运行”和“要关闭的进程”时始终排除服务进程本身。

## 界面

v0.1.6.0 起界面由 `client_ui.cpp`（交互与布局）和 `client_gfx.cpp`（Direct2D / DirectWrite 绘制、图标、主题色）自绘，窗口为 1280×800 DIP 固定布局，按系统 DPI 缩放并在工作区放不下时等比缩小。改名和连招间隔两处文本输入仍使用原生 EDIT 控件。

- 修改通过 400ms 防抖自动写入 `config.json`（方案与设置一次事务），随后经 `settingsChanged` 回调让运行时按当前启停状态重新应用；切换方案、快速切换、隐藏窗口和退出前会立即提交。
- 主题保存在 `settings.theme`（`dark` | `light`），只写配置，不触发运行时重新应用。
- 方案重命名使用 `Store::renameProfile` 原位改写 `name`，保留方案顺序与未知字段。
- “服务管理”抽屉的修改单独防抖保存到 `service` 节，不会重启连发引擎；服务端检测到文件变化后约 1 秒内生效。
- 字体见 [fonts/README.md](fonts/README.md)：EXE 内的 RCDATA 经 `IDWriteFactory5` 内存字体集注册（Windows 10 1703+）；不可用时回退到 Microsoft YaHei UI / Consolas。GDI 私有注册只服务于上述 EDIT 控件。
- 图标以 SVG path 子集写在代码中，圆弧在解析时转为贝塞尔曲线，不依赖各实现的 ArcSegment 差异。
- 程序与托盘图标在 `icons/`：`app.ico`（资源 1，深色键帽 + 方波字符 + 右上角绿灯）、`running.ico`（资源 2，托盘运行中，绿灯亮）、`stopped.ico`（资源 3，托盘未启动，灯灭）。颜色取自暗黑主题色板，由 `scripts/build-icons.py` 生成，16–32px 使用单独的简化几何。

## 时间参数与有效频率

主界面“连发时序”卡片针对当前方案保存 `downMs` / `upMs`，新方案默认 7ms；低于 7ms 时数值与标签变为提示色并在底栏说明漏键风险。一键奔跑“搓招保护”默认 150ms，低于 150ms 同样提示。连发按下/抬起和奔跑搓招保护/双击间隔/按键脉冲只要求正整数毫秒，不按建议值限制范围；配置保留显式填写的值（包括 180/200/350ms），仅缺省字段使用默认值。存储使用 unsigned，毫秒到引擎微秒的换算使用 64 位，步进按钮防止整数溢出。修改自动保存，运行中立即按新时序重新应用。

理论输入频率为 `1000 / (DownMs + UpMs)`，不是技能释放率。Windows 不是实时系统；休眠、负载、Hook 链和 DNF 自身的输入采样/技能规则仍可造成长尾。半窗口保护也意味着严重迟到时实际频率下降。不能承诺“绝对等间隔”或“500 次技能/秒”。

当前测量见 [BENCHMARK.md](BENCHMARK.md)：纯调度默认 10+10ms 约 49.99Hz，2+2ms 约 250Hz，1+1ms 出现跳周期。继续保留已有游戏使用过的 10+10ms 默认值；实际更快配置应在同帧率、同技能、同持续时间下比较游戏成功释放次数。

## 构建与单 EXE 部署

成品支持 **Windows 10/11 x64**，无需安装 AutoHotkey、Python、.NET、Zig 或 VC++ 运行库。C++ 运行时静态链接，仅使用 Windows 自带 DLL/UCRT（界面另用系统自带的 d2d1、dwrite、dwmapi；服务跨会话启动另用 wtsapi32、userenv）。无脚本解释器、文件解包、临时 DLL 装载、加壳或混淆。界面通过原生 Win32 控件和托盘提供全部设置，旧 `config.ini` / `appsettings.json` 首次启动时迁移为 `config.json`，未知字段保留。

这项加固修复本地 DLL 替换风险，不代表安全软件已经确认旧样本误报。旧版火绒检出证据见 [核查记录](../docs/security/HUORONG-20260924.md)，每次需对最终成品分别验证；不得把没有新增日志等同于完成主动扫描。

v0.1.4.1 起，客户端用固定的 `Global` 命名互斥对象保证机器范围唯一，不使用路径或版本作为标识。提权前只读检查已有实例，提权后原子创建并持有对象到进程退出。重复启动时通知已运行实例显示主界面并提示“已在运行”，自身以退出码 0 结束（找不到窗口时静默退出）。旧版不认识此锁，升级部署会先按已确认路径停止旧副本。`--self-test` 是零输入验收入口，不启动客户端，因此与运行中的客户端隔离。

开发者需要便携构建工具（客户端不需要）：

1. 从 [Zig 官方下载](https://ziglang.org/download/0.15.2/) 获取 `zig-x86_64-windows-0.15.2.zip`，放入 `venv/tools` 后解压。官方 SHA-256 为 `3a0ed1e8799a2f8ce2a6e6290a9ff22e6906f8227865911fb7ddedc3cc14cb0c`。
2. 执行 `scripts/build.ps1`（`-Benchmark` 追加调度基准，`-SkipTests` 跳过回归）。校验编译器哈希、编译原生 EXE、检查系统导入依赖及 GUI 子系统、运行配置/输入/调度回归和成品零输入自检。构建开始前 `scripts/bump-version.ps1 -IfSourcesChanged` 检查源码指纹，源码有变化时把版本第四位加一（`native/version.h`、`client.manifest`、根目录 `Version` 同步；`-NoVersionBump` 跳过，编译失败退回原版本号）。默认产物为 `build/release-<版本>/DNFAutoFire.exe`，验证完成前不覆盖已安装文件。使用显式链接参数 `-Wl,--subsystem,windows`；Zig 0.15.2 的 `-mwindows` 不会实际设置 PE 子系统。
3. 火绒保持启用，对该确切成品留存哈希和检测结果。没有新增实时日志不等于主动扫描通过。
4. 执行 `scripts/publish.ps1 -Source <已验收EXE> -ExpectedHash <SHA256>`；按根目录 AGENTS.md 完成停止旧进程、替换 E 盘、按需恢复运行、同步 dist 和 AutoManagerProcess、哈希一致、本地提交。

## 验证边界

`tests/native_schedule_test.cpp` 覆盖 163 万条确定性断言；`client_input_test.cpp` 验证虚拟时钟下的奔跑、连招、焦点丢失和发送失败；`client_config_test.cpp` 验证 config.json 读写、未知字段保留、按键别名和规则 ABI；`json_test.cpp` 验证解析 / 序列化边界；`config_migrate_test.cpp` 用真实格式的 UTF-16 INI 与带注释的 appsettings.json 验证迁移、删除与失败回滚；`service_logic_test.cpp` 验证进程名、路径、启动器规则、服务命令识别、日志滚动，并用本测试程序的子进程副本模拟游戏、待结束进程与随行程序完成一次完整生命周期（会话 0 的 CI 环境跳过该段）。成品 `--self-test` 验证引擎和控制器零输入生命周期，`--ui-self-test` 额外创建隐藏界面但不改用户配置；`client_singleton_test.ps1` 验证跨目录单实例（设置 `DAF_UPDATE_PROMPT=0`，避免开发机已装服务时停在更新提示）；`app_update_test.cpp`（配合 `legacy_client.exe`：旧版版本名 + 隐藏 `AutoHotkey` 窗口的替身）用带客户端版本资源的测试 EXE 副本验证旧版识别、AHK 版经 `WM_CLOSE` 自行退出或超时被结束、旧版文件的选择性删除、异目录旧服务设置迁移，以及版本解析与识别、目标选择、拒绝非本程序文件、运行中副本被结束后原位替换、不留旧文件、重新打开参数，以及运行中程序替换自身后的清理。

自动回归不注入真实键盘输入。新架构的游戏实效、多职业交互和硬件事件链还需要实机验收，纯调度基准不能替代它们。

官方依据：[QPC](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps)、[高精度 waitable timer](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw)、[SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)、[timeBeginPeriod 的进程范围与遮挡限制](https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod)。
