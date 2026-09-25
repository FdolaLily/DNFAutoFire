#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <cstdio>
#include <stdexcept>
#include "app_ids.h"
#include "app_update.h"
#include "client_config.h"
#include "client_input.h"
#include "client_ui.h"
#include "config_migrate.h"
#include "engine_api.h"
#include "service_host.h"
#include "update_dialog.h"
#include "win_fs.h"

namespace {
using namespace dafclient;
constexpr wchar_t kMutex[] = L"Global\\DNFAutoFire.Client.{B797BFB2-305A-44DC-9E06-76D6CC424419}";
constexpr UINT_PTR kHealthTimer = 0xDAF1;
std::wstring executable() { return fs::modulePath(); }
bool administrator() {
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID group = nullptr; BOOL member = FALSE;
    if (!AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &group)) return false;
    CheckTokenMembership(nullptr, group, &member); FreeSid(group); return member != FALSE;
}
// Starts this EXE again with the same arguments through UAC; false when refused.
bool elevate(const std::wstring& path, const std::wstring& arguments, int& exitCode) {
    SHELLEXECUTEINFOW request{}; request.cbSize = sizeof(request);
    request.lpVerb = L"runas"; request.lpFile = path.c_str(); request.nShow = SW_SHOWNORMAL;
    if (!arguments.empty()) request.lpParameters = arguments.c_str();
    const auto directory = path.substr(0, path.find_last_of(L"\\/")); request.lpDirectory = directory.c_str();
    if (!ShellExecuteExW(&request)) { exitCode = GetLastError() == ERROR_CANCELLED ? 0 : 1; return false; }
    exitCode = 0; return true;
}
bool updatePromptEnabled() {
    wchar_t value[8]{};
    const DWORD n = GetEnvironmentVariableW(update::kPromptEnvironment, value, 8);
    return !(n == 1 && value[0] == L'0');
}
// A different build of an installed copy (service or running client elsewhere) was
// opened: offer to replace the installed file in place. Returns true when this process
// is done (updated, handed to an elevated copy, or cancelled) with exitCode set.
bool offerUpdate(const std::wstring& path, int& exitCode) {
    if (!updatePromptEnabled()) return false;
    update::Target target;
    if (!update::findInstalled(path, target)) return false;
    const auto self = update::inspect(path);
    const auto mine = self.version.valid ? self.version : update::currentVersion();
    const auto offer = update::classify(mine, target.info.version, update::sameContent(path, target.path));
    if (offer == update::Offer::None) return false;
    switch (update::confirm(target, mine, offer)) {
    case update::Choice::Skip: {
        // An AHK-era client has no single-instance lock: both would fire the same keys.
        const auto running = update::runningLegacyClient(path);
        if (running.empty()) return false;
        update::notice(TD_WARNING_ICON, L"旧版 DAF 连发工具正在运行",
            L"同时运行新旧两个版本会重复连发。请先在托盘退出旧版后再打开，或再次打开并选择“升级旧版”。\n" + running);
        exitCode = 0; return true;
    }
    case update::Choice::Cancel: exitCode = 0; return true;
    case update::Choice::Replace: break;
    }
    if (administrator()) { exitCode = update::runWithProgress(path, target.path); return true; }
    elevate(path, std::wstring(update::kReplaceArgument) + L" \"" + target.path + L"\"", exitCode);
    return true;
}
bool alreadyRunning() {
    HANDLE handle = OpenMutexW(SYNCHRONIZE, FALSE, kMutex);
    const DWORD error = GetLastError();
    if (handle) { CloseHandle(handle); return true; }
    if (error == ERROR_ACCESS_DENIED) return true;
    if (error != ERROR_FILE_NOT_FOUND) throw std::runtime_error("Cannot inspect singleton mutex");
    return false;
}
// Asks an already running client to show its window and a friendly notice.
// The first instance may still be starting, so its window is awaited briefly.
// Returns false when no window answered; the duplicate still exits quietly.
bool showRunningInstance() {
    const UINT message = RegisterWindowMessageW(kShowRunningMessage);
    if (!message) return false;
    for (int attempt = 0; attempt < 15; ++attempt) {
        if (HWND window = FindWindowW(kMainWindowClass, nullptr)) {
            DWORD process = 0; GetWindowThreadProcessId(window, &process);
            if (process) AllowSetForegroundWindow(process); // We were just launched by the user, so we may grant this.
            return PostMessageW(window, message, 0, 0) != FALSE;
        }
        Sleep(100);
    }
    return false;
}
HANDLE acquireInstance() {
    HANDLE handle = CreateMutexExW(nullptr, kMutex, 0, SYNCHRONIZE);
    const DWORD error = GetLastError();
    if ((handle && error == ERROR_ALREADY_EXISTS) || (!handle && error == ERROR_ACCESS_DENIED)) {
        if (handle) CloseHandle(handle);
        return nullptr;
    }
    if (!handle) throw std::runtime_error("Cannot create singleton mutex");
    return handle; // The OS closes this only when the entire client process exits.
}
void startupShortcut(bool enabled) {
    wchar_t directory[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_STARTUP, nullptr, SHGFP_TYPE_CURRENT, directory)))
        throw std::runtime_error("Cannot locate Startup folder");
    const std::wstring shortcut = std::wstring(directory) + L"\\DAF连发工具.lnk";
    if (!enabled) {
        if (!DeleteFileW(shortcut.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)
            throw std::runtime_error("Cannot remove Startup shortcut");
        return;
    }
    IShellLinkW* link = nullptr;
    HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
        IID_IShellLinkW, reinterpret_cast<void**>(&link));
    if (FAILED(result)) throw std::runtime_error("Cannot create Startup shortcut");
    const auto path = executable();
    result = link->SetPath(path.c_str());
    if (SUCCEEDED(result)) result = link->SetWorkingDirectory(path.substr(0, path.find_last_of(L"\\/")).c_str());
    IPersistFile* file = nullptr;
    if (SUCCEEDED(result)) result = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file));
    if (SUCCEEDED(result)) result = file->Save(shortcut.c_str(), TRUE);
    if (file) file->Release();
    link->Release();
    if (FAILED(result)) throw std::runtime_error("Cannot save Startup shortcut");
}
// Lets the service (session 0) ask this client to exit when the game closes: a named
// event is the only object both sides can reach across sessions. The listener thread
// only posts a command; the UI thread performs the normal quit (stop, release keys).
class QuitListener {
public:
    bool start(HWND window) {
        SECURITY_ATTRIBUTES security{};
        const bool secured = fs::sharedSecurity(security, false);
        quit_ = CreateEventW(secured ? &security : nullptr, TRUE, FALSE, kClientQuitEvent);
        fs::freeSharedSecurity(security);
        if (!quit_) return false;
        ResetEvent(quit_); // Never act on a request meant for a previous instance.
        done_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        window_ = window;
        thread_ = done_ ? CreateThread(nullptr, 0, &QuitListener::main, this, 0, nullptr) : nullptr;
        return thread_ != nullptr;
    }
    ~QuitListener() {
        if (thread_) { SetEvent(done_); WaitForSingleObject(thread_, 5000); CloseHandle(thread_); }
        if (done_) CloseHandle(done_);
        if (quit_) CloseHandle(quit_);
    }
private:
    static DWORD WINAPI main(void* context) {
        auto* self = static_cast<QuitListener*>(context);
        HANDLE handles[2] = {self->done_, self->quit_};
        if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0 + 1)
            PostMessageW(self->window_, kInputCommandMessage, static_cast<WPARAM>(InputCommand::Exit), 0);
        return 0;
    }
    HANDLE quit_ = nullptr, done_ = nullptr, thread_ = nullptr;
    HWND window_ = nullptr;
};
struct Runtime {
    Controller controller;
    void* engine = nullptr;
    bool running = false;
    HWND window = nullptr;
    DWORD lastError = 0;
    bool shutdown() {
        // Combo's pause/release handshake still needs the engine until joined.
        if (!controller.stop()) { lastError = controller.error() ? controller.error() : ERROR_TIMEOUT; return false; }
        if (engine) {
            const int result = AF_Stop(engine);
            if (!result) { lastError = ERROR_TIMEOUT; return false; }
            engine = nullptr;
            if (result < 0) { running = false; lastError = ERROR_WRITE_FAULT; return false; }
        }
        running = false; return true;
    }
    bool configure(const Profile& profile, const Settings& settings, bool enabled) {
        if (!shutdown()) return false;
        lastError = 0;
        try {
        if (enabled) {
            const auto rules = buildRules(profile, settings);
            static_assert(sizeof(Rule) == 132 * sizeof(unsigned), "Engine rule ABI");
            if (!rules.empty()) {
                engine = AF_Start(reinterpret_cast<const unsigned*>(rules.data()),
                    static_cast<unsigned>(rules.size()), profile.downMs * 1000ULL,
                    profile.upMs * 1000ULL, 0, window, 0);
                if (!engine) { lastError = GetLastError(); return false; }
            }
        }
        // Installed last so physical events are seen before the AF hook consumes them.
        if (!controller.start(profile, settings, engine, window, enabled)) {
            lastError = controller.error();
            shutdown(); return false;
        }
        running = enabled; return true;
        } catch (...) {
            lastError = ERROR_NOT_ENOUGH_MEMORY;
            shutdown(); return false;
        }
    }
    DWORD error() const { return lastError ? lastError : (controller.error() ? controller.error() : (engine ? AF_Error(engine) : 0)); }
};
int selfTest(HINSTANCE instance, bool uiTest) {
    if (GetConsoleWindow()) return 15; // GUI clients must not allocate a console.
    // No config file is loaded/saved and no physical trigger can produce input.
    WNDCLASSW cls{}; cls.hInstance = instance; cls.lpfnWndProc = DefWindowProcW;
    cls.lpszClassName = L"DNFAutoFire.SelfTest";
    RegisterClassW(&cls);
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (!window) return 10;
    unsigned rule[132]{}; rule[0] = parseKey(L"F24").descriptor();
    Controller controller;
    Profile profile; Settings settings;
    settings.quickSwitchHotkey.clear(); settings.oneKeyRun.toggleHotkey.clear();
    DWORD initialHandles = 0, finalHandles = 0;
    GetProcessHandleCount(GetCurrentProcess(), &initialHandles);
    for (unsigned iteration = 0; iteration < 25; ++iteration) {
        // Include timing beyond the former UI/engine ceilings, including a
        // duration whose microseconds cannot fit in 32 bits. No key is held.
        const unsigned long long duration = iteration % 3 == 0 ? 7000ULL
            : iteration % 3 == 1 ? 500000ULL : kMaxTimingMs * 1000ULL;
        void* engine = AF_Start(rule, 1, duration, duration, 0, window, 0);
        if (!engine) { DestroyWindow(window); return 11; }
        const bool controllerOk = controller.start(profile, settings, engine, window, false);
        Sleep(10);
        unsigned long long stats[4]{};
        const bool ok = AF_Stats(engine, stats) && stats[0] == 0 && stats[3] == 1 && !AF_Error(engine);
        const bool controllerStopped = controller.stop();
        if (!controllerStopped) return 12; // Process exit retains all live state safely.
        const int stopped = AF_Stop(engine);
        if (!ok || stopped != 1 || !controllerOk) return 12;
    }
    GetProcessHandleCount(GetCurrentProcess(), &finalHandles);
    DestroyWindow(window);
    if (finalHandles > initialHandles + 4) return 14;
    if (uiTest) {
        // An absent, unique path tests defaults without touching installed config.
        wchar_t temporary[MAX_PATH]{}; GetTempPathW(MAX_PATH, temporary);
        Store store(std::wstring(temporary) + L"DNFAutoFire-ui-selftest-" + std::to_wstring(GetCurrentProcessId()) + L".json");
        ClientUi ui(instance, store, {});
        if (!ui.create(false) || !IsWindow(ui.window())) return 13;
    }
    std::printf("PASS native client: 25 zero-input engine/controller lifecycles; handles %lu -> %lu\n", initialHandles, finalHandles);
    return 0;
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    Runtime* runtime = nullptr;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);
    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const bool self = argc == 2 && (wcscmp(argv[1], L"--self-test") == 0 || wcscmp(argv[1], L"--ui-self-test") == 0);
    const bool uiTest = self && wcscmp(argv[1], L"--ui-self-test") == 0;
    const bool service = argc == 2 && _wcsicmp(argv[1], kServiceArgument) == 0;
    // "<new exe>" --replace "<installed exe>": the elevated half of a confirmed update.
    const bool replaceMode = argc == 3 && _wcsicmp(argv[1], update::kReplaceArgument) == 0;
    const std::wstring replaceTarget = replaceMode ? argv[2] : L"";
    // "<installed exe>" --updated <old version> <pid>: reopened after an update.
    update::Version updatedFrom; DWORD waitPid = 0;
    const bool updated = update::parseUpdated(argc, argv, updatedFrom, waitPid);
    if (argv) LocalFree(argv);
    // Service mode: no window, no singleton, no elevation prompt; started by the SCM only.
    if (service) { const int result = svc::runService(); if (SUCCEEDED(com)) CoUninitialize(); return result; }
    try {
        if (self) { const int result = selfTest(instance, uiTest); if (SUCCEEDED(com)) CoUninitialize(); return result; }
        const auto path = executable();
        if (replaceMode) {
            int code = 0;
            if (!administrator()) { elevate(path, std::wstring(update::kReplaceArgument) + L" \"" + replaceTarget + L"\"", code); return code; }
            code = update::runWithProgress(path, replaceTarget);
            if (SUCCEEDED(com)) CoUninitialize();
            return code;
        }
        if (updated && waitPid) {
            // The previous client (which started this update) still holds the single-instance lock.
            if (HANDLE previous = OpenProcess(SYNCHRONIZE, FALSE, waitPid)) { WaitForSingleObject(previous, 30000); CloseHandle(previous); }
        }
        if (!updated) {
            int code = 0;
            if (offerUpdate(path, code)) { if (SUCCEEDED(com)) CoUninitialize(); return code; }
        }
        if (alreadyRunning()) { showRunningInstance(); return 0; }
        if (!administrator()) { int code = 0; elevate(path, L"", code); return code; }
        if (!acquireInstance()) { showRunningInstance(); return 0; }
        update::cleanupLeftovers(path); // Previous program file left by an update of a running copy.
        const std::wstring directory = fs::directoryOf(path);
        const std::wstring configPath = fs::join(directory, kConfigFileName);
        // A first run has nothing configured yet, so it always shows the window once.
        const bool firstRun = !fs::isFile(configPath) && !fs::isFile(fs::join(directory, kLegacyIniName));
        // config.ini and the former service's appsettings.json become config.json once.
        const MigrationReport migration = migrateLegacyConfig(directory);
        Store store(configPath);
        // Deliberately process-lifetime: in a failed thread join we never free memory
        // that a hook/worker may still reference; process exit releases all resources.
        runtime = new Runtime;
        ClientUi* uiPointer = nullptr;
        const Settings initial = store.loadSettings();
        bool startup = initial.onSystemStart;
        // Auto-start goes straight to the tray: the main window is never shown, so it cannot flash.
        const bool startHidden = initial.autoStart && !firstRun && !updated;
        auto report = [&] {
            if (uiPointer) uiPointer->setStatus(L"按键控制发生错误，Windows 错误码：" + std::to_wstring(runtime->error()));
        };
        UiCallbacks callbacks;
        callbacks.start = [&](const Profile& p, const Settings& s) {
            const bool ok = runtime->configure(p, s, true); if (!ok) report(); return ok;
        };
        callbacks.stop = [&] {
            const bool ok = runtime->configure(uiPointer->currentProfile(), uiPointer->settings(), false);
            if (!ok) report();
            return ok;
        };
        callbacks.settingsChanged = [&](const Profile& p, const Settings& s) {
            if (s.onSystemStart != startup) { startupShortcut(s.onSystemStart); startup = s.onSystemStart; }
            const bool active = runtime->running;
            if (!runtime->configure(p, s, active)) { uiPointer->setRunning(runtime->running); report(); }
        };
        callbacks.quit = [&] { runtime->shutdown(); PostQuitMessage(0); };
        ClientUi ui(instance, store, std::move(callbacks)); uiPointer = &ui;
        if (!ui.create(true, !startHidden)) throw std::runtime_error("Cannot create main window");
        runtime->window = ui.window();
        QuitListener quitListener;
        quitListener.start(ui.window());
        const bool ready = runtime->configure(ui.currentProfile(), ui.settings(), false);
        if (!ready) report();
        if (initial.autoStart) {
            const bool started = ui.start(false);
            if (startHidden && (!ready || !started)) ui.show(); // Surface the error instead of failing silently in the tray.
        }
        // Reported after auto-start, which clears the footer line.
        if (!migration.error.empty()) {
            ui.show();
            ui.setStatus(L"部分旧配置未能迁移，相关旧文件保持不变：" + migration.error);
        } else {
            std::wstring hint;
            if (updated) {
                const auto now = update::inspect(path).version;
                hint = L"已更新到 v" + (now.valid ? update::text(now) : std::wstring(kProductVersion)) +
                    (updatedFrom.valid ? L"（原 v" + update::text(updatedFrom) + L"）" : L"") + L"，设置保持不变。";
            }
            if (migration.migrated()) {
                hint += L"已将旧配置合并为 config.json，并移除旧配置文件。";
                ui.show(); // Once, after moving from the earlier package: the result and the next step are visible.
            }
            hint += ui.serviceNotice();
            if (!hint.empty()) ui.showHint(hint);
        }
        SetTimer(ui.window(), kHealthTimer, 500, nullptr);
        MSG message{};
        int status;
        while ((status = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            if (message.message == kInputCommandMessage) {
                switch (static_cast<InputCommand>(message.wParam)) {
                case InputCommand::Start: ui.start(); break;
                case InputCommand::Stop: ui.stop(); break;
                case InputCommand::QuickSwitch: ui.quickSwitch(); break;
                case InputCommand::ToggleRun: ui.toggleRun(); break;
                case InputCommand::Exit: ui.quit(); break;
                case InputCommand::Error: { const DWORD error = runtime->error(); const bool ok = ui.stop(); ui.setStatus((ok ? L"按键控制异常，已停止。Windows 错误码：" : L"按键控制异常，停止未完成。Windows 错误码：") + std::to_wstring(error)); break; }
                }
                continue;
            }
            if (message.message == WM_TIMER && message.wParam == kHealthTimer) {
                const DWORD error = runtime->error();
                if (error && runtime->running) { const bool ok = ui.stop(); ui.setStatus((ok ? L"按键控制异常，已停止。Windows 错误码：" : L"按键控制异常，停止未完成。Windows 错误码：") + std::to_wstring(error)); }
                continue;
            }
            if (ui.processMessage(message)) continue;
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        KillTimer(ui.window(), kHealthTimer);
        const bool stopped = runtime->shutdown();
        if (stopped) { delete runtime; runtime = nullptr; }
        if (SUCCEEDED(com)) CoUninitialize();
        return status == -1 || !stopped ? 1 : static_cast<int>(message.wParam);
    } catch (const std::exception& error) {
        if (runtime) runtime->shutdown();
        if (self) { std::fprintf(stderr, "Self-test failed: %s\n", error.what()); return 20; }
        std::wstring detail; for (const unsigned char c : std::string(error.what())) detail.push_back(c);
        MessageBoxW(nullptr, (L"客户端启动或配置失败：\n" + detail).c_str(), L"DNF 连发工具", MB_ICONERROR);
        return 1;
    } catch (...) {
        if (runtime) runtime->shutdown();
        if (!self) MessageBoxW(nullptr, L"客户端发生异常，已请求停止按键控制。", L"DNF 连发工具", MB_ICONERROR);
        return 21;
    }
}
