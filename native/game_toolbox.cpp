#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include "game_toolbox.h"
#include "process_util.h"
#include "win_fs.h"
#include <algorithm>
#include <cwctype>
#include <functional>

namespace dafclient::toolbox {
namespace {
std::wstring lower(std::wstring s) { for (auto& c : s) c = wchar_t(towlower(c)); return s; }
bool isDirectory(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
bool exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }
std::wstring knownFolder(const KNOWNFOLDERID& id) {
    wchar_t* path = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path)) && path) result = path;
    if (path) CoTaskMemFree(path);
    return result;
}
bool makeDirectories(const std::wstring& path) {
    if (path.empty() || isDirectory(path)) return true;
    const auto parent = fs::directoryOf(path);
    if (parent != path && parent != L"." && !makeDirectories(parent)) return false;
    return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}
bool writeMarker(const std::wstring& path) {
    SYSTEMTIME t{}; GetLocalTime(&t);
    wchar_t text[64];
    swprintf_s(text, L"Blocked at %04u-%02u-%02u %02u:%02u:%02u\r\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    std::string bytes;
    for (const wchar_t* p = text; *p; ++p) bytes.push_back(char(*p));
    try { fs::writeAtomic(path, bytes); return true; } catch (...) { return false; }
}
bool move(const std::wstring& from, const std::wstring& to) { return MoveFileExW(from.c_str(), to.c_str(), 0) != FALSE; }
// Recursively removes a directory tree (rd /s /q), without following junctions.
bool removeTree(const std::wstring& path) {
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(fs::join(path, L"*").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = data.cFileName;
            if (name == L"." || name == L"..") continue;
            const auto child = fs::join(path, name);
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) removeTree(child);
            else if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryW(child.c_str());
            else fs::remove(child);
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    return RemoveDirectoryW(path.c_str()) || !exists(path);
}
// del /f /q <directory>\<pattern> (files only); recursive visits subdirectories like del /s.
void deleteFiles(const std::wstring& directory, const std::wstring& pattern, bool recursive, CleanReport& report,
                 const std::function<bool(const std::wstring&)>& keep = {}) {
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(fs::join(directory, pattern).c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const auto file = fs::join(directory, data.cFileName);
            if (keep && keep(file)) continue;
            if (fs::remove(file)) ++report.deleted; else ++report.failed;
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    if (!recursive) return;
    find = FindFirstFileW(fs::join(directory, L"*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring name = data.cFileName;
        if (name == L"." || name == L".." || !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        deleteFiles(fs::join(directory, name), pattern, true, report, keep);
    } while (FindNextFileW(find, &data));
    FindClose(find);
}
} // namespace

Roots userRoots(const std::wstring& gameDirectory) {
    Roots roots;
    roots.game = gameDirectory;
    const auto roaming = knownFolder(FOLDERID_RoamingAppData);
    if (!roaming.empty()) roots.tencentRoaming = fs::join(roaming, L"Tencent");
    const auto localLow = knownFolder(FOLDERID_LocalAppDataLow);
    if (!localLow.empty()) roots.dnfUserCache = fs::join(localLow, L"DNF");
    return roots;
}

std::vector<Item> items(const Roots& r) {
    std::vector<Item> list;
    const auto game = [&](const wchar_t* label, Kind kind, const wchar_t* relative, bool always = false) {
        if (!r.game.empty()) list.push_back({label, Group::Game, kind, fs::join(r.game, relative), always});
    };
    const auto tencent = [&](const wchar_t* label, Kind kind, const wchar_t* relative) {
        if (!r.tencentRoaming.empty()) list.push_back({label, Group::Tencent, kind, fs::join(r.tencentRoaming, relative), false});
    };
    game(L"Install.dll", Kind::File, L"Install.dll");
    game(L"TP3Helper.exe", Kind::File, L"TP3Helper.exe");
    game(L"游戏目录 TGuard", Kind::Directory, L"TGuard");
    game(L"启动器广告窗口", Kind::File, L"start\\AdvertDialog.exe");
    game(L"启动器广告提示", Kind::File, L"start\\AdvertTips.exe");
    game(L"井盖（Pandora 9193 / GRobot）", Kind::File, L"Pandora\\cache\\archive\\9193\\gamelet9193GRobot_bin.zip", true);
    for (const wchar_t* name : {L"AndroidAssist", L"AndroidServer", L"QQDoctor", L"MiniQBrowser", L"QQPCMgr", L"QQPhoneAssistant",
                                L"QQPhoneManager", L"TAS", L"TCLSCore", L"WebGamePlugin"})
        tencent(name, Kind::Directory, name);
    tencent(L"gjdatareport.dll", Kind::File, L"Common\\gjdatareport.dll");
    return list;
}

State status(const Item& item) {
    const auto& t = item.path;
    if (item.kind == Kind::File) {
        if (exists(fs::join(t, kMarkerName)) || isDirectory(t)) return State::Disabled;
        return exists(t) ? State::Normal : State::NotInstalled;
    }
    if (exists(t + kMarkerName)) return State::Disabled;
    if (isDirectory(t)) return State::Normal;
    return exists(t) ? State::Disabled : State::NotInstalled;
}

Outcome disable(const Item& item) {
    const auto& target = item.path;
    const auto backup = target + kBackupSuffix;
    if (item.kind == Kind::File) {
        if (isDirectory(target)) return Outcome::Unchanged;
        if (!exists(target)) {
            if (!item.createAlways) return Outcome::NotInstalled;
        } else {
            if (exists(backup)) return Outcome::Conflict;
            if (!move(target, backup)) return Outcome::Failed;
        }
        if (!makeDirectories(fs::directoryOf(target)) || !CreateDirectoryW(target.c_str(), nullptr)) return Outcome::Failed;
        return writeMarker(fs::join(target, kMarkerName)) ? Outcome::Changed : Outcome::Failed;
    }
    const auto marker = target + kMarkerName;
    if (exists(marker)) return Outcome::Unchanged;
    if (exists(target) && !isDirectory(target)) return Outcome::Unchanged;
    if (!isDirectory(target)) {
        if (!item.createAlways) return Outcome::NotInstalled;
    } else {
        if (isDirectory(backup)) return Outcome::Conflict;
        if (!move(target, backup)) return Outcome::Failed;
    }
    if (!makeDirectories(fs::directoryOf(target))) return Outcome::Failed;
    HANDLE placeholder = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (placeholder == INVALID_HANDLE_VALUE) return Outcome::Failed;
    CloseHandle(placeholder);
    return writeMarker(marker) ? Outcome::Changed : Outcome::Failed;
}

Outcome restore(const Item& item) {
    const auto& target = item.path;
    const auto backup = target + kBackupSuffix;
    if (item.kind == Kind::File) {
        // Only a placeholder directory carrying our marker is removed, never a user's folder.
        if (exists(fs::join(target, kMarkerName)) && !removeTree(target)) return Outcome::Failed;
        if (!exists(backup)) return Outcome::NoBackup;
        if (exists(target)) return Outcome::Conflict;
        return move(backup, target) ? Outcome::Changed : Outcome::Failed;
    }
    const auto marker = target + kMarkerName;
    if (exists(marker)) { fs::remove(target); fs::remove(marker); }
    if (exists(target) && !isDirectory(target) && !isDirectory(backup)) return Outcome::NoBackup;
    if (!isDirectory(backup)) return Outcome::NoBackup;
    if (exists(target)) return Outcome::Conflict;
    return move(backup, target) ? Outcome::Changed : Outcome::Failed;
}

namespace {
Report apply(const Roots& roots, Outcome (*operation)(const Item&)) {
    Report report;
    for (const auto& item : items(roots)) {
        switch (operation(item)) {
        case Outcome::Changed: ++report.changed; break;
        case Outcome::Unchanged: ++report.unchanged; break;
        case Outcome::NotInstalled: ++report.notInstalled; break;
        case Outcome::NoBackup: ++report.noBackup; break;
        case Outcome::Conflict: ++report.conflicts; report.problems.push_back(item.label); break;
        case Outcome::Failed: ++report.failed; report.problems.push_back(item.label); break;
        }
    }
    return report;
}
}
Report disableAll(const Roots& roots) { return apply(roots, &disable); }
Report restoreAll(const Roots& roots) { return apply(roots, &restore); }

CleanReport cleanLogsAndCache(const Roots& roots) {
    CleanReport report;
    if (!roots.game.empty()) {
        for (const wchar_t* pattern : {L"*_tmp.dat", L"debug.log", L"gameloader.log", L"LagLog.txt", L"BugTrace.log",
                                       L"awesomium.log", L"CrashDNF2.cra", L"Thread*.*"})
            deleteFiles(roots.game, pattern, false, report);
    }
    if (!roots.dnfUserCache.empty() && isDirectory(roots.dnfUserCache)) {
        const auto config = lower(fs::join(roots.dnfUserCache, L"DNF.cfg"));
        deleteFiles(roots.dnfUserCache, L"*", true, report, [&](const std::wstring& file) { return lower(file) == config; });
    }
    if (!roots.tencentRoaming.empty()) {
        deleteFiles(fs::join(roots.tencentRoaming, L"Logs"), L"dnf.tlg", false, report);
        deleteFiles(roots.tencentRoaming, L"QQCall*.exe", false, report);
    }
    return report;
}
CleanReport resetUserCache(const Roots& roots) {
    CleanReport report;
    if (!roots.dnfUserCache.empty() && isDirectory(roots.dnfUserCache)) deleteFiles(roots.dnfUserCache, L"*", true, report);
    return report;
}

bool isGameDirectory(const std::wstring& directory) {
    return !directory.empty() && fs::isFile(fs::join(directory, L"DNF.exe"));
}
bool gameRunning() {
    const auto key = svc::processKey(L"DNF.exe");
    for (const auto& e : svc::snapshotProcesses()) if (svc::matches(e, key)) return true;
    return false;
}

namespace {
// The directory itself or one of its immediate subdirectories.
std::wstring gameIn(const std::wstring& directory) {
    if (directory.empty() || !isDirectory(directory)) return L"";
    if (isGameDirectory(directory)) return fs::fullPath(directory);
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(fs::join(directory, L"*").c_str(), &data);
    std::wstring found;
    if (find == INVALID_HANDLE_VALUE) return found;
    do {
        const std::wstring name = data.cFileName;
        if (name == L"." || name == L".." || !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        const auto child = fs::join(directory, name);
        if (isGameDirectory(child)) { found = fs::fullPath(child); break; }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return found;
}
std::wstring regString(HKEY key, const wchar_t* name) {
    wchar_t buffer[1024]; DWORD size = sizeof(buffer), type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(buffer), &size) != ERROR_SUCCESS) return L"";
    if (type != REG_SZ && type != REG_EXPAND_SZ) return L"";
    buffer[std::min<size_t>(size / sizeof(wchar_t), 1023)] = L'\0';
    std::wstring value = buffer;
    if (type == REG_EXPAND_SZ) {
        wchar_t expanded[1024];
        if (ExpandEnvironmentStringsW(value.c_str(), expanded, 1024)) value = expanded;
    }
    return value;
}
// "C:\x\uninst.exe" /S  or  C:\x\DNF.exe,0  ->  C:\x
std::wstring directoryFromCommand(std::wstring text) {
    while (!text.empty() && iswspace(text.front())) text.erase(text.begin());
    if (text.empty()) return L"";
    if (text.front() == L'"') { const auto end = text.find(L'"', 1); text = text.substr(1, end == std::wstring::npos ? std::wstring::npos : end - 1); }
    else {
        const auto exe = lower(text).find(L".exe");
        if (exe != std::wstring::npos) text = text.substr(0, exe + 4);
        const auto comma = text.find(L',');
        if (comma != std::wstring::npos) text = text.substr(0, comma);
    }
    return fs::directoryOf(text);
}
std::wstring fromRegistry() {
    const struct { HKEY root; const wchar_t* path; } tencent[] = {
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Tencent\\DNF"}, {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Tencent\\DNF"},
        {HKEY_CURRENT_USER, L"SOFTWARE\\Tencent\\DNF"}};
    for (const auto& k : tencent) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(k.root, k.path, 0, KEY_READ, &key) != ERROR_SUCCESS) continue;
        std::wstring found;
        for (const wchar_t* value : {L"InstallPath", L"Install Path", L"GamePath", L"InstallDir", L"Path"})
            if ((found = gameIn(regString(key, value))).size()) break;
        RegCloseKey(key);
        if (!found.empty()) return found;
    }
    const struct { HKEY root; const wchar_t* path; } uninstall[] = {
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"},
        {HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"}};
    for (const auto& u : uninstall) {
        HKEY root = nullptr;
        if (RegOpenKeyExW(u.root, u.path, 0, KEY_READ, &root) != ERROR_SUCCESS) continue;
        std::wstring found;
        wchar_t name[256];
        for (DWORD i = 0; found.empty(); ++i) {
            DWORD length = 256;
            if (RegEnumKeyExW(root, i, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            HKEY app = nullptr;
            if (RegOpenKeyExW(root, name, 0, KEY_READ, &app) != ERROR_SUCCESS) continue;
            const auto display = regString(app, L"DisplayName");
            if (display.find(L"地下城与勇士") != std::wstring::npos || display.find(L"DNF") != std::wstring::npos) {
                for (const auto& candidate : {regString(app, L"InstallLocation"), directoryFromCommand(regString(app, L"DisplayIcon")),
                                              directoryFromCommand(regString(app, L"UninstallString"))})
                    if ((found = gameIn(candidate)).size()) break;
            }
            RegCloseKey(app);
        }
        RegCloseKey(root);
        if (!found.empty()) return found;
    }
    return L"";
}
std::wstring fromCommonFolders() {
    const wchar_t* relative[] = {L"WeGameApps\\地下城与勇士：创新世纪", L"WeGameApps\\地下城与勇士", L"WeGameApps\\DNF",
        L"地下城与勇士", L"腾讯游戏\\地下城与勇士", L"Program Files (x86)\\腾讯游戏\\地下城与勇士", L"Program Files\\腾讯游戏\\地下城与勇士"};
    const DWORD drives = GetLogicalDrives();
    for (wchar_t letter = L'C'; letter <= L'Z'; ++letter) {
        if (!(drives & (1u << (letter - L'A')))) continue;
        const std::wstring root = std::wstring(1, letter) + L":\\";
        if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED) continue;
        for (const auto* r : relative) if (const auto found = gameIn(root + r); !found.empty()) return found;
        // Any game installed directly under WeGameApps.
        if (const auto found = gameIn(root + L"WeGameApps"); !found.empty()) return found;
    }
    return L"";
}
} // namespace

Detection detectGameDirectory(const std::wstring& remembered) {
    if (isGameDirectory(remembered)) return {fs::fullPath(remembered), L"上次使用的目录"};
    const auto processes = svc::snapshotProcesses();
    const auto dnf = svc::processKey(L"DNF.exe");
    for (const auto& e : processes) {
        if (!svc::matches(e, dnf)) continue;
        const auto directory = fs::directoryOf(svc::imagePath(e.pid));
        if (isGameDirectory(directory)) return {directory, L"正在运行的 DNF"};
    }
    // The DNF launcher (Electron client.exe) sits beside or above the game folder.
    for (const auto& e : processes) {
        if (lower(e.name) != L"client.exe") continue;
        const auto launcher = fs::directoryOf(svc::imagePath(e.pid));
        if (launcher.empty() || !fs::isFile(fs::join(launcher, L"resources\\app.asar"))) continue;
        auto directory = launcher;
        for (int level = 0; level < 3 && directory.size() > 3; ++level) {
            if (const auto found = gameIn(directory); !found.empty()) return {found, L"DNF 启动器"};
            const auto parent = fs::directoryOf(directory);
            if (parent == directory || parent == L".") break;
            directory = parent;
        }
    }
    if (const auto found = fromRegistry(); !found.empty()) return {found, L"注册表"};
    if (const auto found = fromCommonFolders(); !found.empty()) return {found, L"常见安装位置"};
    return {};
}

} // namespace dafclient::toolbox
