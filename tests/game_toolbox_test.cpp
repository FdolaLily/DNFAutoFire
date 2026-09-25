// Toolbox (former DNF专用工具箱8.0.bat) on a throw-away folder tree: symmetric
// disable / restore, compatibility with folders the 8.0 script processed, conflict
// safety, cleaning that keeps DNF.cfg, and game-root detection from a remembered path.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../native/game_toolbox.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks = 0;
void check(bool value, const char* name) { ++checks; if (!value) throw std::runtime_error(name); }
namespace sfs = std::filesystem;
void write(const sfs::path& path, const std::string& text) {
    sfs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}
std::string read(const sfs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}
using namespace dafclient::toolbox;
State stateOf(const Roots& roots, const std::wstring& label) {
    for (const auto& item : items(roots)) if (item.label == label) return status(item);
    throw std::runtime_error("unknown item");
}
}

int wmain() {
    const auto root = sfs::absolute(L"build/game-toolbox-test-" + std::to_wstring(GetCurrentProcessId()));
    int result = 0;
    try {
        Roots roots{(root / L"Game").wstring(), (root / L"Roaming" / L"Tencent").wstring(), (root / L"LocalLow" / L"DNF").wstring()};
        const sfs::path game = roots.game, tencent = roots.tencentRoaming, cache = roots.dnfUserCache;
        write(game / L"DNF.exe", "game");
        write(game / L"Install.dll", "install-payload");
        write(game / L"TGuard" / L"guard.bin", "guard-payload");
        write(game / L"start" / L"AdvertDialog.exe", "advert-payload");
        write(tencent / L"QQDoctor" / L"doctor.txt", "doctor-payload");
        write(tencent / L"Common" / L"gjdatareport.dll", "report-payload");
        write(tencent / L"TenioDL" / L"keep.bin", "download-component");  // Must never be touched.

        check(isGameDirectory(roots.game) && !isGameDirectory((root / L"Roaming").wstring()), "game root recognised by DNF.exe");
        check(items(roots).size() == 17, "all 17 components of the 8.0 script");
        check(stateOf(roots, L"Install.dll") == State::Normal && stateOf(roots, L"TP3Helper.exe") == State::NotInstalled, "initial states");

        auto report = disableAll(roots);
        check(report.changed == 6 && report.conflicts == 0 && report.failed == 0, "present components and the Pandora placeholder disabled");
        check(read(game / L"Install.dll.dnf-toolbox-disabled") == "install-payload", "file moved to its 8.0 backup name");
        check(sfs::is_directory(game / L"Install.dll") && sfs::exists(game / L"Install.dll" / L".blocked-by-dnf-toolbox"), "file replaced by marker directory");
        check(read(game / L"TGuard.dnf-toolbox-disabled" / L"guard.bin") == "guard-payload", "directory moved to its backup");
        check(sfs::is_regular_file(game / L"TGuard") && sfs::exists(game / L"TGuard.blocked-by-dnf-toolbox"), "directory replaced by file with sibling marker");
        check(sfs::is_directory(game / L"Pandora" / L"cache" / L"archive" / L"9193" / L"gamelet9193GRobot_bin.zip"), "Pandora 9193 placeholder always created");
        check(read(tencent / L"TenioDL" / L"keep.bin") == "download-component", "download components untouched");
        check(stateOf(roots, L"QQDoctor") == State::Disabled && stateOf(roots, L"gjdatareport.dll") == State::Disabled, "Tencent components disabled");
        report = disableAll(roots);
        check(report.changed == 0 && report.unchanged == 6, "disable is idempotent");

        report = restoreAll(roots);
        check(report.changed == 5 && report.conflicts == 0 && report.failed == 0, "backed-up components restored");
        check(read(game / L"Install.dll") == "install-payload" && read(game / L"TGuard" / L"guard.bin") == "guard-payload", "file and directory contents restored");
        check(read(tencent / L"QQDoctor" / L"doctor.txt") == "doctor-payload" && read(tencent / L"Common" / L"gjdatareport.dll") == "report-payload", "Tencent contents restored");
        check(!sfs::exists(game / L"TGuard.blocked-by-dnf-toolbox") && !sfs::exists(game / L"Install.dll.dnf-toolbox-disabled"), "markers and backups gone");
        check(!sfs::exists(game / L"Pandora" / L"cache" / L"archive" / L"9193" / L"gamelet9193GRobot_bin.zip"), "Pandora placeholder removed on restore");

        // Existing backup: never overwritten, original left as is.
        write(game / L"Install.dll.dnf-toolbox-disabled", "older-backup");
        report = disableAll(roots);
        check(report.conflicts == 1 && read(game / L"Install.dll") == "install-payload" && read(game / L"Install.dll.dnf-toolbox-disabled") == "older-backup", "backup conflict is safe");
        restoreAll(roots);
        sfs::remove(game / L"Install.dll.dnf-toolbox-disabled");

        // A user's own folder at a file path is not a placeholder: it is left alone.
        write(game / L"TP3Helper.exe" / L"mine.txt", "user-data");
        check(restore(items(roots)[1]) == Outcome::NoBackup && read(game / L"TP3Helper.exe" / L"mine.txt") == "user-data", "unmarked folder never deleted");
        sfs::remove_all(game / L"TP3Helper.exe");

        // State left behind by the 8.0 batch script is recognised and restored.
        sfs::rename(tencent / L"QQDoctor", tencent / L"QQDoctor.dnf-toolbox-disabled");
        write(tencent / L"QQDoctor", "");
        write(tencent / L"QQDoctor.blocked-by-dnf-toolbox", "Blocked at 2026/07/01 10:00:00.00\r\n");
        check(stateOf(roots, L"QQDoctor") == State::Disabled, "8.0 script state recognised");
        restoreAll(roots);
        check(read(tencent / L"QQDoctor" / L"doctor.txt") == "doctor-payload", "8.0 script state restored");

        // Cleaning keeps DNF.cfg and downloads; the reset empties the user cache entirely.
        write(game / L"debug.log", "x"); write(game / L"a_tmp.dat", "x"); write(game / L"Thread01.txt", "x"); write(game / L"BugTrace.log", "x");
        write(game / L"update.pak", "download");
        write(cache / L"DNF.cfg", "graphics"); write(cache / L"sub" / L"cache.bin", "x"); write(cache / L"top.bin", "x");
        write(tencent / L"Logs" / L"dnf.tlg", "x"); write(tencent / L"QQCall1.exe", "x");
        const auto cleaned = cleanLogsAndCache(roots);
        check(cleaned.deleted == 8 && cleaned.failed == 0, "logs, caches and Tencent leftovers deleted");
        check(read(cache / L"DNF.cfg") == "graphics" && read(game / L"update.pak") == "download" && sfs::exists(game / L"DNF.exe"), "DNF.cfg, downloads and the game kept");
        check(!sfs::exists(game / L"debug.log") && !sfs::exists(cache / L"sub" / L"cache.bin"), "log and nested cache removed");
        const auto reset = resetUserCache(roots);
        check(reset.deleted == 1 && !sfs::exists(cache / L"DNF.cfg"), "reset clears DNF.cfg too");

        const auto found = detectGameDirectory(roots.game);
        check(sfs::equivalent(found.directory, roots.game) && found.source == L"上次使用的目录", "remembered game root used first");
        std::cout << "PASS: " << checks << " game toolbox checks.\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        result = 1;
    }
    std::error_code ignored; sfs::remove_all(root, ignored);
    return result;
}
