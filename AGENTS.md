# 项目记忆

DNF 连发工具（DAF，chenyu 魔改版）。当前版本 v0.2.0.0：纯 C++17 / Win32 单 EXE，Direct2D / DirectWrite 自绘界面。旧 AHK 实现、Python 原型和 DLL 装载架构已全部移除，只在 Git 历史中保留，不要再恢复或引用。

## 目录

| 路径 | 内容 |
| --- | --- |
| `native/` | 全部客户端源码：`client_main.cpp`（入口、单实例、自检）、`client_ui.cpp`（交互与布局）、`client_gfx.cpp/.h`（D2D 绘制、SVG 图标、主题色）、`client_config.cpp/.h`（config.ini 读写）、`client_input*.{h,cpp}`（奔跑/连招状态机）、`engine.cpp` + `schedule.h` + `win_timer.h`（连发调度引擎）、`client.rc`（图标、字体、版本资源）。架构说明见 `native/README.md`，基准见 `native/BENCHMARK.md` 与 `native/benchmark/` |
| `native/icons/` | `app.ico`（资源 1，窗口/EXE）、`running.ico`（资源 2，托盘运行中）、`stopped.ico`（资源 3，托盘未启动） |
| `native/fonts/` | 内置 Noto Sans SC / JetBrains Mono 子集（SIL OFL 1.1），以 RCDATA 201–207 编入 EXE |
| `tests/` | `client_config_test.cpp`、`client_input_test.cpp`、`client_ui_test.cpp`、`native_schedule_test.cpp`、`native_benchmark.cpp`，以及 `build_tools_test.ps1`、`release_security_test.ps1`、`client_singleton_test.ps1` |
| `scripts/` | `build.ps1`（唯一构建入口）、`publish.ps1`（发布）、`verify-build-tools.ps1`（Zig 哈希固定）、`inspect-native-dependencies.ps1`、`assert-release-security.ps1` + `release-security.json`（已检出样本黑名单）、开发工具 `build-icons.py`、`subset-fonts.py`、`read-huorong-evidence.py` |
| `docs/security/` | 2026-09-24 火绒核查记录与 `evidence/` 原始证据 |
| `dist/` | 发布流程同步的 `DNFAutoFire.exe` 与本地 `config.ini`（Git 忽略） |
| `venv/tools/zig-x86_64-windows-0.15.2/` | 便携编译器，哈希固定在 `verify-build-tools.ps1`（Git 忽略） |
| `build/` | 所有中间产物、测试输出、发布报告和旧 EXE 备份，随时可清空（Git 忽略） |

不要在仓库根目录堆放截图、临时脚本、日志或旧版本产物；临时文件放 `build/`，用完即删。

## 构建与测试

- `scripts/build.ps1`：校验 Zig 哈希 → `zig rc` 编译 `native/client.rc` → 编译并检查导入依赖与 GUI 子系统 → 运行全部 C++ 回归、成品 `--self-test` / `--ui-self-test`、构建工具与发布黑名单测试、单实例测试。默认产物 `build/release-<版本>/DNFAutoFire.exe`。`-SkipTests` 跳过回归，`-Benchmark` 追加调度基准（写入 `build/native-benchmark.csv`）。
- 编译参数固定：`-std=c++17 -O2 -Wall -Wextra -Werror -static -target x86_64-windows-gnu`，链接必须带 `-Wl,--subsystem,windows`（Zig 0.15.2 的 `-mwindows` 无效）。
- 成品只允许导入 Windows 自带 DLL/UCRT（KERNEL32、USER32、WINMM、ADVAPI32、COMCTL32、GDI32、OLE32、SHELL32、D2D1、DWRITE、DWMAPI）。新增依赖须同步 `inspect-native-dependencies.ps1` 白名单并说明理由；禁止解释器、释放临时文件/DLL、加壳或混淆。
- 自动回归不注入真实键盘输入；游戏内实效、多职业交互仍需实机验收，基准频率不等于技能释放频率。

## 界面与图标风格

