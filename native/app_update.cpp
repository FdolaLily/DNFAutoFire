#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "app_update.h"
#include "app_ids.h"
#include "client_ui.h"
#include "config_migrate.h"
#include "json.h"
#include "process_util.h"
#include "service_control.h"
#include "win_fs.h"
#include <algorithm>
#include <cwctype>
#include <vector>

namespace dafclient::update {
namespace {
std::wstring lower(std::wstring s) { for (auto& c : s) c = wchar_t(towlower(c)); return s; }
WORD word(const BYTE* p) { return WORD(p[0] | (p[1] << 8)); }
size_t align4(size_t n) { return (n + 3) & ~size_t(3); }

// VS_VERSIONINFO is a tree of { wLength, wValueLength, wType, szKey, pad, Value, pad, Children }.
// Depth 0 carries VS_FIXEDFILEINFO; StringFileInfo \ <language> \ <name> holds the strings.
void walk(const BYTE* base, size_t size, int depth, bool strings, ExeInfo& info) {
    size_t offset = 0;
    while (offset + 6 <= size) {
        const BYTE* node = base + offset;
        const size_t length = word(node), valueLength = word(node + 2), type = word(node + 4);
        if (length < 6 || length > size - offset) return;
        std::wstring key;
        size_t at = 6;
        for (; at + 2 <= length; at += 2) {
            const wchar_t c = wchar_t(word(node + at));
            if (!c) { at += 2; break; }
            key.push_back(c);
        }
        at = align4(at);
        size_t valueBytes = type == 1 ? valueLength * 2 : valueLength;
        if (at > length) at = length;
        if (valueBytes > length - at) valueBytes = length - at;
        const BYTE* value = node + at;
        if (depth == 0 && valueBytes >= sizeof(VS_FIXEDFILEINFO)) {
            VS_FIXEDFILEINFO fixed{};
            memcpy(&fixed, value, sizeof(fixed));
            if (fixed.dwSignature == 0xFEEF04BD) {
                info.version.part[0] = HIWORD(fixed.dwFileVersionMS); info.version.part[1] = LOWORD(fixed.dwFileVersionMS);
                info.version.part[2] = HIWORD(fixed.dwFileVersionLS); info.version.part[3] = LOWORD(fixed.dwFileVersionLS);
                info.version.valid = true;
            }
        }
        if (depth == 3 && strings && type == 1) {
            std::wstring text;
            for (size_t i = 0; i + 2 <= valueBytes; i += 2) {
                const wchar_t c = wchar_t(word(value + i));
                if (!c) break;
                text.push_back(c);
            }
            const auto name = lower(key), content = lower(text);
            if ((name == L"internalname" && content == L"dnfautofire") || (name == L"originalfilename" && content == L"dnfautofire.exe"))
                info.product = true;
            // The AHK-era client (up to v0.1.4.x) only carried these two names.
            if ((name == L"productname" || name == L"filedescription") && text == L"DAF连发工具") info.legacy = true;
        }
        const size_t children = align4(at + valueBytes);
        if (depth < 3 && children < length)
            walk(node + children, length - children, depth + 1, depth == 1 ? lower(key) == L"stringfileinfo" : strings, info);
        offset = align4(offset + length);
    }
}

struct File {
    HANDLE h = INVALID_HANDLE_VALUE;
    explicit File(const std::wstring& path)
        : h(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                        FILE_FLAG_SEQUENTIAL_SCAN, nullptr)) {}
    ~File() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    bool ok() const { return h != INVALID_HANDLE_VALUE; }
};

Outcome failure(Outcome out, const std::wstring& message) { out.ok = false; out.message = message; return out; }
std::wstring errorText(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring text;
    if (n && buffer) {
        text.assign(buffer, n);
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ' || text.back() == L'。')) text.pop_back();
    }
    if (buffer) LocalFree(buffer);
    return text.empty() ? L"错误码 " + std::to_wstring(code) : text + L"（" + std::to_wstring(code) + L"）";
}
// Anti-virus scanners and Explorer may hold the file for a moment after its owner exited.
bool moveWithRetry(const std::wstring& from, const std::wstring& to, DWORD flags, DWORD& error) {
    for (int attempt = 0;; ++attempt) {
        if (MoveFileExW(from.c_str(), to.c_str(), flags)) { error = 0; return true; }
        error = GetLastError();
        if ((error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED && error != ERROR_LOCK_VIOLATION) || attempt >= 25) return false;
        Sleep(200);
    }
}

std::vector<HWND> autoHotkeyWindows() {
    std::vector<HWND> found;
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        wchar_t name[32]{};
        if (GetClassNameW(window, name, 32) && wcscmp(name, L"AutoHotkey") == 0) reinterpret_cast<std::vector<HWND>*>(data)->push_back(window);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&found));
    return found;
}
bool deleteWithRetry(const std::wstring& path) {
    for (int attempt = 0; attempt < 25; ++attempt) {
        if (DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) return true;
        // Files unpacked from an archive may be read-only; that alone must not keep them.
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY)) {
            SetFileAttributesW(path.c_str(), attributes & ~DWORD(FILE_ATTRIBUTE_READONLY));
            continue;
        }
        Sleep(200); // A service process may still be unmapping its image.
    }
    return false;
}
bool fileContains(const std::wstring& path, const char* marker) {
    try { return fs::read(path, 1024 * 1024).find(marker) != std::string::npos; } catch (...) { return false; }
}

