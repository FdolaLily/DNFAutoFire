# Windows 原生调度基准

本次测试衡量 `schedule.h` 与 `win_timer.h` 的真实调度路径，输出端仅记录模拟边沿，**不调用 SendInput，不向 DNF 发送输入**。因此结果表示本机调度能力，不能当作 DNF 接收或执行技能的频率。

## 环境与方法

- 日期：2026-09-24；完整 CSV 写入时间为本机时间 19:56。
- Windows 报告版本：`10.0.26200.0`；CPU：AMD Ryzen 7 9800X3D，16 个逻辑处理器。
- 编译：Zig 0.15.2，C++17，`-O2 -static -target x86_64-windows-gnu`。
- 每组持续约 2 秒；1 / 3 键，Down 和 Up 各 1 / 2 / 5 / 10 ms，末段自旋 0 / 50 / 100 μs。
- 全部原生组成功创建高精度 waitable timer；普通优先级，不绑定 CPU，不使用实时优先级。
- 同一桌面同时进行开发、构建等工作，未隔离后台负载，未固定 CPU 频率或电源策略。因此这是现场短时测量，不是受控实验或长期稳定性证明。
- 对照组使用 `timeBeginPeriod(1)` 与相对 `Sleep`，仅测试单键。它未模拟旧 AHK 实现的 INI 读取、多进程或 Hook 开销。
- CPU 指标取 `GetThreadTimes` 的线程 CPU 时间增量；短测量中存在采样粒度，`0.000%` 不表示完全没有 CPU 开销。

频率由完成的同键 Down→Down 间隔计算，避免首次立即按下导致次数偏高。`lateness` 为实际模拟提交时间减计划边沿截止时间；旧相对 Sleep 组以连续理想时间轴为参照，因而包含累计漂移。`skipped_cycles` 是调度器为保留最小 Down/Up 窗口而跳过的周期，全部键合计。跳过周期不以短时间密集补发恢复。

原始结果保留于 `native/benchmark/native-benchmark.csv`。测试源码为 `tests/native_benchmark.cpp`。

## 默认不自旋的结果

| 键数 | Down / Up | 实测每键 Hz | P99 边沿迟到 ms | 跳周期 |
| --- | --- | ---: | ---: | ---: |
| 1 | 10 / 10 ms | 49.987 | 0.792 | 0 |
| 1 | 5 / 5 ms | 99.984 | 0.541 | 0 |
| 1 | 2 / 2 ms | 249.956 | 0.534 | 0 |
| 1 | 1 / 1 ms | 436.903 | 0.775 | 126 |
| 3 | 10 / 10 ms | 49.997 | 0.515 | 0 |
| 3 | 5 / 5 ms | 100.003 | 0.553 | 0 |
| 3 | 2 / 2 ms | 249.659 | 0.546 | 2 |
| 3 | 1 / 1 ms | 466.925 | 0.631 | 199 |

单键相对 `Sleep(10)` 对照为 47.600 Hz，约 2 秒后的最大累计相位漂移为 95.812 ms。相同 10 / 10 ms 原生组为 49.987 Hz，最大边沿迟到 1.522 ms，没有累计处理开销导致的持续相位漂移。

该单键 20 ms 周期的实际 Down→Down 间隔：

| 实现 | 中位数 ms | P95 ms | P99 ms | 最大 ms |
| --- | ---: | ---: | ---: | ---: |
| 相对 Sleep 对照 | 20.969 | 21.981 | 22.323 | 22.323 |
| 原生绝对截止点 | 19.987 | 20.392 | 20.462 | 21.173 |

在本次 1 / 1 ms 测量中，0 μs 自旋不能持续达到理论 500 Hz；50 / 100 μs 自旋的结果受现场负载影响，也没有构成稳定性保证。维持默认 0 μs 自旋，不以这些短时结果推导 DNF 的有效最高频率。

## 保留的异常与针对复测

完整测试最后两组（3 键、100 μs 自旋）出现明显长尾，原始数据未删除：

| 窗口 | 首测 Hz | 首测 P99 / 最大迟到 ms | 首测跳周期 | 复测 Hz | 复测 P99 / 最大迟到 ms | 复测跳周期 |
| --- | ---: | --- | ---: | ---: | --- | ---: |
| 5 / 5 ms | 86.891 | 9.348 / 29.682 | 78 | 99.991 | 0.594 / 0.849 | 0 |
| 10 / 10 ms | 45.771 | 12.523 / 15.580 | 25 | 49.998 | 0.659 / 0.801 | 0 |

复测同样各约 2 秒，分别保存于 `native/benchmark/native-benchmark-recheck-3x5.csv` 与 `native/benchmark/native-benchmark-recheck-3x10.csv`。未采集 ETW 等系统追踪，不能确定首测长尾的具体原因；复测恢复只能说明该异常没有稳定复现，不能排除未来负载下再次发生。

## 复现

```powershell
# 编译客户端后追加编译并运行全部基准组，结果写入 build/native-benchmark.csv
./scripts/build.ps1 -SkipTests -Benchmark
# 针对单组：持续秒数、键数、单边毫秒数、自旋微秒数
./build/native_benchmark.exe 2 3 5 100
```

程序启动还验证停止事件优先于过期截止点、状态事件中断、空闲等待唤醒和不提前报告到期。生产引擎还存在真实 SendInput、Hook 链、前台校验和游戏处理开销，需用独立游戏内实验验证有效触发率。

## 成品依赖检查

2026-09-24 对 `build/DNFAutoFireNative.dll` 的 PE 导入表进行了直接解析，架构为 AMD64（`0x8664`），没有延迟导入目录。导入模块只有 `USER32.dll`、`WINMM.dll`、`KERNEL32.dll` 与 `api-ms-win-crt-*` 系统 UCRT API sets，没有 `libc++.dll`、`libstdc++-6.dll`、`libgcc_s_*.dll`、Python 或独立 VC++ 运行库。

最近检查时 DLL SHA-256：`FF654AD072B9D6F17D14806D6D803683156DF9D72B4C859F6D4A40AFFB2377C6`；完整导入名单和时间记录在 `build/native-dependencies.json`。若重新编译，必须运行 `scripts/inspect-native-dependencies.ps1` 对新成品重新核对，不能沿用此哈希作为新成品的验证。

这是静态链接 C++ 运行时并依赖 Windows 系统 UCRT 的成品；Windows 10 / 11 已内置 UCRT，因此用户无需安装 Python、Zig、AutoHotkey 或独立 C++ 开发环境。微软关于系统 UCRT 的说明：[Universal CRT deployment](https://learn.microsoft.com/en-us/cpp/windows/universal-crt-deployment?view=msvc-170)。
