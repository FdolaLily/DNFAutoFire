#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "client_ui.h"
#include "client_gfx.h"
#include "client_ui_service.h"
#include "client_ui_toolbox.h"
#include "ui_motion.h"
#include "version.h"
#include "win_fs.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace dafclient {
using namespace gfx;
namespace {
constexpr UINT kTrayMessage = WM_APP + 0x27;
constexpr UINT_PTR kSaveTimer = 0xDA01, kAnimTimer = 0xDA02, kServiceTimer = 0xDA03, kServiceSaveTimer = 0xDA04, kFrameTimer = 0xDA05, kAuxTimer = 0xDA06;
constexpr UINT kSaveDelayMs = 400;
constexpr float kW = 1280.f, kH = 800.f;
constexpr wchar_t kVersion[] = L"v" DAF_VERSION_SHORT;        // native/version.h
constexpr wchar_t kFullVersion[] = L"v" DAF_VERSION_STRING;
enum TrayCommand : UINT { TrayToggle = 1, TrayQuick, TrayShow, TrayExit };
constexpr float kTrayMenuW = 232.f;

// Hit ids. Stable ids let hover and press states survive a repaint.
enum : int {
    IdTheme = 1, IdSettings, IdMin, IdClose, IdPower, IdNew, IdMore, IdOverflow, IdExt, IdClear,
    IdScrim, IdSheet, IdSheetClose, IdSheetSwitch, IdResetTiming, IdDownDec, IdDownInc, IdUpDec, IdUpInc,
    IdClassLink, IdLvRow, IdZfRow, IdJzRow, IdLvSw, IdZfSw, IdJzSw, IdComboSw, IdComboLink, IdRunSw, IdRunLink,
    IdMenuRename, IdMenuClone, IdMenuDelete, IdMenuPanel, IdAddCombo, IdScopeAll, IdScopeOne,
    IdGuardDec, IdGuardInc, IdGapDec, IdGapInc, IdPressDec, IdPressInc, IdJzDec, IdJzInc,
    IdAutoSw, IdLoginSw, IdWinSw, IdExtPanel, IdQuickChip, IdService, IdToolbox,
    IdKey = 1000, IdExtKey = 1200, IdTab = 1300, IdOverflowItem = 1400,
    IdComboSelect = 2000, IdComboDelete = 2100, IdComboDelay = 2200,
    IdCapRun = 3000, IdCapRunHotkey = 3010, IdCapQuick, IdCapLvShot, IdCapZfShot, IdCapJz, IdCapLvAdd, IdCapZfAdd,
    IdCapComboTrigger = 3100, IdCapComboStep = 3200,
    IdChipLv = 4000, IdChipZf = 4100,
    IdComboStepRemove = 6000 // + combo * 8 + step
};

D2D1_RECT_F box(float x, float y, float w, float h) { return D2D1::RectF(x, y, x + w, y + h); }
bool contains(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
D2D1_RECT_F intersect(const D2D1_RECT_F& a, const D2D1_RECT_F& b) {
    return D2D1::RectF(std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right), std::min(a.bottom, b.bottom));
}
COLORREF colorref(Color c) { return RGB(int(c.r * 255 + .5f), int(c.g * 255 + .5f), int(c.b * 255 + .5f)); }
std::wstring trim(std::wstring value) {
    while (!value.empty() && iswspace(value.front())) value.erase(value.begin());
    while (!value.empty() && iswspace(value.back())) value.pop_back();
    return value;
}
std::wstring lower(std::wstring s) { for (auto& c : s) c = static_cast<wchar_t>(towlower(c)); return s; }

unsigned physical(const std::wstring& name) { const auto key = parseKey(name); return key ? key.physical_id() + 1 : 0; }

// Short labels for bound keys (capture fields, chips, previews).
std::wstring labelOf(const std::wstring& raw) {
    if (raw.empty()) return L"";
    const auto key = parseKey(raw);
    const std::wstring name = key ? key.name : raw;
    static const std::map<std::wstring, std::wstring> labels = {
        {L"Tilde", L"`"}, {L"Sub", L"-"}, {L"Add", L"="}, {L"LeftBracket", L"["}, {L"RightBracket", L"]"},
        {L"Backslash", L"\\"}, {L"Semicolon", L";"}, {L"QuotationMark", L"'"}, {L"Comma", L","}, {L"Period", L"."},
        {L"Slash", L"/"}, {L"LShift", L"L-Shift"}, {L"RShift", L"R-Shift"}, {L"LCtrl", L"L-Ctrl"}, {L"RCtrl", L"R-Ctrl"},
        {L"LAlt", L"L-Alt"}, {L"RAlt", L"R-Alt"}, {L"LWin", L"L-Win"}, {L"RWin", L"R-Win"}, {L"AppsKey", L"Menu"},
        {L"Up", L"↑"}, {L"Down", L"↓"}, {L"Left", L"←"}, {L"Right", L"→"}, {L"NumLk", L"NumLock"},
        {L"NumSlash", L"Num /"}, {L"NumStar", L"Num *"}, {L"NumSub", L"Num -"}, {L"NumAdd", L"Num +"},
        {L"NumEnter", L"Num Enter"}, {L"NumPeriod", L"Num ."}};
    const auto found = labels.find(name);
    if (found != labels.end()) return found->second;
    if (name.size() == 4 && name.rfind(L"Num", 0) == 0 && iswdigit(name[3])) return L"Num " + name.substr(3);
    return name;
}
std::wstring hotkeyLabel(const std::wstring& stored) {
    const auto hotkey = parseHotkey(stored);
    if (!hotkey) return L"";
    std::wstring text;
    if (hotkey.modifiers & MOD_CONTROL) text += L"Ctrl + ";
    if (hotkey.modifiers & MOD_ALT) text += L"Alt + ";
    if (hotkey.modifiers & MOD_SHIFT) text += L"Shift + ";
    if (hotkey.modifiers & MOD_WIN) text += L"Win + ";
    return text + labelOf(hotkey.key.name);
}

struct BoardKey { std::wstring id, label; float x, y, w, h; bool mod; };
// Full-size 104-key layout: 1u = 49 DIP pitch with a 5 DIP gap.
const std::vector<BoardKey>& boardKeys() {
    static const std::vector<BoardKey> keys = [] {
        constexpr float P = 49.f, G = 5.f;
        std::vector<BoardKey> v;
        const auto add = [&](const wchar_t* id, const wchar_t* label, float x, float y, float w = 1, float h = 1, bool mod = false) {
            v.push_back({id, label, std::round(x * P), std::round(y * P), std::round(w * P - G), std::round(h * P - G), mod});
        };
        const auto row = [&](std::initializer_list<std::pair<const wchar_t*, const wchar_t*>> items, float x, float y, bool mod = false) {
            for (const auto& item : items) { add(item.first, item.second, x, y, 1, 1, mod); x += 1; }
        };
        const float R[] = {0.f, 1.4f, 2.4f, 3.4f, 4.4f, 5.4f};
        add(L"Esc", L"Esc", 0, R[0], 1, 1, true);
        const wchar_t* f[] = {L"F1", L"F2", L"F3", L"F4", L"F5", L"F6", L"F7", L"F8", L"F9", L"F10", L"F11", L"F12"};
        for (int i = 0; i < 4; ++i) {
            add(f[i], f[i], 2.f + i, R[0], 1, 1, true);
            add(f[i + 4], f[i + 4], 6.5f + i, R[0], 1, 1, true);
            add(f[i + 8], f[i + 8], 11.f + i, R[0], 1, 1, true);
        }
        row({{L"Tilde", L"`"}, {L"1", L"1"}, {L"2", L"2"}, {L"3", L"3"}, {L"4", L"4"}, {L"5", L"5"}, {L"6", L"6"},
             {L"7", L"7"}, {L"8", L"8"}, {L"9", L"9"}, {L"0", L"0"}, {L"Sub", L"-"}, {L"Add", L"="}}, 0, R[1]);
        add(L"Backspace", L"Backspace", 13, R[1], 2, 1, true);
        add(L"Tab", L"Tab", 0, R[2], 1.5f, 1, true);
        row({{L"Q", L"Q"}, {L"W", L"W"}, {L"E", L"E"}, {L"R", L"R"}, {L"T", L"T"}, {L"Y", L"Y"}, {L"U", L"U"},
             {L"I", L"I"}, {L"O", L"O"}, {L"P", L"P"}, {L"LeftBracket", L"["}, {L"RightBracket", L"]"}}, 1.5f, R[2]);
        add(L"Backslash", L"\\", 13.5f, R[2], 1.5f, 1, false);
        add(L"Caps", L"Caps", 0, R[3], 1.75f, 1, true);
        row({{L"A", L"A"}, {L"S", L"S"}, {L"D", L"D"}, {L"F", L"F"}, {L"G", L"G"}, {L"H", L"H"}, {L"J", L"J"},
             {L"K", L"K"}, {L"L", L"L"}, {L"Semicolon", L";"}, {L"QuotationMark", L"'"}}, 1.75f, R[3]);
        add(L"Enter", L"Enter", 12.75f, R[3], 2.25f, 1, true);
        add(L"LShift", L"Shift", 0, R[4], 2.25f, 1, true);
        row({{L"Z", L"Z"}, {L"X", L"X"}, {L"C", L"C"}, {L"V", L"V"}, {L"B", L"B"}, {L"N", L"N"}, {L"M", L"M"},
             {L"Comma", L","}, {L"Period", L"."}, {L"Slash", L"/"}}, 2.25f, R[4]);
        add(L"RShift", L"Shift", 12.25f, R[4], 2.75f, 1, true);
        add(L"LCtrl", L"Ctrl", 0, R[5], 1.25f, 1, true);
        add(L"LWin", L"Win", 1.25f, R[5], 1.25f, 1, true);
        add(L"LAlt", L"Alt", 2.5f, R[5], 1.25f, 1, true);
        add(L"Space", L"", 3.75f, R[5], 6.25f, 1, false);
        add(L"RAlt", L"Alt", 10, R[5], 1.25f, 1, true);
        add(L"RWin", L"Win", 11.25f, R[5], 1.25f, 1, true);
        add(L"AppsKey", L"Menu", 12.5f, R[5], 1.25f, 1, true);
        add(L"RCtrl", L"Ctrl", 13.75f, R[5], 1.25f, 1, true);
        const float NX = 15.4f, NP = 18.8f;
        row({{L"PrtSc", L"PrtSc"}, {L"ScrLk", L"ScrLk"}, {L"Pause", L"Pause"}}, NX, R[0], true);
        row({{L"Ins", L"Ins"}, {L"Home", L"Home"}, {L"PgUp", L"PgUp"}}, NX, R[1], true);
        row({{L"Del", L"Del"}, {L"End", L"End"}, {L"PgDn", L"PgDn"}}, NX, R[2], true);
        add(L"Up", L"↑", NX + 1, R[4], 1, 1, true);
        row({{L"Left", L"←"}, {L"Down", L"↓"}, {L"Right", L"→"}}, NX, R[5], true);
        row({{L"NumLk", L"Num"}, {L"NumSlash", L"/"}, {L"NumStar", L"*"}, {L"NumSub", L"-"}}, NP, R[1], true);
        row({{L"Num7", L"7"}, {L"Num8", L"8"}, {L"Num9", L"9"}}, NP, R[2]);
        add(L"NumAdd", L"+", NP + 3, R[2], 1, 2, true);
        row({{L"Num4", L"4"}, {L"Num5", L"5"}, {L"Num6", L"6"}}, NP, R[3]);
        row({{L"Num1", L"1"}, {L"Num2", L"2"}, {L"Num3", L"3"}}, NP, R[4]);
        add(L"NumEnter", L"Enter", NP + 3, R[4], 1, 2, true);
        add(L"Num0", L"0", NP, R[5], 2, 1, false);
        add(L"NumPeriod", L".", NP + 2, R[5], 1, 1, false);
        return v;
    }();
    return keys;
}
const std::vector<std::wstring>& extKeys() {
    static const std::vector<std::wstring> keys = [] {
        std::vector<std::wstring> v; for (int i = 13; i <= 24; ++i) v.push_back(L"F" + std::to_wstring(i)); return v;
    }();
    return keys;
}

enum class Drawer { None, Class, Combo, Run, Settings, Service, Toolbox };
enum class Occupied { None, Run, Combo, JianZong };
enum class Edit { None, Rename, Delay, Text };

// Transition keys that are not widget ids (widget transitions use their hit id).
enum : int {
    AnimDrawer = -1, AnimMenu = -2, AnimOverflow = -3, AnimExt = -4, AnimPower = -6, AnimRunScope = -7,
    AnimDrawerContent = -8, AnimScroll = -9, AnimTabX = -10, AnimTabW = -11, AnimMessage = -12, AnimServiceChip = -13,
    AnimQuickSel = -20, AnimQuickFade = -21, AnimTrayFade = -22,
    AnimHover = 100000, AnimLed = 200000, AnimPress = 300000
};

struct Hit {
    D2D1_RECT_F rect;
    int id;
    std::function<void()> click, right, dbl;
};
struct Capture {
    int id = 0;
    bool hotkey = false;
    std::function<bool(const std::wstring&)> apply;
    explicit operator bool() const { return id != 0; }
};
} // namespace

struct ClientUi::Impl : UiHost {
    HINSTANCE instance;
    Store& store;
    UiCallbacks callbacks;
    HWND main = nullptr, quick = nullptr, nameEdit = nullptr, numberEdit = nullptr, textEdit = nullptr;
    ServicePanel servicePanel;
    ToolboxPanel toolboxPanel;
    // Transitions: the drawer being shown (kept while it slides out), hit offsets and suppression.
    Motion motion;
    Drawer shownDrawer = Drawer::None;
    float hitDx = 0.f, drawerDx = 0.f;
    bool suppressHits = false, overlayHitsOff = false;
    bool framing = false;
    // Quick-switch and tray menu are separate windows with their own clock and fade-in.
    Motion auxMotion, fadeMotion;
    bool auxFraming = false, quickPending = false, trayPending = false;
    // Runs while the quick-switch / tray windows fade in or animate their content.
    void ensureAuxFrames() { if (!auxFraming && main) { SetTimer(main, kAuxTimer, 15, nullptr); auxFraming = true; } }
    void auxTick() {
        fadeMotion.beginFrame();
        const auto fade = [&](HWND window, int key) {
            if (!window || !IsWindowVisible(window)) return;
            const float a = fadeMotion.value(key, 1.f, 150);
            SetLayeredWindowAttributes(window, 0, BYTE(std::lround(a * 255.f)), LWA_ALPHA);
        };
        fade(quick, AnimQuickFade);
        fade(trayMenuWnd, AnimTrayFade);
        if (quickPending && quick && IsWindowVisible(quick)) invalidateQuick();
        if (trayPending && trayMenuWnd && IsWindowVisible(trayMenuWnd)) invalidateTray();
        if (!fadeMotion.pending() && !quickPending && !trayPending) { KillTimer(main, kAuxTimer); auxFraming = false; }
    }
    void startFade(HWND window, int key) {
        if (!window) return;
        SetLayeredWindowAttributes(window, 0, Motion::systemEnabled() ? 0 : 255, LWA_ALPHA);
        fadeMotion.jump(key, 0.f);
        ensureAuxFrames();
    }
    void resetScroll() { scroll = scrollTarget = 0.f; motion.jump(AnimScroll, 0.f); }
    // Generic text field (service drawer): the active field id and its commit action.
    int textId = 0;
    TextCommit textCommit;
    TextChange textChange;
    bool textSubmitted = false; // Set while Enter commits the text field.
    HFONT sansFont = nullptr, monoFont = nullptr;
    HBRUSH editBrush = nullptr;
    Canvas canvas, quickCanvas, trayCanvas;
    HWND trayMenuWnd = nullptr;
    int trayHover = -1;
    float scale = 1.f;
    bool visibleUi = false, running = false, dirty = false, trayAdded = false, animating = false;
    Settings options;
    Profile profile;
    std::vector<std::wstring> names;
    std::vector<unsigned> counts;
    NOTIFYICONDATAW tray{};

    // Interaction state.
    std::vector<Hit> hits;
    int hover = -1, pressed = -1;
    Drawer drawer = Drawer::None;
    bool menu = false, overflow = false, ext = false, confirmDelete = false;
    std::vector<int> hiddenTabs;
    Capture capture;
    int errorId = 0; std::wstring errorText;
    std::wstring message; bool messageError = false, messageHint = false;
    int comboSel = 0;
    float scroll = 0.f, scrollTarget = 0.f, contentHeight = 0.f; // scroll eases toward scrollTarget.
    Edit editing = Edit::None; int editCombo = -1, editStep = -1;
    int quickSel = 0, quickHover = -1, quickScroll = 0;
    DWORD animStart = GetTickCount();

    Impl(HINSTANCE h, Store& s, UiCallbacks cb) : instance(h), store(s), callbacks(std::move(cb)), servicePanel(s, fs::modulePath()), toolboxPanel(s) {}
    ~Impl() {
        if (trayAdded) Shell_NotifyIconW(NIM_DELETE, &tray);
        if (quick && IsWindow(quick)) DestroyWindow(quick);
        if (trayMenuWnd && IsWindow(trayMenuWnd)) DestroyWindow(trayMenuWnd);
        if (main && IsWindow(main)) DestroyWindow(main);
        for (HGDIOBJ o : {HGDIOBJ(sansFont), HGDIOBJ(monoFont), HGDIOBJ(editBrush)}) if (o) DeleteObject(o);
    }
    const Theme& theme() const { return options.theme == L"light" ? lightTheme() : darkTheme(); }
    void invalidate() { if (main) InvalidateRect(main, nullptr, FALSE); }