// Ends every process that runs the target image, except this one. The client is
// asked first (it stops auto-fire and releases held keys itself); only processes
// still alive after the grace period are terminated.
bool stopTargetProcesses(const std::wstring& target, const Options& options, std::wstring& message) {
    const DWORD self = GetCurrentProcessId();
    if (options.askClient) {
        DWORD client = 0;
        if (HWND window = FindWindowW(kMainWindowClass, nullptr)) GetWindowThreadProcessId(window, &client);
        // The client holds the single-instance lock, so it must go even when it runs another copy.
        if (client && client != self) {
            if (HANDLE quit = OpenEventW(EVENT_MODIFY_STATE, FALSE, kClientQuitEvent)) { SetEvent(quit); CloseHandle(quit); }
            if (!svc::waitExit(client, options.graceMs)) {
                DWORD error = 0;
                if (!svc::terminate(client, 5000, error)) { message = L"连发程序未能退出：" + errorText(error ? error : WAIT_TIMEOUT); return false; }
            }
        }
    }
    const auto key = svc::processKey(target);
    std::vector<DWORD> running;
    for (const auto& entry : svc::snapshotProcesses()) {
        if (entry.pid == self || !svc::matches(entry, key)) continue;
        const auto image = svc::imagePath(entry.pid);
        if (!image.empty() && fs::samePath(image, target)) running.push_back(entry.pid);
    }
    // An AHK-era client closes on WM_CLOSE to its hidden main window (its exit routine releases keys).
    for (const HWND window : autoHotkeyWindows()) {
        DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
        if (std::find(running.begin(), running.end(), pid) != running.end()) PostMessageW(window, WM_CLOSE, 0, 0);
    }
    const auto deadline = GetTickCount64() + options.graceMs;
    for (const DWORD pid : running) {
        const auto now = GetTickCount64();
        if (svc::waitExit(pid, now < deadline ? DWORD(deadline - now) : 0)) continue;
        DWORD error = 0;
        if (!svc::terminate(pid, 5000, error)) {
            message = L"进程 " + std::to_wstring(pid) + L" 仍在使用已安装的程序：" + errorText(error ? error : WAIT_TIMEOUT);
            return false;
        }
    }
    return true;
}

bool launchClient(const std::wstring& target, const Version& from, bool waitForMe, DWORD& error) {
    std::wstring command = L"\"" + target + L"\" " + updatedArguments(from, waitForMe ? GetCurrentProcessId() : 0);
    const auto directory = fs::directoryOf(target);
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(target.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, directory.c_str(), &startup, &process)) {
        error = GetLastError();
        return false;
    }
    AllowSetForegroundWindow(process.dwProcessId); // The user just confirmed; let the new window come to the front.
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return true;
}
} // namespace

