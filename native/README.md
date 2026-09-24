# Windows / DNF 原生连发引擎

## 审查结论

旧版每个按键创建一个 AHK 进程，另外启动修饰键释放和职业功能进程。每个脉冲都读取多项 INI 配置，按 `Down → Sleep(10) → Up → Sleep(10)` 执行；处理与唤醒开销累计到周期中。仅按键数量变化时校准相位，同数量换键、GUI 名与原始键名不一致也会失配。主循环只在进入持续按键循环前检查游戏窗口。

多个 AHK 键盘 hook 还会使 AHK 的 `SendInput` 自动退回 `SendEvent`；普通连发、原始阻塞热键、职业进程和修饰键释放进程同时发送同一键，无法保证单一输出波形。参见 [AHK v1 SendInput](https://www.autohotkey.com/docs/v1/lib/Send.htm#SendInputDetail)。

## 当前实现

- v0.1.5.0 的界面、配置、奔跑、连招和连发引擎均为 C++17 / Win32，直接静态编入同一个 EXE。旧 AHK 实现及其 DLL 装载、测试已在 v0.1.6.0 从工作区移除，保留在 Git 提交“snapshot: 清理前的完整工作区”中。`engine.cpp` 仍保留非 `DAF_STATIC_ENGINE` 的导出分支，但构建脚本不再生成 DLL。
- 一个 native 消息线程接收低级键盘 hook 与前台窗口事件。硬件状态按扫描码配对，所有 `LLKHF_INJECTED` 输入都排除；普通连发键在 DNF 前台被吞掉，由引擎唯一发送。
- 一个 native 调度线程处理所有普通键和职业触发，使用预先构造的扫描码 `INPUT`、QPC 单调微秒时间、固定数组，无配置读取、热路径分配或 AHK 解释执行。
- 高精度单次 waitable timer 等待下一截止点；按键变化和停止事件可立即打断。前台事件之外保留 20ms 看门狗，每次 Down 前再次核对前台窗口与输入请求。只匹配 `DNF.exe` 和三个原有 DNF 类名。
- 默认普通优先级、无忙等待、不绑定 CPU。不使用实时优先级、全局电源设置或驱动。
- 正常按绝对时间网格发送。迟到时维持至少半个设定 Down/Up 窗口，必要时跳周期；不补发积压事件。同数量换成员也重新定相，正在 Down 的键不会被相位调整延长。
- 默认 10+10ms 保留两/三键 0/8/16ms 相位；其他周期同比缩放，四键以上按周期的 80% 范围均分。变化时可能出现过渡长间隔，这是重新定相的一部分。
- 职业触发与普通连发同一输出键合并；剑宗保留首次硬件 Down，吞系统重复 Down，配置延迟后进入脉冲。战法非托管发射键保留实体事件直通，包括原先按住发射键再关 Num Lock 的操作。职业脉冲不再附带旧 AHK `Sleep,1` 的约 15.6ms 空窗，需游戏内复核。
- 奔跑与连招由另一个原生 hook/工作线程对执行。hook 只更新键态与有界事件队列，所有等待和发送交接在工作线程进行。连招暂停该键普通连发并等待其 Up，再按原有步骤间隔执行，完成后恢复。
- 奔跑/连招是可用虚拟时钟测试的状态机；保留 140ms 首次确认、350ms 搓招保护、90ms 变向确认、斜轴继承和技能释放恢复。Combo 触发键和奔跑方向键从普通连发候选中剔除。
- 所有注入输入按 `LLKHF_INJECTED` 排除，不依赖伪装输入标记。仅保存当前物理键态，不记录键盘文本，不联网，不读取或修改游戏内存。
- 停止时先结束奔跑/连招并释放其拥有的 Down，再停止连发引擎；线程未完成退出或 Up 发送失败时保留状态并报告，禁止悄悄重建控制器。焦点切换取消旧工作，离开 DNF 只允许补释放已拥有的按键。

只有普通托管键的硬件输入被抑制；职业专用输出的实体操作与方向键仍直通各自机制。切换/启动时已经按住的键，原生状态从下一次硬件事件开始建立；测试和操作时应先松开再启动或切方案。

## 界面

v0.1.6.0 起界面由 `client_ui.cpp`（交互与布局）和 `client_gfx.cpp`（Direct2D / DirectWrite 绘制、图标、主题色）自绘，窗口为 1280×800 DIP 固定布局，按系统 DPI 缩放并在工作区放不下时等比缩小。改名和连招间隔两处文本输入仍使用原生 EDIT 控件。

- 修改通过 400ms 防抖自动写入 `config.ini`，随后经 `settingsChanged` 回调让运行时按当前启停状态重新应用；切换方案、快速切换、隐藏窗口和退出前会立即提交。
- 主题保存在 `[设置] SettingTheme=dark|light`，只写配置，不触发运行时重新应用。
- 方案重命名使用 `Store::renameProfile` 原位改写节名，保留方案顺序与未知字段。
- 字体见 [fonts/README.md](fonts/README.md)：EXE 内的 RCDATA 经 `IDWriteFactory5` 内存字体集注册（Windows 10 1703+）；不可用时回退到 Microsoft YaHei UI / Consolas。GDI 私有注册只服务于上述 EDIT 控件。
- 图标以 SVG path 子集写在代码中，圆弧在解析时转为贝塞尔曲线，不依赖各实现的 ArcSegment 差异。
- 程序与托盘图标在 `icons/`：`app.ico`（资源 1，深色键帽 + 方波字符 + 右上角绿灯）、`running.ico`（资源 2，托盘运行中，绿灯亮）、`stopped.ico`（资源 3，托盘未启动，灯灭）。颜色取自暗黑主题色板，由 `scripts/build-icons.py` 生成，16–32px 使用单独的简化几何。

## 时间参数与有效频率

主界面“连发时序”卡片针对当前方案保存 `AutoFireDownMs` / `AutoFireUpMs`，每项 1–100ms，默认 10ms。修改自动保存，运行中立即按新时序重新应用。

理论输入频率为 `1000 / (DownMs + UpMs)`，不是技能释放率。Windows 不是实时系统；休眠、负载、Hook 链和 DNF 自身的输入采样/技能规则仍可造成长尾。半窗口保护也意味着严重迟到时实际频率下降。不能承诺“绝对等间隔”或“500 次技能/秒”。

当前测量见 [BENCHMARK.md](BENCHMARK.md)：纯调度默认 10+10ms 约 49.99Hz，2+2ms 约 250Hz，1+1ms 出现跳周期。继续保留已有游戏使用过的 10+10ms 默认值；实际更快配置应在同帧率、同技能、同持续时间下比较游戏成功释放次数。

## 构建与单 EXE 部署

成品支持 **Windows 10/11 x64**，无需安装 AutoHotkey、Python、.NET、Zig 或 VC++ 运行库。C++ 运行时静态链接，仅使用 Windows 自带 DLL/UCRT（界面另用系统自带的 d2d1、dwrite、dwmapi）。无脚本解释器、文件解包、临时 DLL 装载、加壳或混淆。界面通过原生 Win32 控件和托盘提供全部设置，原 `config.ini` 格式继续兼容，未知字段保留。

这项加固修复本地 DLL 替换风险，不代表安全软件已经确认旧样本误报。旧版火绒检出证据见 [核查记录](../docs/security/HUORONG-20260924.md)，每次需对最终成品分别验证；不得把没有新增日志等同于完成主动扫描。

v0.1.4.1 起，客户端用固定的 `Global` 命名互斥对象保证机器范围唯一，不使用路径或版本作为标识。提权前只读检查已有实例，提权后原子创建并持有对象到进程退出，重复启动静默退出。旧版不认识此锁，升级部署会先按已确认路径停止旧副本。`--self-test` 是零输入验收入口，不启动客户端，因此与运行中的客户端隔离。

开发者需要便携构建工具（客户端不需要）：

1. 从 [Zig 官方下载](https://ziglang.org/download/0.15.2/) 获取 `zig-x86_64-windows-0.15.2.zip`，放入 `venv/tools` 后解压。官方 SHA-256 为 `3a0ed1e8799a2f8ce2a6e6290a9ff22e6906f8227865911fb7ddedc3cc14cb0c`。
2. 执行 `scripts/build.ps1`（`-Benchmark` 追加调度基准，`-SkipTests` 跳过回归）。校验编译器哈希、编译原生 EXE、检查系统导入依赖及 GUI 子系统、运行配置/输入/调度回归和成品零输入自检。默认产物为 `build/release-0.1.6.0/DNFAutoFire.exe`，验证完成前不覆盖已安装文件。使用显式链接参数 `-Wl,--subsystem,windows`；Zig 0.15.2 的 `-mwindows` 不会实际设置 PE 子系统。
3. 火绒保持启用，对该确切成品留存哈希和检测结果。没有新增实时日志不等于主动扫描通过。
4. 执行 `scripts/publish.ps1 -Source <已验收EXE> -ExpectedHash <SHA256>`；按根目录 AGENTS.md 完成停止旧进程、替换 E 盘、按需恢复运行、同步 dist 和 AutoManagerProcess、哈希一致、本地提交。

## 验证边界

`tests/native_schedule_test.cpp` 覆盖 163 万条确定性断言；`client_input_test.cpp` 验证虚拟时钟下的奔跑、连招、焦点丢失和发送失败；`client_config_test.cpp` 验证原 INI、按键别名和规则 ABI。成品 `--self-test` 验证引擎和控制器零输入生命周期，`--ui-self-test` 额外创建隐藏界面但不改用户配置；`client_singleton_test.ps1` 验证跨目录单实例。

自动回归不注入真实键盘输入。新架构的游戏实效、多职业交互和硬件事件链还需要实机验收，纯调度基准不能替代它们。

官方依据：[QPC](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps)、[高精度 waitable timer](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw)、[SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)、[timeBeginPeriod 的进程范围与遮挡限制](https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod)。