- 仿机械键盘：键帽右上角绿灯表示已开启连发，运行时呼吸闪烁；键帽底部色条表示占用（奔跑蓝、连招橙、剑宗紫），冲突亮红灯。明亮/暗黑两套主题色定义在 `client_gfx.cpp`（`capTop`、`capSide`、`line2`、`led` 等），主题保存在 `[设置] SettingTheme`。
- 窗口 1280×800 DIP 固定布局；界面文字用内置 Noto Sans SC，数字/键名用 JetBrains Mono。
- 应用图标 = 深色键帽 + 浅色方波（连发时序）字符 + 右上角绿灯；托盘图标去掉字符，绿灯亮 = 运行中，灯灭 = 未启动。颜色取暗黑主题色板，16–32px 用简化几何。修改图标只改 `scripts/build-icons.py` 后重新生成（需 `pip install cairosvg pillow`），不要手工替换 `.ico`；资源编号 1/2/3 被 `client_ui.cpp` 的窗口类与托盘引用。
- 新增界面元素沿用现有 token 与 SVG path 图标，不引入位图资源。

## 配置与行为约束

- 继续兼容原 `config.ini`（EXE 同目录）：方案、别名、职业、连招、奔跑参数、未知字段均需保留；修改 400ms 防抖自动保存并即时生效。
- 单实例使用固定 `Global\DNFAutoFire.Client.{B797BFB2-305A-44DC-9E06-76D6CC424419}` 互斥锁，不得改名。重复启动时向已运行实例的主窗口投递注册消息 `kShowRunningMessage`（`client_ui.h`），由其显示主界面并给出“已在运行”提示；找不到窗口时静默退出码 0。
- “打开后直接开始连发，窗口隐藏到托盘”（`SettingAutoStart`）默认开启：主窗口不显示、直接驻留托盘（不会一闪而过）；首次运行（尚无 config.ini）或自动启动失败时显示主界面。
- 托盘右键菜单是 `client_ui.cpp` 自绘的弹出窗口（`DAF.Native.TrayMenu`），沿用主题 token，随明亮/暗黑切换；不要改回系统 `TrackPopupMenu`。
- 连发默认 7+7ms，范围 1–100ms，低于 7ms 时界面提示“偏快”；一键奔跑搓招保护默认 150ms（下限 140ms），低于 150ms 时界面提示；两/三键 0/8/16ms 相位。所有注入输入按 `LLKHF_INJECTED` 排除；只在 DNF 前台生效；不读写游戏内存、不联网、不记录键盘文本。

## 版本更新清单

升级版本时同时修改：`native/client.rc`（FILEVERSION/PRODUCTVERSION 与字符串）、`native/client.manifest`（assemblyIdentity version）、`native/client_ui.cpp` 的 `kVersion`/`kFullVersion`、`scripts/build.ps1` 默认输出目录、`scripts/publish.ps1` 默认 `$Version`、根目录 `Version`（发布说明 JSON）和 `README.md` 更新日志。`README.md` 与 `Version` 使用 CRLF 换行。

## 安全软件

`release-security.json` 列出的哈希禁止再次发布（`publish.ps1` 在动任何文件前检查）。不得仅凭换架构或源码审查宣称火绒误报；每个发布成品单独留存哈希与检测结果，没有新增实时日志不等于主动扫描通过。

## EXE 编译发布规则（必须遵守）

每次编译并发布本项目的 EXE 成品时，必须按以下顺序完成（`scripts/publish.ps1 -Source <已验收EXE> -ExpectedHash <SHA256>` 以管理员身份执行即覆盖这些步骤）：

1. 在替换成品前，先检查本项目的 EXE 是否已有运行实例；如果有，停止对应进程并确认已退出。根据可执行文件路径确认目标，避免停止其他程序。
2. 将本次编译成功的 EXE 替换到 `E:\autokill` 下本项目现有 EXE 的路径，沿用现有文件名；没有运行实例时也必须完成替换。
3. 如果替换前存在运行实例，替换成功后必须从 `E:\autokill` 下的目标路径启动新的 EXE，并确认新进程已正常运行，再继续后续步骤。
4. 然后将同一份 EXE 成品输出到 `D:\workspace\AutoManagerProcess`，更新该目录下对应的 EXE。
5. 确认两个目录中的 EXE 均与本次编译成品一致后，在 `D:\workspace\AutoManagerProcess` 项目目录创建本地 Git 提交，提交本次成品更新及相关改动，不混入无关修改。

上述替换、恢复运行（替换前存在运行实例时）、输出和本地提交均属于发布流程的必要步骤。任一步骤失败时，应说明未完成的步骤，不得宣称发布完成。
