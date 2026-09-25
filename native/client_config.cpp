#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "client_config.h"
#include <algorithm>
#include <climits>
#include <cwctype>
#include <map>
#include <stdexcept>
#include "config_schema.h"
#include "json.h"
#include "win_fs.h"

namespace dafclient {
namespace {
std::wstring trim(std::wstring s) {
    const auto first = s.find_first_not_of(L" \t\r\n");
    return first == s.npos ? L"" : s.substr(first, s.find_last_not_of(L" \t\r\n") - first + 1);
}
std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return std::towlower(c); });
    return s;
}
bool equal(const std::wstring& a, const std::wstring& b) { return lower(a) == lower(b); }
unsigned number(const std::wstring& s, unsigned fallback, unsigned low, unsigned high) {
    const auto text = trim(s);
    if (text.empty()) return fallback;
    size_t pos = 0;
    try {
        const auto n = std::stoll(text, &pos);
        if (pos != text.size()) return fallback;
        return n < static_cast<long long>(low) ? low : n > static_cast<long long>(high) ? high : static_cast<unsigned>(n);
    } catch (...) { return fallback; }
}
std::vector<std::wstring> split(const std::wstring& s, wchar_t separator) {
    std::vector<std::wstring> result;
    size_t start = 0;
    for (;;) {
        const auto end = s.find(separator, start);
        const auto part = trim(s.substr(start, end == s.npos ? s.npos : end - start));
        if (!part.empty()) result.push_back(part);
        if (end == s.npos) return result;
        start = end + 1;
    }
}
struct KeyEntry { const wchar_t* name; unsigned scan, vk; const wchar_t* aliases; };
const KeyEntry entries[] = {
    {L"Esc",0x01,VK_ESCAPE,L"Escape"}, {L"F1",0x3b,VK_F1,L""}, {L"F2",0x3c,VK_F2,L""},
    {L"F3",0x3d,VK_F3,L""}, {L"F4",0x3e,VK_F4,L""}, {L"F5",0x3f,VK_F5,L""},
    {L"F6",0x40,VK_F6,L""}, {L"F7",0x41,VK_F7,L""}, {L"F8",0x42,VK_F8,L""},
    {L"F9",0x43,VK_F9,L""}, {L"F10",0x44,VK_F10,L""}, {L"F11",0x57,VK_F11,L""}, {L"F12",0x58,VK_F12,L""},
    {L"Tilde",0x29,VK_OEM_3,L"`|~"},
    {L"1",0x02,'1',L""},{L"2",0x03,'2',L""},{L"3",0x04,'3',L""},{L"4",0x05,'4',L""},{L"5",0x06,'5',L""},
    {L"6",0x07,'6',L""},{L"7",0x08,'7',L""},{L"8",0x09,'8',L""},{L"9",0x0a,'9',L""},{L"0",0x0b,'0',L""},
    {L"Sub",0x0c,VK_OEM_MINUS,L"-"},{L"Add",0x0d,VK_OEM_PLUS,L"="},
    {L"Backspace",0x0e,VK_BACK,L"BS"},{L"Tab",0x0f,VK_TAB,L""},
    {L"Q",0x10,'Q',L""},{L"W",0x11,'W',L""},{L"E",0x12,'E',L""},{L"R",0x13,'R',L""},{L"T",0x14,'T',L""},
    {L"Y",0x15,'Y',L""},{L"U",0x16,'U',L""},{L"I",0x17,'I',L""},{L"O",0x18,'O',L""},{L"P",0x19,'P',L""},
    {L"LeftBracket",0x1a,VK_OEM_4,L"["},{L"RightBracket",0x1b,VK_OEM_6,L"]"},{L"Backslash",0x2b,VK_OEM_5,L"\\"},
    {L"Caps",0x3a,VK_CAPITAL,L"CapsLock"},
    {L"A",0x1e,'A',L""},{L"S",0x1f,'S',L""},{L"D",0x20,'D',L""},{L"F",0x21,'F',L""},{L"G",0x22,'G',L""},
    {L"H",0x23,'H',L""},{L"J",0x24,'J',L""},{L"K",0x25,'K',L""},{L"L",0x26,'L',L""},
    {L"Semicolon",0x27,VK_OEM_1,L";"},{L"QuotationMark",0x28,VK_OEM_7,L"'"},{L"Enter",0x1c,VK_RETURN,L"Return"},
    {L"LShift",0x2a,VK_LSHIFT,L"Shift"},
    {L"Z",0x2c,'Z',L""},{L"X",0x2d,'X',L""},{L"C",0x2e,'C',L""},{L"V",0x2f,'V',L""},
    {L"B",0x30,'B',L""},{L"N",0x31,'N',L""},{L"M",0x32,'M',L""},
    {L"Comma",0x33,VK_OEM_COMMA,L","},{L"Period",0x34,VK_OEM_PERIOD,L"."},{L"Slash",0x35,VK_OEM_2,L"/"},
    {L"RShift",0x36,VK_RSHIFT,L""},{L"LCtrl",0x1d,VK_LCONTROL,L"LControl|Control|Ctrl"},
    {L"LAlt",0x38,VK_LMENU,L"Alt"},{L"Space",0x39,VK_SPACE,L"Spacebar"},{L"RAlt",0x138,VK_RMENU,L""},
    {L"RCtrl",0x11d,VK_RCONTROL,L"RControl"},{L"PrtSc",0x137,VK_SNAPSHOT,L"PrintScreen"},
    {L"ScrLk",0x46,VK_SCROLL,L"ScrollLock"},{L"Pause",0x45,VK_PAUSE,L"Break"},
    {L"Ins",0x152,VK_INSERT,L"Insert"},{L"Home",0x147,VK_HOME,L""},{L"PgUp",0x149,VK_PRIOR,L"PageUp"},
    {L"Del",0x153,VK_DELETE,L"Delete"},{L"End",0x14f,VK_END,L""},{L"PgDn",0x151,VK_NEXT,L"PageDown"},
    {L"Up",0x148,VK_UP,L""},{L"Down",0x150,VK_DOWN,L""},{L"Left",0x14b,VK_LEFT,L""},{L"Right",0x14d,VK_RIGHT,L""},
    {L"Num1",0x4f,VK_NUMPAD1,L"Numpad1|NumpadEnd"},{L"Num2",0x50,VK_NUMPAD2,L"Numpad2|NumpadDown"},
    {L"Num3",0x51,VK_NUMPAD3,L"Numpad3|NumpadPgDn"},{L"Num4",0x4b,VK_NUMPAD4,L"Numpad4|NumpadLeft"},
    {L"Num5",0x4c,VK_NUMPAD5,L"Numpad5|NumpadClear"},{L"Num6",0x4d,VK_NUMPAD6,L"Numpad6|NumpadRight"},
    {L"Num7",0x47,VK_NUMPAD7,L"Numpad7|NumpadHome"},{L"Num8",0x48,VK_NUMPAD8,L"Numpad8|NumpadUp"},
    {L"Num9",0x49,VK_NUMPAD9,L"Numpad9|NumpadPgUp"},{L"Num0",0x52,VK_NUMPAD0,L"Numpad0|NumpadIns"},
    {L"NumPeriod",0x53,VK_DECIMAL,L"NumpadDot|NumpadDel"},{L"NumLk",0x145,VK_NUMLOCK,L"NumLock"},
    {L"NumEnter",0x11c,VK_RETURN,L"NumpadEnter"},{L"NumAdd",0x4e,VK_ADD,L"NumpadAdd"},
    {L"NumSub",0x4a,VK_SUBTRACT,L"NumpadSub"},{L"NumStar",0x37,VK_MULTIPLY,L"NumpadMult"},{L"NumSlash",0x135,VK_DIVIDE,L"NumpadDiv"},
    {L"LWin",0x15b,VK_LWIN,L""},{L"RWin",0x15c,VK_RWIN,L""},{L"AppsKey",0x15d,VK_APPS,L""},
    {L"F13",0x64,VK_F13,L""},{L"F14",0x65,VK_F14,L""},{L"F15",0x66,VK_F15,L""},{L"F16",0x67,VK_F16,L""},
    {L"F17",0x68,VK_F17,L""},{L"F18",0x69,VK_F18,L""},{L"F19",0x6a,VK_F19,L""},{L"F20",0x6b,VK_F20,L""},
    {L"F21",0x6c,VK_F21,L""},{L"F22",0x6d,VK_F22,L""},{L"F23",0x6e,VK_F23,L""},{L"F24",0x76,VK_F24,L""}
};
unsigned scanForVk(unsigned vk) {
    const auto sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
    return (sc & 0xff) | ((sc & 0xff00) == 0xe000 ? 0x100 : 0);
}
void checkLine(const std::wstring& value) {
    if (value.find_first_of(L"\r\n") != value.npos || value.find(L'\0') != value.npos)
        throw std::runtime_error("Configuration values must be a single line.");
}
void checkName(const std::wstring& value) {
    checkLine(value);
    if (trim(value).empty() || value.find_first_of(L"[]|") != value.npos)
        throw std::runtime_error("Invalid profile name.");
}
} // namespace

