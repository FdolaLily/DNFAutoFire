# 项目记忆

DNF 连发工具（DAF，chenyu 魔改版）。当前版本以根目录 `Version` 为准（v0.3.0.x，构建时自动递增）：纯 C++17 / Win32 单 EXE，Direct2D / DirectWrite 自绘界面；同一个 EXE 以 `--service` 运行时是原 DNF Process Manager 的 Windows 服务（已用 C++ 重写并入，`D:\workspace\AutoManagerProcess` 的 .NET 实现不再是发布组成部分）。旧 AHK 实现、Python 原型和 DLL 装载架构已全部移除，只在 Git 历史中保留，不要再恢复或引用。

## 目录

| 路径 | 内容 |
| --- | --- |
| `native/` | 全部源码。服务与配置：`json.*`（JSON 模型）、`win_fs.*`（原子写、配置锁）、`client_config.*` + `config_schema.h`（统一 config.json）、`config_migrate.*`（旧 config.ini / appsettings.json 一次性迁移）、`service_host.*`（`--service` 入口）、`game_monitor.*`、`launcher_monitor.*`、`process_util.*`、`service_control.*`（界面侧 SCM）、`service_log.*`、`client_ui_service.*`（服务管理抽屉与主界面服务状态胶囊、程序版本卡片）、`app_update.*` + `update_dialog.*`（已安装服务时原位更新程序）、`client_ui_picker.*`（进程选择浮层、程序与文件夹选择）、`game_toolbox.*` + `client_ui_toolbox.*`（游戏工具箱）、`client_ui_host.h`（抽屉控件接口）、`ui_motion.h`（过渡动画）、`app_ids.h`（服务名、跨会话事件名）、`version.h`（代码中唯一的版本来源，由 `scripts/bump-version.ps1` 写入，不要手改）。客户端：`client_main.cpp`（入口与模式分派、单实例、自检）、`client_ui.cpp`（交互与布局）、`client_gfx.cpp/.h`（D2D 绘制、SVG 图标、主题色）、`client_config.cpp/.h`（config.json 读写）、`client_input*.{h,cpp}`（奔跑/连招状态机）、`engine.cpp` + `schedule.h` + `win_timer.h`（连发调度引擎）、`client.rc`（图标、字体、版本资源）。架构说明见 `native/README.md`，基准见 `native/BENCHMARK.md` 与 `native/benchmark/` |
| `native/icons/` | `app.ico`（资源 1，窗口/EXE）、`running.ico`（资源 2，托盘运行中）、`stopped.ico`（资源 3，托盘未启动） |
| `native/fonts/` | 内置 Noto Sans SC / JetBrains Mono 子集（SIL OFL 1.1），以 RCDATA 201–207 编入 EXE |
| `tests/` | `json_test.cpp`、`game_toolbox_test.cpp`、`client_config_test.cpp`、`config_migrate_test.cpp`、`service_logic_test.cpp`、`client_input_test.cpp`、`client_ui_test.cpp`、`native_schedule_test.cpp`、`app_update_test.cpp` + `legacy_client.cpp/.rc`（AHK 版替身）、`native_benchmark.cpp`，以及 `build_tools_test.ps1`、`release_security_test.ps1`、`client_singleton_test.ps1` |
| `scripts/` | `build.ps1`（唯一构建入口）、`bump-version.ps1`（版本递增 / 校验）、`publish.ps1`（发布）、`verify-build-tools.ps1`（Zig 哈希固定）、`inspect-native-dependencies.ps1`、`assert-release-security.ps1` + `release-security.json`（已检出样本黑名单）、开发工具 `build-icons.py`、`subset-fonts.py`、`read-huorong-evidence.py` |
| `docs/security/` | 2026-09-24 火绒核查记录与 `evidence/` 原始证据 |
| `docs/操作手册.md` + `docs/images/manual/` | 面向普通玩家的图文操作手册，五张带编号标注的界面截图（1280×800 窗口原尺寸）。界面文字、布局或默认值变化时同步重拍并更新说明，README 顶部链接到此 |
| `dist/` | 发布流程同步的 `DNFAutoFire.exe` 与本地 `config.json`（Git 忽略） |
| `venv/tools/zig-x86_64-windows-0.15.2/` | 便携编译器，哈希固定在 `verify-build-tools.ps1`（Git 忽略） |
| `build/` | 所有中间产物、测试输出、发布报告和旧 EXE 备份，随时可清空（Git 忽略） |

不要在仓库根目录堆放截图、临时脚本、日志或旧版本产物；临时文件放 `build/`，用完即删。

## 构建与测试

