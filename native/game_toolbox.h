#pragma once
// Native port of "DNF专用工具箱8.0.bat": disable / restore unused DNF and Tencent
// components, clean logs and caches, and locate the game's root directory.
// File names, backup suffix and marker names are identical to the 8.0 script, so
// components it disabled earlier are recognised and restored here, and vice versa.
// Download and update components (BackgroundDownloader, Tencentdl, TenioDL, TesService,
// QQDownload, QQMiniDL, DeskUpdate, TXPTOP, TXFTN) are never touched.
#include <string>
#include <vector>

namespace dafclient::toolbox {

constexpr wchar_t kBackupSuffix[] = L".dnf-toolbox-disabled";
constexpr wchar_t kMarkerName[] = L".blocked-by-dnf-toolbox";

struct Roots {
    std::wstring game;            // Game root (contains DNF.exe).
    std::wstring tencentRoaming;  // %APPDATA%\Tencent
    std::wstring dnfUserCache;    // %USERPROFILE%\AppData\LocalLow\DNF
};
Roots userRoots(const std::wstring& gameDirectory); // Current user's folders.

enum class Kind { File, Directory };
enum class Group { Game, Tencent };
struct Item {
    std::wstring label;
    Group group;
    Kind kind;
    std::wstring path;
    bool createAlways; // Placeholder created even when the component is absent (Pandora 9193).
};
std::vector<Item> items(const Roots& roots);

enum class State { NotInstalled, Normal, Disabled };
State status(const Item& item);

enum class Outcome { Changed, Unchanged, NotInstalled, NoBackup, Conflict, Failed };
// A file is replaced by a directory holding the marker; a directory by an empty file
// with a sibling "<name>.blocked-by-dnf-toolbox" marker. The original is moved to
// "<name>.dnf-toolbox-disabled" and never overwritten.
Outcome disable(const Item& item);
Outcome restore(const Item& item);

struct Report {
    unsigned changed = 0, unchanged = 0, notInstalled = 0, noBackup = 0, conflicts = 0, failed = 0;
    std::vector<std::wstring> problems; // Labels with a conflict or failure.
};
Report disableAll(const Roots& roots);
Report restoreAll(const Roots& roots);

struct CleanReport { unsigned deleted = 0, failed = 0; };
// Logs, crash dumps and temporary files in the game root, the per-user cache
// (keeping DNF.cfg), Tencent's dnf.tlg and QQCall*.exe. Downloads are left alone.
CleanReport cleanLogsAndCache(const Roots& roots);
// "修复黑屏连接服务器": empties the per-user cache including DNF.cfg.
CleanReport resetUserCache(const Roots& roots);

bool isGameDirectory(const std::wstring& directory); // Contains DNF.exe.
bool gameRunning();                                   // Any DNF.exe process.

struct Detection {
    std::wstring directory;   // Empty when nothing was found.
    std::wstring source;      // Where it came from, for the UI.
};
// Remembered path, running DNF.exe, running DNF launcher, uninstall / Tencent
// registry entries, then common install folders on fixed drives.
Detection detectGameDirectory(const std::wstring& remembered);

} // namespace dafclient::toolbox
