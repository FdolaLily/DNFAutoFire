// Service logic without the service control manager: process identity, path
// resolution, launcher rules, service command parsing, log rolling and a full
// game lifecycle driven by harmless child copies of this test executable.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../native/game_monitor.h"
#include "../native/launcher_monitor.h"
#include "../native/process_util.h"
#include "../native/service_control.h"
#include "../native/service_log.h"
#include "../native/win_fs.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks = 0;
void check(bool value, const char* name) { ++checks; if (!value) throw std::runtime_error(name); }
using namespace dafclient;
using namespace dafclient::svc;

struct Child {
    PROCESS_INFORMATION info{};
    explicit Child(const std::wstring& exe) {
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        std::wstring command = L"\"" + exe + L"\" --child-sleep";
        if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info))
            throw std::runtime_error("cannot start child");
    }
    bool alive() const { return WaitForSingleObject(info.hProcess, 0) == WAIT_TIMEOUT; }
    ~Child() {
        if (alive()) { TerminateProcess(info.hProcess, 0); WaitForSingleObject(info.hProcess, 5000); }
        CloseHandle(info.hThread); CloseHandle(info.hProcess);
    }
};
std::wstring copyAs(const std::wstring& self, const std::filesystem::path& dir, const std::wstring& name) {
    const auto target = (dir / name).wstring();
    if (!CopyFileW(self.c_str(), target.c_str(), FALSE)) throw std::runtime_error("cannot copy test executable");
    return target;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc > 1 && std::wstring(argv[1]) == L"--child-sleep") { Sleep(60000); return 0; }
    namespace sfs = std::filesystem;
    const auto dir = sfs::absolute(L"build/service-logic-test-" + std::to_wstring(GetCurrentProcessId()));
    sfs::create_directories(dir);
    int result = 0;
    try {
        check(processKey(L"DNF.exe") == L"dnf" && processKey(L" dnf ") == L"dnf" && processKey(L"C:\\Game\\DNF.EXE") == L"dnf", "process key normalisation");
        check(processKey(L"Tools/Other.exe") == L"other" && processKey(L".exe") == L".exe", "process key edge cases");
        ProcessEntry entry; entry.name = L"SGuard64.exe";
        check(matches(entry, processKey(L"sguard64")) && !matches(entry, processKey(L"SGuard")) && !matches(entry, L""), "name matching");

        const std::wstring base = L"C:\\Tools\\DAF";
        check(resolvePath(L"DNFAutoFire.exe", base) == L"C:\\Tools\\DAF\\DNFAutoFire.exe", "relative to service directory");
        check(resolvePath(L"Tools\\..\\x.exe", base) == L"C:\\Tools\\DAF\\x.exe", "relative path normalised");
        check(resolvePath(L"\"D:\\A B\\y.exe\"", base) == L"D:\\A B\\y.exe", "quoted absolute path");
        SetEnvironmentVariableW(L"DAF_TEST_DIR", L"E:\\env");
        check(resolvePath(L"%DAF_TEST_DIR%\\z.exe", base) == L"E:\\env\\z.exe", "environment variables expanded");

        check(!launcherCloseDue(false, false, 999999), "launcher kept when the game never ran (download / update)");
        check(!launcherCloseDue(true, true, 999999), "launcher kept while the game runs");
        check(!launcherCloseDue(true, false, kLauncherCloseDelayMs - 1), "launcher kept for one minute after exit");
        check(launcherCloseDue(true, false, kLauncherCloseDelayMs), "launcher closed after one minute");

        const std::string launcher = "{\n  \"app_settings\": {\n    \"showMainWindowOnGameExit\" : true,\n    \"theme\": \"x\",\n"
            "    \"MainWindowCloseAction\": \"exit\\\"now\"\n  },\n  \"other\": [1, 2]\n}";
        std::string out, error;
        check(rewriteLauncherConfig(launcher, out, error) == LauncherConfigStatus::Updated, "launcher config updated");
        check(out == "{\n  \"app_settings\": {\n    \"showMainWindowOnGameExit\" : false,\n    \"theme\": \"x\",\n"
            "    \"MainWindowCloseAction\": \"minimize\"\n  },\n  \"other\": [1, 2]\n}", "only the two values change, formatting kept");
        std::string again;
        check(rewriteLauncherConfig(out, again, error) == LauncherConfigStatus::AlreadyConfigured && again.empty(), "configured file is not rewritten");
        check(rewriteLauncherConfig("{\"app_settings\":{\"showMainWindowOnGameExit\":true}}", out, error) == LauncherConfigStatus::Retry, "missing setting retried");
        check(rewriteLauncherConfig("{\"app_settings\":{\"showMainWindowOnGameExit\":true,\"MainWindowCloseAction\":\"exit\"},"
            "\"copy\":{\"showMainWindowOnGameExit\":false}}", out, error) == LauncherConfigStatus::Retry, "ambiguous setting retried");
        check(rewriteLauncherConfig("not json", out, error) == LauncherConfigStatus::Retry, "invalid launcher config retried");

        check(urlHost(L"https://DNF.gcloudcdn.qq.com/a/b") == L"dnf.gcloudcdn.qq.com", "URL host case-insensitive");
        check(urlHost(L"http://user@dnf.gcloudcdn.qq.com:8080/x") == L"dnf.gcloudcdn.qq.com", "URL host strips user and port");
        check(urlHost(L"https://dnf.gcloudcdn.qq.com.evil.example/x") != L"dnf.gcloudcdn.qq.com" && urlHost(L"dnf.gcloudcdn.qq.com") == L"", "look-alike hosts rejected");
        check(isDnfUpdateManifest("{\"filelist\":[{\"url\":\"https://other/x\"},{\"bkcurl\":\"https://dnf.gcloudcdn.qq.com/p\"}]}"), "DNF manifest recognised");
        check(!isDnfUpdateManifest("{\"filelist\":[{\"url\":\"https://game.gcloudcdn.qq.com/p\"}]}") && !isDnfUpdateManifest("{}") && !isDnfUpdateManifest("x"), "other manifests rejected");

        const std::wstring exe = L"E:\\autokill\\DNFAutoFire.exe";
        check(svcctl::serviceCommand(exe) == L"\"E:\\autokill\\DNFAutoFire.exe\" --service", "service command line");
        check(svcctl::commandPointsTo(L"\"e:\\AUTOKILL\\DNFAutoFire.exe\" --service", exe), "registered command recognised");
        check(svcctl::commandPointsTo(L"E:\\autokill\\DNFAutoFire.exe --service", exe), "unquoted registration recognised");
        check(!svcctl::commandPointsTo(L"\"E:\\autokill\\DNFAutoFire.exe\"", exe), "client-mode registration is not the service");
        check(!svcctl::commandPointsTo(L"\"E:\\autokill\\DNFAutoFire.exe\" --service-x", exe), "argument must match exactly");
        check(!svcctl::commandPointsTo(L"\"E:\\old\\DNFProcessManager.exe\"", exe), "legacy service is another program");

        {
            const auto logPath = (dir / L"logs" / L"service.log").wstring();
            Log log(logPath, 2048);
            for (int i = 0; i < 100; ++i) log.info(L"第 " + std::to_wstring(i) + L" 条日志，用于验证滚动");
            check(sfs::exists(logPath) && sfs::exists(logPath + L".1"), "log rolls into one previous file");
            check(sfs::file_size(logPath) <= 2048 && sfs::file_size(logPath + L".1") <= 2048, "log files stay within the limit");
            check(!sfs::exists(logPath + L".2"), "only one previous log is kept");
        }

        const auto self = fs::modulePath();
        const auto snapshot = snapshotProcesses();
        DWORD session = 0; ProcessIdToSessionId(GetCurrentProcessId(), &session);
        check(runningInSession(snapshot, processKey(self), session, GetCurrentProcessId()), "snapshot contains this process");

        {
            Child victim(copyAs(self, dir, L"DafVictim.exe"));
            DWORD terminateError = 0;
            check(terminate(victim.info.dwProcessId, 5000, terminateError) && !victim.alive(), "terminate one process");
            check(waitExit(victim.info.dwProcessId, 0), "exited process reported as exited");
        }

        if (session == 0) {
            std::cout << "NOTE: session 0 (service / CI runner); interactive game lifecycle check skipped.\n";
        } else {
            // A fake game, a process to kill after the delay and a companion closed on exit.
            Child game(copyAs(self, dir, L"DafFakeGame.exe"));
            Child killed(copyAs(self, dir, L"DafFakeLoader.exe"));
            Child companion(copyAs(self, dir, L"DafCompanion.exe"));
            Sleep(300);
            ServiceOptions options;
            options.gameProcess = L"DafFakeGame.exe"; options.pollSeconds = 1; options.actionDelaySeconds = 0;
            options.autoStart.clear(); options.limit.clear();
            options.kill = {L"DafFakeLoader"}; options.autoStop = {L"DafCompanion.exe"};
            options.aboveNormalPriority = true;
            HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            Log log((dir / L"logs" / L"lifecycle.log").wstring());
            GameMonitor monitor(log, dir.wstring(), self, stop);
            const auto run = [&](int iterations) {
                for (int i = 0; i < iterations; ++i) { Snapshot s; monitor.step(options, s, GetTickCount64()); Sleep(50); }
            };
            run(3);
            check(monitor.gameRunning() && monitor.exitHandle(), "game detected with an exit handle");
            check(!killed.alive() && companion.alive(), "kill list applied after the delay, companion untouched");
            check(GetPriorityClass(game.info.hProcess) == ABOVE_NORMAL_PRIORITY_CLASS, "game priority raised to AboveNormal");
            TerminateProcess(game.info.hProcess, 0); WaitForSingleObject(game.info.hProcess, 5000);
            run(2);
            check(!monitor.gameRunning() && !companion.alive(), "companion closed after the game exited");
            monitor.shutdown();
            CloseHandle(stop);
        }
        std::cout << "PASS: " << checks << " service logic checks.\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        result = 1;
    }
    Sleep(200);
    std::error_code ignored; sfs::remove_all(dir, ignored);
    return result;
}