Version parseVersion(const std::wstring& raw) {
    Version v;
    std::wstring s = raw;
    while (!s.empty() && iswspace(s.front())) s.erase(s.begin());
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    if (!s.empty() && (s.front() == L'v' || s.front() == L'V')) s.erase(s.begin());
    if (s.empty()) return v;
    size_t index = 0, at = 0;
    for (;;) {
        if (index >= 4 || at >= s.size() || !iswdigit(s[at])) return Version{};
        unsigned long value = 0;
        while (at < s.size() && iswdigit(s[at])) {
            value = value * 10 + unsigned(s[at++] - L'0');
            if (value > 65535) return Version{};
        }
        v.part[index++] = unsigned(value);
        if (at == s.size()) break;
        if (s[at++] != L'.') return Version{};
    }
    v.valid = true;
    return v;
}
int compare(const Version& a, const Version& b) {
    if (a.valid != b.valid) return a.valid ? 1 : -1;
    for (int i = 0; i < 4; ++i) if (a.part[i] != b.part[i]) return a.part[i] < b.part[i] ? -1 : 1;
    return 0;
}
std::wstring text(const Version& v) {
    if (!v.valid) return L"?";
    return std::to_wstring(v.part[0]) + L"." + std::to_wstring(v.part[1]) + L"." + std::to_wstring(v.part[2]) + L"." + std::to_wstring(v.part[3]);
}
Version currentVersion() { return parseVersion(kProductVersion); }

ExeInfo inspect(const std::wstring& path) {
    ExeInfo info;
    HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!module) return info;
    if (HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(16) /* RT_VERSION */)) {
        const DWORD size = SizeofResource(module, resource);
        HGLOBAL loaded = LoadResource(module, resource);
        const auto* data = loaded ? static_cast<const BYTE*>(LockResource(loaded)) : nullptr;
        if (data && size) walk(data, size, 0, false, info);
    }
    FreeLibrary(module);
    return info;
}

bool sameContent(const std::wstring& a, const std::wstring& b) {
    File fa(a), fb(b);
    LARGE_INTEGER sa{}, sb{};
    if (!fa.ok() || !fb.ok() || !GetFileSizeEx(fa.h, &sa) || !GetFileSizeEx(fb.h, &sb) || sa.QuadPart != sb.QuadPart) return false;
    std::vector<BYTE> ba(1 << 16), bb(1 << 16);
    for (;;) {
        DWORD ra = 0, rb = 0;
        if (!ReadFile(fa.h, ba.data(), DWORD(ba.size()), &ra, nullptr) || !ReadFile(fb.h, bb.data(), DWORD(bb.size()), &rb, nullptr)) return false;
        if (ra != rb || memcmp(ba.data(), bb.data(), ra) != 0) return false;
        if (!ra) return true;
    }
}

Offer classify(const Version& self, const Version& installed, bool identical) {
    if (identical) return Offer::None;
    const int order = compare(self, installed);
    return order > 0 ? Offer::Upgrade : order == 0 ? Offer::Reinstall : Offer::Downgrade;
}

bool chooseTarget(const std::vector<Candidate>& candidates, const std::wstring& self, Candidate& chosen) {
    for (const auto& candidate : candidates) {
        if (candidate.path.empty() || fs::samePath(candidate.path, self) || !fs::isFile(candidate.path)) continue;
        chosen = candidate;
        chosen.path = fs::fullPath(candidate.path);
        return true;
    }
    return false;
}

std::wstring legacyClientFor(const std::wstring& serviceExe) {
    if (serviceExe.empty()) return L"";
    const auto directory = fs::directoryOf(fs::fullPath(serviceExe));
    try {
        const auto options = importAppSettings(fs::read(fs::join(directory, kLegacyServiceJsonName), 1024 * 1024));
        for (const auto& item : options.autoStart) {
            const auto path = svc::resolvePath(item, directory);
            if (fs::isFile(path) && inspect(path).replaceable()) return path;
        }
    } catch (...) {} // Missing or unreadable settings: the documented default layout.
    const auto beside = fs::join(directory, L"DNFAutoFire.exe");
    return fs::isFile(beside) ? beside : L"";
}

std::wstring runningLegacyClient(const std::wstring& self) {
    for (const HWND window : autoHotkeyWindows()) {
        DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
        if (!pid || pid == GetCurrentProcessId()) continue;
        const auto image = svc::imagePath(pid);
        if (!image.empty() && !fs::samePath(image, self) && inspect(image).legacy) return image;
    }
    return L"";
}