Key parseKey(const std::wstring& name) {
    const auto text = trim(name);
    for (const auto& entry : entries) {
        bool match = equal(text, entry.name);
        if (!match) for (const auto& alias : split(entry.aliases, L'|')) if (equal(text, alias)) { match = true; break; }
        if (match) {
            unsigned vk = entry.vk;
            // NumLock-off aliases share a physical key, but retain their VK semantics.
            const std::map<std::wstring, unsigned> navigation{{L"numpadins",VK_INSERT},{L"numpaddel",VK_DELETE},{L"numpadend",VK_END},
                {L"numpaddown",VK_DOWN},{L"numpadpgdn",VK_NEXT},{L"numpadleft",VK_LEFT},{L"numpadclear",VK_CLEAR},
                {L"numpadright",VK_RIGHT},{L"numpadhome",VK_HOME},{L"numpadup",VK_UP},{L"numpadpgup",VK_PRIOR}};
            const auto found = navigation.find(lower(text));
            if (found != navigation.end()) vk = found->second;
            return {entry.name, entry.scan, vk};
        }
    }
    const auto code = lower(text);
    try {
        size_t used = 0;
        if (code.rfind(L"sc", 0) == 0) {
            const auto scan = std::stoul(code.substr(2), &used, 16);
            if (used == code.size() - 2 && scan && scan <= 0x1ff) {
                for (const auto& entry : entries) if (entry.scan == scan) return {entry.name, entry.scan, entry.vk};
                const unsigned windowsScan = (scan & 0xff) | ((scan & 0x100) ? 0xe000 : 0);
                const auto vk = MapVirtualKeyW(windowsScan, MAPVK_VSC_TO_VK_EX);
                if (vk) return {text, static_cast<unsigned>(scan), vk};
            }
        }
        if (code.rfind(L"vk", 0) == 0) {
            const auto pos = code.find(L"sc", 2);
            const auto vkText = code.substr(2, pos == code.npos ? code.npos : pos - 2);
            const auto vk = std::stoul(vkText, &used, 16);
            if (used != vkText.size() || !vk || vk > 255) return {};
            unsigned scan = scanForVk(static_cast<unsigned>(vk));
            if (pos != code.npos) {
                const auto scanText = code.substr(pos + 2);
                scan = static_cast<unsigned>(std::stoul(scanText, &used, 16));
                if (used != scanText.size() || !scan || scan > 0x1ff) return {};
            }
            if (scan) return {text, scan, static_cast<unsigned>(vk)};
        }
    } catch (...) {}
    return {};
}
Key keyFromEvent(unsigned vk, unsigned scan, bool extended) {
    scan = (scan & 0xff) | (extended ? 0x100 : 0);
    if (vk == VK_PAUSE) return parseKey(L"Pause");
    if (vk == VK_NUMLOCK) return parseKey(L"NumLk");
    for (const auto& entry : entries) if (entry.scan == scan && entry.vk != VK_PAUSE) return {entry.name, entry.scan, entry.vk};
    if (vk >= 'A' && vk <= 'Z') return parseKey(std::wstring(1, static_cast<wchar_t>(vk)));
    return {};
}
std::vector<std::wstring> keyNames() {
    std::vector<std::wstring> result;
    for (const auto& entry : entries) result.push_back(entry.name);
    return result;
}
bool isNumpadKey(const std::wstring& name) {
    const auto key = parseKey(name);
    return key && key.name.rfind(L"Num", 0) == 0 && key.name != L"NumLk";
}
Hotkey parseHotkey(const std::wstring& text) {
    auto key = trim(text);
    unsigned modifiers = 0;
    while (!key.empty()) {
        if (key.front() == L'!') modifiers |= MOD_ALT;
        else if (key.front() == L'^') modifiers |= MOD_CONTROL;
        else if (key.front() == L'+') modifiers |= MOD_SHIFT;
        else if (key.front() == L'#') modifiers |= MOD_WIN;
        else if (key.front() != L'$' && key.front() != L'*' && key.front() != L'~') break;
        key.erase(key.begin());
    }
    return {parseKey(key), modifiers};
}
std::wstring formatHotkey(const Hotkey& hotkey) {
    std::wstring text;
    if (hotkey.modifiers & MOD_CONTROL) text += L'^';
    if (hotkey.modifiers & MOD_ALT) text += L'!';
    if (hotkey.modifiers & MOD_SHIFT) text += L'+';
    if (hotkey.modifiers & MOD_WIN) text += L'#';
    return text + hotkey.key.name;
}
bool hotkeysClash(const Hotkey& a, bool aAnyModifiers, const Hotkey& b, bool bAnyModifiers) {
    if (!a || !b || a.key.physical_id() != b.key.physical_id()) return false;
    if (a.modifiers == b.modifiers) return true;
    return (aAnyModifiers && (b.modifiers & a.modifiers) == a.modifiers)
        || (bAnyModifiers && (a.modifiers & b.modifiers) == b.modifiers);
}
std::vector<std::wstring> splitKeys(const std::wstring& text) { return split(text, L'|'); }
std::wstring joinKeys(const std::vector<std::wstring>& keys) {
    std::wstring result;
    for (const auto& key : keys) { if (!result.empty()) result += L'|'; result += key; }
    return result;
}

