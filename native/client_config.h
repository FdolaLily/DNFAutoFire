#pragma once
#include <array>
#include <string>
#include <vector>

namespace dafclient {
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
    unsigned pressMs = 30, gapMs = 30, guardMs = 140;
    std::wstring toggleHotkey = L"F10";
};
struct Settings {
    bool autoStart = false, onSystemStart = false, blockWin = false;
    std::wstring lastPreset, quickSwitchHotkey = L"!Tilde";
    std::wstring theme = L"dark"; // "dark" or "light"
    RunSettings oneKeyRun;
};
struct Profile {
    std::wstring name;
    std::vector<std::wstring> keys;
    unsigned downMs = 10, upMs = 10;
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

class Store {
public:
    explicit Store(std::wstring path);
    Settings loadSettings() const;
    Profile loadProfile(const std::wstring& name) const;
    std::vector<std::wstring> presetNames() const;
    void saveSettings(const Settings& settings) const;
    void saveProfile(const Profile& profile) const;
    void cloneProfile(const std::wstring& source, const std::wstring& newName) const;
    void renameProfile(const std::wstring& name, const std::wstring& newName) const; // Keeps its position.
    void deleteProfile(const std::wstring& name) const;
    const std::wstring& path() const { return path_; }
private:
    std::wstring path_;
};
} // namespace dafclient