- GitHub Actions：`.github/workflows/build-release.yml` 在 main 推送、v* 标签、PR 和手动触发时构建测试；只有推送 v* 标签才创建 Release（标签须与 `Version` 一致），main 推送只上传构建产物。`scripts/install-build-tools.ps1` 下载并校验 Zig，`scripts/package-release.ps1` 校验版本与黑名单并打包。已发布版本不覆盖，版本与发布说明以根目录 `Version` 为准。

- `scripts/build.ps1`：校验 Zig 哈希 → 版本号自动递增（见下方“版本号”）→ `zig rc` 编译 `native/client.rc` → 编译并检查导入依赖与 GUI 子系统 → 运行全部 C++ 回归、成品 `--self-test` / `--ui-self-test`、构建工具与发布黑名单测试、单实例测试。默认产物 `build/release-<版本>/DNFAutoFire.exe`。`-SkipTests` 跳过回归，`-NoVersionBump` 不改版本号，`-Benchmark` 追加调度基准（写入 `build/native-benchmark.csv`）。
- 编译参数固定：`-std=c++17 -O2 -Wall -Wextra -Werror -static -target x86_64-windows-gnu`，链接必须带 `-Wl,--subsystem,windows`（Zig 0.15.2 的 `-mwindows` 无效）。
- 成品只允许导入 Windows 自带 DLL/UCRT（KERNEL32、USER32、WINMM、ADVAPI32、COMCTL32、GDI32、OLE32、SHELL32、D2D1、DWRITE、DWMAPI，以及服务跨会话启动用的 WTSAPI32、USERENV）。NtSetInformationProcess 通过 `GetProcAddress` 取得，不新增 ntdll 导入。新增依赖须同步 `inspect-native-dependencies.ps1` 白名单并说明理由；禁止解释器、释放临时文件/DLL、加壳或混淆。
- 自动回归不注入真实键盘输入；游戏内实效、多职业交互仍需实机验收，基准频率不等于技能释放频率。

## 界面与图标风格

- 仿机械键盘：键帽右上角绿灯表示已开启连发，运行时呼吸闪烁；键帽底部色条表示占用（奔跑蓝、连招橙、剑宗紫），冲突亮红灯。明亮/暗黑两套主题色定义在 `client_gfx.cpp`（`capTop`、`capSide`、`line2`、`led` 等），主题保存在 `settings.theme`。
- 窗口 1280×800 DIP 固定布局；界面文字用内置 Noto Sans SC，数字/键名用 JetBrains Mono。
- 应用图标 = 深色键帽 + 浅色方波（连发时序）字符 + 右上角绿灯；托盘图标去掉字符，绿灯亮 = 运行中，灯灭 = 未启动。颜色取暗黑主题色板，16–32px 用简化几何。修改图标只改 `scripts/build-icons.py` 后重新生成（需 `pip install cairosvg pillow`），不要手工替换 `.ico`；资源编号 1/2/3 被 `client_ui.cpp` 的窗口类与托盘引用。
- 新增界面元素沿用现有 token 与 SVG path 图标，不引入位图资源。
- 状态灯语义统一：绿灯（`led`）= 运行中；黄灯（`ledWarn`）= 需要更新（服务注册在其他位置的 EXE、旧版服务待替换）；红灯（`ledBad`）= 未运行（服务未安装 / 已停止 / 状态读取失败，连发未启动时底栏与键盘指示区的“连发”灯）。服务状态由 `ServicePanel::summarize` 统一给出，主界面状态胶囊与服务抽屉共用 `ServicePanel::toneColor`。
- 服务管理抽屉只从主界面“启动连发”开关左侧的“后台服务”状态胶囊打开，顶栏不再放服务按钮（顶栏只保留工具箱、主题、设置与窗口按钮）。

## 配置与行为约束