std::vector<Combo> parseCombos(const std::wstring& text) {
    std::vector<Combo> result;
    // Legacy "W>W,30;E,30|..." text from config.ini; config.json stores combos structurally.
    for (const auto& group : split(text, L'|')) {
        const auto separator = group.find(L'>');
        if (separator == group.npos || group.find(L'>', separator + 1) != group.npos) continue;
        Combo combo{trim(group.substr(0, separator)), {}};
        if (!parseKey(combo.trigger)) continue;
        for (const auto& step : split(group.substr(separator + 1), L';')) {
            const auto comma = step.find(L',');
            const auto key = trim(step.substr(0, comma));
            if (!parseKey(key)) continue;
            const auto delay = comma == step.npos ? 0 : number(step.substr(comma + 1), 0, 0, 3600000);
            combo.steps.push_back({key, delay});
            if (combo.steps.size() >= 5) break;
        }
        if (!combo.steps.empty()) result.push_back(combo);
    }
    return result;
}
std::wstring serializeCombos(const std::vector<Combo>& combos) {
    std::wstring result;
    for (const auto& combo : combos) {
        const auto trigger = parseKey(combo.trigger);
        if (!trigger || combo.steps.empty()) continue;
        std::wstring group = trigger.name + L">";
        unsigned count = 0;
        for (const auto& step : combo.steps) {
            const auto key = parseKey(step.key);
            if (!key) continue;
            if (count) group += L';';
            group += key.name + L"," + std::to_wstring(std::min(step.intervalMs, 3600000u));
            if (++count == 5) break;
        }
        if (count) { if (!result.empty()) result += L'|'; result += group; }
    }
    return result;
}