    // ---------------------------------------------------------------- model
    bool keyOn(const std::wstring& name) const {
        const unsigned id = physical(name);
        return id && std::any_of(profile.keys.begin(), profile.keys.end(), [id](const std::wstring& k) { return physical(k) == id; });
    }
    void setKey(const std::wstring& name, bool on) {
        const unsigned id = physical(name);
        if (!id) return;
        if (on) { if (!keyOn(name)) profile.keys.push_back(name); }
        else profile.keys.erase(std::remove_if(profile.keys.begin(), profile.keys.end(),
            [id](const std::wstring& k) { return physical(k) == id; }), profile.keys.end());
        changed();
    }
    unsigned keyCount(const Profile& p) const {
        std::set<unsigned> ids;
        for (const auto& k : p.keys) if (const unsigned id = physical(k)) ids.insert(id);
        return unsigned(ids.size());
    }
    std::array<std::wstring, 4>& runKeyArray() { return profile.usePresetRunKeys ? profile.runKeys : options.oneKeyRun.keys; }
    std::array<std::wstring, 4> currentRunKeys() const { return resolveRunSettings(profile, options).keys; }
    Occupied occupied(const std::wstring& name) const {
        const unsigned id = physical(name);
        if (!id) return Occupied::None;
        Occupied result = Occupied::None;
        for (const auto& k : currentRunKeys()) if (physical(k) == id) result = Occupied::Run;
        if (profile.combo) for (const auto& c : profile.combos) if (physical(c.trigger) == id) result = Occupied::Combo;
        if (profile.jianZong && physical(profile.jianZongSkillKey) == id) result = Occupied::JianZong;
        return result;
    }
    void changed() { dirty = true; if (main) SetTimer(main, kSaveTimer, kSaveDelayMs, nullptr); invalidate(); }
    bool flush() {
        if (main) KillTimer(main, kServiceSaveTimer);
        servicePanel.flush(*this);
        if (main) KillTimer(main, kSaveTimer);
        if (!dirty) return true;
        try {
            options.lastPreset = profile.name;
            store.save(profile, options);
            dirty = false;
            refreshCount();
            if (callbacks.settingsChanged) callbacks.settingsChanged(profile, options);
            invalidate();
            return true;
        } catch (...) {
            showMessage(L"无法写入配置文件，请检查 config.json 的写入权限。", true);
            return false;
        }
    }
    void saveTheme() {
        try { store.saveSettings(options); } catch (...) { showMessage(L"无法保存主题设置。", true); }
    }
    void refreshNames() {
        names = store.presetNames();
        counts.clear();
        for (const auto& n : names) counts.push_back(lower(n) == lower(profile.name) ? keyCount(profile) : keyCount(store.loadProfile(n)));
    }
    unsigned countFor(size_t i) const {
        if (i < names.size() && names[i] == profile.name) return keyCount(profile);
        return i < counts.size() ? counts[i] : 0;
    }
    void refreshCount() {
        for (size_t i = 0; i < names.size(); ++i) if (names[i] == profile.name && i < counts.size()) counts[i] = keyCount(profile);
    }
    void showMessage(const std::wstring& text, bool isError) {
        if (text != message) motion.jump(AnimMessage, 0.f);
        message = text; messageError = isError; messageHint = false; invalidate();
    }
    // Friendly advisory (not an error): shown in the warm hint colour.
    void showHint(const std::wstring& text) {
        if (text != message) motion.jump(AnimMessage, 0.f);
        message = text; messageError = false; messageHint = true; invalidate();
    }
    static std::wstring timingHint() {
        const auto ms = std::to_wstring(kDefaultFireMs);
        return L"按下/抬起低于 " + ms + L"ms 时，游戏可能来不及识别，出现漏键或技能释放不稳；建议保持 " + ms + L"ms 及以上。";
    }
    static std::wstring guardHint() {
        const auto ms = std::to_wstring(kDefaultGuardMs);
        return L"搓招保护低于 " + ms + L"ms 时，搓招指令容易被误判为奔跑；建议保持 " + ms + L"ms 及以上。";
    }
    void setError(int id, const std::wstring& text) { errorId = id; errorText = text; invalidate(); }
    void clearError() { errorId = 0; errorText.clear(); }

    bool switchProfile(const std::wstring& name) {
        if (!flush()) return false;
        try {
            profile = store.loadProfile(name);
            options.lastPreset = profile.name;
            store.saveSettings(options);
            refreshNames();
        } catch (...) { showMessage(L"无法读取方案，请检查配置文件。", true); return false; }
        comboSel = 0; resetScroll(); menu = overflow = confirmDelete = false; cancelCapture(); clearError();
        if (callbacks.settingsChanged) callbacks.settingsChanged(profile, options);
        invalidate();
        return true;
    }
    std::wstring uniqueName(const std::wstring& base) const {
        std::set<std::wstring> taken; for (const auto& n : names) taken.insert(lower(n));
        std::wstring candidate = base;
        for (unsigned suffix = 2; taken.count(lower(candidate)); ++suffix) candidate = base + L" " + std::to_wstring(suffix);
        return candidate;
    }
    void newProfile() {
        if (!flush()) return;
        try {
            Profile p; p.name = uniqueName(L"新方案");
            store.saveProfile(p);
            if (switchProfile(p.name)) beginRename();
        } catch (...) { showMessage(L"无法新建方案。", true); }
    }
    bool cloneProfile() {
        if (!flush()) return false;
        try {
            const auto name = uniqueName(profile.name + L" 副本");
            store.cloneProfile(profile.name, name);
            return switchProfile(name);
        } catch (...) { showMessage(L"无法复制方案。", true); return false; }
    }
    void deleteProfile() {
        if (names.size() < 2) return;
        if (!confirmDelete) { confirmDelete = true; invalidate(); return; }
        try {
            KillTimer(main, kSaveTimer); dirty = false;
            const auto index = std::find(names.begin(), names.end(), profile.name) - names.begin();
            store.deleteProfile(profile.name);
            names = store.presetNames();
            const auto next = names[std::min<size_t>(size_t(std::max<ptrdiff_t>(0, index - 1)), names.size() - 1)];
            menu = false; confirmDelete = false;
            switchProfile(next);
        } catch (...) { showMessage(L"无法删除方案。", true); }
    }
    bool renameProfile(const std::wstring& raw) {
        const auto name = trim(raw);
        if (name == profile.name) return true;
        if (name.empty() || name.find_first_of(L"[]|\r\n") != std::wstring::npos) {
            showMessage(L"方案名称不能为空，也不能包含 [ ] |。", true); return false;
        }
        for (const auto& n : names) if (lower(n) == lower(name) && lower(n) != lower(profile.name)) {
            showMessage(L"已有同名方案。", true); return false;
        }
        if (!flush()) return false;
        try {
            store.renameProfile(profile.name, name);
            profile.name = name; options.lastPreset = name;
            store.saveSettings(options); refreshNames();
            if (callbacks.settingsChanged) callbacks.settingsChanged(profile, options);
        } catch (...) { showMessage(L"无法重命名方案。", true); return false; }
        invalidate();
        return true;
    }
    void setTiming(unsigned down, unsigned up) {
        profile.downMs = std::max(down, 1u); profile.upMs = std::max(up, 1u); changed();
        if (profile.downMs < kDefaultFireMs || profile.upMs < kDefaultFireMs) showHint(timingHint());
    }
    // Validates and stores the in-game quick-switch hotkey; errors are shown on the field `id`.
    bool applyQuickHotkey(int id, const std::wstring& k) {
        const auto hotkey = parseHotkey(k);
        if (!hotkey) { setError(id, L"请设置快速切换热键"); return false; }
        const auto other = parseHotkey(options.oneKeyRun.toggleHotkey);
        if (other && other.key.physical_id() == hotkey.key.physical_id() && other.modifiers == hotkey.modifiers) {
            setError(id, L"与一键奔跑开关热键相同"); return false;
        }
        if (k != options.quickSwitchHotkey) { options.quickSwitchHotkey = k; changed(); }
        return true;
    }
    void setRunScope(bool perProfile) {
        if (profile.usePresetRunKeys == perProfile) return;
        if (perProfile && std::all_of(profile.runKeys.begin(), profile.runKeys.end(), [](const std::wstring& k) { return k.empty(); }))
            profile.runKeys = options.oneKeyRun.keys;
        profile.usePresetRunKeys = perProfile; clearError(); changed();
    }