- 唯一配置文件为 EXE 同目录的 `config.json`（UTF-8，`version` 字段为架构版本）：`settings`、`profiles`（数组，保持顺序）、`service`。`Store` 缓存解析结果，仅在文件大小或写入时间变化时重读；写入在 `Global\DNFAutoFire.Config.{…}` 锁内先重读再改已知字段，未知字段原样保留，临时文件 + `MoveFileEx` 原子替换。方案、别名、职业、连招、奔跑参数均需保留；修改 400ms 防抖自动保存并即时生效。
- 旧 `config.ini` / `appsettings.json` / `appsettings.Development.json` 由 `migrateLegacyConfig` 在客户端和服务启动时迁移：只导入 `config.json` 中尚缺的部分；先写 `.migrating` 并用真实 `Store` 校验、再原子改名，成功后才删除旧文件；失败时不改动任何文件并在界面 / 日志说明。未知 INI 字段进入 `legacyIni`。不要重新引入 INI 写入或第二个配置文件。
- 单实例使用固定 `Global\DNFAutoFire.Client.{B797BFB2-305A-44DC-9E06-76D6CC424419}` 互斥锁，不得改名；`--service` 模式不取该锁、不提权、不建窗口。重复启动时向已运行实例的主窗口投递注册消息 `kShowRunningMessage`（`client_ui.h`），由其显示主界面并给出“已在运行”提示；找不到窗口时静默退出码 0。
- “运行后自动隐藏到托盘”（`SettingAutoStart`）默认开启：主窗口不显示、直接驻留托盘（不会一闪而过）；首次运行（尚无 config.json 与旧 config.ini）、刚从旧版配置迁移、更新后重新打开或自动启动失败时显示主界面。
- 托盘右键菜单是 `client_ui.cpp` 自绘的弹出窗口（`DAF.Native.TrayMenu`），沿用主题 token，随明亮/暗黑切换；不要改回系统 `TrackPopupMenu`。
- 服务（来自原 AutoManagerProcess，边界保持不变）：服务名 `DNFAutoFire`，LocalSystem、自动启动、异常退出自动重启；安装 / 更新时必须先停止并删除旧服务 `DNFProcessManager`、`AutoManagerProcess`，避免两套自动化同时运行。不得写死游戏安装路径（从运行中的 `DNF.exe` 获取会话）；游戏优先级只允许 Normal / AboveNormal；Kill 列表只结束目标进程本身，不结束进程树；随游戏启动的程序必须经 `WTSQueryUserToken` + `CreateProcessAsUserW` 启动到游戏会话，不得在会话 0 直接启动 GUI；游戏退出后关闭连发时先置位 `Global\DNFAutoFire.Client.Quit.{…}` 让客户端自行释放按键，5 秒后才强制结束；启动器只在同会话运行过游戏并退出满一分钟后、通过全部身份校验才结束，且不得影响下载 / 更新。服务名、跨会话事件名定义在 `app_ids.h`。服务运行时占用 EXE、无法删除 / 移动，资源管理器“文件正在使用”提示按服务显示名称显示占用者，所以显示名称本身写明“删除或移动程序前：打开连发 → 后台服务 → 卸载”，服务说明给出完整步骤与 `sc` 命令；服务每次启动经 `svcctl::refreshLabels` 刷新这两项（文件替换式更新后旧注册也能更新）。修改措辞时同步 README 与操作手册常见问题。
- 游戏工具箱（原 `DNF专用工具箱8.0.bat`，`game_toolbox.*` + `client_ui_toolbox.*`）只在客户端界面中由用户操作，不得放进服务自动执行。备份后缀 `.dnf-toolbox-disabled`、标记 `.blocked-by-dnf-toolbox` 与 8.0 脚本保持一致，禁用 / 恢复必须成对、可重复，并能恢复脚本先前的处理结果；只删除带标记的占位目录，不能把用户原有目录当作占位；文件操作前必须确认 DNF 已关闭；不得处理下载与更新组件（BackgroundDownloader、Tencentdl、TenioDL、TesService、QQDownload、QQMiniDL、DeskUpdate、TXPTOP、TXFTN）。游戏根目录按“config.json 记住的目录 → 运行中的 DNF.exe → DNF 启动器 → 注册表 → 常见安装位置”识别，失败时由用户选择包含 DNF.exe 的目录，结果保存在 `toolbox.gameDirectory`。
- 界面过渡动画统一走 `ui_motion.h` 的 `Motion`（按 key 缓动、首次出现不动画、遵循系统“动画效果”开关）；有动画进行时才以 15ms 帧定时器重绘，静止时停止。淡出 / 滑出中的内容不得接受点击。
- “随游戏启动”列表中指向本 EXE 的条目锁定，界面不可移除。
- 服务日志 `logs\service.log`，128 KB 滚动、最多两个文件；不要改成无限增长。
- 程序更新（`app_update.*`）：双击另一份不同内容的本程序时，若服务注册的 EXE 或正在运行的客户端在别处，先询问再替换；服务管理“程序版本”卡片可“从文件更新…”。目标路径只能来自服务注册信息、运行中的客户端或当前 EXE，不得写死目录；源和目标都必须按版本资源识别为 DNFAutoFire。顺序固定：SCM 停止服务（不得杀服务进程）→ 置位客户端退出事件、5 秒后才强制结束 → 同目录暂存副本、原文件改名、换入，失败恢复原文件 → 先打开新客户端（`--updated <原版本> <pid>`）再重启服务，避免游戏运行时服务再开一个。用户已决定不保留旧版本文件：成功后删除，仍被占用的由新客户端与服务启动时 `cleanupLeftovers` 清理。`DAF_UPDATE_PROMPT=0` 只供自动测试关闭双击提示。
- 旧版组合（AHK 连发 v0.1.3.x + .NET 服务 DNFProcessManager + config.ini / appsettings.json）：按版本名 `DAF连发工具` 与 `AutoHotkey` 主窗口识别 AHK 版，按旧服务注册路径与其 AutoStart 找到目标；AHK 版先 `WM_CLOSE` 再宽限结束（含其 `/Run=` 子进程）；旧版配置只由 `migrateLegacyConfig` 在 `config.json` 校验成功后删除；用户已决定升级后删除旧服务程序、`服务管理.bat`、`DNF专用工具箱8.0.bat`、`appsettings.Development.json`、`logs\auto-manager*.log`，但只在旧服务确认删除后、且 bat 内容匹配（`DNFProcessManager` / `dnf-toolbox-disabled`）时删除，服务程序只认 `DNFProcessManager.exe` / `AutoManagerProcess.exe`，其他文件一律不动。AHK 版运行时新版不得同时启动。
- 自动回归不得安装真实服务或修改真实游戏目录；`service_logic_test` 只用自身子进程副本模拟游戏生命周期。
- 连发默认 7+7ms，范围 1–100ms，低于 7ms 时界面提示“偏快”；一键奔跑搓招保护默认 150ms（下限 140ms），低于 150ms 时界面提示；两/三键 0/8/16ms 相位。所有注入输入按 `LLKHF_INJECTED` 排除；只在 DNF 前台生效；不读写游戏内存、不联网、不记录键盘文本。

