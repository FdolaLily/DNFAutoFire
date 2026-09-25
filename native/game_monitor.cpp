#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "game_monitor.h"
#include "app_ids.h"
#include "win_fs.h"
#include <algorithm>

namespace dafclient::svc {
namespace {
std::wstring priorityName(DWORD value) {
    switch (value) {
    case IDLE_PRIORITY_CLASS: return L"Idle";
    case BELOW_NORMAL_PRIORITY_CLASS: return L"BelowNormal";
    case NORMAL_PRIORITY_CLASS: return L"Normal";
    case ABOVE_NORMAL_PRIORITY_CLASS: return L"AboveNormal";
    case HIGH_PRIORITY_CLASS: return L"High";
    case REALTIME_PRIORITY_CLASS: return L"RealTime";
    default: return std::to_wstring(value);
    }
}
std::wstring describe(const std::wstring& name, DWORD pid) { return name + L" (" + std::to_wstring(pid) + L")"; }
}

GameMonitor::GameMonitor(Log& log, std::wstring baseDirectory, std::wstring selfPath, HANDLE stopEvent)
    : log_(log), base_(std::move(baseDirectory)), self_(std::move(selfPath)), stop_(stopEvent) {}
GameMonitor::~GameMonitor() { shutdown(); }
void GameMonitor::shutdown() {
    if (game_.handle) CloseHandle(game_.handle);
    game_ = Game{};
}
bool GameMonitor::pause(DWORD ms) const { return WaitForSingleObject(stop_, ms) == WAIT_TIMEOUT; }

unsigned long long GameMonitor::nextWake(unsigned long long now) const {
    if (!game_.pid) return std::max(now, nextPoll_);
    unsigned long long wake = game_.nextPriority;
    if (!game_.actionsDone) wake = std::min(wake, game_.actionsDue);
    if (!game_.handle) wake = std::min(wake, game_.nextExitPoll);
    return std::max(now, wake);
}

void GameMonitor::step(const ServiceOptions& options, Snapshot& snapshot, unsigned long long now) {
    if (!game_.pid) {
        if (now < nextPoll_) return;
        nextPoll_ = now + options.pollSeconds * 1000ull;
        const auto key = processKey(options.gameProcess);
        for (const auto& e : snapshot.get()) {
            if (e.session > 0 && matches(e, key)) { detected(e, options, now); return; }
        }
        return;
    }
    bool gone = false;
    if (game_.handle) gone = WaitForSingleObject(game_.handle, 0) == WAIT_OBJECT_0;
    else if (now >= game_.nextExitPoll) {
        game_.nextExitPoll = now + 1000;
        gone = !runningInSession(snapshot.get(), game_.key, game_.session, game_.pid);
    }
    if (gone) { snapshot.invalidate(); exited(options); nextPoll_ = 0; return; }
    if (!game_.actionsDone && now >= game_.actionsDue) {
        game_.actionsDone = true;
        killProcesses(game_.options.kill);
        limitProcesses(game_.options.limit);
        snapshot.invalidate();
    }
    if (now >= game_.nextPriority) {
        maintainPriority(options);
        game_.nextPriority = now + options.pollSeconds * 1000ull;
    }
}

void GameMonitor::detected(const ProcessEntry& entry, const ServiceOptions& options, unsigned long long now) {
    game_ = Game{};
    game_.pid = entry.pid; game_.session = entry.session; game_.key = processKey(entry.name); game_.options = options;
    game_.handle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION, FALSE, entry.pid);
    game_.canSetPriority = game_.handle != nullptr;
    if (!game_.handle) game_.handle = OpenProcess(SYNCHRONIZE, FALSE, entry.pid);
    if (!game_.handle) log_.warn(L"无法直接等待游戏进程退出，改为每秒轮询：" + Log::errorText(GetLastError()));
    log_.info(L"检测到 " + describe(entry.name, entry.pid) + L" 在用户会话 " + std::to_wstring(entry.session) + L" 启动");
    startApplications(options.autoStart, entry.session);
    const unsigned long long after = GetTickCount64();
    game_.actionsDue = after + options.actionDelaySeconds * 1000ull;
    game_.nextPriority = now;
    game_.nextExitPoll = after + 1000;
}

void GameMonitor::exited(const ServiceOptions& options) {
    const DWORD session = game_.session;
    const auto key = game_.key;
    log_.info(L"检测到游戏进程 " + std::to_wstring(game_.pid) + L" 已从用户会话 " + std::to_wstring(session) + L" 退出");
    shutdown();
    // Another game client in the same session keeps the companions running.
    if (runningInSession(snapshotProcesses(), key, session)) return;
    stopApplications(options.autoStop, session);
}

