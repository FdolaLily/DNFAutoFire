#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <cstdio>
#include <stdexcept>
#include "client_config.h"
#include "client_input.h"
#include "client_ui.h"
#include "engine_api.h"

namespace {
using namespace dafclient;
constexpr wchar_t kMutex[] = L"Global\\DNFAutoFire.Client.{B797BFB2-305A-44DC-9E06-76D6CC424419}";
constexpr UINT_PTR kHealthTimer = 0xDAF1;
std::wstring executable() {
    std::wstring path(32768, L'\0');
    const DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!count || count >= path.size()) throw std::runtime_error("GetModuleFileName failed");
    path.resize(count); return path;
}
bool administrator() {
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID group = nullptr; BOOL member = FALSE;
    if (!AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &group)) return false;
    CheckTokenMembership(nullptr, group, &member); FreeSid(group); return member != FALSE;
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
                    static_cast<unsigned>(rules.size()), profile.downMs * 1000,
                    profile.upMs * 1000, 0, window, 0);
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
        void* engine = AF_Start(rule, 1, 7000, 7000, 0, window, 0);
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
        Store store(std::wstring(temporary) + L"DNFAutoFire-ui-selftest-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
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
    if (argv) LocalFree(argv);
    try {
        if (self) { const int result = selfTest(instance, uiTest); if (SUCCEEDED(com)) CoUninitialize(); return result; }
        if (alreadyRunning()) { showRunningInstance(); return 0; }
        const auto path = executable();
        if (!administrator()) {
            SHELLEXECUTEINFOW request{}; request.cbSize = sizeof(request);
            request.lpVerb = L"runas"; request.lpFile = path.c_str(); request.nShow = SW_SHOWNORMAL;
            const auto directory = path.substr(0, path.find_last_of(L"\\/")); request.lpDirectory = directory.c_str();
            if (!ShellExecuteExW(&request)) return GetLastError() == ERROR_CANCELLED ? 0 : 1;
            return 0;
        }
        if (!acquireInstance()) { showRunningInstance(); return 0; }
        const std::wstring configPath = path.substr(0, path.find_last_of(L"\\/")) + L"\\config.ini";
        // A first run has nothing configured yet, so it always shows the window once.
        const bool firstRun = GetFileAttributesW(configPath.c_str()) == INVALID_FILE_ATTRIBUTES;
        Store store(configPath);
        // Deliberately process-lifetime: in a failed thread join we never free memory
        // that a hook/worker may still reference; process exit releases all resources.
        runtime = new Runtime;
        ClientUi* uiPointer = nullptr;
        const Settings initial = store.loadSettings();
        bool startup = initial.onSystemStart;
        // Auto-start goes straight to the tray: the main window is never shown, so it cannot flash.
        const bool startHidden = initial.autoStart && !firstRun;
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
        const bool ready = runtime->configure(ui.currentProfile(), ui.settings(), false);
        if (!ready) report();
        if (initial.autoStart) {
            const bool started = ui.start(false);
            if (startHidden && (!ready || !started)) ui.show(); // Surface the error instead of failing silently in the tray.
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