RunSettings resolveRunSettings(const Profile& profile, const Settings& settings) {
    auto run = settings.oneKeyRun;
    if (profile.usePresetRunKeys) run.keys = profile.runKeys;
    return run;
}
std::vector<Rule> buildRules(const Profile& profile) {
    std::vector<Rule> result;
    auto ensure = [&](const Key& key, bool manual) -> Rule* {
        if (!key) return nullptr;
        for (auto& rule : result) {
            const Key existing{L"", rule.descriptor & 0x1ff, rule.descriptor >> 16};
            if (existing.physical_id() == key.physical_id()) { if (manual) rule.manual = 1; return &rule; }
        }
        if (result.size() >= 128) throw std::runtime_error("Too many output keys.");
        Rule rule; rule.descriptor = key.descriptor(); rule.manual = manual;
        result.push_back(rule); return &result.back();
    };
    auto trigger = [](Rule& rule, const Key& key) {
        for (unsigned i = 0; i < rule.triggerCount; ++i) {
            const Key existing{L"", rule.triggers[i] & 0x1ff, rule.triggers[i] >> 16};
            if (existing.physical_id() == key.physical_id()) return;
        }
        if (rule.triggerCount >= 128) throw std::runtime_error("Too many trigger keys.");
        rule.triggers[rule.triggerCount++] = key.descriptor();
    };
    auto addTriggers = [&](const std::wstring& output, const std::vector<std::wstring>& keys) {
        const auto key = parseKey(output); if (!key) return;
        for (const auto& name : keys) { const auto input = parseKey(name); if (input) trigger(*ensure(key, false), input); }
    };
    for (const auto& key : profile.keys) ensure(parseKey(key), true);
    if (profile.lvRen) addTriggers(profile.lvRenShotKey, profile.lvRenSkillKeys);
    if (profile.zhanFa && isNumpadKey(profile.zhanFaShotKey)) addTriggers(profile.zhanFaShotKey, profile.zhanFaSkillKeys);
    if (profile.jianZong) {
        const auto key = parseKey(profile.jianZongSkillKey);
        if (auto* rule = ensure(key, false)) {
            rule->manual = 0; rule->delayUs = std::clamp(profile.jianZongDelayMs, 1u, 10000u) * 1000;
            trigger(*rule, key);
        }
    }
    return result;
}
std::vector<Rule> buildRules(const Profile& profile, const Settings& settings) {
    auto filtered = profile;
    const auto run = resolveRunSettings(profile, settings);
    filtered.keys.erase(std::remove_if(filtered.keys.begin(), filtered.keys.end(), [&](const std::wstring& name) {
        const auto key = parseKey(name);
        if (!key) return true;
        // Preserve the existing client's reserved direction keys even while its
        // running aid is disabled; changing that behavior is a separate feature.
        for (const auto& direction : run.keys) if (key.physical_id() == parseKey(direction).physical_id()) return true;
        if (profile.combo) for (const auto& combo : profile.combos) if (key.physical_id() == parseKey(combo.trigger).physical_id()) return true;
        return false;
    }), filtered.keys.end());
    return buildRules(filtered);
}