bool findInstalled(const std::wstring& self, Target& target) {
    const auto service = svcctl::query(self);
    // The service already runs this very file: nothing else counts as "installed".
    if (service.installed && service.pointsHere) return false;
    std::vector<Candidate> candidates;
    if (service.installed) candidates.push_back({svcctl::commandExecutable(service.command), Source::Service});
    std::wstring legacyName, legacyExe;
    for (size_t i = 0; i < service.legacy.size() && i < service.legacyCommands.size(); ++i) {
        const auto exe = svcctl::commandExecutable(service.legacyCommands[i]);
        if (exe.empty()) continue;
        if (legacyName.empty()) { legacyName = service.legacy[i]; legacyExe = exe; }
        candidates.push_back({legacyClientFor(exe), Source::LegacyService});
    }
    DWORD pid = 0;
    if (HWND window = FindWindowW(kMainWindowClass, nullptr)) GetWindowThreadProcessId(window, &pid);
    if (pid && pid != GetCurrentProcessId()) candidates.push_back({svc::imagePath(pid), Source::Client});
    candidates.push_back({runningLegacyClient(self), Source::Client});
    Candidate chosen;
    // Never offer to overwrite a file that is not this program.
    while (chooseTarget(candidates, self, chosen)) {
        const auto info = inspect(chosen.path);
        if (info.replaceable()) {
            target.path = chosen.path; target.info = info; target.source = chosen.source;
            target.legacyService = legacyName; target.legacyServiceExe = legacyExe;
            return true;
        }
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
            [&](const Candidate& c) { return !c.path.empty() && fs::samePath(c.path, chosen.path); }), candidates.end());
    }
    return false;
}

std::vector<std::wstring> removeLegacyFiles(const std::wstring& serviceExe) {
    std::vector<std::wstring> removed;
    if (serviceExe.empty()) return removed;
    const auto exe = fs::fullPath(serviceExe);
    const auto directory = fs::directoryOf(exe);
    const auto name = [](const std::wstring& path) { std::wstring n = fs::fileName(path); for (auto& c : n) c = wchar_t(towlower(c)); return n; };
    const auto drop = [&](const std::wstring& path) {
        if (fs::isFile(path) && deleteWithRetry(path)) removed.push_back(fs::fileName(path));
    };
    // Only the known service binaries: never an arbitrary file a service happened to point at.
    const auto binary = name(exe);
    if (binary == L"dnfprocessmanager.exe" || binary == L"automanagerprocess.exe") {
        drop(exe);
        drop(exe.substr(0, exe.size() - 4) + L".pdb");
    }
    const auto manager = fs::join(directory, L"服务管理.bat");
    if (fileContains(manager, "DNFProcessManager")) drop(manager);
    const auto toolbox = fs::join(directory, L"DNF专用工具箱8.0.bat"); // Built into the client now.
    if (fileContains(toolbox, "dnf-toolbox-disabled")) drop(toolbox);
    if (!fs::isFile(fs::join(directory, kLegacyServiceJsonName))) drop(fs::join(directory, kLegacyServiceDevJsonName));
    const auto logs = fs::join(directory, kLogDirectory);
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(fs::join(logs, L"auto-manager*.log").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) drop(fs::join(logs, data.cFileName));
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    RemoveDirectoryW(logs.c_str()); // Only succeeds when nothing else (service.log) is there.
    return removed;
}