    bool start(bool saveFirst) {
        if (saveFirst && !flush()) return false;
        if (callbacks.start && !callbacks.start(profile, options)) { invalidate(); return false; }
        message.clear();
        setRunning(true);
        return true;
    }
    bool stop() {
        if (callbacks.stop && !callbacks.stop()) {
            showMessage(L"停止未完成，请重试；设置尚未应用。", true);
            return false;
        }
        setRunning(false);
        return true;
    }
    void setRunning(bool state) {
        running = state;
        if (trayAdded) {
            wcscpy_s(tray.szTip, state ? L"DAF 连发工具 · 运行中" : L"DAF 连发工具 · 已停止");
            tray.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(state ? 2 : 3));
            if (!tray.hIcon) tray.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
            tray.uFlags = NIF_TIP | NIF_ICON; Shell_NotifyIconW(NIM_MODIFY, &tray);
        }
        updateAnimation(); invalidate(); invalidateQuick(); invalidateTray();
    }
    void toggleRun() {
        options.oneKeyRun.enabled = !options.oneKeyRun.enabled;
        changed(); flush();
        showMessage(options.oneKeyRun.enabled ? L"一键奔跑已开启" : L"一键奔跑已关闭", false);
    }
    void quit() {
        commitEdit();
        flush();
        if (!stop()) return;
        if (callbacks.quit) callbacks.quit(); else DestroyWindow(main);
    }

    // ---------------------------------------------------------------- capture & editing
    void beginCapture(int id, bool hotkey, std::function<bool(const std::wstring&)> apply) {
        commitEdit();
        if (capture.id == id) { cancelCapture(); return; }
        capture = Capture{id, hotkey, std::move(apply)};
        if (errorId && errorId != id) clearError();
        SetFocus(main); updateAnimation(); invalidate();
    }
    void cancelCapture() {
        if (errorId == IdQuickChip) clearError(); // The header has no room for a stale error.
        if (capture) { capture = Capture{}; updateAnimation(); invalidate(); }
    }
    bool captureKey(WPARAM vk, LPARAM lParam) {
        if (!capture) return false;
        if (vk == VK_ESCAPE) { cancelCapture(); return true; }
        const bool extended = (static_cast<ULONG_PTR>(lParam) & (1u << 24)) != 0;
        const auto key = keyFromEvent(static_cast<unsigned>(vk), (static_cast<ULONG_PTR>(lParam) >> 16) & 0xff, extended);
        if (!key) return true;
        std::wstring value = key.name;
        if (capture.hotkey) {
            if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN ||
                vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_LMENU || vk == VK_RMENU) return true;
            unsigned modifiers = 0;
            if (GetKeyState(VK_CONTROL) < 0) modifiers |= MOD_CONTROL;
            if (GetKeyState(VK_MENU) < 0) modifiers |= MOD_ALT;
            if (GetKeyState(VK_SHIFT) < 0) modifiers |= MOD_SHIFT;
            if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) modifiers |= MOD_WIN;
            value = formatHotkey({key, modifiers});
        }
        auto apply = capture.apply;
        if (apply && apply(value)) { clearError(); cancelCapture(); }
        invalidate();
        return true;
    }
    HWND editorFor(Edit kind) const { return kind == Edit::Rename ? nameEdit : kind == Edit::Text ? textEdit : numberEdit; }
    void placeEditor(HWND edit, const D2D1_RECT_F& r, const std::wstring& text) {
        SetWindowPos(edit, HWND_TOP, int(std::lround(r.left * scale)), int(std::lround(r.top * scale)),
            int(std::lround((r.right - r.left) * scale)), int(std::lround((r.bottom - r.top) * scale)), SWP_SHOWWINDOW);
        SetWindowTextW(edit, text.c_str());
        SetFocus(edit); SendMessageW(edit, EM_SETSEL, 0, -1);
    }
    void beginRename() {
        cancelCapture(); commitEdit();
        editing = Edit::Rename; menu = false; confirmDelete = false;
        invalidate(); UpdateWindow(main); // Layout places the editor.
    }
    void beginDelay(int combo, int step) {
        cancelCapture(); commitEdit();
        editing = Edit::Delay; editCombo = combo; editStep = step;
        invalidate(); UpdateWindow(main);
    }
    std::wstring editorText(HWND edit) const {
        const int n = GetWindowTextLengthW(edit);
        std::wstring text(size_t(n) + 1, L'\0'); GetWindowTextW(edit, text.data(), n + 1); text.resize(size_t(n));
        return text;
    }
    void commitEdit() {
        if (editing == Edit::None) return;
        const Edit kind = editing; editing = Edit::None;
        HWND edit = editorFor(kind);
        const auto text = editorText(edit);
        ShowWindow(edit, SW_HIDE);
        if (GetFocus() == edit) SetFocus(main);
        if (kind == Edit::Rename) renameProfile(text);
        else if (kind == Edit::Text) {
            auto commit = std::move(textCommit);
            textCommit = {}; textChange = {}; textId = 0;
            if (commit) commit(text, textSubmitted);
        }
        else if (editCombo >= 0 && size_t(editCombo) < profile.combos.size() &&
                 editStep >= 0 && size_t(editStep) < profile.combos[size_t(editCombo)].steps.size()) {
            unsigned long value = 0;
            for (wchar_t c : text) if (iswdigit(c)) value = std::min<unsigned long>(value * 10 + (c - L'0'), 60000);
            profile.combos[size_t(editCombo)].steps[size_t(editStep)].intervalMs = unsigned(value);
            changed();
        }
        invalidate();
    }
    void cancelEdit() {
        if (editing == Edit::None) return;
        HWND edit = editorFor(editing); editing = Edit::None;
        textId = 0; textCommit = {}; textChange = {};
        ShowWindow(edit, SW_HIDE); SetFocus(main); invalidate();
    }

    // ---------------------------------------------------------------- hit testing
    bool clipHits = false; D2D1_RECT_F hitClip{};
    void hit(const D2D1_RECT_F& r, int id, std::function<void()> click = {}, std::function<void()> right = {}, std::function<void()> dbl = {}) {
        if (suppressHits || overlayHitsOff) return; // Content that is fading or sliding away.
        D2D1_RECT_F area = D2D1::RectF(r.left + hitDx, r.top, r.right + hitDx, r.bottom);
        if (clipHits) {
            area = intersect(area, D2D1::RectF(hitClip.left + hitDx, hitClip.top, hitClip.right + hitDx, hitClip.bottom));
            if (area.right <= area.left || area.bottom <= area.top) return; // Scrolled out of the drawer body.
        }
        hits.push_back({area, id, std::move(click), std::move(right), std::move(dbl)});
    }
    const Hit* hitAt(float x, float y) const {
        for (auto it = hits.rbegin(); it != hits.rend(); ++it) if (contains(it->rect, x, y)) return &*it;
        return nullptr;
    }
    bool hovered(int id) const { return hover == id; }
    bool isPressed(int id) const { return pressed == id && hover == id; }
    void openDrawer(Drawer d) {
        commitEdit(); cancelCapture(); clearError();
        servicePanel.closeOverlays(*this);
        if (d != Drawer::None && d != drawer) {
            resetScroll();
            // Switching drawers while one is open cross-fades the content instead of cutting.
            if (drawer != Drawer::None) motion.jump(AnimDrawerContent, 0.f);
        }
        drawer = d; menu = overflow = ext = confirmDelete = false; invalidate();
        if (d != Drawer::None) shownDrawer = d;
        if (d == Drawer::Toolbox) toolboxPanel.opened(*this);
    }

    // ---------------------------------------------------------------- widgets
    void iconButton(int id, float x, float y, Icon icon, std::function<void()> click, float size = 18.f, float side = 32.f) {
        const auto& t = theme();
        const auto r = box(x, y, side, side);
        const float h = motion.value(AnimHover + id, hovered(id) ? 1.f : 0.f, 120);
        Color bg = t.surface3; bg.a *= h;
        canvas.fillRound(r, 8, bg);
        canvas.icon(icon, x + (side - size) / 2, y + (side - size) / 2, size, mix(t.text2, t.text, h));
        hit(r, id, std::move(click));
    }
    void toggleSwitch(int id, float x, float y, bool on, std::function<void()> click) {
        const auto& t = theme();
        const auto r = box(x, y, 36, 20);
        // The knob slides and the track fades between off and on.
        const float k = motion.value(id, on ? 1.f : 0.f, 170);
        canvas.fillRound(r, 10, mix(t.surface3, t.accent, k));
        Color edge = t.line2; edge.a *= 1.f - k;
        canvas.insetRing(r, 10, edge, 1);
        canvas.fillCircle(lerp(x + 10, x + 26, k), y + 10, 8, mix(t.text3, t.onAccent, k));
        hit(r, id, std::move(click));
    }
    void stepper(int idDec, int idInc, float x, float y, float w, const std::wstring& value, std::function<void()> dec, std::function<void()> inc, bool warn = false) {
        const auto& t = theme();
        const auto r = box(x, y, w, 32);
        canvas.fillRound(r, 8, t.surface2); canvas.insetRing(r, 8, t.line, 1);
        const auto minus = box(x + 1, y + 1, 30, 30), plus = box(x + w - 31, y + 1, 30, 30);
        if (hovered(idDec)) canvas.fillRound(minus, 7, t.surface3);
        if (hovered(idInc)) canvas.fillRound(plus, 7, t.surface3);
        canvas.line(x + 11.5f, y + 16, x + 19.5f, y + 16, hovered(idDec) ? t.text : t.text2, 1.4f);
        canvas.line(x + w - 20, y + 16, x + w - 12, y + 16, hovered(idInc) ? t.text : t.text2, 1.4f);
        canvas.line(x + w - 16, y + 12, x + w - 16, y + 20, hovered(idInc) ? t.text : t.text2, 1.4f);
        canvas.text(value, mono(13), x + w / 2, y + 16, warn ? t.combo : t.text, Align::Center);
        hit(minus, idDec, std::move(dec)); hit(plus, idInc, std::move(inc));
    }
    float kbd(float x, float cy, const std::wstring& label) {
        const auto& t = theme();
        const float w = canvas.textWidth(label, mono(11)) + 14;
        const auto r = box(x, cy - 11, w, 22);
        canvas.keyShape(r, 5, t.capTop, t.capSide, 2); canvas.ring(r, 5, t.line2, 1);
        canvas.text(label, mono(11), x + 7, cy - 1, t.text);
        return w;
    }
    float mcap(float x, float cy, const std::wstring& label, Color ring, float ringWidth, float fixedWidth = 0, float maxRight = 0) {
        const auto& t = theme();
        float w = fixedWidth > 0 ? fixedWidth : std::max(28.f, canvas.textWidth(label, mono(11.5f)) + 14);
        if (maxRight > 0 && x + w > maxRight) return w;
        const auto r = box(x, cy - 13, w, 26);
        canvas.keyShape(r, 6, t.capTop, t.capSide, 2); canvas.ring(r, 6, ring, ringWidth);
        canvas.text(label, mono(11.5f), x + w / 2, cy - 1, t.text, Align::Center, w - 4);
        return w;
    }
    float link(int id, float right, float cy, const std::wstring& label, std::function<void()> click, bool alignRight = true) {
        const auto& t = theme();
        const Font f = sans(12.5f);
        const float w = canvas.textWidth(label, f) + 2 + 15;
        const float x = alignRight ? right - w : right;
        const Color c = hovered(id) ? t.text : t.text2;
        canvas.text(label, f, x, cy, c);
        canvas.icon(Icon::ChevronRight, x + w - 15, cy - 7.5f, 15, c);
        hit(box(x, cy - 10, w, 20), id, std::move(click));
        return w;
    }
    // A keycap-shaped binding field: click, then press a key; right click clears.
    float captureField(int id, float x, float y, float minWidth, const std::wstring& display, bool small,
                       std::function<bool(const std::wstring&)> apply, bool hotkey = false,
                       Color ring = {}, float ringWidth = 1.f, const std::wstring& emptyText = L"未设置") {
        const auto& t = theme();
        const bool listening = capture.id == id, bad = errorId == id;
        const std::wstring shown = listening ? L"按下按键…" : (display.empty() ? emptyText : display);
        const Font f = mono(small ? 12.f : 13.f);
        const float h = small ? 28.f : 36.f, pad = small ? 10.f : 12.f;
        const float w = std::max(minWidth, canvas.textWidth(shown, f) + pad * 2);
        const auto r = box(x, y, w, h);
        canvas.keyShape(r, 8, hovered(id) ? t.capTopH : t.capTop, t.capSide, 3);
        if (listening) {
            const float phase = float((GetTickCount() - animStart) % 1000) / 1000.f;
            const float k = 0.5f + 0.5f * std::cos(phase * 6.2831853f);
            canvas.ring(r, 8, mix(t.accentSoft, t.accent, k), 2);
        } else if (bad) canvas.ring(r, 8, t.ledBad, 2);
        else canvas.ring(r, 8, ring.a > 0 ? ring : t.line2, ring.a > 0 ? ringWidth : 1.f);
        const Color c = listening ? t.accentText : (display.empty() ? t.text3 : t.text);
        canvas.text(shown, f, x + w / 2, y + (h - 3) / 2 + 0.5f, c, Align::Center);
        auto applyCopy = apply;
        hit(r, id, [this, id, hotkey, apply] { beginCapture(id, hotkey, apply); },
            [this, applyCopy] { cancelCapture(); if (applyCopy) applyCopy(L""); invalidate(); });
        return w;
    }
    void led(float cx, float cy, bool on, bool bad, bool breathe, int key = 0) {
        const auto& t = theme();
        // The LED lights up and dims gradually, like a real indicator.
        const float k = key ? motion.value(AnimLed + key, (on || bad) ? 1.f : 0.f, 220) : ((on || bad) ? 1.f : 0.f);
        if (k < 1.f) canvas.fillCircle(cx, cy, 2.5f, t.ledOff);
        if (k <= 0.001f) return;
        const float saved = canvas.opacity();
        canvas.setOpacity(saved * k);
        float blur = 8.f;
        if (breathe) {
            const float phase = float((GetTickCount() - animStart) % 1200) / 1200.f;
            blur = 2.f + 6.f * (0.5f + 0.5f * std::cos(phase * 6.2831853f));
        }
        canvas.glow(cx, cy, 3.5f, 3.5f + blur, bad ? t.ledBadGlow : t.ledGlow);
        canvas.fillCircle(cx, cy, 4.f, bad ? rgb(0xFF5D5D, .25f) : t.ledRing);
        canvas.fillCircle(cx, cy, 2.5f, bad ? t.ledBad : t.led);
        canvas.setOpacity(saved);
    }
    void keycap(const BoardKey& k, float bx, float by, int id, bool breathe) {
        const auto& t = theme();
        const bool on = keyOn(k.id);
        const Occupied occ = occupied(k.id);
        // Key travel and hover light follow the pointer smoothly.
        const float oy = motion.value(AnimPress + id, isPressed(id) ? 1.5f : 0.f, 70);
        const float hk = motion.value(AnimHover + id, hovered(id) ? 1.f : 0.f, 110);
        const auto r = box(bx + k.x, by + k.y + oy, k.w, k.h);
        canvas.fillRound(r, 7, k.mod ? t.modSide : t.capSide);
        const auto face = D2D1::RectF(r.left + 3, r.top + 2, r.right - 3, r.bottom - 5);
        canvas.fillRound(face, 5, k.mod ? mix(t.modTop, t.modTopH, hk) : mix(t.capTop, t.capTopH, hk));
        if (!k.label.empty()) {
            if (k.mod) canvas.text(k.label, mono(10.5f), face.left + 7, face.bottom - 6 - 5.25f, on ? t.capLegend : t.modLegend);
            else canvas.text(k.label, mono(13), face.left + 7, face.top + 5 + 6.5f, t.capLegend);
        }
        led(face.right - 8.5f, face.top + 8.5f, on, on && occ != Occupied::None, breathe, id);
        if (occ != Occupied::None) {
            const Color c = occ == Occupied::Run ? t.run : occ == Occupied::Combo ? t.combo : t.cls;
            canvas.fillRound(box(face.right - 18, face.bottom - 9, 12, 3), 1.5f, c);
        }
        hit(D2D1::RectF(r.left, r.top - oy, r.right, r.bottom), id, [this, k, on, occ] {
            if (occ != Occupied::None && !on) return; // Reserved by a feature; the footer explains.
            setKey(k.id, !on);
        });
    }

    // ---------------------------------------------------------------- painting
    void paint() {
        const auto& t = theme();
        hits.clear();
        motion.beginFrame();
        if (!canvas.begin(t.bg)) return;
        paintHeader(); paintProfiles(); paintKeyboard(); paintCards(); paintFooter();
        // Popovers fade in with a slight drop and fade out without taking clicks.
        const auto popover = [&](int key, bool open, void (Impl::*paintFn)()) {
            const float a = motion.value(key, open ? 1.f : 0.f, 140);
            if (a <= 0.001f) return;
            suppressHits = !open;
            canvas.setLayer(a, 0.f, (a - 1.f) * 6.f);
            (this->*paintFn)();
            canvas.resetLayer();
            suppressHits = false;
        };
        popover(AnimExt, ext, &Impl::paintExt);
        popover(AnimMenu, menu, &Impl::paintMenu);
        popover(AnimOverflow, overflow, &Impl::paintOverflow);
        const float d = motion.value(AnimDrawer, drawer != Drawer::None ? 1.f : 0.f, 260);
        if (d <= 0.001f && drawer == Drawer::None) shownDrawer = Drawer::None;
        if (shownDrawer != Drawer::None) paintDrawer(d);
        canvas.end();
        // Keep producing frames only while something is moving.
        if (motion.pending() && !framing) { SetTimer(main, kFrameTimer, 15, nullptr); framing = true; }
        else if (!motion.pending() && framing) { KillTimer(main, kFrameTimer); framing = false; }
        if (editing != Edit::None && drawer != Drawer::Combo && editing == Edit::Delay) cancelEdit();
        if (editing == Edit::Text && drawer != Drawer::Service) cancelEdit();
    }
    void paintHeader() {
        const auto& t = theme();
        canvas.fill(box(0, 0, kW, 44), t.bar); canvas.fill(box(0, 43, kW, 1), t.line);
        const auto logo = box(20, 11, 22, 22);
        canvas.keyShape(logo, 6, t.capTop, t.capSide, 3); canvas.ring(logo, 6, t.line2, 1);
        canvas.glow(36, 17, 2, 8, t.ledGlow); canvas.fillCircle(36, 17, 2, t.led);
        Font word = mono(13, 700); word.tracking = 1.3f;
        float x = 52;
        canvas.text(L"DAF", word, x, 22, t.text); x += canvas.textWidth(L"DAF", word) + 10;
        canvas.text(L"连发工具", sans(13), x, 22, t.text2); x += canvas.textWidth(L"连发工具", sans(13)) + 10;
        canvas.text(kVersion, mono(11, 400), x, 22, t.text3);
        const bool dark = options.theme != L"light";
        iconButton(IdTheme, 1100, 6, dark ? Icon::Sun : Icon::Moon, [this, dark] {
            options.theme = dark ? L"light" : L"dark"; saveTheme(); applyEditColors(); invalidate(); invalidateQuick(); invalidateTray();
        });
        // The service drawer opens from the status chip beside the power switch (paintProfiles).
        iconButton(IdToolbox, 1058, 6, Icon::Wrench, [this] { openDrawer(drawer == Drawer::Toolbox ? Drawer::None : Drawer::Toolbox); });
        iconButton(IdSettings, 1142, 6, Icon::Gear, [this] { openDrawer(drawer == Drawer::Settings ? Drawer::None : Drawer::Settings); });
        canvas.fill(box(1188, 13, 1, 18), t.line2);
        iconButton(IdMin, 1198, 6, Icon::Minus, [this] { ShowWindow(main, SW_MINIMIZE); }, 15);
        iconButton(IdClose, 1240, 6, Icon::Close, [this] { commitEdit(); flush(); ShowWindow(main, SW_HIDE); updateAnimation(); }, 15);
    }
    float tabWidth(size_t i) {
        const bool active = names[i] == profile.name;
        if (active && editing == Edit::Rename) return 148;
        const float nameW = canvas.textWidth(names[i], sans(13, active ? 500 : 400));
        const float badgeW = canvas.textWidth(std::to_wstring(countFor(i)), mono(11)) + 12;
        return 12 + nameW + 8 + badgeW + 12 + (active ? 32 : 0);
    }
    void paintProfiles() {
        const auto& t = theme();
        const float cy = 78;
        // Power switch, right aligned.
        const std::wstring powerLabel = running ? L"运行中" : L"启动连发";
        // The power switch blends between off and on: width, fill, ring, knob and label.
        const float k = motion.value(AnimPower, running ? 1.f : 0.f, 220);
        const float offW = 6 + 28 + 10 + canvas.textWidth(L"启动连发", sans(14, 600)) + 20;
        const float onW = 6 + 28 + 10 + canvas.textWidth(L"运行中", sans(14, 600)) + 20;
        const float pw = lerp(offW, onW, k);
        const auto pr = box(1240 - pw, 58, pw, 40);
        Color halo = t.accentSoft; halo.a *= k;
        canvas.ring(pr, 20, halo, 4);
        canvas.fillRound(pr, 20, mix(t.surface2, t.accent, k));
        Color edge = hovered(IdPower) ? t.text3 : t.line2; edge.a *= 1.f - k;
        canvas.insetRing(pr, 20, edge, 1);
        canvas.fillCircle(pr.left + 20, cy, 14, mix(t.surface3, t.onAccent, k));
        canvas.icon(Icon::Power, pr.left + 12.5f, cy - 7.5f, 15, mix(t.text2, t.accent, k));
        canvas.pushClip(pr);
        canvas.text(powerLabel, sans(14, 600), pr.left + 44, cy, mix(t.text, t.onAccent, k));
        canvas.popClip();
        hit(pr, IdPower, [this] { if (running) stop(); else start(true); });
        const auto sr = paintServiceChip(pr.left - 12, cy);

        // Tabs with overflow.
        // In-game quick-switch hotkey: the keycap itself is a capture field (click, press a combination).
        const bool quickListening = capture.id == IdQuickChip, quickBad = errorId == IdQuickChip;
        const std::wstring quickLabel = quickListening ? L"按下组合键…" : hotkeyLabel(options.quickSwitchHotkey);
        const std::wstring quickNote = quickBad ? errorText : (quickListening ? L"Esc 取消" : L"游戏内切换");
        const float groupW = 16 + canvas.textWidth(quickLabel, mono(11)) + 14 + 8 + canvas.textWidth(quickNote, sans(12));
        const float limit = sr.left - 16 - groupW - 1 - 14 - 32 - 6 - 40;
        std::vector<float> widths; float total = 0; size_t active = 0;
        for (size_t i = 0; i < names.size(); ++i) { widths.push_back(tabWidth(i)); total += widths.back() + 6; if (names[i] == profile.name) active = i; }
        std::vector<size_t> shown; hiddenTabs.clear();
        if (total <= limit) for (size_t i = 0; i < names.size(); ++i) shown.push_back(i);
        else {
            float budget = limit - 56 - widths[active] - 6;
            for (size_t i = 0; i < names.size(); ++i) {
                if (i == active) { shown.push_back(i); continue; }
                if (widths[i] + 6 <= budget) { shown.push_back(i); budget -= widths[i] + 6; }
                else hiddenTabs.push_back(int(i));
            }
        }
        float x = 40;
        {
            // The active-tab background glides to the newly selected profile.
            float px = 40, tx = 40, tw = 0;
            for (size_t i : shown) { if (i == active) { tx = px; tw = widths[i]; } px += widths[i] + 6; }
            const float ax = motion.value(AnimTabX, tx, 220), aw = motion.value(AnimTabW, tw, 220);
            if (aw > 0) { const auto pill = box(ax, 60, aw, 36); canvas.fillRound(pill, 10, t.surface2); canvas.insetRing(pill, 10, t.line, 1); }
        }
        for (size_t i : shown) {
            const bool isActive = i == active;
            const auto r = box(x, 60, widths[i], 36);
            const int id = IdTab + int(i);
            if (isActive && editing == Edit::Rename) {
                const auto field = box(x + 4, 63, 140, 30);
                canvas.fillRound(field, 7, t.surface); canvas.insetRing(field, 7, t.accent, 1);
                if (!IsWindowVisible(nameEdit)) placeEditor(nameEdit, D2D1::RectF(field.left + 8, cy - 10, field.right - 8, cy + 10), profile.name);
            } else {
                const Font f = sans(13, isActive ? 500 : 400);
                canvas.text(names[i], f, x + 12, cy, isActive || hovered(id) ? t.text : t.text2);
                const float nameW = canvas.textWidth(names[i], f);
                const auto count = std::to_wstring(countFor(i));
                const auto badge = box(x + 12 + nameW + 8, cy - 9, canvas.textWidth(count, mono(11)) + 12, 18);
                canvas.fillRound(badge, 5, isActive ? t.accentSoft : t.surface3);
                canvas.text(count, mono(11), (badge.left + badge.right) / 2, cy, isActive ? t.accentText : t.text3, Align::Center);
                const std::wstring name = names[i];
                hit(box(x, 60, widths[i] - (isActive ? 32 : 0), 36), id,
                    [this, name, isActive] { if (!isActive) switchProfile(name); },
                    {}, [this, isActive] { if (isActive) beginRename(); });
                if (isActive) iconButton(IdMore, r.right - 32, 64, Icon::More, [this] { menu = !menu; confirmDelete = false; ext = overflow = false; invalidate(); }, 15, 28);
            }
            x += widths[i] + 6;
        }
        if (!hiddenTabs.empty()) {
            const std::wstring label = L"+" + std::to_wstring(hiddenTabs.size());
            const float w = canvas.textWidth(label, mono(12)) + 20;
            const auto r = box(x, 64, w, 28);
            if (hovered(IdOverflow) || overflow) canvas.fillRound(r, 7, t.surface2);
            canvas.insetRing(r, 7, t.line2, 1);
            canvas.text(label, mono(12), x + w / 2, cy, t.text2, Align::Center);
            hit(r, IdOverflow, [this] { overflow = !overflow; menu = ext = false; invalidate(); });
            overflowAnchor = r;
            x += w + 6;
        }
        iconButton(IdNew, x, 62, Icon::Plus, [this] { newProfile(); });
        x += 32 + 14;
        canvas.fill(box(x, 67, 1, 22), t.line);
        x += 17;
        {
            const float w = canvas.textWidth(quickLabel, mono(11)) + 14;
            const auto r = box(x, cy - 11, w, 22);
            const bool hot = hovered(IdQuickChip);
            canvas.keyShape(r, 5, hot || quickListening ? t.capTopH : t.capTop, t.capSide, 2);
            if (quickListening) {
                const float phase = float((GetTickCount() - animStart) % 1000) / 1000.f;
                canvas.ring(r, 5, mix(t.accentSoft, t.accent, 0.5f + 0.5f * std::cos(phase * 6.2831853f)), 2);
            } else canvas.ring(r, 5, quickBad ? t.ledBad : (hot ? t.text3 : t.line2), quickBad ? 2.f : 1.f);
            canvas.text(quickLabel, mono(11), x + 7, cy - 1, quickListening ? t.accentText : t.text);
            const float noteW = canvas.textWidth(quickNote, sans(12));
            canvas.text(quickNote, sans(12), x + w + 8, cy, quickBad ? t.danger : (hot && !quickListening ? t.text2 : t.text3));
            // Chip and caption form one generous click target.
            hit(box(x - 2, 62, w + 8 + noteW + 4, 32), IdQuickChip, [this] {
                beginCapture(IdQuickChip, true, [this](const std::wstring& k) { return applyQuickHotkey(IdQuickChip, k); });
            });
        }
        activeTabRight = 40;
        float ax = 40;
        for (size_t i : shown) { if (i == active) activeTabRight = ax + widths[i]; ax += widths[i] + 6; }
    }
    float activeTabRight = 0; D2D1_RECT_F overflowAnchor{};
    // Background service state as a friendly pill ending at right; a click opens the service drawer.
    D2D1_RECT_F paintServiceChip(float right, float cy) {
        const auto& t = theme();
        const auto svc = servicePanel.summary();
        const bool busy = svc.tone == ServiceTone::Busy;
        const Font keyFont = sans(12.5f), stateFont = sans(13, 600);
        const std::wstring key = L"后台服务";
        const float keyW = canvas.textWidth(key, keyFont), stateW = canvas.textWidth(svc.state, stateFont);
        const float target = 12 + (busy ? 26.f : 8.f) + 8 + keyW + 6 + stateW + 2 + 15 + 8;
        const float w = motion.value(AnimServiceChip, target, 220); // Glides when the wording changes.
        const auto r = box(right - w, cy - 16, w, 32);
        const float h = motion.value(AnimHover + IdService, hovered(IdService) || drawer == Drawer::Service ? 1.f : 0.f, 120);
        Color bg = t.surface2; bg.a *= h;
        canvas.fillRound(r, 16, bg);
        canvas.insetRing(r, 16, mix(t.line, t.line2, h), 1);
        canvas.pushClip(r);
        float x = r.left + 12;
        if (busy) { uiSpinner(x - 3, cy); x += 26; }
        else {
            // Green running, yellow needs updating, red not running.
            const Color dot = ServicePanel::toneColor(t, svc.tone);
            Color glow = dot; glow.a *= .6f;
            canvas.glow(x + 4, cy, 3.f, 10.f, glow);
            canvas.fillCircle(x + 4, cy, 3.5f, dot);
            x += 8;
        }
        x += 8;
        canvas.text(key, keyFont, x, cy, mix(t.text3, t.text2, h));
        x += keyW + 6;
        canvas.text(svc.state, stateFont, x, cy, t.text);
        x += stateW + 2;
        canvas.icon(Icon::ChevronRight, x, cy - 7.5f, 15, mix(t.text3, t.text, h));
        canvas.popClip();
        hit(r, IdService, [this] { openDrawer(drawer == Drawer::Service ? Drawer::None : Drawer::Service); });
        return r;
    }
    void paintMenu() {
        const auto& t = theme();
        const float w = 168, h = 6 + 34 + 34 + 9 + 34 + 6;
        const auto r = box(activeTabRight - w, 102, w, h);
        canvas.shadow(r, 10, t.shadow, 24, 64); canvas.fillRound(r, 10, t.surface); canvas.insetRing(r, 10, t.line2, 1);
        hit(r, IdMenuPanel, [] {});
        const auto item = [&](int id, float y, Icon icon, const std::wstring& label, Color color, bool enabled, std::function<void()> click) {
            const auto ir = box(r.left + 6, y, w - 12, 34);
            Color bg = t.surface2; bg.a *= motion.value(AnimHover + id, hovered(id) && enabled ? 1.f : 0.f, 110);
            canvas.fillRound(ir, 7, bg);
            canvas.setOpacity(enabled ? 1.f : .4f);
            canvas.icon(icon, ir.left + 10, y + 9.5f, 15, color);
            canvas.text(label, sans(13), ir.left + 35, y + 17, color);
            canvas.setOpacity(1.f);
            if (enabled) hit(ir, id, std::move(click));
        };
        item(IdMenuRename, r.top + 6, Icon::Edit, L"重命名", t.text, true, [this] { beginRename(); });
        item(IdMenuClone, r.top + 40, Icon::Copy, L"复制方案", t.text, true, [this] { menu = false; cloneProfile(); });
        canvas.fill(box(r.left + 8, r.top + 78, w - 16, 1), t.line);
        item(IdMenuDelete, r.top + 83, Icon::Trash, confirmDelete ? L"再次点击确认删除" : L"删除方案", t.danger, names.size() > 1, [this] { deleteProfile(); });
    }
    void paintOverflow() {
        const auto& t = theme();
        const float w = 200, h = 12 + 34.f * hiddenTabs.size();
        const auto r = box(overflowAnchor.left, 102, w, h);
        canvas.shadow(r, 10, t.shadow, 24, 64); canvas.fillRound(r, 10, t.surface); canvas.insetRing(r, 10, t.line2, 1);
        hit(r, IdMenuPanel, [] {});
        float y = r.top + 6;
        for (int index : hiddenTabs) {
            const int id = IdOverflowItem + index;
            const auto ir = box(r.left + 6, y, w - 12, 34);
            Color bg = t.surface2; bg.a *= motion.value(AnimHover + id, hovered(id) ? 1.f : 0.f, 110);
            canvas.fillRound(ir, 7, bg);
            canvas.text(names[size_t(index)], sans(13), ir.left + 10, y + 17, t.text, Align::Left, w - 70);
            canvas.text(std::to_wstring(countFor(size_t(index))), mono(11), ir.right - 10, y + 17, t.text3, Align::Right);
            const auto name = names[size_t(index)];
            hit(ir, id, [this, name] { overflow = false; switchProfile(name); });
            y += 34;
        }
    }
    void paintKeyboard() {
        const auto& t = theme();
        const auto caseRect = box(40, 112, 1200, 380);
        canvas.fillRound(caseRect, 18, t.caseBg); canvas.insetRing(caseRect, 18, t.caseLine, 1);
        const float cy = 138;
        Font etch = mono(10.5f, 600); etch.tracking = 2.52f;
        canvas.text(L"DAF · 104", etch, 84, cy, t.caseEtch);
        // Right-aligned tools: count, extension keys, clear.
        float right = 1196;
        const unsigned count = keyCount(profile);
        const auto chip = [&](int id, float w, bool selected) {
            const auto r = box(right - w, cy - 14, w, 28);
            if (hovered(id) || selected) canvas.fillRound(r, 7, t.surface2);
            canvas.insetRing(r, 7, t.caseLine, 1);
            return r;
        };
        if (count) {
            const float w = 10 + 15 + 6 + canvas.textWidth(L"清空", sans(12)) + 10;
            const auto r = chip(IdClear, w, false);
            const Color c = hovered(IdClear) ? t.text : t.text2;
            canvas.icon(Icon::TrashSmall, r.left + 10, cy - 7.5f, 15, c);
            canvas.text(L"清空", sans(12), r.left + 31, cy, c);
            hit(r, IdClear, [this] { profile.keys.clear(); changed(); });
            right -= w + 10;
        }
        unsigned extCount = 0;
        for (const auto& k : extKeys()) if (keyOn(k)) ++extCount;
        {
            const std::wstring label = L"F13–F24";
            const std::wstring badge = std::to_wstring(extCount);
            const float badgeW = extCount ? canvas.textWidth(badge, mono(11)) + 12 + 6 : 0;
            const float w = 10 + canvas.textWidth(label, sans(12)) + badgeW + 10;
            const auto r = chip(IdExt, w, ext);
            const Color c = hovered(IdExt) || ext ? t.text : t.text2;
            canvas.text(label, sans(12), r.left + 10, cy, c);
            if (extCount) {
                const auto b = box(r.right - 10 - badgeW + 6, cy - 9, badgeW - 6, 18);
                canvas.fillRound(b, 5, t.accentSoft);
                canvas.text(badge, mono(11), (b.left + b.right) / 2, cy, t.accentText, Align::Center);
            }
            hit(r, IdExt, [this] { ext = !ext; menu = overflow = false; invalidate(); });
            right -= w + 10 + 6;
        }
        {
            const Font label = sans(12), number = mono(15, 600);
            const float baseline = cy + canvas.baselineOffset(number);
            const float lw = canvas.textWidth(L"键连发", label);
            canvas.text(L"键连发", label, right - lw, baseline - canvas.baselineOffset(label), t.text2);
            const std::wstring n = std::to_wstring(count);
            canvas.text(n, number, right - lw - 6, cy, t.text, Align::Right);
        }
        // Key board.
        const float bx = 84, by = 163;
        const bool breathe = running;
        const auto& keys = boardKeys();
        for (size_t i = 0; i < keys.size(); ++i) keycap(keys[i], bx, by, IdKey + int(i), breathe);
        // Status LEDs in the numpad's indicator strip.
        const auto strip = box(bx + 921, by, 191, 44);
        canvas.insetRing(strip, 8, t.caseLine, 1);
        const std::pair<const wchar_t*, bool> leds[] = {{L"连发", running}, {L"奔跑", options.oneKeyRun.enabled}, {L"连招", profile.combo}};
        float sum = 0; float widths[3];
        for (int i = 0; i < 3; ++i) { widths[i] = 12 + canvas.textWidth(leds[i].first, sans(10.5f, 500)); sum += widths[i]; }
        const float space = (191 - sum) / 3;
        float lx = strip.left + space / 2;
        for (int i = 0; i < 3; ++i) {
            if (leds[i].second) { canvas.glow(lx + 3, by + 22, 3, 9, t.ledGlow); canvas.fillCircle(lx + 3, by + 22, 3, t.led); }
            else if (i == 0) { canvas.glow(lx + 3, by + 22, 3, 9, t.ledBadGlow); canvas.fillCircle(lx + 3, by + 22, 3, t.ledBad); } // Auto-fire stopped.
            else canvas.fillCircle(lx + 3, by + 22, 3, t.ledOff);
            canvas.text(leds[i].first, sans(10.5f, 500), lx + 12, by + 22, leds[i].second ? t.text2 : t.caseEtch);
            lx += widths[i] + space;
        }
    }
    void paintExt() {
        const auto& t = theme();
        const auto r = box(856, 157, 340, 151);
        canvas.shadow(r, 12, t.shadow, 24, 64); canvas.fillRound(r, 12, t.surface); canvas.insetRing(r, 12, t.line2, 1);
        hit(r, IdExtPanel, [] {});
        canvas.text(L"扩展功能键", sans(12), r.left + 14, r.top + 22.5f, t.text2);
        const float colW = (312 - 30) / 6.f;
        for (size_t i = 0; i < extKeys().size(); ++i) {
            BoardKey k{extKeys()[i], extKeys()[i], (colW + 6) * float(i % 6), 50.f * float(i / 6), colW, 44, true};
            keycap(k, r.left + 14, r.top + 43, IdExtKey + int(i), running);
        }
    }
    void card(float x) {
        const auto& t = theme();
        const auto r = box(x, 508, 288, 220);
        canvas.fillRound(r, 14, t.surface); canvas.insetRing(r, 14, t.line, 1);
    }
    void cardTitle(float cx, Color marker, const std::wstring& title) {
        const auto& t = theme();
        canvas.fillRound(box(cx, 533, 8, 8), 2, marker);
        canvas.text(title, sans(14, 600), cx + 16, 537, t.text);
    }
    void paintCards() {
        const auto& t = theme();
        // 1. Timing.
        {
            const float x = 40, cx = x + 19;
            card(x); cardTitle(cx, t.accent, L"连发时序");
            if (profile.downMs != kDefaultFireMs || profile.upMs != kDefaultFireMs) {
                const std::wstring reset = L"恢复 " + std::to_wstring(kDefaultFireMs) + L" + " + std::to_wstring(kDefaultFireMs);
                const float w = canvas.textWidth(reset, sans(12.5f));
                canvas.text(reset, sans(12.5f), cx + 250 - w, 537, hovered(IdResetTiming) ? t.text : t.text2);
                hit(box(cx + 250 - w, 527, w, 20), IdResetTiming, [this] { setTiming(kDefaultFireMs, kDefaultFireMs); });
            }
            const double periodMs = double(profile.downMs) + double(profile.upMs);
            wchar_t hz[32]{}; swprintf_s(hz, L"%.1f", 1000.0 / periodMs);
            Font big = mono(32, 600); big.tracking = -0.64f;
            canvas.text(hz, big, cx, 577, t.text);
            const float baseline = 577 + canvas.baselineOffset(big);
            canvas.text(L"次 / 秒 · 每键", sans(12), cx + canvas.textWidth(hz, big) + 6, baseline - canvas.baselineOffset(sans(12)), t.text2);
            // Square wave: one high segment per press, drawn over three cycles.
            const float cyc = 250.f / 3.f, hi = cyc * float(double(profile.downMs) / periodMs);
            std::vector<D2D1_POINT_2F> pts{{cx, 605 + 30.5f}};
            for (int i = 0; i < 3; ++i) {
                const float px = cx + cyc * i;
                pts.push_back({px, 609}); pts.push_back({px + hi, 609});
                pts.push_back({px + hi, 635.5f}); pts.push_back({px + cyc, 635.5f});
            }
            canvas.line(cx, 635.5f, cx + 250, 635.5f, t.line2, 1, true);
            canvas.polyline(pts.data(), pts.size(), t.accent, 1.6f);
            // Below the default is allowed but flagged: a warm label and value, plus a footer hint on change.
            const bool fastDown = profile.downMs < kDefaultFireMs, fastUp = profile.upMs < kDefaultFireMs;
            canvas.text(fastDown ? L"按下 ms · 偏快" : L"按下 ms", sans(11.5f), cx, 659.5f, fastDown ? t.combo : t.text2);
            canvas.text(fastUp ? L"抬起 ms · 偏快" : L"抬起 ms", sans(11.5f), cx + 130, 659.5f, fastUp ? t.combo : t.text2);
            stepper(IdDownDec, IdDownInc, cx, 673, 120, std::to_wstring(profile.downMs),
                [this] { setTiming(profile.downMs - (profile.downMs > 1 ? 1 : 0), profile.upMs); },
                [this] { setTiming(increaseTiming(profile.downMs), profile.upMs); }, fastDown);
            stepper(IdUpDec, IdUpInc, cx + 130, 673, 120, std::to_wstring(profile.upMs),
                [this] { setTiming(profile.downMs, profile.upMs - (profile.upMs > 1 ? 1 : 0)); },
                [this] { setTiming(profile.downMs, increaseTiming(profile.upMs)); }, fastUp);
        }
        // 2. Class aids.
        {
            const float x = 344, cx = x + 19;
            card(x); cardTitle(cx, t.cls, L"职业辅助");
            link(IdClassLink, cx + 250, 537, L"设置", [this] { openDrawer(Drawer::Class); });
            const auto row = [&](int rowId, int swId, float y, const std::wstring& name, const std::wstring& sub, bool on, std::function<void()> toggle) {
                const auto r = box(cx, y, 250 - 46, 40);
                canvas.text(name, sans(13, 500), cx, y + 10.7f, hovered(rowId) ? t.accentText : t.text);
                canvas.text(sub, sans(11.5f), cx, y + 29.8f, t.text2, Align::Left, 250 - 46);
                hit(r, rowId, [this] { openDrawer(Drawer::Class); });
                toggleSwitch(swId, cx + 250 - 36, y + 10, on, std::move(toggle));
            };
            const auto keyOrUnset = [](const std::wstring& k) { return k.empty() ? std::wstring(L"未设置") : labelOf(k); };
            row(IdLvRow, IdLvSw, 561, L"旅人 · 自动流星", L"发射 " + keyOrUnset(profile.lvRenShotKey) + L" · " + std::to_wstring(profile.lvRenSkillKeys.size()) + L" 个技能键",
                profile.lvRen, [this] { profile.lvRen = !profile.lvRen; changed(); });
            row(IdZfRow, IdZfSw, 605, L"战法 · 自动炫纹", profile.zhanFaShotKey.empty() ? std::wstring(L"未设置发射键") :
                L"发射 " + labelOf(profile.zhanFaShotKey) + L" · " + std::to_wstring(profile.zhanFaSkillKeys.size()) + L" 个技能键",
                profile.zhanFa, [this] { profile.zhanFa = !profile.zhanFa; changed(); });
            row(IdJzRow, IdJzSw, 649, L"剑宗 · 帝剑延迟", keyOrUnset(profile.jianZongSkillKey) + L" · 首刀后 " + std::to_wstring(profile.jianZongDelayMs) + L"ms",
                profile.jianZong, [this] { profile.jianZong = !profile.jianZong; changed(); });
        }
        // 3. Combos.
        {
            const float x = 648, cx = x + 19;
            card(x); cardTitle(cx, t.combo, L"一键连招");
            toggleSwitch(IdComboSw, cx + 214, 527, profile.combo, [this] { profile.combo = !profile.combo; changed(); });
            canvas.setOpacity(profile.combo ? 1.f : .45f);
            float y = 561 + 13;
            size_t shown = 0;
            for (const auto& c : profile.combos) {
                if (shown == 3) break;
                float px = cx;
                px += mcap(px, y, c.trigger.empty() ? L"?" : labelOf(c.trigger), t.combo, 1.5f) + 5;
                canvas.icon(Icon::ArrowRight, px, y - 7.5f, 15, t.text3); px += 20;
                for (const auto& s : c.steps) { const float w = mcap(px, y, labelOf(s.key), t.line2, 1, 0, cx + 250); px += w + 5; }
                y += 34; ++shown;
            }
            if (profile.combos.empty()) canvas.text(L"一个触发键，依次按出多个技能", sans(12), cx, 570, t.text3);
            canvas.setOpacity(1.f);
            link(IdComboLink, cx, 702, profile.combos.empty() ? std::wstring(L"新建连招") :
                L"编辑连招 · 共 " + std::to_wstring(profile.combos.size()) + L" 组", [this] { openDrawer(Drawer::Combo); }, false);
        }
        // 4. One-key run.
        {
            const float x = 952, cx = x + 19;
            card(x); cardTitle(cx, t.run, L"一键奔跑");
            toggleSwitch(IdRunSw, cx + 214, 527, options.oneKeyRun.enabled, [this] { options.oneKeyRun.enabled = !options.oneKeyRun.enabled; changed(); });
            canvas.setOpacity(options.oneKeyRun.enabled ? 1.f : .45f);
            const auto keys = currentRunKeys();
            const auto cell = [&](int col, int row, const std::wstring& k) {
                mcap(cx + col * 34.f, 593 + row * 30.f + 13, k.empty() ? L"—" : labelOf(k), t.run, 1.5f, 30);
            };
            cell(1, 0, keys[0]); cell(0, 1, keys[2]); cell(1, 1, keys[1]); cell(2, 1, keys[3]);
            const float ix = cx + 114;
            const float w = kbd(ix, 595.6f, hotkeyLabel(options.oneKeyRun.toggleHotkey));
            canvas.text(L"游戏内开关", sans(12), ix + w + 8, 595.6f, t.text2);
            const std::wstring guard = std::to_wstring(options.oneKeyRun.guardMs) + L"ms";
            canvas.text(guard, mono(12), ix, 623.3f, options.oneKeyRun.guardMs < kDefaultGuardMs ? t.combo : t.text);
            canvas.text(L"后起跑", sans(12), ix + canvas.textWidth(guard, mono(12)) + 8, 623.3f, t.text2);
            canvas.text(profile.usePresetRunKeys ? L"方向键仅本方案" : L"方向键所有方案共用", sans(12), ix, 648.7f, t.text2);
            canvas.setOpacity(1.f);
            link(IdRunLink, cx, 702, L"调整方向键与时序", [this] { openDrawer(Drawer::Run); }, false);
        }
    }
    std::wstring hoveredKey() const {
        if (hover >= IdKey && hover < IdKey + int(boardKeys().size())) return boardKeys()[size_t(hover - IdKey)].id;
        if (hover >= IdExtKey && hover < IdExtKey + int(extKeys().size())) return extKeys()[size_t(hover - IdExtKey)];
        return L"";
    }
    void paintFooter() {
        const auto& t = theme();
        canvas.fill(box(0, 760, kW, 40), t.bar); canvas.fill(box(0, 760, kW, 1), t.line);
        const float cy = 780;
        // Green while running, red while the auto-fire is stopped.
        canvas.glow(43.5f, cy, 3.5f, 11.5f, running ? t.ledGlow : t.ledBadGlow);
        canvas.fillCircle(43.5f, cy, 3.5f, running ? t.led : t.ledBad);
        canvas.text(running ? L"运行中 · 仅 DNF 前台生效" : L"连发未启动", sans(12), 55, cy, t.text2);
        float x = 276;
        const Font f = sans(12);
        const std::wstring key = hoveredKey();
        if (!message.empty()) {
            const float a = motion.value(AnimMessage, 1.f, 240);
            canvas.setLayer(a, (a - 1.f) * 10.f, 0.f);
            canvas.text(message, f, x, cy, messageError ? t.danger : (messageHint ? t.combo : t.text2), Align::Left, 780);
            canvas.resetLayer();
        } else if (capture.id == IdQuickChip) {
            canvas.text(L"按下新的游戏内切换热键，可带 Ctrl / Alt / Shift，例如 Alt + PgUp · Esc 取消", f, x, cy, t.text2);
        } else if (capture) {
            canvas.text(L"按下要绑定的按键 · 右键清除 · Esc 取消", f, x, cy, t.text2);
        } else if (hover == IdQuickChip) {
            canvas.text(L"点击修改游戏内快速切换方案的热键", f, x, cy, t.text2);
        } else if (hover == IdToolbox) {
            canvas.text(L"游戏工具箱：禁用无用组件、清理日志缓存、修复黑屏（原 DNF专用工具箱 8.0）", f, x, cy, t.text2);
        } else if (hover >= ToolboxPanel::kIdBase && hover < ToolboxPanel::kIdEnd && !toolboxPanel.hoverText(hover).empty()) {
            canvas.text(toolboxPanel.hoverText(hover), f, x, cy, t.text2, Align::Left, 780);
        } else if (hover == IdService) {
            const auto svc = servicePanel.summary();
            canvas.text(L"后台服务" + svc.state + L"：" + svc.detail + L" · 点击打开服务管理", f, x, cy, t.text2, Align::Left, 780);
        } else if (hover >= ServicePanel::kIdBase && hover < ServicePanel::kIdEnd && !servicePanel.hoverText(hover).empty()) {
            canvas.text(servicePanel.hoverText(hover), f, x, cy, t.text2, Align::Left, 780);
        } else if (!key.empty()) {
            const bool on = keyOn(key);
            const Occupied occ = occupied(key);
            const std::wstring label = key == L"Space" ? L"Space" : labelOf(key);
            x += kbd(x, cy, label) + 8;
            const wchar_t* what = occ == Occupied::Run ? L"一键奔跑方向键" : occ == Occupied::Combo ? L"一键连招触发键" : L"帝国剑术键";
            std::wstring text;
            if (occ != Occupied::None && on) text = std::wstring(L"已被") + what + L"占用，红灯表示冲突，点击关闭连发";
            else if (occ != Occupied::None) text = std::wstring(what) + L"，不参与连发";
            else text = on ? L"连发已开启 · 点击关闭" : L"点击开启连发";
            canvas.text(text, f, x, cy, t.text2);
        } else {
            canvas.text(keyCount(profile) ? L"在 DNF 中按住亮灯的键即自动连发" : L"点击键帽，亮起绿灯即开启连发", f, x, cy, t.text2);
        }
        const std::wstring saved = dirty ? L"正在保存…" : L"已自动保存";
        const float sw = canvas.textWidth(saved, f);
        canvas.text(saved, f, 1240 - sw, cy, t.text3);
        if (!dirty) canvas.icon(Icon::Check, 1240 - sw - 6 - 15, cy - 7.5f, 15, t.text3);
    }

    // ---------------------------------------------------------------- drawer
    D2D1_RECT_F body() const { return box(841, 108, 439, 692); }
    void bodyHit(const D2D1_RECT_F& r, int id, std::function<void()> click, std::function<void()> right = {}) {
        hit(r, id, std::move(click), std::move(right));
    }
    void sectionTitle(float y, const std::wstring& text) { canvas.text(text, sans(12, 500), 864, y + 8.7f, theme().text2); }
    void paintDrawer(float progress) {
        const auto& t = theme();
        const Drawer d = shownDrawer;
        // Scrim fades; the sheet slides in from the right edge. While closing nothing is clickable.
        suppressHits = drawer == Drawer::None;
        canvas.setLayer(progress);
        canvas.fill(box(0, 44, kW, kH - 44), t.scrim);
        hit(box(0, 44, kW, kH - 44), IdScrim, [this] { openDrawer(Drawer::None); });
        drawerDx = (1.f - progress) * 440.f;
        hitDx = drawerDx;
        canvas.setLayer(1.f, drawerDx, 0.f);
        scroll = motion.value(AnimScroll, scrollTarget, 220);
        const auto sheet = box(840, 44, 440, kH - 44);
        canvas.shadow(sheet, 0, t.shadow, 0, 48);
        canvas.fill(sheet, t.surface); canvas.fill(box(840, 44, 1, kH - 44), t.line);
        hit(sheet, IdSheet, [] {});
        const std::pair<const wchar_t*, Color> heads[] = {{L"", t.text3}, {L"职业辅助", t.cls}, {L"一键连招", t.combo}, {L"一键奔跑", t.run},
            {L"软件设置", t.text3}, {L"服务管理", t.focus}, {L"游戏工具箱", t.combo}};
        const auto& head = heads[int(d)];
        const float contentAlpha = motion.value(AnimDrawerContent, 1.f, 220);
        canvas.fill(box(840, 107, 440, 1), t.line);
        canvas.setLayer(contentAlpha, drawerDx, 0.f);
        canvas.fillRound(box(864, 71, 10, 10), 2, head.second);
        canvas.text(head.first, sans(16, 600), 884, 76, t.text);
        canvas.setLayer(1.f, drawerDx, 0.f);
        iconButton(IdSheetClose, 1232, 60, Icon::Close, [this] { openDrawer(Drawer::None); });
        if (d == Drawer::Combo) toggleSwitch(IdSheetSwitch, 1180, 66, profile.combo, [this] { profile.combo = !profile.combo; changed(); });
        if (d == Drawer::Run) toggleSwitch(IdSheetSwitch, 1180, 66, options.oneKeyRun.enabled, [this] { options.oneKeyRun.enabled = !options.oneKeyRun.enabled; changed(); });
        canvas.pushClip(body());
        canvas.setLayer(contentAlpha, drawerDx, (1.f - contentAlpha) * 10.f);
        clipHits = true; hitClip = body(); // hit() shifts both the area and this clip by hitDx.
        const float top = 132 - scroll;
        float bottom = top;
        switch (d) {
        case Drawer::Combo: bottom = paintComboDrawer(top); break;
        case Drawer::Run: bottom = paintRunDrawer(top); break;
        case Drawer::Class: bottom = paintClassDrawer(top); break;
        case Drawer::Settings: bottom = paintSettingsDrawer(top); break;
        case Drawer::Service: bottom = servicePanel.paint(*this, top); break;
        case Drawer::Toolbox: bottom = toolboxPanel.paint(*this, top); break;
        case Drawer::None: break;
        }
        clipHits = false;
        canvas.setLayer(1.f, drawerDx, 0.f);
        canvas.popClip();
        contentHeight = bottom - top + 48;
        // Content that shrank (e.g. a closed combo) pulls the scroll position back smoothly.
        scrollTarget = std::clamp(scrollTarget, 0.f, std::max(0.f, contentHeight - 692.f));
        if (d == Drawer::Service) servicePanel.paintOverlay(*this);
        canvas.resetLayer();
        hitDx = drawerDx = 0.f;
        suppressHits = false;
    }
    float paintComboDrawer(float y) {
        const auto& t = theme();
        if (comboSel >= int(profile.combos.size())) comboSel = int(profile.combos.size()) - 1;
        for (size_t i = 0; i < profile.combos.size(); ++i) {
            auto& c = profile.combos[i];
            const bool open = int(i) == comboSel;
            if (!open) {
                const auto r = box(864, y, 392, 50);
                const int id = IdComboSelect + int(i);
                if (hovered(id)) canvas.fillRound(r, 12, t.surface2);
                canvas.insetRing(r, 12, t.line, 1);
                float px = 879; const float cy = y + 25;
                px += mcap(px, cy, c.trigger.empty() ? L"?" : labelOf(c.trigger), t.combo, 1.5f) + 8;
                canvas.icon(Icon::ArrowRight, px, cy - 7.5f, 15, t.text3); px += 23;
                for (const auto& s : c.steps) px += mcap(px, cy, labelOf(s.key), t.line2, 1, 0, 1215) + 8;
                canvas.icon(Icon::ChevronDown, 1226, cy - 7.5f, 15, t.text3);
                bodyHit(r, id, [this, i] { commitEdit(); cancelCapture(); comboSel = int(i); invalidate(); });
                y += 60;
                continue;
            }
            const size_t slots = std::min<size_t>(5, c.steps.size() + 1);
            const float h = 16 + 36 + 14 + slots * 36.f + (slots - 1) * 8.f + 16;
            const auto r = box(864, y, 392, h);
            canvas.fillRound(r, 12, t.surface2); canvas.insetRing(r, 12, t.line2, 1);
            const float cy = y + 34;
            canvas.text(L"触发键", sans(12), 881, cy, t.text2);
            const int trigId = IdCapComboTrigger + int(i);
            const float fw = captureField(trigId, 937, cy - 18, 64, labelOf(c.trigger), false, [this, i](const std::wstring& k) {
                if (!k.empty()) for (size_t j = 0; j < profile.combos.size(); ++j)
                    if (j != i && physical(profile.combos[j].trigger) == physical(k)) { setError(IdCapComboTrigger + int(i), L"已是其他连招的触发键"); return false; }
                auto& combo = profile.combos[i];
                // The trigger is usually also the first skill: seed it as step 1, and keep
                // step 1 in sync while it still mirrors the previous trigger.
                if (!k.empty()) {
                    if (combo.steps.empty()) combo.steps.push_back({k, kDefaultComboDelayMs});
                    else if (!combo.trigger.empty() && physical(combo.steps.front().key) == physical(combo.trigger)) combo.steps.front().key = k;
                }
                combo.trigger = k; changed(); return true;
            }, false, t.combo, 1.5f);
            if (errorId == trigId) canvas.text(errorText, sans(12), 937 + fw + 12, cy, t.danger);
            iconButton(IdComboDelete + int(i), 1207, cy - 16, Icon::Trash, [this, i] {
                commitEdit(); cancelCapture();
                profile.combos.erase(profile.combos.begin() + ptrdiff_t(i));
                comboSel = std::max(0, std::min(comboSel, int(profile.combos.size()) - 1)); changed();
            }, 15);
            const float listTop = y + 66;
            canvas.line(902.75f, listTop, 902.75f, listTop + slots * 36.f + (slots - 1) * 8.f, t.line2, 1.5f, true);
            for (size_t j = 0; j < slots; ++j) {
                const float ry = listTop + j * 44.f;
                const bool filled = j < c.steps.size();
                canvas.text(std::to_wstring(j + 1), mono(11), 924.5f, ry + 18, t.text3);
                const int capId = IdCapComboStep + int(i) * 8 + int(j);
                const float cw = captureField(capId, 950.5f, ry, 88, filled ? labelOf(c.steps[j].key) : L"", false,
                    [this, i, j](const std::wstring& k) {
                        auto& steps = profile.combos[i].steps;
                        if (k.empty()) { if (j < steps.size()) steps.erase(steps.begin() + ptrdiff_t(j)); }
                        else if (j < steps.size()) steps[j].key = k;
                        else steps.push_back({k, kDefaultComboDelayMs});
                        changed(); return true;
                    }, false, {}, 1.f, L"＋ 输出键");
                if (filled) {
                    float px = 950.5f + cw + 10;
                    canvas.icon(Icon::Clock, px, ry + 10.5f, 15, t.text3); px += 25;
                    const auto field = box(px, ry + 2, 64, 32);
                    const int delayId = IdComboDelay + int(i) * 8 + int(j);
                    canvas.fillRound(field, 8, t.surface); canvas.insetRing(field, 8, editing == Edit::Delay && editCombo == int(i) && editStep == int(j) ? t.accent : t.line2, 1);
                    if (editing == Edit::Delay && editCombo == int(i) && editStep == int(j)) {
                        if (!IsWindowVisible(numberEdit)) placeEditor(numberEdit, D2D1::RectF(field.left + 6, ry + 9, field.right - 8, ry + 27), std::to_wstring(c.steps[j].intervalMs));
                    } else canvas.text(std::to_wstring(c.steps[j].intervalMs), mono(12.5f), field.right - 8, ry + 18, t.text, Align::Right);
                    bodyHit(field, delayId, [this, i, j] { beginDelay(int(i), int(j)); });
                    canvas.text(L"ms", sans(12), px + 74, ry + 18, t.text3);
                    iconButton(IdComboStepRemove + int(i) * 8 + int(j), 1207, ry + 2, Icon::Close, [this, i, j] {
                        commitEdit(); cancelCapture();
                        auto& steps = profile.combos[i].steps;
                        if (j < steps.size()) { steps.erase(steps.begin() + ptrdiff_t(j)); changed(); }
                    }, 14);
                }
            }
            y += h + 10;
        }
        const auto add = box(864, y, 392, 40);
        canvas.strokeRound(D2D1::RectF(add.left + .5f, add.top + .5f, add.right - .5f, add.bottom - .5f), 10, hovered(IdAddCombo) ? t.text3 : t.line2, 1, true);
        const float tw = canvas.textWidth(L"新建连招", sans(13));
        const Color c = hovered(IdAddCombo) ? t.text : t.text2;
        canvas.icon(Icon::Plus, 1060 - (tw + 21) / 2, y + 12.5f, 15, c);
        canvas.text(L"新建连招", sans(13), 1060 - (tw + 21) / 2 + 21, y + 20, c);
        bodyHit(add, IdAddCombo, [this] {
            commitEdit(); profile.combos.push_back({L"", {}}); comboSel = int(profile.combos.size()) - 1; invalidate();
        });
        return y + 40;
    }
    float paintRunDrawer(float y) {
        const auto& t = theme();
        canvas.setOpacity(options.oneKeyRun.enabled ? 1.f : .45f);
        sectionTitle(y, L"方向键"); y += 27.4f;
        const float cx = 864;
        const auto directionField = [&](int index, float x, float fy, const wchar_t* name) {
            (void)name;
            captureField(IdCapRun + index, x, fy, 64, labelOf(runKeyArray()[size_t(index)]), false, [this, index](const std::wstring& k) {
                if (k.empty()) return false;
                auto& keys = runKeyArray();
                for (int j = 0; j < 4; ++j) if (j != index && physical(keys[size_t(j)]) == physical(k)) { setError(IdCapRun + index, L"四个方向键不能重复"); return false; }
                keys[size_t(index)] = k; changed(); return true;
            });
        };
        directionField(0, cx + 70, y, L"上"); directionField(2, cx, y + 42, L"左");
        directionField(1, cx + 70, y + 42, L"下"); directionField(3, cx + 140, y + 42, L"右");
        const auto seg = box(1088, y, 168, 36);
        canvas.fillRound(seg, 9, t.surface2); canvas.insetRing(seg, 9, t.line, 1);
        {
            // The selected pill slides between "所有方案" and "仅本方案".
            const float k = motion.value(AnimRunScope, profile.usePresetRunKeys ? 1.f : 0.f, 180);
            const auto pill = box(lerp(1091, 1172, k), y + 3, 81, 30);
            canvas.shadow(pill, 7, rgb(0, .18f), 1, 2); canvas.fillRound(pill, 7, t.surface); canvas.ring(pill, 7, t.line2, 1);
        }
        const auto segButton = [&](int id, float x, const std::wstring& label, bool selected, bool perProfile) {
            const auto r = box(x, y + 3, 81, 30);
            canvas.text(label, sans(12.5f), x + 40.5f, y + 18, selected ? t.text : (hovered(id) ? t.text : t.text2), Align::Center);
            bodyHit(r, id, [this, perProfile] { setRunScope(perProfile); });
        };
        segButton(IdScopeAll, 1091, L"所有方案", !profile.usePresetRunKeys, false);
        segButton(IdScopeOne, 1172, L"仅本方案", profile.usePresetRunKeys, true);
        if (errorId >= IdCapRun && errorId < IdCapRun + 4) canvas.text(errorText, sans(12), 1088, y + 52, t.danger);
        y += 78 + 24;
        sectionTitle(y, L"起跑时序"); y += 27.4f;
        {
            const float guard = float(options.oneKeyRun.guardMs), gap = float(options.oneKeyRun.gapMs), run = 120.f;
            float wg = 392 * guard / (guard + gap + run), wp = 392 * gap / (guard + gap + run), wr = 392 - wg - wp;
            if (wp < 44) { const float extra = 44 - wp; wp = 44; wg -= extra * guard / (guard + run); wr = 392 - wg - wp; }
            const auto tl = box(864, y, 392, 30);
            canvas.pushClip(intersect(tl, body()));
            canvas.fillRound(tl, 8, t.surface3);
            canvas.fill(box(864 + wg, y, wp, 30), t.surface);
            canvas.strokeRound(box(864 + wg + .5f, y + .5f, wp - 1, 29), 0, t.line2, 1, true);
            canvas.fill(box(864 + wg + wp, y, wr, 30), t.surface);
            canvas.fillRound(box(864 + wg + wp - 8, y, wr + 8, 30), 8, t.runSoft);
            canvas.fill(box(864 + wg + wp - 8, y, 8, 30), t.surface);
            canvas.popClip();
            canvas.text(L"行走 " + std::to_wstring(options.oneKeyRun.guardMs), mono(11), 874, y + 15, t.text2);
            canvas.text(std::to_wstring(options.oneKeyRun.gapMs), mono(11), 864 + wg + wp / 2, y + 15, t.text3, Align::Center);
            const float rx = 864 + wg + wp + 10;
            canvas.text(L"奔跑", mono(11), rx, y + 15, t.run);
            canvas.icon(Icon::ArrowRight, rx + canvas.textWidth(L"奔跑", mono(11)) + 6, y + 7.5f, 15, t.run);
        }
        y += 44;
        // advisory: values below it stay allowed but show a warm hint beside the label and in the footer.
        const auto row = [&](const std::wstring& label, int dec, int inc, unsigned& value, unsigned step,
                             unsigned advisory, const std::wstring& inline_, std::wstring (*hint)()) {
            const bool low = advisory && value < advisory;
            canvas.text(label, sans(13), 864, y + 16, t.text);
            if (low) canvas.text(inline_, sans(12), 864 + canvas.textWidth(label, sans(13)) + 12, y + 16, t.combo);
            const auto after = [this, &value, advisory, hint] { changed(); if (advisory && value < advisory && hint) showHint(hint()); };
            stepper(dec, inc, 1084, y, 140, std::to_wstring(value),
                [&value, step, after] { value = value > step ? value - step : 1; after(); },
                [&value, step, after] { value = increaseTiming(value, step); after(); }, low);
            canvas.text(L"ms", sans(12), 1236, y + 16, t.text3);
            y += 42;
        };
        row(L"搓招保护", IdGuardDec, IdGuardInc, options.oneKeyRun.guardMs, 10,
            kDefaultGuardMs, L"低于 " + std::to_wstring(kDefaultGuardMs) + L"ms 易误触奔跑", &Impl::guardHint);
        row(L"双击间隔", IdGapDec, IdGapInc, options.oneKeyRun.gapMs, 5, 0, L"", nullptr);
        row(L"按键脉冲", IdPressDec, IdPressInc, options.oneKeyRun.pressMs, 5, 0, L"", nullptr);
        y += 14;
        sectionTitle(y, L"游戏内开关"); y += 27.4f;
        captureField(IdCapRunHotkey, 864, y, 64, hotkeyLabel(options.oneKeyRun.toggleHotkey), false, [this](const std::wstring& k) {
            const auto hotkey = parseHotkey(k);
            if (!hotkey) { setError(IdCapRunHotkey, L"请设置开关热键"); return false; }
            const auto other = parseHotkey(options.quickSwitchHotkey);
            if (other && other.key.physical_id() == hotkey.key.physical_id() && other.modifiers == hotkey.modifiers) {
                setError(IdCapRunHotkey, L"与快速切换热键相同"); return false;
            }
            options.oneKeyRun.toggleHotkey = k; changed(); return true;
        }, true);
        if (errorId == IdCapRunHotkey) canvas.text(errorText, sans(12), 864 + 120, y + 18, t.danger);
        canvas.setOpacity(1.f);
        return y + 36;
    }
    float chipsRow(float x, float y, float width, std::vector<std::wstring>& keys, int chipBase, int addId) {
        const auto& t = theme();
        float px = x, py = y;
        for (size_t k = 0; k < keys.size(); ++k) {
            const std::wstring label = labelOf(keys[k]);
            const float w = 10 + canvas.textWidth(label, mono(12)) + 2 + 20 + 3;
            if (px + w > x + width && px > x) { px = x; py += 34; }
            const auto r = box(px, py, w, 28);
            canvas.keyShape(r, 7, t.capTop, t.capSide, 2); canvas.ring(r, 7, t.line2, 1);
            canvas.text(label, mono(12), px + 10, py + 13, t.text);
            const auto remove = box(r.right - 23, py + 3, 20, 20);
            const int id = chipBase + int(k);
            if (hovered(id)) canvas.fillRound(remove, 5, t.surface3);
            canvas.icon(Icon::Close, remove.left + 4, remove.top + 4, 12, hovered(id) ? t.text : t.text3);
            bodyHit(remove, id, [this, &keys, k] { if (k < keys.size()) keys.erase(keys.begin() + ptrdiff_t(k)); changed(); });
            px += w + 6;
        }
        const float addW = std::max(44.f, canvas.textWidth(L"＋ 添加", mono(12)) + 20);
        if (px + addW > x + width && px > x) { px = x; py += 34; }
        captureField(addId, px, py, 44, L"", true, [this, &keys](const std::wstring& k) {
            if (k.empty()) return true;
            if (std::none_of(keys.begin(), keys.end(), [&](const std::wstring& e) { return physical(e) == physical(k); })) keys.push_back(k);
            changed(); return true;
        }, false, {}, 1.f, L"＋ 添加");
        return py + 28 - y;
    }
    float classBlock(float y, const std::wstring& name, const std::wstring& desc, bool on, int swId, std::function<void()> toggle,
                     const std::function<float(float)>& bodyFn) {
        const auto& t = theme();
        // Measure the body first by painting it off-screen (clipped out), then draw for real.
        const float headerH = 37.5f;
        canvas.setMuted(true);
        const auto saved = hits.size();
        const float bodyH = bodyFn(-10000);
        hits.resize(saved);
        canvas.setMuted(false);
        const float h = 1 + 14 + headerH + 14 + bodyH + 14 + 1;
        const auto r = box(864, y, 392, h);
        canvas.insetRing(r, 12, t.line, 1);
        canvas.text(name, sans(13, 500), 881, y + 15 + 9.4f, t.text);
        canvas.text(desc, sans(11.5f), 881, y + 15 + 18.85f + 2 + 8.35f, t.text2);
        toggleSwitch(swId, 1203, y + 15 + 8.75f, on, std::move(toggle));
        canvas.setOpacity(on ? 1.f : .4f);
        const auto mark = hits.size();
        bodyFn(y + 15 + headerH + 14);
        if (!on) hits.resize(mark);
        canvas.setOpacity(1.f);
        return h;
    }
    float paintClassDrawer(float y) {
        const auto& t = theme();
        const float labelX = 881, contentX = 945;
        const auto label = [&](float cy, const wchar_t* text) { canvas.text(text, sans(12), labelX, cy, t.text2); };
        y += classBlock(y, L"旅人 · 自动流星", L"按住技能键时自动发射流星", profile.lvRen, IdLvSw, [this] { profile.lvRen = !profile.lvRen; changed(); },
            [&](float by) {
                const float chipsH = chipsRow(contentX, by, 294, profile.lvRenSkillKeys, IdChipLv, IdCapLvAdd);
                label(by + 14, L"技能键");
                const float sy = by + chipsH + 12;
                label(sy + 18, L"发射键");
                captureField(IdCapLvShot, contentX, sy, 64, labelOf(profile.lvRenShotKey), false, [this](const std::wstring& k) { profile.lvRenShotKey = k; changed(); return true; });
                return chipsH + 12 + 36;
            }) + 24;
        y += classBlock(y, L"战法 · 自动炫纹", L"先按住发射键，再关闭 Num Lock", profile.zhanFa, IdZfSw, [this] { profile.zhanFa = !profile.zhanFa; changed(); },
            [&](float by) {
                const float chipsH = chipsRow(contentX, by, 294, profile.zhanFaSkillKeys, IdChipZf, IdCapZfAdd);
                label(by + 14, L"技能键");
                const float sy = by + chipsH + 12;
                label(sy + 18, L"发射键");
                const float w = captureField(IdCapZfShot, contentX, sy, 64, labelOf(profile.zhanFaShotKey), false, [this](const std::wstring& k) {
                    if (!k.empty() && !isNumpadKey(k)) { setError(IdCapZfShot, L"须为小键盘按键"); return false; }
                    profile.zhanFaShotKey = k; changed(); return true;
                });
                const bool bad = errorId == IdCapZfShot || (!profile.zhanFaShotKey.empty() && !isNumpadKey(profile.zhanFaShotKey));
                canvas.text(bad ? L"须为小键盘按键" : L"仅限小键盘", sans(11.5f), contentX + w + 12, sy + 18, bad ? t.danger : t.text3);
                return chipsH + 12 + 36;
            }) + 24;
        y += classBlock(y, L"剑宗 · 帝剑延迟", L"首刀后延迟，再进入连发", profile.jianZong, IdJzSw, [this] { profile.jianZong = !profile.jianZong; changed(); },
            [&](float by) {
                label(by + 18, L"帝剑键");
                captureField(IdCapJz, contentX, by, 64, labelOf(profile.jianZongSkillKey), false,
                    [this](const std::wstring& k) { if (k.empty()) return false; profile.jianZongSkillKey = k; changed(); return true; }, false, t.cls, 1.5f);
                const float sy = by + 48;
                label(sy + 16, L"延迟");
                stepper(IdJzDec, IdJzInc, contentX, sy, 140, std::to_wstring(profile.jianZongDelayMs),
                    [this] { profile.jianZongDelayMs = profile.jianZongDelayMs > 10 ? profile.jianZongDelayMs - 10 : 1; changed(); },
                    [this] { profile.jianZongDelayMs = std::min(10000u, profile.jianZongDelayMs + 10); changed(); });
                canvas.text(L"ms", sans(12), contentX + 152, sy + 16, t.text3);
                return 48.f + 32.f;
            });
        return y;
    }
    float paintSettingsDrawer(float y) {
        const auto& t = theme();
        const auto group = [&](float gy, float h) { canvas.insetRing(box(864, gy, 392, h), 12, t.line, 1); };
        const auto switchRow = [&](float ry, const std::wstring& name, int id, bool& value, bool divider) {
            canvas.text(name, sans(13, 500), 881, ry + 20, t.text);
            toggleSwitch(id, 1203, ry + 10, value, [this, &value] { value = !value; changed(); });
            if (divider) canvas.fill(box(865, ry + 39, 390, 1), t.line);
        };
        sectionTitle(y, L"启动"); y += 27.4f;
        group(y, 81);
        switchRow(y + .5f, L"运行后自动隐藏到托盘", IdAutoSw, options.autoStart, true);
        switchRow(y + 40.5f, L"登录 Windows 时运行", IdLoginSw, options.onSystemStart, false);
        y += 81 + 24;
        sectionTitle(y, L"游戏内"); y += 27.4f;
        group(y, 97);
        switchRow(y + .5f, L"屏蔽 Windows 键", IdWinSw, options.blockWin, true);
        canvas.text(L"快速切换方案", sans(13, 500), 881, y + 40.5f + 28, t.text);
        const std::wstring quickLabel = hotkeyLabel(options.quickSwitchHotkey);
        const float qw = std::max(64.f, canvas.textWidth(quickLabel, mono(13)) + 24);
        captureField(IdCapQuick, 1239 - qw, y + 40.5f + 10, 64, quickLabel, false,
            [this](const std::wstring& k) { return applyQuickHotkey(IdCapQuick, k); }, true);
        y += 97;
        if (errorId == IdCapQuick) { canvas.text(errorText, sans(12), 864, y + 16, t.danger); y += 24; }
        y += 24;
        const auto about = box(864, y, 392, 65);
        canvas.fillRound(about, 12, t.surface2);
        const auto keyIcon = box(880, y + 17.5f, 30, 30);
        canvas.keyShape(keyIcon, 8, t.capTop, t.capSide, 3); canvas.ring(keyIcon, 8, t.line2, 1);
        canvas.glow(902.5f, y + 25, 2.5f, 8, t.ledGlow); canvas.fillCircle(902.5f, y + 25, 2.5f, t.led);
        const float tx = 922;
        canvas.text(L"DAF 连发工具", sans(12, 500), tx, y + 23.7f, t.text);
        canvas.text(kFullVersion, mono(12, 400), tx + canvas.textWidth(L"DAF 连发工具", sans(12, 500)) + 4, y + 23.7f, t.text3);
        canvas.text(L"仅在 DNF 窗口前台生效 · 关闭窗口后在托盘继续运行", sans(12), tx, y + 43.1f, t.text2);
        return y + 65;
    }

    // ---------------------------------------------------------------- UiHost (service drawer)
    Canvas& uiCanvas() override { return canvas; }
    const Theme& uiTheme() const override { return theme(); }
    void uiHit(const D2D1_RECT_F& r, int id, std::function<void()> click) override { hit(r, id, std::move(click)); }
    bool uiHovered(int id) const override { return hovered(id); }
    void uiSwitch(int id, float x, float y, bool on, std::function<void()> click) override { toggleSwitch(id, x, y, on, std::move(click)); }
    void uiStepper(int idDec, int idInc, float x, float y, float w, const std::wstring& value,
                   std::function<void()> dec, std::function<void()> inc) override {
        stepper(idDec, idInc, x, y, w, value, std::move(dec), std::move(inc));
    }
    void uiSection(float y, const std::wstring& title) override { sectionTitle(y, title); }
    float uiLink(int id, float x, float cy, const std::wstring& label, std::function<void()> click) override {
        return link(id, x, cy, label, std::move(click), false);
    }
    void uiTextField(int id, const D2D1_RECT_F& r, const std::wstring& value, const std::wstring& placeholder,
                     TextCommit commit, TextChange change) override {
        const auto& t = theme();
        const bool active = editing == Edit::Text && textId == id;
        canvas.fillRound(r, 8, t.surface);
        canvas.insetRing(r, 8, active ? t.accent : (hovered(id) ? t.text3 : t.line2), 1);
        const float cy = (r.top + r.bottom) / 2;
        if (active) {
            if (!IsWindowVisible(textEdit)) placeEditor(textEdit, D2D1::RectF(r.left + 8, cy - 10, r.right - 8, cy + 10), value);
            return;
        }
        const bool empty = value.empty();
        canvas.text(empty ? placeholder : value, empty ? sans(12.5f) : mono(12.5f), r.left + 10, cy, empty ? t.text3 : t.text, Align::Left, r.right - r.left - 20);
        hit(r, id, [this, id, commit, change] { uiFocusText(id, commit, change); });
    }
    void uiFocusText(int id, TextCommit commit, TextChange change) override {
        cancelCapture(); commitEdit();
        editing = Edit::Text; textId = id; textCommit = std::move(commit); textChange = std::move(change);
        invalidate(); UpdateWindow(main); // Layout places the editor.
    }
    void uiCancelText() override { if (editing == Edit::Text) cancelEdit(); }
    void uiMessage(const std::wstring& text, bool error) override { showMessage(text, error); }
    void uiHint(const std::wstring& text) override { showHint(text); }
    void uiInvalidate() override { invalidate(); }
    HWND uiWindow() const override { return main; }
    void uiScheduleServiceSave() override { if (main) SetTimer(main, kServiceSaveTimer, kSaveDelayMs, nullptr); }
    void uiQuit() override { quit(); }
    float uiButtonWidth(const std::wstring& label) override { return canvas.textWidth(label, sans(13, 500)) + 28; }
    float uiButton(int id, float x, float y, const std::wstring& label, ButtonStyle style, std::function<void()> click) override {
        const auto& t = theme();
        const float w = uiButtonWidth(label);
        const auto r = box(x, y, w, 32);
        const float h = motion.value(AnimHover + id, hovered(id) ? 1.f : 0.f, 120);
        Color fg = t.text;
        if (style == ButtonStyle::Primary) {
            canvas.fillRound(r, 8, mix(t.accent, mix(t.accent, t.onAccent, .12f), h));
            fg = t.onAccent;
        } else if (style == ButtonStyle::Danger) {
            Color soft = t.danger; soft.a = (t.dark ? .14f : .10f) * h;
            canvas.fillRound(r, 8, t.surface); canvas.fillRound(r, 8, soft); canvas.insetRing(r, 8, mix(t.line2, t.danger, h), 1);
            fg = t.danger;
        } else {
            canvas.fillRound(r, 8, mix(t.surface, t.surface3, h)); canvas.insetRing(r, 8, t.line2, 1);
        }
        canvas.text(label, sans(13, 500), x + w / 2, y + 16, fg, Align::Center);
        hit(r, id, std::move(click));
        return w;
    }
    float uiAnim(int key, float target, float ms) override { return motion.value(key, target, ms); }
    void uiLayer(float opacity, float dy) override { canvas.setLayer(opacity, drawerDx, dy); }
    void uiHitsEnabled(bool enabled) override { overlayHitsOff = !enabled; }
    void uiSpinner(float x, float cy) override {
        const auto& t = theme();
        static LARGE_INTEGER frequency = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return f; }();
        LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
        const double phase = std::fmod(double(now.QuadPart) / double(frequency.QuadPart), 0.9) / 0.9;
        for (int i = 0; i < 3; ++i) {
            const double p = std::fmod(phase - i * 0.16 + 1.0, 1.0);
            const float a = 0.3f + 0.7f * float(std::max(0.0, std::sin(p * 3.14159265)));
            Color c = t.accent; c.a *= a;
            canvas.fillCircle(x + 3 + i * 9.f, cy, 2.6f, c);
        }
        motion.keepAlive();
    }

    // ---------------------------------------------------------------- quick switch
    void invalidateQuick() { if (quick) InvalidateRect(quick, nullptr, FALSE); }
    float quickHeight() const {
        const size_t rows = std::min<size_t>(names.size(), 8);
        return 52 + 10 + rows * 50.f - 4 + 10 + 46;
    }
    void paintQuick() {
        const auto& t = darkTheme();
        if (!quickCanvas.begin(t.surface)) return;
        auxMotion.beginFrame();
        const float w = 360, h = quickHeight();
        quickCanvas.insetRing(box(0, 0, w, h), 0, t.line2, 1);
        quickCanvas.text(L"切换方案", sans(14, 600), 18, 26, t.text);
        const auto label = hotkeyLabel(options.quickSwitchHotkey);
        const float kw = quickCanvas.textWidth(label, mono(11)) + 14;
        const auto kr = box(342 - kw, 15, kw, 22);
        quickCanvas.keyShape(kr, 5, t.capTop, t.capSide, 2); quickCanvas.ring(kr, 5, t.line2, 1);
        quickCanvas.text(label, mono(11), kr.left + 7, 25, t.text);
        quickCanvas.fill(box(0, 52, w, 1), t.line);
        const size_t rows = std::min<size_t>(names.size(), 8);
        {
            // The selection glides between rows as ↑ / ↓ are pressed.
            const float sy = auxMotion.value(AnimQuickSel, 62 + float(quickSel - quickScroll) * 50.f, 130);
            const auto rr = box(10, sy, 340, 46);
            quickCanvas.pushClip(box(0, 53, w, h - 46 - 53));
            quickCanvas.fillRound(rr, 10, t.surface2); quickCanvas.ring(rr, 10, t.accent, 1.5f);
            quickCanvas.popClip();
        }
        for (size_t r = 0; r < rows; ++r) {
            const size_t i = r + size_t(quickScroll);
            if (i >= names.size()) break;
            const float y = 62 + r * 50.f;
            const bool sel = int(i) == quickSel, current = names[i] == profile.name;
            if (current && running) { quickCanvas.glow(27, y + 23, 3, 10, t.ledGlow); quickCanvas.fillCircle(27, y + 23, 3, t.led); }
            else quickCanvas.fillCircle(27, y + 23, 3, current ? t.led : t.ledOff);
            float right = 336;
            const auto count = std::to_wstring(countFor(i)) + L" 键";
            quickCanvas.text(count, mono(11.5f), right, y + 23, t.text3, Align::Right);
            right -= quickCanvas.textWidth(count, mono(11.5f)) + 12;
            if (current) { quickCanvas.text(L"当前", sans(11), right, y + 23, t.accentText, Align::Right); right -= quickCanvas.textWidth(L"当前", sans(11)) + 12; }
            quickCanvas.text(names[i], sans(14, sel ? 600 : 400), 42, y + 23, t.text, Align::Left, right - 42);
        }
        const float fy = h - 46;
        quickCanvas.fill(box(0, fy, w, 1), t.line);
        float x = 18;
        const auto hintKey = [&](const std::wstring& k, const std::wstring& text) {
            const float kw2 = quickCanvas.textWidth(k, mono(11)) + 14;
            const auto r = box(x, fy + 12, kw2, 22);
            quickCanvas.keyShape(r, 5, t.capTop, t.capSide, 2); quickCanvas.ring(r, 5, t.line2, 1);
            quickCanvas.text(k, mono(11), x + 7, fy + 22, t.text);
            x += kw2 + 6;
            quickCanvas.text(text, sans(11.5f), x, fy + 23, t.text2);
            x += quickCanvas.textWidth(text, sans(11.5f)) + 14;
        };
        hintKey(L"↑↓", L"选择"); hintKey(L"Enter", L"切换并启动"); hintKey(L"Esc", L"关闭");
        quickCanvas.end();
        quickPending = auxMotion.pending();
        if (quickPending) ensureAuxFrames();
    }
    void showQuick() {
        commitEdit();
        flush();
        refreshNames();
        if (names.empty()) return;
        const auto found = std::find(names.begin(), names.end(), profile.name);
        quickSel = found == names.end() ? 0 : int(found - names.begin());
        quickScroll = std::max(0, quickSel - 7);
        HMONITOR monitor = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{}; info.cbSize = sizeof(info); GetMonitorInfoW(monitor, &info);
        const int w = int(std::lround(360 * scale)), h = int(std::lround(quickHeight() * scale));
        const RECT& wa = info.rcWork;
        SetWindowPos(quick, HWND_TOPMOST, wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h, SWP_NOACTIVATE);
        quickCanvas.resize(UINT(w), UINT(h));
        ShowWindow(main, SW_HIDE);
        auxMotion.jump(AnimQuickSel, 62 + float(quickSel - quickScroll) * 50.f);
        startFade(quick, AnimQuickFade); // Fades in instead of popping up.
        ShowWindow(quick, SW_SHOW); SetForegroundWindow(quick); SetFocus(quick);
        invalidateQuick(); updateAnimation();
    }
    void quickStart() {
        if (quickSel < 0 || size_t(quickSel) >= names.size()) return;
        const auto name = names[size_t(quickSel)];
        ShowWindow(quick, SW_HIDE);
        if (switchProfile(name)) start(false);
    }
    void quickKey(WPARAM vk) {
        const int n = int(names.size());
        if (vk == VK_UP && n) quickSel = (quickSel + n - 1) % n;
        else if (vk == VK_DOWN && n) quickSel = (quickSel + 1) % n;
        else if (vk == VK_RETURN || vk == VK_SPACE) { quickStart(); return; }
        else if (vk == VK_ESCAPE) { ShowWindow(quick, SW_HIDE); return; }
        if (quickSel < quickScroll) quickScroll = quickSel;
        if (quickSel >= quickScroll + 8) quickScroll = quickSel - 7;
        invalidateQuick();
    }

    // ---------------------------------------------------------------- window plumbing
    void show() {
        if (quick) ShowWindow(quick, SW_HIDE);
        refreshNames();
        servicePanel.refresh();
        ShowWindow(main, IsIconic(main) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(main); SetFocus(main);
        updateAnimation(); invalidate();
    }
    void updateAnimation() {
        const bool need = main && ((IsWindowVisible(main) && !IsIconic(main) && (running || capture)) ||
                                   (quick && IsWindowVisible(quick) && running));
        if (need && !animating) { SetTimer(main, kAnimTimer, 33, nullptr); animating = true; }
        else if (!need && animating) { KillTimer(main, kAnimTimer); animating = false; }
    }
    void applyEditColors() {
        const auto& t = theme();
        if (editBrush) DeleteObject(editBrush);
        editBrush = CreateSolidBrush(colorref(t.surface));
        if (nameEdit) InvalidateRect(nameEdit, nullptr, TRUE);
        if (numberEdit) InvalidateRect(numberEdit, nullptr, TRUE);
        if (textEdit) InvalidateRect(textEdit, nullptr, TRUE);
    }
    // ---------------------------------------------------------------- tray menu
    // A themed popup drawn with the same tokens as the main window (replaces the
    // system menu, which cannot follow the light/dark theme).
    static constexpr float kTrayTop = 62.f, kTrayRow = 36.f, kTraySep = 13.f;
    static constexpr TrayCommand kTrayItems[] = {TrayToggle, TrayQuick, TrayShow, TrayExit};
    float trayMenuHeight() const { return kTrayTop + 3 * kTrayRow + kTraySep + kTrayRow + 6; }
    D2D1_RECT_F trayItemRect(int index) const {
        const float y = kTrayTop + index * kTrayRow + (index >= 3 ? kTraySep : 0.f);
        return box(6, y, kTrayMenuW - 12, kTrayRow);
    }
    int trayItemAt(float x, float y) const {
        for (int i = 0; i < 4; ++i) if (contains(trayItemRect(i), x, y)) return i;
        return -1;
    }
    void invalidateTray() { if (trayMenuWnd) InvalidateRect(trayMenuWnd, nullptr, FALSE); }
    void hideTrayMenu() {
        trayHover = -1;
        if (trayMenuWnd && IsWindowVisible(trayMenuWnd)) ShowWindow(trayMenuWnd, SW_HIDE);
    }
    void trayMenu() {
        if (!trayMenuWnd) return;
        refreshNames();
        POINT p{}; GetCursorPos(&p);
        MONITORINFO info{}; info.cbSize = sizeof(info);
        GetMonitorInfoW(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &info);
        const RECT& wa = info.rcWork;
        const int w = int(std::lround(kTrayMenuW * scale)), h = int(std::lround(trayMenuHeight() * scale));
        // Like TrackPopupMenu: open at the cursor, flipping left/up when it would leave the work area.
        int x = p.x + w > wa.right ? p.x - w : p.x;
        int y = p.y + h > wa.bottom ? p.y - h : p.y;
        x = std::max<int>(wa.left, std::min<int>(x, wa.right - w));
        y = std::max<int>(wa.top, std::min<int>(y, wa.bottom - h));
        trayHover = -1;
        SetWindowPos(trayMenuWnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
        trayCanvas.resize(UINT(w), UINT(h));
        startFade(trayMenuWnd, AnimTrayFade);
        ShowWindow(trayMenuWnd, SW_SHOW);
        SetForegroundWindow(trayMenuWnd); SetFocus(trayMenuWnd);
        invalidateTray();
    }
    void runTray(int index) {
        if (index < 0 || index >= 4) return;
        hideTrayMenu();
        try {
            switch (kTrayItems[index]) {
            case TrayToggle: if (running) stop(); else start(true); break;
            case TrayQuick: showQuick(); break;
            case TrayShow: show(); break;
            case TrayExit: quit(); break;
            }
        } catch (...) { show(); showMessage(L"操作未完成，请检查设置与配置文件。", true); }
    }
    void paintTrayMenu() {
        const auto& t = theme();
        auto& c = trayCanvas;
        if (!c.begin(t.surface)) return;
        auxMotion.beginFrame();
        const float w = kTrayMenuW, h = trayMenuHeight();
        c.insetRing(box(0, 0, w, h), 0, t.line2, 1);
        // Header: status LED, product name, state and current profile.
        c.glow(22, 31, 3.5f, 11.5f, running ? t.ledGlow : t.ledBadGlow); // Green running, red stopped.
        c.fillCircle(22, 31, 3.5f, running ? t.led : t.ledBad);
        c.text(L"DAF 连发工具", sans(13, 600), 36, 22, t.text);
        const std::wstring state = running ? L"运行中" : L"连发未启动";
        const Font small = sans(11.5f);
        c.text(state, small, 36, 40, running ? t.accentText : t.text3);
        c.text(L"· " + profile.name, small, 36 + c.textWidth(state, small) + 5, 40, t.text3, Align::Left, w - 56 - c.textWidth(state, small));
        c.fill(box(0, 55, w, 1), t.line);
        for (int i = 0; i < 4; ++i) {
            const auto r = trayItemRect(i);
            const bool hot = trayHover == i, exit = kTrayItems[i] == TrayExit;
            Color exitSoft = t.danger; exitSoft.a = t.dark ? .14f : .10f;
            Color hotBg = exit ? exitSoft : t.surface2; hotBg.a *= auxMotion.value(AnimHover + 9000 + i, hot ? 1.f : 0.f, 110);
            c.fillRound(r, 8, hotBg);
            const float cy = (r.top + r.bottom) / 2;
            const Color fg = exit && hot ? t.danger : t.text;
            switch (kTrayItems[i]) {
            case TrayToggle: {
                c.text(running ? L"停止连发" : L"启动连发", sans(13), 18, cy, fg);
                const float sx = r.right - 12 - 32;
                const auto sw = box(sx, cy - 9, 32, 18);
                if (running) c.fillRound(sw, 9, t.accent);
                else { c.fillRound(sw, 9, t.surface3); c.insetRing(sw, 9, t.line2, 1); }
                c.fillCircle(running ? sx + 23 : sx + 9, cy, 7, running ? t.onAccent : t.text3);
                break;
            }
            case TrayQuick: {
                c.text(L"快速切换方案", sans(13), 18, cy, fg);
                const auto label = hotkeyLabel(options.quickSwitchHotkey);
                if (!label.empty()) {
                    const float kw = c.textWidth(label, mono(11)) + 14;
                    const auto kr = box(r.right - 12 - kw, cy - 11, kw, 22);
                    c.keyShape(kr, 5, t.capTop, t.capSide, 2); c.ring(kr, 5, t.line2, 1);
                    c.text(label, mono(11), kr.left + 7, cy - 1, t.text2);
                }
                break;
            }
            case TrayShow: c.text(L"显示主界面", sans(13), 18, cy, fg); break;
            case TrayExit: c.text(L"退出", sans(13), 18, cy, fg); break;
            }
        }
        const float sepY = trayItemRect(2).bottom + kTraySep / 2;
        c.fill(box(12, sepY, w - 24, 1), t.line);
        c.end();
        trayPending = auxMotion.pending();
        if (trayPending) ensureAuxFrames();
    }
    LRESULT trayProc(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps{}; BeginPaint(trayMenuWnd, &ps); try { paintTrayMenu(); } catch (...) {} EndPaint(trayMenuWnd, &ps); return 0; }
        case WM_SIZE: trayCanvas.resize(LOWORD(lParam), HIWORD(lParam)); return 0;
        case WM_ACTIVATE: if (LOWORD(wParam) == WA_INACTIVE) hideTrayMenu(); return 0;
        case WM_MOUSEACTIVATE: return MA_ACTIVATE;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, trayMenuWnd, 0}; TrackMouseEvent(&track);
            const int index = trayItemAt(float(GET_X_LPARAM(lParam)) / scale, float(GET_Y_LPARAM(lParam)) / scale);
            if (index != trayHover) { trayHover = index; invalidateTray(); }
            return 0;
        }
        case WM_MOUSELEAVE: if (trayHover != -1) { trayHover = -1; invalidateTray(); } return 0;
        case WM_LBUTTONUP: case WM_RBUTTONUP:
            runTray(trayItemAt(float(GET_X_LPARAM(lParam)) / scale, float(GET_Y_LPARAM(lParam)) / scale));
            return 0;
        case WM_KEYDOWN: case WM_SYSKEYDOWN:
            if (wParam == VK_ESCAPE || wParam == VK_MENU || wParam == VK_F10) hideTrayMenu();
            else if (wParam == VK_UP) { trayHover = trayHover <= 0 ? 3 : trayHover - 1; invalidateTray(); }
            else if (wParam == VK_DOWN) { trayHover = trayHover >= 3 ? 0 : trayHover + 1; invalidateTray(); }
            else if (wParam == VK_RETURN || wParam == VK_SPACE) runTray(trayHover);
            return 0;
        case WM_CLOSE: hideTrayMenu(); return 0;
        default: break;
        }
        return DefWindowProcW(trayMenuWnd, message, wParam, lParam);
    }
    static LRESULT CALLBACK trayWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            if (self) self->trayMenuWnd = window;
        }
        if (!self || window != self->trayMenuWnd) return DefWindowProcW(window, message, wParam, lParam);
        return self->trayProc(message, wParam, lParam);
    }
    void addTray() {
        tray = {}; tray.cbSize = sizeof(tray); tray.hWnd = main; tray.uID = 1;
        tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; tray.uCallbackMessage = kTrayMessage;
        tray.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(running ? 2 : 3));
        if (!tray.hIcon) tray.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
        wcscpy_s(tray.szTip, running ? L"DAF 连发工具 · 运行中" : L"DAF 连发工具 · 已停止");
        trayAdded = Shell_NotifyIconW(NIM_ADD, &tray) != FALSE;
    }
    void mouse(UINT msg, LPARAM lParam) {
        const float x = float(GET_X_LPARAM(lParam)) / scale, y = float(GET_Y_LPARAM(lParam)) / scale;
        const Hit* h = hitAt(x, y);
        const int id = h ? h->id : -1;
        switch (msg) {
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, main, 0}; TrackMouseEvent(&track);
            if (id != hover) { hover = id; invalidate(); }
            break;
        }
        case WM_LBUTTONDOWN:
            message.clear();
            if (editing != Edit::None) commitEdit();
            if (capture && id != capture.id) cancelCapture();
            // Clicks outside an open popover close it.
            if (menu && id != IdMore && id != IdMenuPanel && id != IdMenuRename && id != IdMenuClone && id != IdMenuDelete) { menu = false; confirmDelete = false; }
            if (overflow && id != IdOverflow && id != IdMenuPanel && !(id >= IdOverflowItem && id < IdOverflowItem + 256)) overflow = false;
            if (ext && id != IdExt && id != IdExtPanel && !(id >= IdExtKey && id < IdExtKey + 12)) ext = false;
            if (id != ServicePanel::kUninstallId) servicePanel.cancelConfirm();
            toolboxPanel.cancelConfirm(id);
            pressed = id; SetCapture(main); SetFocus(main); invalidate();
            break;
        case WM_LBUTTONUP: {
            ReleaseCapture();
            const int was = pressed; pressed = -1;
            if (h && id == was && h->click) { auto click = h->click; click(); }
            invalidate();
            break;
        }
        case WM_LBUTTONDBLCLK:
            if (h && h->dbl) { auto dbl = h->dbl; dbl(); }
            else if (h && h->click) { auto click = h->click; click(); }
            break;
        case WM_RBUTTONUP:
            if (h && h->right) { auto right = h->right; right(); }
            break;
        default: break;
        }
    }
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, message, wParam, lParam);
        if (window == self->quick) return self->quickProc(message, wParam, lParam);
        static const UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        if (message == taskbarCreated && self->main) { self->addTray(); return 0; }
        static const UINT showRunning = RegisterWindowMessageW(kShowRunningMessage);
        if (showRunning && message == showRunning && window == self->main) {
            self->show();
            self->showHint(L"DAF 连发工具已经在运行，无需重复打开；关闭窗口后可在任务栏右下角托盘找到它。");
            return 0;
        }
        switch (message) {
        case WM_NCCALCSIZE: if (wParam) return 0; break;
        case WM_NCACTIVATE: return DefWindowProcW(window, message, wParam, -1);
        case WM_NCHITTEST: {
            POINT p{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; ScreenToClient(window, &p);
            const float x = float(p.x) / self->scale, y = float(p.y) / self->scale;
            if (y < 44 && !self->hitAt(x, y)) return HTCAPTION;
            return HTCLIENT;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{}; BeginPaint(window, &ps);
            try { self->paint(); } catch (...) {}
            EndPaint(window, &ps); return 0;
        }
        case WM_SIZE: self->canvas.resize(LOWORD(lParam), HIWORD(lParam)); self->updateAnimation(); return 0;
        case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK: case WM_RBUTTONUP:
            try { self->mouse(message, lParam); } catch (...) { self->showMessage(L"操作未完成，请检查输入值和配置文件写入权限。", true); }
            return 0;
        case WM_MOUSELEAVE: if (self->hover != -1) { self->hover = -1; self->invalidate(); } return 0;
        case WM_MOUSEWHEEL:
            if (self->drawer == Drawer::Service && self->servicePanel.wheel(float(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA)) {
                self->invalidate(); return 0;
            }
            if (self->drawer != Drawer::None) {
                self->commitEdit();
                const float delta = float(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA * 48.f;
                const float maxScroll = std::max(0.f, self->contentHeight - 692.f);
                self->scrollTarget = std::clamp(self->scrollTarget - delta, 0.f, maxScroll); // Smooth scrolling.
                self->invalidate();
            }
            return 0;
        case WM_TIMER:
            if (wParam == kSaveTimer) self->flush();
            else if (wParam == kServiceSaveTimer) { KillTimer(window, kServiceSaveTimer); self->servicePanel.flush(*self); }
            else if (wParam == kServiceTimer) { if (IsWindowVisible(window) && !IsIconic(window) && self->servicePanel.refresh()) self->invalidate(); }
            else if (wParam == kFrameTimer) self->invalidate();
            else if (wParam == kAuxTimer) self->auxTick();
            else if (wParam == kAnimTimer) { self->invalidate(); if (self->quick && IsWindowVisible(self->quick)) self->invalidateQuick(); }
            return 0;
        case kServiceDoneMessage: self->servicePanel.finish(*self); return 0;
        case kToolboxDoneMessage: self->toolboxPanel.finish(*self); return 0;
        case WM_COMMAND:
            if (HIWORD(wParam) == EN_CHANGE && reinterpret_cast<HWND>(lParam) == self->textEdit && self->editing == Edit::Text && self->textChange) {
                auto change = self->textChange;
                change(self->editorText(self->textEdit));
                return 0;
            }
            if (HIWORD(wParam) == EN_KILLFOCUS && (reinterpret_cast<HWND>(lParam) == self->nameEdit || reinterpret_cast<HWND>(lParam) == self->numberEdit ||
                                                   reinterpret_cast<HWND>(lParam) == self->textEdit))
                self->commitEdit();
            return 0;
        case WM_CTLCOLOREDIT: {
            const auto& t = self->theme();
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, colorref(t.text)); SetBkColor(dc, colorref(t.surface));
            return reinterpret_cast<LRESULT>(self->editBrush);
        }
        case WM_SHOWWINDOW: self->updateAnimation(); break;
        case WM_CLOSE: self->commitEdit(); self->flush(); ShowWindow(window, SW_HIDE); self->updateAnimation(); return 0;
        case WM_DESTROY: if (window == self->main) PostQuitMessage(0); return 0;
        case kTrayMessage:
            if (lParam == WM_LBUTTONDBLCLK || lParam == WM_LBUTTONUP) self->show();
            else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) self->trayMenu();
            return 0;
        default: break;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
    LRESULT quickProc(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps{}; BeginPaint(quick, &ps); try { paintQuick(); } catch (...) {} EndPaint(quick, &ps); return 0; }
        case WM_SIZE: quickCanvas.resize(LOWORD(lParam), HIWORD(lParam)); return 0;
        case WM_ACTIVATE: if (LOWORD(wParam) == WA_INACTIVE) { ShowWindow(quick, SW_HIDE); updateAnimation(); } return 0;
        case WM_KEYDOWN: case WM_SYSKEYDOWN: quickKey(wParam); return 0;
        case WM_MOUSEMOVE: case WM_LBUTTONUP: {
            const float y = float(GET_Y_LPARAM(lParam)) / scale;
            const int row = y >= 62 ? int((y - 62) / 50) : -1;
            const int index = row + quickScroll;
            if (row >= 0 && row < 8 && index < int(names.size())) {
                if (quickSel != index) { quickSel = index; invalidateQuick(); }
                if (message == WM_LBUTTONUP) quickStart();
            }
            return 0;
        }
        case WM_CLOSE: ShowWindow(quick, SW_HIDE); return 0;
        default: break;
        }
        return DefWindowProcW(quick, message, wParam, lParam);
    }
    HWND makeEdit(bool numeric, unsigned limit = 0) {
        const DWORD style = WS_CHILD | ES_AUTOHSCROLL | (numeric ? ES_NUMBER | ES_RIGHT : 0);
        HWND edit = CreateWindowExW(0, L"EDIT", L"", style, 0, 0, 10, 10, main, nullptr, instance, nullptr);
        SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(numeric ? monoFont : sansFont), FALSE);
        SendMessageW(edit, EM_SETLIMITTEXT, limit ? limit : numeric ? 5 : 40, 0);
        return edit;
    }
    bool create(bool visible) {
        Fonts::load(instance);
        options = store.loadSettings(); names = store.presetNames();
        if (names.empty()) {
            profile.name = L"默认方案";
            if (visible) store.saveProfile(profile);
            names.push_back(profile.name);
        }
        if (std::find(names.begin(), names.end(), options.lastPreset) == names.end()) options.lastPreset = names.front();
        profile = store.loadProfile(options.lastPreset);
        if (visible) refreshNames();
        else { counts.assign(names.size(), 0); refreshCount(); }

        HDC screen = GetDC(nullptr); const int dpi = GetDeviceCaps(screen, LOGPIXELSY); ReleaseDC(nullptr, screen);
        RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        scale = float(dpi) / 96.f;
        if (work.right > work.left && work.bottom > work.top)
            scale = std::min({scale, float(work.right - work.left - 32) / kW, float(work.bottom - work.top - 32) / kH});
        scale = std::max(scale, 0.5f);

        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.hInstance = instance; wc.lpfnWndProc = windowProc;
        wc.style = CS_DBLCLKS; wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1)); if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
        wc.lpszClassName = kMainWindowClass; RegisterClassExW(&wc);
        wc.style = CS_DROPSHADOW; wc.lpszClassName = L"DAF.Native.QuickSwitch"; RegisterClassExW(&wc);
        wc.lpfnWndProc = trayWindowProc; wc.lpszClassName = L"DAF.Native.TrayMenu"; RegisterClassExW(&wc);
        const int w = int(std::lround(kW * scale)), h = int(std::lround(kH * scale));
        const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
        main = CreateWindowExW(WS_EX_APPWINDOW, kMainWindowClass, L"DAF 连发工具", style,
            work.left + (work.right - work.left - w) / 2, work.top + (work.bottom - work.top - h) / 2, w, h,
            nullptr, nullptr, instance, this);
        if (!main) return false;
        // The client runs elevated; let a non-elevated duplicate launch reach it through UIPI.
        if (const UINT showRunning = RegisterWindowMessageW(kShowRunningMessage))
            ChangeWindowMessageFilterEx(main, showRunning, MSGFLT_ALLOW, nullptr);
        const MARGINS margins{0, 0, 0, 1};
        DwmExtendFrameIntoClientArea(main, &margins);
        SetWindowPos(main, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        quick = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, L"DAF.Native.QuickSwitch", L"切换方案", WS_POPUP,
            0, 0, int(360 * scale), int(400 * scale), main, nullptr, instance, this);
        const DWORD round = 2; // DWMWCP_ROUND on Windows 11; ignored elsewhere.
        if (quick) DwmSetWindowAttribute(quick, static_cast<DWMWINDOWATTRIBUTE>(33), &round, sizeof(round));
        CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, L"DAF.Native.TrayMenu", L"DAF 连发工具", WS_POPUP,
            0, 0, int(kTrayMenuW * scale), int(trayMenuHeight() * scale), main, nullptr, instance, this); // Sets trayMenuWnd.
        if (trayMenuWnd) {
            const DWORD small = 3; // DWMWCP_ROUNDSMALL, the Windows 11 menu radius; ignored elsewhere.
            DwmSetWindowAttribute(trayMenuWnd, static_cast<DWMWINDOWATTRIBUTE>(33), &small, sizeof(small));
        }
        sansFont = CreateFontW(-int(std::lround(13 * scale)), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, Fonts::gdiFamily(Face::Sans));
        monoFont = CreateFontW(-int(std::lround(12.5f * scale)), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH, Fonts::gdiFamily(Face::Mono));
        nameEdit = makeEdit(false); numberEdit = makeEdit(true); textEdit = makeEdit(false, 260);
        applyEditColors();
        canvas.attach(main, scale);
        // Layered (alpha-faded) popups start fully opaque; startFade() animates them in.
        if (quick) SetLayeredWindowAttributes(quick, 0, 255, LWA_ALPHA);
        if (trayMenuWnd) SetLayeredWindowAttributes(trayMenuWnd, 0, 255, LWA_ALPHA);
        if (quick) quickCanvas.attach(quick, scale);
        if (trayMenuWnd) trayCanvas.attach(trayMenuWnd, scale);
        servicePanel.load();
        servicePanel.refresh();
        SetTimer(main, kServiceTimer, 2000, nullptr);
        setRunning(false);
        if (visible) addTray();
        visibleUi = visible;
        return true;
    }
};

