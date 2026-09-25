#pragma once
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace dafclient {
// 新方案的默认按下/抬起时长，以及一键奔跑的默认搓招保护；低于默认值时界面给出提示。
constexpr unsigned kDefaultFireMs = 7;
constexpr unsigned kDefaultGuardMs = 150, kMinGuardMs = 140, kMaxGuardMs = 1000;
// Delay after each newly added one-key combo output step.
constexpr unsigned kDefaultComboDelayMs = 30;
struct Key {
    std::wstring name;
    unsigned scan = 0, vk = 0;
    unsigned descriptor() const { return scan | (vk << 16); }
    unsigned physical_id() const { return vk == 0x13 ? 512 : scan & 0x1ff; }
    explicit operator bool() const { return scan != 0 && vk != 0; }
};
Key parseKey(const std::wstring& name);
Key keyFromEvent(unsigned vk, unsigned scan, bool extended);
std::vector<std::wstring> keyNames();
bool isNumpadKey(const std::wstring& name);
struct Hotkey { Key key; unsigned modifiers = 0; explicit operator bool() const { return bool(key); } };
Hotkey parseHotkey(const std::wstring& text); // Win32 MOD_ALT/CONTROL/SHIFT/WIN flags.
std::wstring formatHotkey(const Hotkey& key);
std::vector<std::wstring> splitKeys(const std::wstring& text);
std::wstring joinKeys(const std::vector<std::wstring>& keys);

struct ComboStep { std::wstring key; unsigned intervalMs = 0; };
struct Combo { std::wstring trigger; std::vector<ComboStep> steps; };
std::vector<Combo> parseCombos(const std::wstring& text);
std::wstring serializeCombos(const std::vector<Combo>& combos);
struct RunSettings {
    bool enabled = false;
    std::array<std::wstring, 4> keys{{L"Up", L"Down", L"Left", L"Right"}};
    unsigned pressMs = 30, gapMs = 30, guardMs = kDefaultGuardMs;
    std::wstring toggleHotkey = L"F10";
};
struct Settings {
    // autoStart: start auto-fire on launch and keep the window in the tray (default on).
    bool autoStart = true, onSystemStart = false, blockWin = false;
    std::wstring lastPreset, quickSwitchHotkey = L"!Tilde";
    std::wstring theme = L"dark"; // "dark" or "light"
    RunSettings oneKeyRun;
};
struct Profile {
    std::wstring name;
    std::vector<std::wstring> keys;
    unsigned downMs = kDefaultFireMs, upMs = kDefaultFireMs;
    bool lvRen = false, zhanFa = false, jianZong = false, combo = false;
    std::wstring lvRenShotKey = L"Z", zhanFaShotKey, jianZongSkillKey = L"A";
    std::vector<std::wstring> lvRenSkillKeys, zhanFaSkillKeys;
    unsigned jianZongDelayMs = 200;
    bool usePresetRunKeys = false;
    std::array<std::wstring, 4> runKeys{{L"Up", L"Down", L"Left", L"Right"}};
    std::vector<Combo> combos;
};
// Binary-compatible with the engine's 132-word rule records.
struct Rule {
    unsigned descriptor = 0, manual = 0, delayUs = 0, triggerCount = 0;
    unsigned triggers[128]{};
};
std::vector<Rule> buildRules(const Profile& profile);
std::vector<Rule> buildRules(const Profile& profile, const Settings& settings);
RunSettings resolveRunSettings(const Profile& profile, const Settings& settings);

// Windows service options (formerly appsettings.json "Manager"). Relative paths in
// autoStart resolve against the EXE directory; names match with or without ".exe".
struct ServiceOptions {
    std::wstring gameProcess = L"DNF.exe";
    unsigned pollSeconds = 2;          // 1–60
    unsigned actionDelaySeconds = 60;  // 0–3600
    bool closeLauncherAfterGame = true;
    bool optimizeGamePriority = true;
    bool aboveNormalPriority = true;   // Only Normal / AboveNormal are ever applied.
    std::vector<std::wstring> limit{L"SGuard64.exe", L"SGuardSvc64.exe", L"CrossProxy.exe"};
    std::vector<std::wstring> kill{L"GameLoader.exe", L"TXPlatform.exe"};
    std::vector<std::wstring> autoStart{L"DNFAutoFire.exe"};
    std::vector<std::wstring> autoStop{L"DNFAutoFire.exe"};
    bool operator==(const ServiceOptions& o) const;
    bool operator!=(const ServiceOptions& o) const { return !(*this == o); }
};
constexpr unsigned kMinPollSeconds = 1, kMaxPollSeconds = 60, kMaxActionDelaySeconds = 3600;

// The single configuration file next to the EXE. Legacy config.ini / appsettings.json
// are imported once by config_migrate and then removed.
constexpr wchar_t kConfigFileName[] = L"config.json";
constexpr unsigned kConfigSchemaVersion = 1;

// config.json backed store. The parsed document is cached and re-read only when the
// file's size or write time changes; every save is an atomic replace under a machine-wide
// lock, and members this version does not understand are preserved.
class Store {
public:
    explicit Store(std::wstring path);
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    Settings loadSettings() const;
    Profile loadProfile(const std::wstring& name) const;
    std::vector<std::wstring> presetNames() const;
    ServiceOptions loadService() const;
    void saveSettings(const Settings& settings) const;
    void saveProfile(const Profile& profile) const;
    void save(const Profile& profile, const Settings& settings) const; // One transaction.
    void saveService(const ServiceOptions& options) const;
    std::wstring loadGameDirectory() const;                  // toolbox.gameDirectory (game root chosen or detected).
    void saveGameDirectory(const std::wstring& directory) const;
    void cloneProfile(const std::wstring& source, const std::wstring& newName) const;
    void renameProfile(const std::wstring& name, const std::wstring& newName) const; // Keeps its position.
    void deleteProfile(const std::wstring& name) const;
    bool changedOnDisk() const; // Cheap metadata check; true when the next load re-reads.
    const std::wstring& path() const { return path_; }
private:
    struct Cache;
    std::wstring path_;
    std::unique_ptr<Cache> cache_;
};
} // namespace dafclient