bool adoptLegacySettings(const std::wstring& serviceExe, const std::wstring& client) {
    if (serviceExe.empty() || client.empty()) return false;
    const auto from = fs::directoryOf(fs::fullPath(serviceExe)), to = fs::directoryOf(fs::fullPath(client));
    if (fs::samePath(from, to)) return false; // Migrated in place by the client.
    const auto source = fs::join(from, kLegacyServiceJsonName), destination = fs::join(to, kLegacyServiceJsonName);
    if (!fs::isFile(source) || fs::isFile(destination)) return false;
    try {
        if (fs::isFile(fs::join(to, kConfigFileName)) && json::parse(fs::read(fs::join(to, kConfigFileName), 4 * 1024 * 1024)).find(L"service"))
            return false; // The client already has service settings of its own.
        auto options = importAppSettings(fs::read(source, 1024 * 1024));
        for (auto& item : options.autoStart) item = svc::resolvePath(item, from); // Relative to the old service folder.
        json::Value manager = json::Value::object();
        manager.set(L"ProcessName", options.gameProcess);
        manager.set(L"ProcessPollSeconds", options.pollSeconds);
        manager.set(L"ActionDelaySeconds", options.actionDelaySeconds);
        manager.set(L"CloseLauncherIfGameNotStarted", options.closeLauncherAfterGame);
        manager.set(L"OptimizeGamePriority", options.optimizeGamePriority);
        manager.set(L"GamePriority", options.aboveNormalPriority ? L"AboveNormal" : L"Normal");
        manager.set(L"LimitList", json::Value::strings(options.limit));
        manager.set(L"KillList", json::Value::strings(options.kill));
        manager.set(L"AutoStart", json::Value::strings(options.autoStart));
        manager.set(L"AutoStop", json::Value::strings(options.autoStop));
        json::Value root = json::Value::object();
        root.set(L"Manager", std::move(manager));
        fs::writeAtomic(destination, json::serialize(root));
        fs::remove(source);
        return true;
    } catch (...) { return false; }
}

