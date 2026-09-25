#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../native/config_migrate.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks = 0;
void check(bool value, const char* name) { ++checks; if (!value) throw std::runtime_error(name); }
void writeBytes(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary); file << bytes;
}
// The former client wrote UTF-16LE with BOM and CRLF.
void writeUtf16(const std::filesystem::path& path, const std::wstring& text) {
    std::string bytes("\xff\xfe", 2);
    for (const wchar_t c : text) { bytes += char(c & 0xff); bytes += char((c >> 8) & 0xff); }
    writeBytes(path, bytes);
}
std::string readBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}
}
int main() {
    using namespace dafclient;
    namespace fs = std::filesystem;
    const auto dir = fs::absolute(L"build/config-migrate-test-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(dir);
    struct Cleanup { fs::path dir; ~Cleanup() { std::error_code e; fs::remove_all(dir, e); } } cleanup{dir};
    try {
        check(!migrateLegacyConfig(dir.wstring()).migrated() && !fs::exists(dir / L"config.json"), "nothing to migrate is a no-op");
        for (unsigned value : {1u, 180u, 200u, 350u, 1001u, 5000000u, kMaxTimingMs}) {
            const auto timingDir = dir / (L"timing-" + std::to_wstring(value));
            fs::create_directory(timingDir);
            const auto text = std::to_wstring(value);
            writeUtf16(timingDir / L"config.ini", L"[设置]\r\nOneKeyRunGuardDelay=" + text
                + L"\r\nOneKeyRunPressDelay=" + text + L"\r\nOneKeyRunGapDelay=" + text
                + L"\r\n[预设:custom]\r\nAutoFireDownMs=" + text + L"\r\nAutoFireUpMs=" + text + L"\r\n");
            check(migrateLegacyConfig(timingDir.wstring()).migrated(), "custom timing INI migration succeeds");
            Store timingStore((timingDir / L"config.json").wstring());
            const auto run = timingStore.loadSettings().oneKeyRun;
            const auto profile = timingStore.loadProfile(L"custom");
            check(run.guardMs == value && run.pressMs == value && run.gapMs == value
                && profile.downMs == value && profile.upMs == value, "migration preserves all positive custom timing values");
        }

        // The user's real legacy config.ini, including an unused empty section and a foreign section.
        writeUtf16(dir / L"config.ini", L"[设置]\r\nLastPreset=普通\r\nQuickChangeHotKey=!PgUp\r\nSettingAutoStart=1\r\n"
            L"SettingOnSystemStart=0\r\nOneKeyRunState=1\r\nOneKeyRunUpKey=Up\r\nOneKeyRunGuardDelay=140\r\nOneKeyRunToggleHotKey=PgDn\r\n"
            L"UnknownGlobal=保留\r\n[预设:]\r\nLvRenShotKey=G\r\n[预设:普通]\r\nkeys=X|Z|A|Num0\r\nAutoFireDownMs=7\r\n"
            L"AutoFireUpMs=7\r\nOneKeyRunUsePresetKeys=0\r\nOneKeyRunUpKey=W\r\nUnknownProfile=still here\r\n; comment\r\n"
            L"[预设:连招]\r\nkeys=G|X\r\nComboState=1\r\ncombos=W>W,30;E,30;F,30|S>S,30;E,30;F,30|D>D,10;E,10;F,10\r\n"
            L"[预设:战法]\r\nkeys=X|Q\r\nZhanFaState=1\r\nZhanFaSkillKeys=A|S|D\r\nZhanFaShotKey=Numpad6\r\nOneKeyRunUsePresetKeys=1\r\n"
            L"OneKeyRunUpKey=W\r\nOneKeyRunDownKey=S\r\nOneKeyRunLeftKey=A\r\nOneKeyRunRightKey=D\r\n[OtherTool]\r\nFuture=123\r\n");
        // appsettings.json exactly as the .NET service shipped it, plus comments and a trailing comma.
        writeBytes(dir / L"appsettings.json", u8R"({
  // .NET configuration allows comments
  "Logging": { "LogLevel": { "Default": "Information" } },
  "Manager": {
    "processName": "DNF.exe",
    "ProcessPollSeconds": 3,
    "ActionDelaySeconds": "90",
    "CloseLauncherIfGameNotStarted": false,
    "OptimizeGamePriority": true,
    "GamePriority": "Normal",
    "LimitList": [ "SGuard64.exe", "SGuardSvc64.exe" ],
    "KillList": [ "GameLoader.exe", "TXPlatform.exe", "gameloader.exe" ],
    "AutoStart": [ "DNFAutoFire.exe", "Tools\\Other.exe" ],
    "AutoStop": [ "DNFAutoFire.exe" ],
  },
})");
        writeBytes(dir / L"appsettings.Development.json", "{\"Logging\":{}}");

        const auto report = migrateLegacyConfig(dir.wstring());
        check(report.error.empty() && report.imported.size() == 2, "both legacy files imported");
        check(report.removed.size() == 3 && report.kept.empty(), "all legacy files removed after success");
        check(!fs::exists(dir / L"config.ini") && !fs::exists(dir / L"appsettings.json") && !fs::exists(dir / L"appsettings.Development.json"), "legacy files gone");
        check(fs::exists(dir / L"config.json") && !fs::exists(dir / L"config.json.migrating"), "single config.json written");

        const Store store((dir / L"config.json").wstring());
        const auto settings = store.loadSettings();
        check(settings.lastPreset == L"普通" && settings.quickSwitchHotkey == L"!PgUp" && settings.oneKeyRun.enabled, "settings migrated");
        check(settings.oneKeyRun.guardMs == 140 && settings.oneKeyRun.toggleHotkey == L"PgDn", "run settings migrated");
        check(store.presetNames() == std::vector<std::wstring>{L"普通", L"连招", L"战法"}, "profile order kept, empty section excluded");
        const auto normal = store.loadProfile(L"普通");
        check(normal.keys == std::vector<std::wstring>{L"X", L"Z", L"A", L"Num0"} && normal.downMs == 7, "ordinary profile migrated");
        check(!normal.usePresetRunKeys && normal.runKeys[0] == L"W", "dormant direction override migrated");
        const auto combo = store.loadProfile(L"连招");
        check(combo.combo && serializeCombos(combo.combos) == L"W>W,30;E,30;F,30|S>S,30;E,30;F,30|D>D,10;E,10;F,10", "combos migrated exactly");
        const auto zhanfa = store.loadProfile(L"战法");
        check(zhanfa.zhanFa && zhanfa.zhanFaShotKey == L"Numpad6" && zhanfa.zhanFaSkillKeys.size() == 3, "profession migrated");
        check(zhanfa.usePresetRunKeys && zhanfa.runKeys[3] == L"D", "per-profile directions migrated");
        const auto service = store.loadService();
        check(service.pollSeconds == 3 && service.actionDelaySeconds == 90 && !service.closeLauncherAfterGame, "service numbers and flags migrated");
        check(!service.aboveNormalPriority && service.optimizeGamePriority, "service priority migrated");
        check(service.kill == std::vector<std::wstring>{L"GameLoader.exe", L"TXPlatform.exe"}, "service lists migrated and deduplicated");
        check(service.autoStart.size() == 2 && service.autoStart[1] == L"Tools\\Other.exe" && service.limit.size() == 2, "service paths migrated");
        const auto text = json::widen(readBytes(dir / L"config.json"));
        check(text.find(L"\"UnknownGlobal\": \"保留\"") != text.npos && text.find(L"\"UnknownProfile\": \"still here\"") != text.npos, "unknown INI keys kept");
        check(text.find(L"\"OtherTool\"") != text.npos && text.find(L"\"LvRenShotKey\": \"G\"") != text.npos, "foreign and unnamed sections kept");
        check(text.find(L"Logging") == text.npos, "obsolete logging settings dropped");

        // A later stray legacy file never overwrites the unified configuration.
        writeUtf16(dir / L"config.ini", L"[设置]\r\nLastPreset=别的\r\n[预设:别的]\r\nkeys=Q\r\n");
        const auto second = migrateLegacyConfig(dir.wstring());
        check(second.error.empty() && !second.migrated() && second.kept.size() == 1, "superseded legacy file is left alone");
        check(fs::exists(dir / L"config.ini") && Store((dir / L"config.json").wstring()).loadSettings().lastPreset == L"普通", "config.json unchanged");
        fs::remove(dir / L"config.ini");

        // Only the service part missing: import appsettings.json into the existing config.json.
        {
            auto root = json::parse(readBytes(dir / L"config.json"));
            root.erase(L"service");
            writeBytes(dir / L"config.json", json::serialize(root));
        }
        writeBytes(dir / L"appsettings.json", "{\"Manager\":{\"ActionDelaySeconds\":5}}");
        const auto third = migrateLegacyConfig(dir.wstring());
        check(third.error.empty() && third.imported.size() == 1 && !fs::exists(dir / L"appsettings.json"), "service-only import");
        check(Store((dir / L"config.json").wstring()).loadService().actionDelaySeconds == 5, "service-only import values");
        check(Store((dir / L"config.json").wstring()).presetNames().size() == 3, "service-only import keeps profiles");

        // Broken legacy input changes nothing and keeps the old file for the user.
        fs::remove(dir / L"config.json");
        writeBytes(dir / L"appsettings.json", "{ not json");
        const auto broken = migrateLegacyConfig(dir.wstring());
        check(!broken.error.empty() && !broken.migrated() && fs::exists(dir / L"appsettings.json") && !fs::exists(dir / L"config.json"), "failed import is harmless");

        // A damaged appsettings.json does not block a valid config.ini.
        writeUtf16(dir / L"config.ini", L"[设置]\r\nLastPreset=甲\r\n[预设:甲]\r\nkeys=Q\r\n");
        const auto partial = migrateLegacyConfig(dir.wstring());
        check(partial.imported == std::vector<std::wstring>{L"config.ini"} && !fs::exists(dir / L"config.ini"), "valid INI imported independently");
        check(partial.error.find(L"appsettings.json") != std::wstring::npos && fs::exists(dir / L"appsettings.json"), "damaged service file reported and kept");
        check(Store((dir / L"config.json").wstring()).loadService() == ServiceOptions{}, "service defaults until its file is fixed");
        fs::remove(dir / L"appsettings.json");

        // ANSI (GBK) and UTF-8 INI files decode as before.
        check(decodeLegacyText("\xef\xbb\xbf[a]\r\nk=\xe5\x80\xbc") == L"[a]\r\nk=值", "UTF-8 INI decoded");
        std::cout << "PASS: " << checks << " configuration migration checks.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