// ---------------------------------------------------------------- config.json store
namespace {
using json::Value;
const std::array<const wchar_t*, 4> directionNames{{L"up", L"down", L"left", L"right"}};
constexpr size_t kMaxConfigBytes = 4 * 1024 * 1024;

Value keysObject(const std::array<std::wstring, 4>& keys) {
    Value object = Value::object();
    for (size_t i = 0; i < 4; ++i) object.set(directionNames[i], keys[i]);
    return object;
}
std::wstring directionFrom(const Value& keys, size_t i, const std::wstring& fallback) {
    const Value* value = keys.find(directionNames[i]);
    return value && value->isString() ? value->str() : fallback;
}
Value* findProfile(Value& root, const std::wstring& name) {
    Value* profiles = root.find(L"profiles");
    if (!profiles || !profiles->isArray()) return nullptr;
    for (auto& item : profiles->items()) if (item.isObject() && equal(item.at(L"name").string(), name)) return &item;
    return nullptr;
}
const Value* findProfile(const Value& root, const std::wstring& name) {
    return findProfile(const_cast<Value&>(root), name);
}
Value& profileArray(Value& root) {
    Value& profiles = root[L"profiles"];
    if (!profiles.isArray()) profiles = Value::array();
    return profiles;
}
// Writes known members into an object in place so unknown members survive.
void writeSettings(Value& root, const Settings& s) {
    Value& o = root[L"settings"];
    if (!o.isObject()) o = Value::object();
    o.set(L"autoStart", s.autoStart);
    o.set(L"startWithWindows", s.onSystemStart);
    o.set(L"blockWinKey", s.blockWin);
    checkLine(s.lastPreset); o.set(L"lastProfile", s.lastPreset);
    checkLine(s.quickSwitchHotkey); o.set(L"quickSwitchHotkey", s.quickSwitchHotkey);
    checkLine(s.powerHotkey); o.set(L"powerHotkey", s.powerHotkey);
    o.set(L"theme", s.theme == L"light" ? L"light" : L"dark");
    Value& run = o[L"oneKeyRun"];
    if (!run.isObject()) run = Value::object();
    run.set(L"enabled", s.oneKeyRun.enabled);
    for (const auto& k : s.oneKeyRun.keys) checkLine(k);
    run.set(L"keys", keysObject(s.oneKeyRun.keys));
    run.set(L"pressMs", s.oneKeyRun.pressMs);
    run.set(L"gapMs", s.oneKeyRun.gapMs);
    run.set(L"guardMs", s.oneKeyRun.guardMs);
    checkLine(s.oneKeyRun.toggleHotkey); run.set(L"toggleHotkey", s.oneKeyRun.toggleHotkey);
}
Value comboArray(const std::vector<Combo>& combos) {
    Value items = Value::array();
    for (const auto& combo : combos) {
        const auto trigger = parseKey(combo.trigger);
        if (!trigger) continue;
        Value steps = Value::array();
        unsigned count = 0;
        for (const auto& step : combo.steps) {
            if (!parseKey(step.key)) continue;
            Value entry = Value::object();
            entry.set(L"key", step.key);
            entry.set(L"delayMs", std::min(step.intervalMs, 3600000u));
            steps.push(std::move(entry));
            if (++count == 5) break;
        }
        if (!count) continue;
        Value item = Value::object();
        item.set(L"trigger", combo.trigger);
        item.set(L"steps", std::move(steps));
        items.push(std::move(item));
    }
    return items;
}
std::vector<Combo> combosFrom(const Value& items) {
    std::vector<Combo> result;
    if (!items.isArray()) return result;
    for (const auto& item : items.items()) {
        Combo combo{trim(item.at(L"trigger").string()), {}};
        if (!parseKey(combo.trigger)) continue;
        for (const auto& step : item.at(L"steps").items()) {
            const auto key = trim(step.at(L"key").string());
            if (!parseKey(key)) continue;
            combo.steps.push_back({key, step.at(L"delayMs").integer(0, 0, 3600000)});
            if (combo.steps.size() >= 5) break;
        }
        if (!combo.steps.empty()) result.push_back(std::move(combo));
    }
    return result;
}
void writeProfile(Value& o, const Profile& p) {
    checkName(p.name);
    for (const auto* list : {&p.keys, &p.lvRenSkillKeys, &p.zhanFaSkillKeys}) for (const auto& k : *list) checkLine(k);
    o.set(L"name", p.name);
    o.set(L"keys", Value::strings(p.keys));
    o.set(L"downMs", p.downMs);
    o.set(L"upMs", p.upMs);
    const auto group = [&](const wchar_t* name) -> Value& { Value& g = o[name]; if (!g.isObject()) g = Value::object(); return g; };
    Value& lv = group(L"lvRen");
    lv.set(L"enabled", p.lvRen); lv.set(L"shotKey", p.lvRenShotKey); lv.set(L"skillKeys", Value::strings(p.lvRenSkillKeys));
    Value& zf = group(L"zhanFa");
    zf.set(L"enabled", p.zhanFa); zf.set(L"shotKey", p.zhanFaShotKey); zf.set(L"skillKeys", Value::strings(p.zhanFaSkillKeys));
    Value& jz = group(L"jianZong");
    jz.set(L"enabled", p.jianZong); jz.set(L"skillKey", p.jianZongSkillKey); jz.set(L"delayMs", p.jianZongDelayMs);
    Value& combo = group(L"combo");
    combo.set(L"enabled", p.combo); combo.set(L"items", comboArray(p.combos));
    Value& run = group(L"oneKeyRun");
    run.set(L"useOwnKeys", p.usePresetRunKeys);
    // A disabled override stays dormant on disk, exactly as the INI format behaved.
    if (p.usePresetRunKeys) run.set(L"keys", keysObject(p.runKeys));
}
Value serviceObject(const ServiceOptions& s, Value o) {
    if (!o.isObject()) o = Value::object();
    o.set(L"gameProcess", s.gameProcess);
    o.set(L"pollSeconds", s.pollSeconds);
    o.set(L"actionDelaySeconds", s.actionDelaySeconds);
    o.set(L"closeLauncherAfterGame", s.closeLauncherAfterGame);
    o.set(L"optimizeGamePriority", s.optimizeGamePriority);
    o.set(L"gamePriority", s.aboveNormalPriority ? L"AboveNormal" : L"Normal");
    o.set(L"limit", Value::strings(s.limit));
    o.set(L"kill", Value::strings(s.kill));
    o.set(L"autoStart", Value::strings(s.autoStart));
    o.set(L"autoStop", Value::strings(s.autoStop));
    return o;
}
std::vector<std::wstring> cleanList(const Value& value, const std::vector<std::wstring>& fallback) {
    if (!value.isArray()) return fallback;
    std::vector<std::wstring> result;
    for (auto item : value.stringList()) {
        item = trim(item);
        if (item.empty() || item.find_first_of(L"\r\n") != item.npos) continue;
        if (std::none_of(result.begin(), result.end(), [&](const std::wstring& e) { return equal(e, item); })) result.push_back(item);
    }
    return result;
}
} // namespace