ClientUi::ClientUi(HINSTANCE instance, Store& store, UiCallbacks callbacks) : impl_(new Impl(instance, store, std::move(callbacks))) {}
ClientUi::~ClientUi() = default;
bool ClientUi::create(bool visible, bool showWindow) {
    const bool created = impl_->create(visible);
    if (created && visible && showWindow) impl_->show();
    return created;
}
HWND ClientUi::window() const { return impl_->main; }
const Profile& ClientUi::currentProfile() const { return impl_->profile; }
const Settings& ClientUi::settings() const { return impl_->options; }
void ClientUi::show() { impl_->show(); }
void ClientUi::quickSwitch() {
    try { impl_->showQuick(); } catch (...) { impl_->showMessage(L"无法读取或保存方案。", true); }
}
bool ClientUi::start(bool saveFirst) {
    try { return impl_->start(saveFirst); } catch (...) { impl_->showMessage(L"无法启动，请检查设置与配置文件。", true); }
    return false;
}
bool ClientUi::stop() { return impl_->stop(); }
void ClientUi::toggle() { if (impl_->running) stop(); else start(); }
void ClientUi::toggleRun() {
    try { impl_->toggleRun(); } catch (...) { impl_->showMessage(L"无法保存一键奔跑开关。", true); }
}
void ClientUi::setRunning(bool running) { impl_->setRunning(running); }
void ClientUi::setStatus(const std::wstring& text) { impl_->showMessage(text, true); }
void ClientUi::showHint(const std::wstring& text) { impl_->showHint(text); }
void ClientUi::quit() {
    try { impl_->quit(); } catch (...) { if (impl_->callbacks.quit) impl_->callbacks.quit(); }
}
const ServiceOptions& ClientUi::serviceOptions() const { return impl_->servicePanel.options(); }
bool ClientUi::addServiceItem(int list, const std::wstring& value) {
    std::wstring error;
    const bool added = impl_->servicePanel.addItem(list, value, error);
    if (!error.empty()) impl_->showMessage(error, !added);
    return added;
}
bool ClientUi::removeServiceItem(int list, size_t index) { return impl_->servicePanel.removeItem(list, index); }
bool ClientUi::flushService() { return impl_->servicePanel.flush(*impl_); }
void ClientUi::openServicePage() { impl_->openDrawer(Drawer::Service); }
std::wstring ClientUi::serviceNotice() const { return impl_->servicePanel.notice(); }
bool ClientUi::processMessage(MSG& message) {
    auto& ui = *impl_;
    const bool key = message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN;
    if ((message.hwnd == ui.nameEdit || message.hwnd == ui.numberEdit || message.hwnd == ui.textEdit) && message.message == WM_KEYDOWN) {
        if (message.wParam == VK_RETURN || message.wParam == VK_TAB) {
            ui.textSubmitted = message.wParam == VK_RETURN && message.hwnd == ui.textEdit;
            ui.commitEdit();
            ui.textSubmitted = false;
            return true;
        }
        if (message.wParam == VK_ESCAPE) {
            // Esc in the process picker's search box closes the whole picker.
            if (!(message.hwnd == ui.textEdit && ui.drawer == Drawer::Service && ui.servicePanel.escape(ui))) ui.cancelEdit();
            return true;
        }
        return false;
    }
    if (message.hwnd != ui.main) return false;
    if (ui.capture) {
        if (key) { ui.captureKey(message.wParam, message.lParam); return true; }
        if (message.message == WM_KEYUP || message.message == WM_SYSKEYUP || message.message == WM_CHAR || message.message == WM_SYSCHAR) return true;
    }
    if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
        if (ui.menu || ui.overflow || ui.ext) { ui.menu = ui.overflow = ui.ext = ui.confirmDelete = false; ui.invalidate(); }
        else if (ui.drawer == Drawer::Service && ui.servicePanel.escape(ui)) {}
        else if (ui.drawer != Drawer::None) ui.openDrawer(Drawer::None);
        else { ui.flush(); ShowWindow(ui.main, SW_HIDE); ui.updateAnimation(); }
        return true;
    }
    if (message.message == WM_KEYDOWN && message.wParam == 'S' && GetKeyState(VK_CONTROL) < 0) { ui.flush(); return true; }
    return false;
}
bool ClientUi::keyEnabled(const std::wstring& key) const { return impl_->keyOn(key); }
void ClientUi::setKeyEnabled(const std::wstring& key, bool enabled) { impl_->setKey(key, enabled); }
void ClientUi::setTiming(unsigned downMs, unsigned upMs) { impl_->setTiming(downMs, upMs); }
void ClientUi::setRunScope(bool perProfile) { impl_->setRunScope(perProfile); }
std::vector<std::wstring> ClientUi::runKeys() const { const auto k = impl_->currentRunKeys(); return {k.begin(), k.end()}; }
bool ClientUi::selectProfile(const std::wstring& name) { return impl_->switchProfile(name); }
bool ClientUi::cloneProfile() { return impl_->cloneProfile(); }
bool ClientUi::renameProfile(const std::wstring& name) { return impl_->renameProfile(name); }
bool ClientUi::flush() { return impl_->flush(); }
std::wstring ClientUi::status() const { return impl_->message; }
bool ClientUi::running() const { return impl_->running; }
} // namespace dafclient