void GameMonitor::startApplications(const std::vector<std::wstring>& paths, DWORD session) {
    for (const auto& configured : paths) {
        const auto path = resolvePath(configured, base_);
        if (!fs::isFile(path)) { log_.warn(L"随游戏启动的程序不存在：" + path); continue; }
        const auto key = processKey(path);
        const auto runningCopy = [&] {
            // The service itself shares the client's image name; it never counts as a running client.
            for (const auto& e : snapshotProcesses()) if (e.session == session && e.pid != GetCurrentProcessId() && matches(e, key)) return true;
            return false;
        };
        if (runningCopy()) continue; // Already running; never start a second copy.
        // Only the client itself needs (and gets) the elevated token.
        const auto launch = launchInSession(path, session, fs::samePath(path, self_));
        if (!launch.pid) {
            log_.error(L"无法在用户会话 " + std::to_wstring(session) + L" 启动 " + path + L"（" + launch.step + L"）：" + Log::errorText(launch.error));
            continue;
        }
        if (!pause(1000)) return;
        if (runningCopy())
            log_.info(L"已在用户会话 " + std::to_wstring(session) + L" 启动 " + path + L"，进程 " + std::to_wstring(launch.pid) +
                (launch.elevated ? L"（管理员权限）" : L""));
        else
            log_.warn(L"程序 " + path + L" 已创建为进程 " + std::to_wstring(launch.pid) + L"，但随即退出");
    }
}

void GameMonitor::killProcesses(const std::vector<std::wstring>& names) {
    const auto entries = snapshotProcesses();
    const DWORD self = GetCurrentProcessId();
    for (const auto& name : names) {
        const auto key = processKey(name);
        for (const auto& e : entries) {
            if (!matches(e, key) || e.pid == self || e.pid == game_.pid) continue;
            DWORD error = 0;
            if (terminate(e.pid, 5000, error)) log_.info(L"已结束进程 " + describe(e.name, e.pid));
            else if (error) log_.warn(L"无法结束进程 " + describe(e.name, e.pid) + L"：" + Log::errorText(error));
            else log_.warn(L"进程 " + describe(e.name, e.pid) + L" 未在 5 秒内退出");
        }
    }
}

void GameMonitor::limitProcesses(const std::vector<std::wstring>& names) {
    const auto entries = snapshotProcesses();
    for (const auto& name : names) {
        const auto key = processKey(name);
        for (const auto& e : entries) {
            if (!matches(e, key) || e.pid == game_.pid) continue;
            DWORD error = 0; long status = 0;
            const bool cpu = restrictToLastCpu(e.pid, error);
            if (!cpu) log_.warn(L"无法限制进程 " + describe(e.name, e.pid) + L" 的 CPU：" + Log::errorText(error));
            if (setVeryLowIoPriority(e.pid, status)) log_.info(L"已限制进程 " + describe(e.name, e.pid) + (cpu ? L"：最后一个 CPU 核心、极低 I/O 优先级" : L"：极低 I/O 优先级"));
            else {
                wchar_t hex[16]; swprintf_s(hex, L"0x%08lX", static_cast<unsigned long>(status));
                log_.warn(L"无法降低进程 " + describe(e.name, e.pid) + L" 的 I/O 优先级：" + hex);
            }
        }
    }
}

void GameMonitor::stopApplications(const std::vector<std::wstring>& names, DWORD session) {
    const auto entries = snapshotProcesses();
    const DWORD self = GetCurrentProcessId();
    for (const auto& name : names) {
        const auto key = processKey(name);
        for (const auto& e : entries) {
            if (e.session != session || !matches(e, key) || e.pid == self) continue;
            if (fs::samePath(imagePath(e.pid), self_)) {
                // Our own client: ask it to stop auto-fire and release keys itself.
                bool asked = false;
                if (HANDLE quit = OpenEventW(EVENT_MODIFY_STATE, FALSE, kClientQuitEvent)) { asked = SetEvent(quit) != FALSE; CloseHandle(quit); }
                if (asked && waitExit(e.pid, 5000)) { log_.info(L"游戏退出后连发程序已自行关闭 " + describe(e.name, e.pid)); continue; }
            }
            DWORD error = 0;
            if (terminate(e.pid, 5000, error)) log_.info(L"游戏退出后已关闭 " + describe(e.name, e.pid));
            else if (error) log_.warn(L"游戏退出后无法关闭 " + describe(e.name, e.pid) + L"：" + Log::errorText(error));
            else log_.warn(L"游戏退出后 " + describe(e.name, e.pid) + L" 未在 5 秒内退出");
        }
    }
}

void GameMonitor::maintainPriority(const ServiceOptions& options) {
    if (!options.optimizeGamePriority) return;
    const DWORD target = options.aboveNormalPriority ? ABOVE_NORMAL_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS;
    HANDLE process = game_.canSetPriority ? game_.handle
        : OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION, FALSE, game_.pid);
    DWORD error = 0, previous = 0;
    bool ok = false;
    if (process) {
        previous = GetPriorityClass(process);
        if (previous == target) ok = true;
        else if (previous && SetPriorityClass(process, target)) ok = GetPriorityClass(process) == target;
        if (!ok) error = GetLastError();
        if (process != game_.handle) CloseHandle(process);
    } else error = GetLastError();
    if (ok) {
        game_.priorityFailureLogged = false;
        if (!game_.priorityLogged) {
            game_.priorityLogged = true;
            log_.info(L"游戏优先级为 " + priorityName(target) + L"（进程 " + std::to_wstring(game_.pid) + L"，原优先级 " + priorityName(previous) + L"）");
        }
    } else if (!game_.priorityFailureLogged) {
        game_.priorityFailureLogged = true;
        log_.warn(L"无法设置游戏优先级，游戏照常运行：" + Log::errorText(error));
    }
}

} // namespace dafclient::svc
