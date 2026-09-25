#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../native/client_config.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks = 0;
void check(bool value, const char* name) { ++checks; if (!value) throw std::runtime_error(name); }
std::wstring read(const std::wstring& path) {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    const std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::wstring text;
    for (size_t i = 2; i + 1 < data.size(); i += 2) text += static_cast<wchar_t>(static_cast<unsigned char>(data[i]) | static_cast<unsigned char>(data[i + 1]) << 8);
    return text;
}
}
int main(int argc, char** argv) {
    using namespace dafclient;
    try {
        for (const auto& name : keyNames()) {
            const auto key = parseKey(name);
            check(bool(key), "UI key is valid");
            check(keyFromEvent(key.vk,key.scan & 255,(key.scan & 256) != 0).physical_id() == key.physical_id(), "keyboard round trip");
        }
        check(parseKey(L"LControl").descriptor() == parseKey(L"LCtrl").descriptor(), "control alias");
        check(parseKey(L"Numpad6").descriptor() == parseKey(L"Num6").descriptor(), "numpad alias");
        check(parseKey(L"NumpadRight").physical_id() == parseKey(L"Num6").physical_id(), "NumLock-independent identity");
        check(parseKey(L"NumpadRight").vk == VK_RIGHT, "NumLock-off alias VK");
        check(parseKey(L"Pause").physical_id() != parseKey(L"NumLk").physical_id(), "Pause and NumLock distinction");
        check(parseKey(L"Up").physical_id() != parseKey(L"Num8").physical_id(), "extended arrows independent");
        check(parseKey(L"sc11c").descriptor() == parseKey(L"NumEnter").descriptor(), "scan notation");
        check(parseKey(L"vk41sc01e").descriptor() == parseKey(L"A").descriptor(), "full descriptor notation");
        check(!parseKey(L"garbage") && !parseKey(L"sc0") && !parseKey(L"vk400"), "invalid key rejected");
        check(isNumpadKey(L"NumpadRight") && !isNumpadKey(L"NumLock") && !isNumpadKey(L"Right"), "profession keypad validation");
        check(parseHotkey(L"!PgUp").modifiers == MOD_ALT && parseHotkey(L"!PgUp").key.vk == VK_PRIOR, "existing quick switch hotkey");
        check(formatHotkey(parseHotkey(L"^!+X")) == L"^!+X", "hotkey serialization");
        const auto combos = parseCombos(L"W>W,30;E,30;F,30|S>S,30;E,30;F,30|D>D,10;E,10;F,10");
        check(combos.size() == 3 && combos[2].steps[1].intervalMs == 10, "legacy combo migration");
        check(serializeCombos(combos) == L"W>W,30;E,30;F,30|S>S,30;E,30;F,30|D>D,10;E,10;F,10", "legacy combo exact round trip");
        check(parseCombos(L"A>X,-5;Y,nonsense;Z,9999999999")[0].steps[2].intervalMs == 3600000, "combo delay bounds");
        check(parseCombos(L"bad|Q>X,0;bad,10|A>").size() == 1, "invalid combo discarded");

        const auto path = std::filesystem::absolute(L"build/client-config-test-" + std::to_wstring(GetCurrentProcessId()) + L".ini").wstring();
        struct Cleanup { std::wstring path; ~Cleanup() { DeleteFileW(path.c_str()); } } cleanup{path};
        // UTF-8 is accepted; subsequent atomic writes use AHK-compatible UTF-16LE.
        {
            std::ofstream file(std::filesystem::path(path), std::ios::binary);
            file << u8"[设置]\nLastPreset=普通\nQuickChangeHotKey=!PgUp\nSettingAutoStart=1\nOneKeyRunState=1\nOneKeyRunToggleHotKey=PgDn\nUnknownGlobal=保留\n"
                u8"[预设:]\nLvRenShotKey=G\n[预设:普通]\nkeys=X|Z|A|Num0\nAutoFireDownMs=7\nAutoFireUpMs=7\nUnknownProfile=still here\n"
                u8"OneKeyRunUsePresetKeys=0\nOneKeyRunUpKey=W\n; keep comment\n[OtherTool]\nFuture=123\n";
        }
        Store store(path);
        auto settings = store.loadSettings(); auto profile = store.loadProfile(L"普通");
        check(settings.lastPreset == L"普通" && settings.autoStart && settings.oneKeyRun.enabled, "Chinese section migration");
        check(settings.oneKeyRun.toggleHotkey == L"PgDn" && settings.quickSwitchHotkey == L"!PgUp", "existing hotkeys preserved");
        check(profile.keys == std::vector<std::wstring>{L"X",L"Z",L"A",L"Num0"} && profile.downMs == 7 && profile.upMs == 7, "current user's ordinary preset unchanged");
        check(settings.oneKeyRun.guardMs == 150, "missing guard delay defaults to 150ms");
        check(Settings{}.autoStart && Store(path + L".absent").loadSettings().autoStart, "auto-start to tray is on by default");
        const auto fresh = store.loadProfile(L"不存在的方案");
        check(fresh.downMs == 7 && fresh.upMs == 7 && Profile{}.downMs == 7 && Profile{}.upMs == 7, "new profiles default to 7+7ms");
        check(store.presetNames() == std::vector<std::wstring>{L"普通"}, "exclude empty and unrelated sections");
        check(resolveRunSettings(profile,settings).keys[0] == L"Up", "disabled preset overrides use global directions");
        check(profile.runKeys[0] == L"W", "dormant preset override loaded");
        profile.runKeys[0] = L"A"; // Disabled editor values must not overwrite dormant override.
        store.saveProfile(profile); store.saveSettings(settings);
        check(store.loadProfile(L"普通").runKeys[0] == L"W", "dormant preset override preserved on save");
        const auto saved = read(path);
        check(saved.find(L"UnknownGlobal=保留") != saved.npos && saved.find(L"UnknownProfile=still here") != saved.npos, "unknown properties preserved");
        check(saved.find(L"[OtherTool]\r\nFuture=123") != saved.npos && saved.find(L"; keep comment") != saved.npos, "unknown sections and comments preserved");
        check(store.loadSettings().quickSwitchHotkey == settings.quickSwitchHotkey, "UTF-16 reload");
        store.cloneProfile(L"普通",L"克隆");
        check(store.loadProfile(L"克隆").keys == profile.keys, "clone known properties");
        check(read(path).find(L"[预设:克隆]") != std::wstring::npos, "clone section exists");
        bool duplicate = false; try { store.cloneProfile(L"普通",L"克隆"); } catch (...) { duplicate = true; }
        check(duplicate, "clone cannot overwrite existing profile");
        store.deleteProfile(L"克隆"); check(store.presetNames().size() == 1, "delete only target profile");
        bool badName = false; try { profile.name = L"bad\n[设置]"; store.saveProfile(profile); } catch (...) { badName = true; }
        check(badName, "prevent INI section injection");

        profile = store.loadProfile(L"普通");
        auto rules = buildRules(profile,settings);
        check(rules.size() == 4, "existing ordinary rules count");
        check(rules[0].descriptor == parseKey(L"X").descriptor() && rules[3].descriptor == parseKey(L"Num0").descriptor(), "existing rules stable order");
        profile.keys = {L"Num6",L"NumpadRight",L"Up",L"W",L"A"};
        profile.zhanFa = true; profile.zhanFaShotKey = L"Numpad6"; profile.zhanFaSkillKeys = {L"A",L"A",L"LControl",L"LCtrl"};
        profile.combo = true; profile.combos = combos;
        rules = buildRules(profile,settings);
        check(rules.size() == 2, "merged duplicate output and filtered combo/direction keys");
        check(rules[0].manual && rules[0].triggerCount == 2, "manual and profession owner merged; trigger alias dedup");
        settings.oneKeyRun.enabled = false;
        check(buildRules(profile,settings).size() == 2, "disabled running retains legacy reserved directions");
        profile.jianZong = true; profile.jianZongSkillKey = L"A"; profile.jianZongDelayMs = 237;
        rules = buildRules(profile,settings);
        check(!rules[1].manual && rules[1].delayUs == 237000 && rules[1].triggerCount == 1, "sword skill delay overrides ordinary autofire");
        profile.zhanFaShotKey = L"X";
        rules = buildRules(profile,settings);
        check(rules[0].triggerCount == 0, "invalid profession output not activated");
        profile = store.loadProfile(L"普通"); profile.usePresetRunKeys = true; profile.runKeys = {{L"X",L"Z",L"A",L"Num0"}};
        check(buildRules(profile,settings).empty(), "actual preset-specific run directions filtered");

        Profile aliases;
        aliases.name = L"旧版别名";
        aliases.keys = {L"LControl",L"NumpadRight",L"sc02d"};
        aliases.zhanFa = true; aliases.zhanFaShotKey = L"Numpad6";
        aliases.zhanFaSkillKeys = {L"LControl",L"LCtrl",L"X"};
        store.saveProfile(aliases);
        const auto aliasReload = store.loadProfile(aliases.name);
        check(aliasReload.keys == aliases.keys && aliasReload.zhanFaShotKey == L"Numpad6", "retain original alias strings on configuration save");
        check(parseKey(aliasReload.keys[1]).vk == VK_RIGHT, "retain NumLock-off alias VK after reload");
        const auto aliasRules = buildRules(aliasReload,settings);
        check(aliasRules.size() == 3 && aliasRules[1].manual && aliasRules[1].triggerCount == 2, "alias-loaded ordinary and profession rules share one owner");
        check(aliasRules[1].descriptor >> 16 == VK_RIGHT, "merged rule retains original output semantics");
        for (const unsigned legacy : {180u,200u,350u}) {
            auto legacySettings = settings; legacySettings.oneKeyRun.guardMs = legacy;
            store.saveSettings(legacySettings);
            check(store.loadSettings().oneKeyRun.guardMs == 150, "historical guard defaults migrate to the current default");
        }
        auto customGuard = settings; customGuard.oneKeyRun.guardMs = 247; store.saveSettings(customGuard);
        check(store.loadSettings().oneKeyRun.guardMs == 247, "custom guard value is retained");
        customGuard.oneKeyRun.guardMs = 140; store.saveSettings(customGuard);
        check(store.loadSettings().oneKeyRun.guardMs == 140, "explicit 140ms guard is kept (only hinted in the UI)");

        check(store.loadSettings().theme == L"dark", "theme defaults to dark");
        auto themed = store.loadSettings(); themed.theme = L"light"; store.saveSettings(themed);
        check(store.loadSettings().theme == L"light", "light theme persisted");
        themed.theme = L"unexpected"; store.saveSettings(themed);
        check(store.loadSettings().theme == L"dark", "unknown theme falls back to dark");

        Profile second; second.name = L"第二"; second.keys = {L"Q"}; store.saveProfile(second);
        const auto before = store.presetNames();
        store.renameProfile(L"普通", L"改名");
        const auto after = store.presetNames();
        check(after.size() == before.size() && after[0] == L"改名", "rename keeps profile position");
        check(store.loadProfile(L"改名").keys == std::vector<std::wstring>{L"X",L"Z",L"A",L"Num0"}, "rename keeps profile content");
        check(read(path).find(L"UnknownProfile=still here") != std::wstring::npos, "rename keeps unknown fields");
        bool duplicateRejected = false;
        try { store.renameProfile(L"改名", L"第二"); } catch (const std::exception&) { duplicateRejected = true; }
        check(duplicateRejected, "rename rejects an existing name");
        store.renameProfile(L"改名", L"普通");
        check(store.presetNames() == before, "rename round trip");

        if (argc > 1) {
            const auto actual = std::filesystem::path(argv[1]).wstring();
            const Store live(actual); const auto s = live.loadSettings(); const auto p = live.loadProfile(s.lastPreset);
            check(!live.presetNames().empty() && !p.keys.empty(), "real INI read-only migration");
            for (const auto& name : live.presetNames()) {
                const auto existing = live.loadProfile(name);
                for (const auto& key : existing.keys) check(bool(parseKey(key)), "real preset ordinary key alias valid");
                const auto merged = buildRules(existing,s);
                check(merged.size() <= 128, "real preset merged engine rules within limit");
                if (existing.zhanFa) check(isNumpadKey(existing.zhanFaShotKey), "real profession keypad alias valid");
                const auto roundTrip = parseCombos(serializeCombos(existing.combos));
                check(roundTrip.size() == existing.combos.size(), "real preset combo count survives legacy serialization");
                for (size_t i = 0; i < roundTrip.size(); ++i) {
                    check(parseKey(roundTrip[i].trigger).physical_id() == parseKey(existing.combos[i].trigger).physical_id(), "real combo trigger survives round trip");
                    check(roundTrip[i].steps.size() == existing.combos[i].steps.size(), "real combo steps survive round trip");
                    for (size_t j = 0; j < roundTrip[i].steps.size(); ++j)
                        check(roundTrip[i].steps[j].intervalMs == existing.combos[i].steps[j].intervalMs, "real combo timing survives round trip");
                }
            }
            std::cout << "Live configuration loaded read-only; " << buildRules(p,s).size() << " output rules.\n";
        }
        std::cout << "PASS: " << checks << " native configuration compatibility checks.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