bool ServiceOptions::operator==(const ServiceOptions& o) const {
    return gameProcess == o.gameProcess && pollSeconds == o.pollSeconds && actionDelaySeconds == o.actionDelaySeconds &&
        closeLauncherAfterGame == o.closeLauncherAfterGame && optimizeGamePriority == o.optimizeGamePriority &&
        aboveNormalPriority == o.aboveNormalPriority && limit == o.limit && kill == o.kill && autoStart == o.autoStart && autoStop == o.autoStop;
}

namespace schema {
void writeSettings(json::Value& root, const Settings& settings) { dafclient::writeSettings(root, settings); }
void writeProfile(json::Value& profile, const Profile& model) {
    if (!profile.isObject()) profile = json::Value::object();
    dafclient::writeProfile(profile, model);
}
void writeDormantRunKeys(json::Value& profile, const std::array<std::wstring, 4>& keys) {
    json::Value& run = profile[L"oneKeyRun"];
    if (!run.isObject()) run = json::Value::object();
    run.set(L"keys", keysObject(keys));
}
void writeService(json::Value& root, const ServiceOptions& options) {
    const json::Value* existing = root.find(L"service");
    root.set(L"service", serviceObject(normalizeService(options), existing ? *existing : json::Value::object()));
}
ServiceOptions normalizeService(ServiceOptions s) {
    const ServiceOptions defaults;
    s.gameProcess = trim(s.gameProcess);
    if (s.gameProcess.empty() || s.gameProcess.find_first_of(L"\\/:*?\"<>|") != s.gameProcess.npos) s.gameProcess = defaults.gameProcess;
    s.pollSeconds = std::clamp(s.pollSeconds, kMinPollSeconds, kMaxPollSeconds);
    s.actionDelaySeconds = std::min(s.actionDelaySeconds, kMaxActionDelaySeconds);
    for (auto* list : {&s.limit, &s.kill, &s.autoStart, &s.autoStop}) {
        std::vector<std::wstring> clean;
        for (auto item : *list) {
            item = trim(item);
            if (item.empty() || item.find_first_of(L"\r\n") != item.npos) continue;
            if (std::none_of(clean.begin(), clean.end(), [&](const std::wstring& e) { return equal(e, item); })) clean.push_back(item);
        }
        *list = std::move(clean);
    }
    return s;
}
} // namespace schema

