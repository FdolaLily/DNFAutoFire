#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "client_config.h"
#include <algorithm>
#include <climits>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

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
std::wstring sectionName(const std::wstring& line) {
    const auto text = trim(line);
    if (text.size() >= 2 && text.front() == L'[' && text.back() == L']') return trim(text.substr(1, text.size() - 2));
    return L"";
}
std::wstring decode(const std::string& bytes) {
    if (bytes.size() >= 2 && (static_cast<unsigned char>(bytes[0]) == 0xff || static_cast<unsigned char>(bytes[0]) == 0xfe)) {
        const bool le = static_cast<unsigned char>(bytes[0]) == 0xff && static_cast<unsigned char>(bytes[1]) == 0xfe;
        const bool be = static_cast<unsigned char>(bytes[0]) == 0xfe && static_cast<unsigned char>(bytes[1]) == 0xff;
        if (le || be) {
            if (bytes.size() % 2) throw std::runtime_error("Truncated UTF-16 configuration.");
            std::wstring text;
            for (size_t i = 2; i < bytes.size(); i += 2) {
                const auto a = static_cast<unsigned char>(bytes[i]), b = static_cast<unsigned char>(bytes[i + 1]);
                text += static_cast<wchar_t>(le ? a | (b << 8) : b | (a << 8));
            }
            return text;
        }
    }
    size_t start = bytes.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0;
    if (bytes.size() == start) return L"";
    UINT cp = CP_UTF8;
    int count = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, bytes.data() + start, static_cast<int>(bytes.size() - start), nullptr, 0);
    if (!count) { cp = CP_ACP; count = MultiByteToWideChar(cp, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0); start = 0; }
    if (!count) throw std::runtime_error("Cannot decode configuration.");
    std::wstring text(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(cp, 0, bytes.data() + start, static_cast<int>(bytes.size() - start), text.data(), count);
    return text;
}
class Ini {
public:
    explicit Ini(const std::wstring& path) : path_(path) {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const auto error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return;
            throw std::runtime_error("Cannot inspect configuration.");
        }
        std::ifstream file(std::filesystem::path(path), std::ios::binary);
        if (!file) throw std::runtime_error("Cannot read configuration.");
        const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (bytes.size() > 4 * 1024 * 1024) throw std::runtime_error("Configuration is too large.");
        std::wistringstream input(decode(bytes));
        std::wstring line;
        while (std::getline(input, line)) { if (!line.empty() && line.back() == L'\r') line.pop_back(); lines_.push_back(line); }
    }
    std::wstring get(const std::wstring& section, const std::wstring& key, const std::wstring& fallback = L"") const {
        bool active = false;
        for (const auto& line : lines_) {
            const auto name = sectionName(line);
            if (!name.empty()) { active = equal(name, section); continue; }
            if (!active) continue;
            const auto text = trim(line);
            if (text.empty() || text.front() == L';' || text.front() == L'#') continue;
            const auto separator = text.find(L'=');
            if (separator != text.npos && equal(trim(text.substr(0, separator)), key)) {
                auto value = trim(text.substr(separator + 1));
                if (value.size() >= 2 && ((value.front() == L'"' && value.back() == L'"') || (value.front() == L'\'' && value.back() == L'\'')))
                    value = value.substr(1, value.size() - 2);
                return value;
            }
        }
        return fallback;
    }
    unsigned num(const std::wstring& section, const std::wstring& key, unsigned fallback, unsigned low, unsigned high) const {
        return number(get(section, key), fallback, low, high);
    }
    void set(const std::wstring& section, const std::wstring& key, const std::wstring& value) {
        checkLine(value);
        bool active = false, found = false;
        size_t insert = lines_.size();
        for (size_t i = 0; i < lines_.size(); ++i) {
            const auto name = sectionName(lines_[i]);
            if (!name.empty()) {
                if (active) { insert = i; break; }
                active = equal(name, section); found = found || active; continue;
            }
            const auto text = trim(lines_[i]);
            const auto separator = text.find(L'=');
            if (active && separator != text.npos && equal(trim(text.substr(0, separator)), key)) {
                lines_[i] = key + L"=" + value; return;
            }
        }
        if (!found) { lines_.push_back(L"[" + section + L"]"); insert = lines_.size(); }
        lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(insert), key + L"=" + value);
    }
    void set(const std::wstring& section, const std::wstring& key, unsigned value) { set(section, key, std::to_wstring(value)); }
    std::vector<std::wstring> sections() const {
        std::vector<std::wstring> result;
        for (const auto& line : lines_) { const auto name = sectionName(line); if (!name.empty()) result.push_back(name); }
        return result;
    }
    void rename(const std::wstring& source, const std::wstring& target) {
        const auto names = sections();
        if (!equal(source, target) && std::any_of(names.begin(), names.end(), [&](const std::wstring& n) { return equal(n, target); }))
            throw std::runtime_error("Profile already exists.");
        bool found = false;
        for (auto& line : lines_) if (equal(sectionName(line), source)) { line = L"[" + target + L"]"; found = true; }
        if (!found) throw std::runtime_error("Profile to rename does not exist.");
    }
    void remove(const std::wstring& section) {
        bool active = false;
        lines_.erase(std::remove_if(lines_.begin(), lines_.end(), [&](const std::wstring& line) {
            const auto name = sectionName(line); if (!name.empty()) active = equal(name, section); return active;
        }), lines_.end());
    }
    void clone(const std::wstring& source, const std::wstring& target) {
        const auto names = sections();
        if (std::any_of(names.begin(), names.end(), [&](const std::wstring& n) { return equal(n, target); }))
            throw std::runtime_error("Profile already exists.");
        bool active = false, found = false;
        std::vector<std::wstring> copy;
        for (const auto& line : lines_) {
            const auto name = sectionName(line);
            if (!name.empty()) { active = equal(name, source); if (active) { found = true; copy.push_back(L"[" + target + L"]"); } }
            else if (active) copy.push_back(line);
        }
        if (!found) throw std::runtime_error("Profile to clone does not exist.");
        lines_.insert(lines_.end(), copy.begin(), copy.end());
    }
    void save() const {
        std::wstring data(1, L'\xfeff');
        for (const auto& line : lines_) data += line + L"\r\n";
        const auto temporary = path_ + L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetCurrentThreadId());
        HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create configuration transaction.");
        const DWORD size = static_cast<DWORD>(data.size() * sizeof(wchar_t));
        DWORD written = 0;
        const bool ok = WriteFile(file, data.data(), size, &written, nullptr) && written == size && FlushFileBuffers(file);
        CloseHandle(file);
        if (!ok || !MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temporary.c_str()); throw std::runtime_error("Cannot commit configuration transaction.");
        }
    }
