#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "config_migrate.h"
#include "config_schema.h"
#include "win_fs.h"
#include <algorithm>
#include <cwctype>
#include <sstream>
#include <stdexcept>

namespace dafclient {
namespace {
using json::Value;

std::wstring trim(std::wstring s) {
    const auto first = s.find_first_not_of(L" \t\r\n");
    return first == s.npos ? L"" : s.substr(first, s.find_last_not_of(L" \t\r\n") - first + 1);
}
std::wstring lower(std::wstring s) { for (auto& c : s) c = wchar_t(towlower(c)); return s; }
bool equal(const std::wstring& a, const std::wstring& b) { return lower(a) == lower(b); }
unsigned number(const std::wstring& s, unsigned fallback, unsigned low, unsigned high) {
    const auto text = trim(s);
    if (text.empty()) return fallback;
    try {
        size_t pos = 0;
        const auto n = std::stoll(text, &pos);
        if (pos != text.size()) return fallback;
        return n < static_cast<long long>(low) ? low : n > static_cast<long long>(high) ? high : static_cast<unsigned>(n);
    } catch (...) { return fallback; }
}

// Read-only INI model of the former config.ini, including its section order and
// every key, so nothing the old client wrote is lost by the import.
struct IniSection { std::wstring name; std::vector<std::pair<std::wstring, std::wstring>> entries; };
std::vector<IniSection> parseIni(const std::wstring& text) {
    std::vector<IniSection> sections;
    std::wistringstream input(text);
    std::wstring line;
    IniSection* current = nullptr;
    while (std::getline(input, line)) {
        const auto t = trim(line);
        if (t.empty() || t.front() == L';' || t.front() == L'#') continue;
        if (t.size() >= 2 && t.front() == L'[' && t.back() == L']') {
            const auto name = trim(t.substr(1, t.size() - 2));
            auto found = std::find_if(sections.begin(), sections.end(), [&](const IniSection& s) { return equal(s.name, name); });
            if (found == sections.end()) { sections.push_back({name, {}}); current = &sections.back(); }
            else current = &*found;
            continue;
        }
        if (!current) continue;
        const auto separator = t.find(L'=');
        if (separator == t.npos) continue;
        const auto key = trim(t.substr(0, separator));
        auto value = trim(t.substr(separator + 1));
        if (value.size() >= 2 && ((value.front() == L'"' && value.back() == L'"') || (value.front() == L'\'' && value.back() == L'\'')))
            value = value.substr(1, value.size() - 2);
        auto existing = std::find_if(current->entries.begin(), current->entries.end(), [&](const auto& e) { return equal(e.first, key); });
        if (existing == current->entries.end()) current->entries.push_back({key, value}); // First value wins, as GetPrivateProfileString.
    }
    return sections;
}
const IniSection* section(const std::vector<IniSection>& all, const std::wstring& name) {
    for (const auto& s : all) if (equal(s.name, name)) return &s;
    return nullptr;
}
bool has(const IniSection* s, const std::wstring& key) {
    return s && std::any_of(s->entries.begin(), s->entries.end(), [&](const auto& e) { return equal(e.first, key); });
}
std::wstring get(const IniSection* s, const std::wstring& key, const std::wstring& fallback = L"") {
    if (s) for (const auto& e : s->entries) if (equal(e.first, key)) return e.second;
    return fallback;
}
unsigned num(const IniSection* s, const std::wstring& key, unsigned fallback, unsigned low, unsigned high) {
    return number(get(s, key), fallback, low, high);
}
const wchar_t* const kDirectionFields[] = {L"OneKeyRunUpKey", L"OneKeyRunDownKey", L"OneKeyRunLeftKey", L"OneKeyRunRightKey"};
const wchar_t* const kSettingsKeys[] = {L"SettingAutoStart", L"SettingOnSystemStart", L"SettingBlockWin", L"LastPreset",
    L"QuickChangeHotKey", L"OneKeyRunState", L"OneKeyRunUpKey", L"OneKeyRunDownKey", L"OneKeyRunLeftKey", L"OneKeyRunRightKey",
    L"OneKeyRunPressDelay", L"OneKeyRunGapDelay", L"OneKeyRunGuardDelay", L"OneKeyRunToggleHotKey", L"SettingTheme"};
const wchar_t* const kProfileKeys[] = {L"keys", L"AutoFireDownMs", L"AutoFireUpMs", L"LvRenState", L"ZhanFaState", L"JianZongState",
    L"ComboState", L"LvRenShotKey", L"ZhanFaShotKey", L"JianZongSkillKey", L"LvRenSkillKeys", L"ZhanFaSkillKeys", L"JianZongDelay",
    L"OneKeyRunUsePresetKeys", L"OneKeyRunUpKey", L"OneKeyRunDownKey", L"OneKeyRunLeftKey", L"OneKeyRunRightKey", L"combos"};
template <size_t N> bool known(const wchar_t* const (&list)[N], const std::wstring& key) {
    return std::any_of(std::begin(list), std::end(list), [&](const wchar_t* k) { return equal(k, key); });
}
Value unknownEntries(const IniSection& s, bool (*isKnown)(const std::wstring&)) {
    Value extra = Value::object();
    for (const auto& e : s.entries) if (!isKnown(e.first)) extra.set(e.first, e.second);
    return extra;
}
bool knownSetting(const std::wstring& key) { return known(kSettingsKeys, key); }
bool knownProfile(const std::wstring& key) { return known(kProfileKeys, key); }
bool noneKnown(const std::wstring&) { return false; }

const std::wstring kSettingsSection = L"设置";
const std::wstring kProfilePrefix = L"预设:";

Settings settingsFrom(const IniSection* s) {
    Settings result;
    result.autoStart = num(s, L"SettingAutoStart", 1, 0, 1) != 0;
    result.onSystemStart = num(s, L"SettingOnSystemStart", 0, 0, 1) != 0;
    result.blockWin = num(s, L"SettingBlockWin", 0, 0, 1) != 0;
    result.lastPreset = get(s, L"LastPreset");
    result.quickSwitchHotkey = get(s, L"QuickChangeHotKey", result.quickSwitchHotkey);
    result.oneKeyRun.enabled = num(s, L"OneKeyRunState", 0, 0, 1) != 0;
    for (size_t i = 0; i < 4; ++i) result.oneKeyRun.keys[i] = get(s, kDirectionFields[i], result.oneKeyRun.keys[i]);
    result.oneKeyRun.pressMs = num(s, L"OneKeyRunPressDelay", 30, 1, 1000);
    result.oneKeyRun.gapMs = num(s, L"OneKeyRunGapDelay", 30, 1, 1000);
    result.oneKeyRun.guardMs = num(s, L"OneKeyRunGuardDelay", kDefaultGuardMs, kMinGuardMs, kMaxGuardMs);
    if (result.oneKeyRun.guardMs == 350 || result.oneKeyRun.guardMs == 200 || result.oneKeyRun.guardMs == 180)
        result.oneKeyRun.guardMs = kDefaultGuardMs;
    result.oneKeyRun.toggleHotkey = get(s, L"OneKeyRunToggleHotKey", L"F10");
    result.theme = equal(get(s, L"SettingTheme", L"dark"), L"light") ? L"light" : L"dark";
    return result;
}
Profile profileFrom(const std::wstring& name, const IniSection* s, const IniSection* settings) {
    Profile p; p.name = name;
    p.keys = splitKeys(get(s, L"keys"));
    p.downMs = num(s, L"AutoFireDownMs", kDefaultFireMs, 1, 100);
    p.upMs = num(s, L"AutoFireUpMs", kDefaultFireMs, 1, 100);
    p.lvRen = num(s, L"LvRenState", 0, 0, 1) != 0;
    p.zhanFa = num(s, L"ZhanFaState", 0, 0, 1) != 0;
    p.jianZong = num(s, L"JianZongState", 0, 0, 1) != 0;
    p.combo = num(s, L"ComboState", 0, 0, 1) != 0;
    p.lvRenShotKey = get(s, L"LvRenShotKey", L"Z");
    p.zhanFaShotKey = get(s, L"ZhanFaShotKey");
    p.jianZongSkillKey = get(s, L"JianZongSkillKey", L"A");
    p.lvRenSkillKeys = splitKeys(get(s, L"LvRenSkillKeys"));
    p.zhanFaSkillKeys = splitKeys(get(s, L"ZhanFaSkillKeys"));
    p.jianZongDelayMs = num(s, L"JianZongDelay", 200, 1, 10000);
    p.usePresetRunKeys = num(s, L"OneKeyRunUsePresetKeys", 0, 0, 1) != 0;
    for (size_t i = 0; i < 4; ++i) p.runKeys[i] = get(s, kDirectionFields[i], get(settings, kDirectionFields[i], p.runKeys[i]));
    p.combos = parseCombos(get(s, L"combos"));
    return p;
}
// Case-insensitive member lookup, as .NET configuration keys are case-insensitive.
const Value* member(const Value& object, const std::wstring& key) {
    for (const auto& m : object.members()) if (equal(m.key, key)) return &m.value;
    return nullptr;
}
bool boolFrom(const Value* v, bool fallback) {
    if (!v) return fallback;
    if (v->isBool()) return v->boolean(fallback);
    if (v->isString()) { const auto t = lower(trim(v->str())); if (t == L"true") return true; if (t == L"false") return false; }
    return fallback;
}
unsigned unsignedFrom(const Value* v, unsigned fallback, unsigned low, unsigned high) {
    if (!v) return fallback;
    if (v->isNumber()) return v->integer(fallback, low, high);
    if (v->isString()) return number(v->str(), fallback, low, high);
    return fallback;
}
std::vector<std::wstring> listFrom(const Value* v, const std::vector<std::wstring>& fallback) {
    if (!v) return fallback;
    if (v->isArray()) return v->stringList();
    // .NET binds {"0": "a", "1": "b"} objects to lists as well.
    if (v->isObject()) { std::vector<std::wstring> out; for (const auto& m : v->members()) if (m.value.isString()) out.push_back(m.value.str()); return out; }
    return fallback;
}
} // namespace

std::wstring decodeLegacyText(const std::string& bytes) {
    if (bytes.size() >= 2) {
        const auto b0 = static_cast<unsigned char>(bytes[0]), b1 = static_cast<unsigned char>(bytes[1]);
        const bool le = b0 == 0xff && b1 == 0xfe, be = b0 == 0xfe && b1 == 0xff;
        if (le || be) {
            if (bytes.size() % 2) throw std::runtime_error("Truncated UTF-16 configuration.");
            std::wstring text;
            text.reserve(bytes.size() / 2);
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
    if (!count) { cp = CP_ACP; start = 0; count = MultiByteToWideChar(cp, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0); }
    if (!count) throw std::runtime_error("Cannot decode configuration.");
    std::wstring text(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(cp, 0, bytes.data() + start, static_cast<int>(bytes.size() - start), text.data(), count);
    return text;
}

void importLegacyIni(Value& root, const std::wstring& iniText) {
    const auto sections = parseIni(iniText);
    const IniSection* settingsSection = section(sections, kSettingsSection);
    schema::writeSettings(root, settingsFrom(settingsSection));
    if (settingsSection) {
        Value extra = unknownEntries(*settingsSection, knownSetting);
        if (!extra.members().empty()) root[L"settings"].set(L"legacyIni", std::move(extra));
    }
    Value& profiles = root[L"profiles"];
    if (!profiles.isArray()) profiles = Value::array();
    Value others = Value::object();
    std::vector<std::wstring> names;
    for (const auto& s : sections) {
        if (equal(s.name, kSettingsSection)) continue;
        const bool isProfile = s.name.size() > kProfilePrefix.size() && s.name.rfind(kProfilePrefix, 0) == 0;
        const std::wstring name = isProfile ? s.name.substr(kProfilePrefix.size()) : L"";
        const bool validName = isProfile && !trim(name).empty() && name.find_first_of(L"[]|\r\n") == name.npos;
        if (!validName || std::any_of(names.begin(), names.end(), [&](const std::wstring& n) { return equal(n, name); })) {
            others.set(s.name, unknownEntries(s, noneKnown)); // Unrelated or unusable sections are kept verbatim.
            continue;
        }
        names.push_back(name);
        const Profile model = profileFrom(name, &s, settingsSection);
        Value object = Value::object();
        schema::writeProfile(object, model);
        const bool explicitDirections = std::any_of(std::begin(kDirectionFields), std::end(kDirectionFields),
            [&](const wchar_t* field) { return has(&s, field); });
        if (!model.usePresetRunKeys && explicitDirections) schema::writeDormantRunKeys(object, model.runKeys);
        Value extra = unknownEntries(s, knownProfile);
        if (!extra.members().empty()) object.set(L"legacyIni", std::move(extra));
        profiles.push(std::move(object));
    }
    if (!others.members().empty()) root.set(L"legacyIni", std::move(others));
}

ServiceOptions importAppSettings(const std::string& utf8) {
    json::ParseOptions options; options.comments = true; options.trailingCommas = true;
    const Value root = json::parse(utf8, options);
    if (!root.isObject()) throw std::runtime_error("appsettings.json must contain an object.");
    const Value* manager = member(root, L"Manager");
    ServiceOptions s;
    if (!manager || !manager->isObject()) return s;
    const Value& m = *manager;
    if (const Value* v = member(m, L"ProcessName"); v && v->isString()) s.gameProcess = v->str();
    s.pollSeconds = unsignedFrom(member(m, L"ProcessPollSeconds"), s.pollSeconds, kMinPollSeconds, kMaxPollSeconds);
    s.actionDelaySeconds = unsignedFrom(member(m, L"ActionDelaySeconds"), s.actionDelaySeconds, 0, kMaxActionDelaySeconds);
    s.closeLauncherAfterGame = boolFrom(member(m, L"CloseLauncherIfGameNotStarted"), s.closeLauncherAfterGame);
    s.optimizeGamePriority = boolFrom(member(m, L"OptimizeGamePriority"), s.optimizeGamePriority);
    if (const Value* v = member(m, L"GamePriority"); v && v->isString()) s.aboveNormalPriority = !equal(trim(v->str()), L"Normal");
    s.limit = listFrom(member(m, L"LimitList"), s.limit);
    s.kill = listFrom(member(m, L"KillList"), s.kill);
    s.autoStart = listFrom(member(m, L"AutoStart"), s.autoStart);
    s.autoStop = listFrom(member(m, L"AutoStop"), s.autoStop);
    return schema::normalizeService(s);
}

MigrationReport migrateLegacyConfig(const std::wstring& directory) {
    MigrationReport report;
    const auto target = fs::join(directory, kConfigFileName);
    const auto ini = fs::join(directory, kLegacyIniName);
    const auto app = fs::join(directory, kLegacyServiceJsonName);
    const auto dev = fs::join(directory, kLegacyServiceDevJsonName);
    if (!fs::isFile(ini) && !fs::isFile(app) && !fs::isFile(dev)) return report;
    fs::ConfigLock lock; // Client and service may start together; the second caller sees no legacy file.
    try {
        Value root = Value::object();
        if (fs::isFile(target)) {
            root = json::parse(fs::read(target, 4 * 1024 * 1024));
            if (!root.isObject()) throw std::runtime_error("config.json must contain a JSON object.");
        }
        const bool hadClient = root.find(L"settings") || root.find(L"profiles");
        const bool hadService = root.find(L"service") != nullptr;
        bool iniImported = false, appImported = false;
        std::wstring problems;
        const auto note = [&](const wchar_t* file, const std::exception& error) {
            if (!problems.empty()) problems += L"；";
            problems += std::wstring(file) + L"：";
            for (const unsigned char c : std::string(error.what())) problems.push_back(wchar_t(c));
        };
        // Each legacy file is imported on its own, so a damaged one never blocks the other.
        if (fs::isFile(ini)) {
            if (hadClient) report.kept.push_back(kLegacyIniName);
            else try {
                Value next = root;
                importLegacyIni(next, decodeLegacyText(fs::read(ini, 4 * 1024 * 1024)));
                root = std::move(next); iniImported = true;
            } catch (const std::exception& error) { note(kLegacyIniName, error); report.kept.push_back(kLegacyIniName); }
        }
        if (fs::isFile(app)) {
            if (hadService) report.kept.push_back(kLegacyServiceJsonName);
            else try {
                const auto options = importAppSettings(fs::read(app, 1024 * 1024));
                schema::writeService(root, options); appImported = true;
            } catch (const std::exception& error) { note(kLegacyServiceJsonName, error); report.kept.push_back(kLegacyServiceJsonName); }
        }
        report.error = problems;
        if (iniImported || appImported) {
            if (!root.find(L"version")) root.members().insert(root.members().begin(), {L"version", Value(kConfigSchemaVersion)});
            // Stage and verify through the real loader before config.json or anything legacy changes.
            const auto staged = target + L".migrating";
            fs::writeAtomic(staged, json::serialize(root));
            try {
                const Store verify(staged);
                const auto names = verify.presetNames();
                verify.loadSettings(); verify.loadService();
                for (const auto& name : names) verify.loadProfile(name);
            } catch (...) { fs::remove(staged); throw; }
            if (!MoveFileExW(staged.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                fs::remove(staged);
                throw std::runtime_error("Cannot commit config.json.");
            }
            if (iniImported) report.imported.push_back(kLegacyIniName);
            if (appImported) report.imported.push_back(kLegacyServiceJsonName);
        }
        const auto drop = [&](const std::wstring& path, const wchar_t* name) {
            if (fs::remove(path)) report.removed.push_back(name);
            else report.kept.push_back(name);
        };
        if (iniImported) drop(ini, kLegacyIniName);
        if (appImported) drop(app, kLegacyServiceJsonName);
        // The former service's logging override has no meaning once its settings are merged.
        if (fs::isFile(dev) && (appImported || hadService)) drop(dev, kLegacyServiceDevJsonName);
    } catch (const std::exception& error) {
        report.imported.clear(); report.removed.clear();
        std::wstring detail;
        for (const unsigned char c : std::string(error.what())) detail.push_back(wchar_t(c));
        report.error = report.error.empty() ? detail : report.error + L"；" + detail;
    }
    return report;
}

} // namespace dafclient