struct Store::Cache {
    Value document = Value::object();
    fs::Stamp stamp;
    bool loaded = false;
    // Re-reads only when the file's metadata changed since the last read or write.
    const Value& current(const std::wstring& path) {
        const auto now = fs::stamp(path);
        if (loaded && now == stamp) return document;
        Value next = Value::object();
        if (now.exists) {
            try {
                next = json::parse(fs::read(path, kMaxConfigBytes));
            } catch (const json::ParseError& error) {
                throw std::runtime_error(std::string("config.json is not valid JSON. ") + error.what());
            }
            if (!next.isObject()) throw std::runtime_error("config.json must contain a JSON object.");
        }
        document = std::move(next); stamp = now; loaded = true;
        return document;
    }
    template <typename Fn> void mutate(const std::wstring& path, Fn&& change) {
        fs::ConfigLock lock;
        Value next = current(path);
        if (!next.find(L"version")) next.members().insert(next.members().begin(), {L"version", Value(kConfigSchemaVersion)});
        change(next);
        fs::writeAtomic(path, json::serialize(next));
        document = std::move(next);
        stamp = fs::stamp(path);
    }
};

Store::Store(std::wstring path) : path_(std::move(path)), cache_(new Cache) {}
Store::~Store() = default;
bool Store::changedOnDisk() const { return !cache_->loaded || fs::stamp(path_) != cache_->stamp; }