Outcome replace(const std::wstring& sourceRaw, const std::wstring& targetRaw, const Options& options, const Progress& progress) {
    Outcome out;
    const auto say = [&](const std::wstring& step) { if (progress) progress(step); };
    const auto source = fs::fullPath(sourceRaw), target = fs::fullPath(targetRaw);
    if (!fs::isFile(source)) return failure(out, L"找不到新版本文件：" + source);
    const auto from = inspect(source);
    if (!from.product) return failure(out, L"所选文件不是 DAF 连发工具程序，未作任何更改");
    out.to = from.version;
    if (fs::samePath(source, target)) return failure(out, L"新版本文件就是已安装的程序本身，无需替换");
    if (!fs::isFile(target)) return failure(out, L"找不到已安装的程序：" + target);
    const auto installed = inspect(target);
    if (!installed.replaceable()) return failure(out, L"目标文件不是 DAF 连发工具程序，未作任何更改：" + target);
    out.from = installed.version;
    if (sameContent(source, target)) { out.ok = true; out.message = L"两个文件完全相同，无需更新"; return out; }

    // 1. The service keeps the installed EXE open: stop it through the SCM (never by killing it).
    bool restartService = false;
    // Earlier stand-alone services (DNFProcessManager) are replaced by the DNFAutoFire service.
    std::vector<std::wstring> legacyStopped, legacyExes;
    if (options.manageService) {
        const auto info = svcctl::query(target);
        if (info.error) return failure(out, L"无法读取服务状态（错误码 " + std::to_wstring(info.error) + L"），未作任何更改");
        const auto restoreLegacy = [&] { for (const auto& name : legacyStopped) svcctl::startNamed(name.c_str()); };
        for (size_t i = 0; i < info.legacy.size(); ++i) {
            if (i < info.legacyCommands.size()) legacyExes.push_back(svcctl::commandExecutable(info.legacyCommands[i]));
            say(L"正在停止旧版服务 " + info.legacy[i] + L"…");
            const auto stopped = svcctl::stopNamed(info.legacy[i].c_str());
            if (!stopped.ok) { restoreLegacy(); return failure(out, stopped.message + L"，未作任何更改"); }
            legacyStopped.push_back(info.legacy[i]);
        }
        if (info.installed && info.pointsHere && info.state != SERVICE_STOPPED) {
            say(L"正在停止后台服务…");
            const auto stopped = svcctl::stop();
            if (!stopped.ok) { restoreLegacy(); return failure(out, stopped.message + L"，未作任何更改"); }
            restartService = true;
        }
    }
    const auto restoreService = [&] {
        if (restartService) svcctl::start();
        for (const auto& name : legacyStopped) svcctl::startNamed(name.c_str());
    };

    // 2. Clients using the file (and the one holding the single-instance lock) exit.
    say(L"正在关闭连发程序（先停止连发并释放按键）…");
    std::wstring message;
    if (!stopTargetProcesses(target, options, message)) { restoreService(); return failure(out, message + L"，未作任何更改"); }

    // 3. Swap: copy beside the target, rename the target away (allowed even while this
    //    process runs from it), move the copy in. Any failure puts the original back.
    say(L"正在替换程序文件…");
    const auto suffix = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    const auto staged = target + L".update-" + suffix + L".tmp";
    const auto old = target + L".old-" + suffix;
    if (!CopyFileW(source.c_str(), staged.c_str(), FALSE) || !sameContent(source, staged)) {
        const DWORD error = GetLastError();
        DeleteFileW(staged.c_str()); restoreService();
        return failure(out, L"无法在程序目录写入新版本：" + errorText(error ? error : ERROR_WRITE_FAULT) + L"，未作任何更改");
    }
    DWORD error = 0;
    if (!moveWithRetry(target, old, 0, error)) {
        DeleteFileW(staged.c_str()); restoreService();
        return failure(out, L"已安装的程序仍被占用：" + errorText(error) + L"，未作任何更改");
    }
    if (!moveWithRetry(staged, target, MOVEFILE_WRITE_THROUGH, error)) {
        DWORD ignored = 0;
        moveWithRetry(old, target, 0, ignored);
        DeleteFileW(staged.c_str()); restoreService();
        return failure(out, L"无法写入新版本：" + errorText(error) + L"，已恢复原程序");
    }
    out.changed = true;
    // No backup is kept. A copy still mapped by this very process goes on its last handle
    // (the reopened client removes it, otherwise it is deleted at the next restart).
    if (!DeleteFileW(old.c_str())) MoveFileExW(old.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);

    // 4. The client first, so a service that sees a running game does not start a second one.
    std::wstring problems;
    // An old service kept its settings beside itself; the client migrates only its own folder.
    for (const auto& exe : legacyExes) adoptLegacySettings(exe, target);
    if (options.launch) {
        say(L"正在打开新版本…");
        DWORD launchError = 0;
        if (!launchClient(target, out.from, options.holdsInstance, launchError)) problems += L"无法打开新版本：" + errorText(launchError) + L"。";
    }
    if (restartService || !legacyStopped.empty()) {
        say(legacyStopped.empty() ? L"正在启动后台服务…" : L"正在安装新的后台服务并删除旧版服务…");
        // Re-registering also refreshes the description and recovery settings of the new version,
        // and deletes the earlier stand-alone services.
        const auto started = svcctl::install(target);
        if (!started.ok) problems += started.message + L"。";
    }
    if (!legacyStopped.empty()) {
        if (svcctl::query(target).legacy.empty()) {
            say(L"正在删除旧版服务程序和脚本…");
            for (const auto& exe : legacyExes) for (auto& name : removeLegacyFiles(exe)) out.removed.push_back(std::move(name));
        } else problems += L"旧版服务未能删除，旧版服务程序和脚本保留。";
    }
    out.ok = problems.empty();
    out.message = L"已从 v" + text(out.from) + L" 更新到 v" + text(out.to) + (problems.empty() ? L"" : L"，但" + problems);
    return out;
}

unsigned cleanupLeftovers(const std::wstring& executable) {
    const auto directory = fs::directoryOf(executable), name = fs::fileName(executable);
    unsigned removed = 0;
    for (const auto& pattern : {name + L".old-*", name + L".update-*.tmp"}) {
        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileW(fs::join(directory, pattern).c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) continue;
        do {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (DeleteFileW(fs::join(directory, data.cFileName).c_str())) ++removed;
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    return removed;
}

std::wstring updatedArguments(const Version& from, DWORD waitPid) {
    return std::wstring(kUpdatedArgument) + L" " + (from.valid ? text(from) : L"0") + L" " + std::to_wstring(waitPid);
}
bool parseUpdated(int argc, wchar_t** argv, Version& from, DWORD& waitPid) {
    if (argc != 4 || !argv || _wcsicmp(argv[1], kUpdatedArgument) != 0) return false;
    from = parseVersion(argv[2]);
    wchar_t* end = nullptr;
    const unsigned long pid = wcstoul(argv[3], &end, 10);
    if (!end || *end) return false;
    waitPid = DWORD(pid);
    return true;
}

} // namespace dafclient::update