## 版本号

- 每次代码更新后版本号自动递增（用户要求）：`build.ps1` 调用 `scripts/bump-version.ps1 -IfSourcesChanged`，编进 EXE 的源码（`native/` 下 .cpp/.h/.rc/.manifest、图标、字体；不含 `version.h` 与 manifest 自身的版本号，文本去掉 CR 后计算）与 `native/version.h` 中 `source-sha256` 指纹不同时，第四位加一并同步写入 `Version` 的 `tag_name`、`native/version.h`、`native/client.manifest`；源码未变不加，编译失败退回原版本号。CI 与 `-NoVersionBump` 只做 `-Check`（三处不一致即失败，指纹过期只警告）。
- 改完代码后必须至少运行一次 `build.ps1`（或 `bump-version.ps1 -IfSourcesChanged`），让版本号和指纹随代码一起提交；不要手改 `version.h`、manifest 或 `tag_name` 中的版本号。升级前三位用 `bump-version.ps1 -Set <a.b.c.d>`。
- `version.h` 是代码中唯一的版本来源：`client.rc` 版本资源、`app_ids.h` 的 `kProductVersion`、`client_ui.cpp` 的 `kVersion`/`kFullVersion` 都取自它；`build.ps1` 输出目录与 `publish.ps1` 默认版本读取 `Version`。
- `Version` 的 `body` 累积自上次发布以来的更新说明，`README.md` 更新日志按发布版本记录；发布时推送与 `Version` 一致的 v* 标签。`README.md` 与 `Version` 使用 CRLF 换行（脚本只改 `tag_name`，保留原有换行）。

## 安全软件

`release-security.json` 列出的哈希禁止再次发布（`publish.ps1` 在动任何文件前检查）。不得仅凭换架构或源码审查宣称火绒误报；每个发布成品单独留存哈希与检测结果，没有新增实时日志不等于主动扫描通过。

## EXE 编译发布规则（必须遵守）

每次编译并发布本项目的 EXE 成品时，必须按以下顺序完成（`scripts/publish.ps1 -Source <已验收EXE> -ExpectedHash <SHA256>` 以管理员身份执行即覆盖这些步骤）：

1. 在替换成品前，先检查本项目的 EXE 是否已有运行实例（包括以 `--service` 运行的 `DNFAutoFire` 服务：注册路径指向目标 EXE 时必须经服务管理器停止，替换后再启动并确认运行，`publish.ps1` 已覆盖）；如果有，停止对应进程并确认已退出。根据可执行文件路径确认目标，避免停止其他程序。
2. 将本次编译成功的 EXE 替换到 `E:\autokill` 下本项目现有 EXE 的路径，沿用现有文件名；没有运行实例时也必须完成替换。
3. 如果替换前存在运行实例，替换成功后必须从 `E:\autokill` 下的目标路径启动新的 EXE，并确认新进程已正常运行，再继续后续步骤。
4. 然后将同一份 EXE 成品输出到 `D:\workspace\AutoManagerProcess`，更新该目录下对应的 EXE。
5. 确认两个目录中的 EXE 均与本次编译成品一致后，在 `D:\workspace\AutoManagerProcess` 项目目录创建本地 Git 提交，提交本次成品更新及相关改动，不混入无关修改。

上述替换、恢复运行（替换前存在运行实例时）、输出和本地提交均属于发布流程的必要步骤。任一步骤失败时，应说明未完成的步骤，不得宣称发布完成。
