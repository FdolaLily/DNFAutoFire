#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "service_host.h"
#include "app_ids.h"
#include "app_update.h"
#include "client_config.h"
#include "config_migrate.h"
#include "game_monitor.h"
#include "launcher_monitor.h"
#include "service_control.h"
#include "service_log.h"
#include "win_fs.h"
#include <algorithm>

namespace dafclient::svc {
namespace {
struct Host {
    SERVICE_STATUS_HANDLE handle = nullptr;
    SERVICE_STATUS status{};
    HANDLE stop = nullptr;
    DWORD checkpoint = 0;
} host;

void report(DWORD state, DWORD exitCode = NO_ERROR, DWORD waitHint = 0) {
    host.status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    host.status.dwCurrentState = state;
    host.status.dwWin32ExitCode = exitCode;
    host.status.dwWaitHint = waitHint;
    host.status.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    host.status.dwCheckPoint = state == SERVICE_RUNNING || state == SERVICE_STOPPED ? 0 : ++host.checkpoint;
    if (host.handle) SetServiceStatus(host.handle, &host.status);
}
DWORD WINAPI control(DWORD code, DWORD, void*, void*) {
    switch (code) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        report(SERVICE_STOP_PENDING, NO_ERROR, 10000);
        SetEvent(host.stop);
        return NO_ERROR;
    case SERVICE_CONTROL_INTERROGATE: return NO_ERROR;
    default: return ERROR_CALL_NOT_IMPLEMENTED;
    }
}
std::wstring widen(const char* text) { std::wstring out; for (const char* p = text; *p; ++p) out.push_back(wchar_t(static_cast<unsigned char>(*p))); return out; }
std::wstring joinNames(const std::vector<std::wstring>& names) {
    std::wstring out;
    for (const auto& n : names) { if (!out.empty()) out += L", "; out += n; }
    return out;
}

void run() {
    const auto self = fs::modulePath();
    const auto directory = fs::directoryOf(self);
    Log log(fs::join(fs::join(directory, kLogDirectory), kServiceLogName));
    log.info(std::wstring(L"服务已启动 v") + kProductVersion + L"：" + self);
    if (const unsigned removed = update::cleanupLeftovers(self)) log.info(L"已删除更新留下的旧程序文件 " + std::to_wstring(removed) + L" 个");
    // Keep the "how to stop before deleting the EXE" wording current after a file-only update.
    if (!svcctl::refreshLabels()) log.warn(L"无法刷新服务显示名称与说明（不影响运行）");

    const auto migration = migrateLegacyConfig(directory);
    if (migration.migrated()) log.info(L"已将旧配置合并到 config.json：" + joinNames(migration.imported) + L"；已移除：" + joinNames(migration.removed));
    if (!migration.kept.empty()) log.warn(L"以下旧配置文件未删除（内容已由 config.json 取代或无法删除）：" + joinNames(migration.kept));
    if (!migration.error.empty()) log.error(L"旧配置迁移失败，旧文件保持不变：" + migration.error);

    Store store(fs::join(directory, kConfigFileName));
    ServiceOptions options;
    bool configError = false;
    const auto load = [&](bool initial) {
        try {
            const auto next = store.loadService();
            if (!initial && next != options) log.info(L"配置已更新，立即生效");
            options = next; configError = false;
        } catch (const std::exception& error) {
            if (!configError) log.error(L"无法读取 config.json，继续使用上一次的有效配置：" + widen(error.what()));
            configError = true;
        }
    };
    load(true);
    log.info(L"监视游戏进程 " + options.gameProcess + L"，每 " + std::to_wstring(options.pollSeconds) + L" 秒检测一次");
    report(SERVICE_RUNNING);

    GameMonitor game(log, directory, self, host.stop);
    LauncherMonitor launcher(log);
    unsigned long long nextConfigCheck = 0, nextLauncher = 0;
    for (;;) {
        const unsigned long long now = GetTickCount64();
        if (now >= nextConfigCheck) {
            nextConfigCheck = now + 1000;
            if (store.changedOnDisk()) load(false);
        }
        Snapshot snapshot;
        try {
            game.step(options, snapshot, now);
            if (now >= nextLauncher) { nextLauncher = now + 1000; launcher.step(options, snapshot, now); }
        } catch (const std::exception& error) {
            log.error(L"服务处理发生异常，将继续运行：" + widen(error.what()));
        }
        const unsigned long long wake = std::min({game.nextWake(now), nextConfigCheck, nextLauncher});
        const auto after = GetTickCount64();
        const DWORD timeout = wake > after ? DWORD(std::min<unsigned long long>(wake - after, 60000)) : 0;
        HANDLE handles[2] = {host.stop, game.exitHandle()};
        const DWORD count = handles[1] ? 2 : 1;
        if (WaitForMultipleObjects(count, handles, FALSE, timeout) == WAIT_OBJECT_0) break;
    }
    game.shutdown();
    log.info(L"服务已停止");
}

void WINAPI serviceMain(DWORD, wchar_t**) {
    host.handle = RegisterServiceCtrlHandlerExW(kServiceName, control, nullptr);
    if (!host.handle) return;
    report(SERVICE_START_PENDING, NO_ERROR, 10000);
    host.stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!host.stop) { report(SERVICE_STOPPED, GetLastError()); return; }
    DWORD exitCode = NO_ERROR;
    try { run(); } catch (...) { exitCode = ERROR_SERVICE_SPECIFIC_ERROR; }
    CloseHandle(host.stop); host.stop = nullptr;
    host.status.dwServiceSpecificExitCode = exitCode ? 1 : 0;
    report(SERVICE_STOPPED, exitCode);
}
} // namespace

int runService() {
    wchar_t name[64];
    wcscpy_s(name, kServiceName);
    SERVICE_TABLE_ENTRYW table[] = {{name, serviceMain}, {nullptr, nullptr}};
    if (StartServiceCtrlDispatcherW(table)) return 0;
    return GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT ? 2 : 1;
}

} // namespace dafclient::svc