private:
    std::wstring path_;
    std::vector<std::wstring> lines_;
};
const std::array<const wchar_t*, 4> directionFields{{L"OneKeyRunUpKey",L"OneKeyRunDownKey",L"OneKeyRunLeftKey",L"OneKeyRunRightKey"}};
const std::wstring settingsSection = L"设置";
const std::wstring profilePrefix = L"预设:";
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
std::vector<std::wstring> splitKeys(const std::wstring& text) { return split(text, L'|'); }
std::wstring joinKeys(const std::vector<std::wstring>& keys) {
    std::wstring result;
    for (const auto& key : keys) { if (!result.empty()) result += L'|'; result += key; }
    return result;
}

std::vector<Combo> parseCombos(const std::wstring& text) {
    std::vector<Combo> result;
    // Legacy representation remains the on-disk format for downgrade compatibility.
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
Store::Store(std::wstring path) : path_(std::move(path)) {}
Settings Store::loadSettings() const {
    const Ini ini(path_); Settings s; const auto& section = settingsSection;
    s.autoStart = ini.num(section,L"SettingAutoStart",1,0,1) != 0;
    s.onSystemStart = ini.num(section,L"SettingOnSystemStart",0,0,1) != 0;
    s.blockWin = ini.num(section,L"SettingBlockWin",0,0,1) != 0;
    s.lastPreset = ini.get(section,L"LastPreset");
    s.quickSwitchHotkey = ini.get(section,L"QuickChangeHotKey",s.quickSwitchHotkey);
    s.oneKeyRun.enabled = ini.num(section,L"OneKeyRunState",0,0,1) != 0;
    for (size_t i = 0; i < 4; ++i) s.oneKeyRun.keys[i] = ini.get(section,directionFields[i],s.oneKeyRun.keys[i]);
    s.oneKeyRun.pressMs = ini.num(section,L"OneKeyRunPressDelay",30,1,1000);
    s.oneKeyRun.gapMs = ini.num(section,L"OneKeyRunGapDelay",30,1,1000);
    s.oneKeyRun.guardMs = ini.num(section,L"OneKeyRunGuardDelay",kDefaultGuardMs,kMinGuardMs,kMaxGuardMs);
    // Historical AHK default values migrate to the current default, as OneKeyRunGetGuardDelay did.
    if (s.oneKeyRun.guardMs == 350 || s.oneKeyRun.guardMs == 200 || s.oneKeyRun.guardMs == 180)
        s.oneKeyRun.guardMs = kDefaultGuardMs;
    s.oneKeyRun.toggleHotkey = ini.get(section,L"OneKeyRunToggleHotKey",L"F10");
    s.theme = equal(ini.get(section,L"SettingTheme",L"dark"), L"light") ? L"light" : L"dark";
    return s;
}
Profile Store::loadProfile(const std::wstring& name) const {
    const Ini ini(path_); Profile p; p.name = name; const auto section = profilePrefix + name;
    p.keys = splitKeys(ini.get(section,L"keys"));
    p.downMs = ini.num(section,L"AutoFireDownMs",kDefaultFireMs,1,100);
    p.upMs = ini.num(section,L"AutoFireUpMs",kDefaultFireMs,1,100);
    p.lvRen = ini.num(section,L"LvRenState",0,0,1) != 0;
    p.zhanFa = ini.num(section,L"ZhanFaState",0,0,1) != 0;
    p.jianZong = ini.num(section,L"JianZongState",0,0,1) != 0;
    p.combo = ini.num(section,L"ComboState",0,0,1) != 0;
    p.lvRenShotKey = ini.get(section,L"LvRenShotKey",L"Z");
    p.zhanFaShotKey = ini.get(section,L"ZhanFaShotKey");
    p.jianZongSkillKey = ini.get(section,L"JianZongSkillKey",L"A");
    p.lvRenSkillKeys = splitKeys(ini.get(section,L"LvRenSkillKeys"));
    p.zhanFaSkillKeys = splitKeys(ini.get(section,L"ZhanFaSkillKeys"));
    p.jianZongDelayMs = ini.num(section,L"JianZongDelay",200,1,10000);
    p.usePresetRunKeys = ini.num(section,L"OneKeyRunUsePresetKeys",0,0,1) != 0;
    for (size_t i = 0; i < 4; ++i) p.runKeys[i] = ini.get(section,directionFields[i],ini.get(settingsSection,directionFields[i],p.runKeys[i]));
    p.combos = parseCombos(ini.get(section,L"combos"));
    return p;
}
std::vector<std::wstring> Store::presetNames() const {
    const Ini ini(path_); std::vector<std::wstring> result;
    for (const auto& section : ini.sections()) {
        if (section.size() > profilePrefix.size() && section.rfind(profilePrefix,0) == 0) {
            const auto name = section.substr(profilePrefix.size());
            if (std::none_of(result.begin(),result.end(),[&](const std::wstring& n) { return equal(n,name); })) result.push_back(name);
        }
    }
    return result;
}
void Store::saveSettings(const Settings& s) const {
    Ini ini(path_); const auto& section = settingsSection;
    ini.set(section,L"SettingAutoStart",s.autoStart); ini.set(section,L"SettingOnSystemStart",s.onSystemStart);
    ini.set(section,L"SettingBlockWin",s.blockWin); ini.set(section,L"LastPreset",s.lastPreset);
    ini.set(section,L"QuickChangeHotKey",s.quickSwitchHotkey); ini.set(section,L"OneKeyRunState",s.oneKeyRun.enabled);
    for (size_t i = 0; i < 4; ++i) ini.set(section,directionFields[i],s.oneKeyRun.keys[i]);
    ini.set(section,L"OneKeyRunPressDelay",s.oneKeyRun.pressMs); ini.set(section,L"OneKeyRunGapDelay",s.oneKeyRun.gapMs);
    ini.set(section,L"OneKeyRunGuardDelay",s.oneKeyRun.guardMs); ini.set(section,L"OneKeyRunToggleHotKey",s.oneKeyRun.toggleHotkey);
    ini.set(section,L"SettingTheme",s.theme == L"light" ? L"light" : L"dark");
    ini.save();
}
void Store::saveProfile(const Profile& p) const {
    checkName(p.name); Ini ini(path_); const auto section = profilePrefix + p.name;
    ini.set(section,L"keys",joinKeys(p.keys)); ini.set(section,L"AutoFireDownMs",p.downMs); ini.set(section,L"AutoFireUpMs",p.upMs);
    ini.set(section,L"LvRenState",p.lvRen); ini.set(section,L"ZhanFaState",p.zhanFa); ini.set(section,L"JianZongState",p.jianZong);
    ini.set(section,L"ComboState",p.combo); ini.set(section,L"LvRenShotKey",p.lvRenShotKey); ini.set(section,L"ZhanFaShotKey",p.zhanFaShotKey);
    ini.set(section,L"JianZongSkillKey",p.jianZongSkillKey); ini.set(section,L"LvRenSkillKeys",joinKeys(p.lvRenSkillKeys));
    ini.set(section,L"ZhanFaSkillKeys",joinKeys(p.zhanFaSkillKeys)); ini.set(section,L"JianZongDelay",p.jianZongDelayMs);
    ini.set(section,L"OneKeyRunUsePresetKeys",p.usePresetRunKeys);
    if (p.usePresetRunKeys) for (size_t i = 0; i < 4; ++i) ini.set(section,directionFields[i],p.runKeys[i]);
    ini.set(section,L"combos",serializeCombos(p.combos)); ini.save();
}
void Store::cloneProfile(const std::wstring& source, const std::wstring& newName) const {
    checkName(source); checkName(newName); Ini ini(path_); ini.clone(profilePrefix + source,profilePrefix + newName); ini.save();
}
void Store::renameProfile(const std::wstring& name, const std::wstring& newName) const {
    checkName(name); checkName(newName); Ini ini(path_); ini.rename(profilePrefix + name, profilePrefix + newName); ini.save();
}
void Store::deleteProfile(const std::wstring& name) const {
    checkName(name); Ini ini(path_); ini.remove(profilePrefix + name); ini.save();
}
static_assert(sizeof(Rule) == 132 * sizeof(unsigned), "Engine rule ABI mismatch");
} // namespace dafclient