Settings Store::loadSettings() const {
    const Value& o = cache_->current(path_).at(L"settings");
    Settings s;
    s.autoStart = o.at(L"autoStart").boolean(true);
    s.onSystemStart = o.at(L"startWithWindows").boolean(false);
    s.blockWin = o.at(L"blockWinKey").boolean(false);
    s.lastPreset = o.at(L"lastProfile").string();
    s.quickSwitchHotkey = o.at(L"quickSwitchHotkey").string(s.quickSwitchHotkey);
    s.powerHotkey = o.at(L"powerHotkey").string(s.powerHotkey);
    s.theme = equal(o.at(L"theme").string(L"dark"), L"light") ? L"light" : L"dark";
    const Value& run = o.at(L"oneKeyRun");
    s.oneKeyRun.enabled = run.at(L"enabled").boolean(false);
    for (size_t i = 0; i < 4; ++i) s.oneKeyRun.keys[i] = directionFrom(run.at(L"keys"), i, s.oneKeyRun.keys[i]);
    s.oneKeyRun.pressMs = run.at(L"pressMs").integer(30, 1, kMaxTimingMs);
    s.oneKeyRun.gapMs = run.at(L"gapMs").integer(30, 1, kMaxTimingMs);
    s.oneKeyRun.guardMs = run.at(L"guardMs").integer(kDefaultGuardMs, 1, kMaxTimingMs);
    s.oneKeyRun.toggleHotkey = run.at(L"toggleHotkey").string(L"F10");
    return s;
}
Profile Store::loadProfile(const std::wstring& name) const {
    const Value& root = cache_->current(path_);
    Profile p; p.name = name;
    const Value* found = findProfile(root, name);
    static const Value empty = Value::object();
    const Value& o = found ? *found : empty;
    const Value& globalKeys = root.at(L"settings").at(L"oneKeyRun").at(L"keys");
    p.keys = o.at(L"keys").stringList();
    p.downMs = o.at(L"downMs").integer(kDefaultFireMs, 1, kMaxTimingMs);
    p.upMs = o.at(L"upMs").integer(kDefaultFireMs, 1, kMaxTimingMs);
    const Value& lv = o.at(L"lvRen");
    p.lvRen = lv.at(L"enabled").boolean(false);
    p.lvRenShotKey = lv.at(L"shotKey").string(L"Z");
    p.lvRenSkillKeys = lv.at(L"skillKeys").stringList();
    const Value& zf = o.at(L"zhanFa");
    p.zhanFa = zf.at(L"enabled").boolean(false);
    p.zhanFaShotKey = zf.at(L"shotKey").string();
    p.zhanFaSkillKeys = zf.at(L"skillKeys").stringList();
    const Value& jz = o.at(L"jianZong");
    p.jianZong = jz.at(L"enabled").boolean(false);
    p.jianZongSkillKey = jz.at(L"skillKey").string(L"A");
    p.jianZongDelayMs = jz.at(L"delayMs").integer(200, 1, 10000);
    const Value& combo = o.at(L"combo");
    p.combo = combo.at(L"enabled").boolean(false);
    p.combos = combosFrom(combo.at(L"items"));
    const Value& run = o.at(L"oneKeyRun");
    p.usePresetRunKeys = run.at(L"useOwnKeys").boolean(false);
    for (size_t i = 0; i < 4; ++i) p.runKeys[i] = directionFrom(run.at(L"keys"), i, directionFrom(globalKeys, i, p.runKeys[i]));
    return p;
}
std::vector<std::wstring> Store::presetNames() const {
    std::vector<std::wstring> result;
    for (const auto& item : cache_->current(path_).at(L"profiles").items()) {
        const auto name = item.at(L"name").string();
        if (trim(name).empty()) continue;
        if (std::none_of(result.begin(), result.end(), [&](const std::wstring& n) { return equal(n, name); })) result.push_back(name);
    }
    return result;
}
ServiceOptions Store::loadService() const {
    const Value& o = cache_->current(path_).at(L"service");
    ServiceOptions s;
    const auto game = trim(o.at(L"gameProcess").string());
    if (!game.empty() && game.find_first_of(L"\\/:*?\"<>|") == game.npos) s.gameProcess = game;
    s.pollSeconds = o.at(L"pollSeconds").integer(s.pollSeconds, kMinPollSeconds, kMaxPollSeconds);
    s.actionDelaySeconds = o.at(L"actionDelaySeconds").integer(s.actionDelaySeconds, 0, kMaxActionDelaySeconds);
    s.closeLauncherAfterGame = o.at(L"closeLauncherAfterGame").boolean(s.closeLauncherAfterGame);
    s.optimizeGamePriority = o.at(L"optimizeGamePriority").boolean(s.optimizeGamePriority);
    s.aboveNormalPriority = !equal(trim(o.at(L"gamePriority").string(L"AboveNormal")), L"Normal");
    s.limit = cleanList(o.at(L"limit"), s.limit);
    s.kill = cleanList(o.at(L"kill"), s.kill);
    s.autoStart = cleanList(o.at(L"autoStart"), s.autoStart);
    s.autoStop = cleanList(o.at(L"autoStop"), s.autoStop);
    return s;
}
void Store::saveSettings(const Settings& s) const {
    cache_->mutate(path_, [&](Value& root) { writeSettings(root, s); });
}
void Store::saveProfile(const Profile& p) const {
    checkName(p.name);
    cache_->mutate(path_, [&](Value& root) {
        if (Value* existing = findProfile(root, p.name)) { writeProfile(*existing, p); return; }
        Value created = Value::object();
        writeProfile(created, p);
        profileArray(root).push(std::move(created));
    });
}
void Store::save(const Profile& p, const Settings& s) const {
    checkName(p.name);
    cache_->mutate(path_, [&](Value& root) {
        if (Value* existing = findProfile(root, p.name)) writeProfile(*existing, p);
        else { Value created = Value::object(); writeProfile(created, p); profileArray(root).push(std::move(created)); }
        writeSettings(root, s);
    });
}
void Store::saveService(const ServiceOptions& s) const {
    for (const auto* list : {&s.limit, &s.kill, &s.autoStart, &s.autoStop}) for (const auto& item : *list) checkLine(item);
    checkLine(s.gameProcess);
    cache_->mutate(path_, [&](Value& root) {
        schema::writeService(root, s);
    });
}
std::wstring Store::loadGameDirectory() const {
    return cache_->current(path_).at(L"toolbox").at(L"gameDirectory").string();
}
void Store::saveGameDirectory(const std::wstring& directory) const {
    checkLine(directory);
    cache_->mutate(path_, [&](Value& root) {
        Value& toolbox = root[L"toolbox"];
        if (!toolbox.isObject()) toolbox = Value::object();
        toolbox.set(L"gameDirectory", directory);
    });
}
void Store::cloneProfile(const std::wstring& source, const std::wstring& newName) const {
    checkName(source); checkName(newName);
    cache_->mutate(path_, [&](Value& root) {
        if (findProfile(root, newName)) throw std::runtime_error("Profile already exists.");
        const Value* original = findProfile(root, source);
        if (!original) throw std::runtime_error("Profile to clone does not exist.");
        Value copy = *original;
        copy.set(L"name", newName);
        profileArray(root).push(std::move(copy));
    });
}
void Store::renameProfile(const std::wstring& name, const std::wstring& newName) const {
    checkName(name); checkName(newName);
    cache_->mutate(path_, [&](Value& root) {
        if (!equal(name, newName) && findProfile(root, newName)) throw std::runtime_error("Profile already exists.");
        Value* profile = findProfile(root, name);
        if (!profile) throw std::runtime_error("Profile to rename does not exist.");
        profile->set(L"name", newName);
    });
}
void Store::deleteProfile(const std::wstring& name) const {
    checkName(name);
    cache_->mutate(path_, [&](Value& root) {
        auto& items = profileArray(root).items();
        items.erase(std::remove_if(items.begin(), items.end(), [&](const Value& item) {
            return item.isObject() && equal(item.at(L"name").string(), name);
        }), items.end());
    });
}
static_assert(sizeof(Rule) == 132 * sizeof(unsigned), "Engine rule ABI mismatch");
} // namespace dafclient
