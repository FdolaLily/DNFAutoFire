#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "client_ui_service.h"
#include "app_ids.h"
#include "process_util.h"
#include "win_fs.h"
#include "ui_motion.h"
#include <shellapi.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <algorithm>
#include <cwctype>

namespace dafclient {
using namespace gfx;
namespace {
enum : int {
    IdInstall = ServicePanel::kIdBase, IdStart, IdStop, IdRestart, IdUninstall, IdLog,
    IdGame, IdPollDec, IdPollInc, IdDelayDec, IdDelayInc, IdPrioritySw, IdPriorityNormal, IdPriorityAbove, IdLauncherSw, IdLocked,
    IdUpdatePick, IdUpdateConfirm, IdUpdateCancel,
    IdAdd = ServicePanel::kIdBase + 50,      // + list
    IdRemove = ServicePanel::kIdBase + 100   // + list * 100 + index
};
D2D1_RECT_F box(float x, float y, float w, float h) { return D2D1::RectF(x, y, x + w, y + h); }
std::wstring trim(std::wstring s) {
    while (!s.empty() && iswspace(s.front())) s.erase(s.begin());
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    return s;
}
std::wstring lower(std::wstring s) { for (auto& c : s) c = wchar_t(towlower(c)); return s; }
// Long paths keep their end (folder and file name) and lose the beginning.
std::wstring fitPath(Canvas& c, const std::wstring& path, const Font& font, float maxWidth) {
    if (c.textWidth(path, font) <= maxWidth) return path;
    std::wstring tail = path;
    while (tail.size() > 1 && c.textWidth(L"…" + tail, font) > maxWidth) tail.erase(tail.begin());
    return L"…" + tail;
}
std::wstring downloadsFolder(const std::wstring& fallback) {
    wchar_t* path = nullptr;
    std::wstring result = fallback;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &path)) && path) result = path;
    if (path) CoTaskMemFree(path);
    return result;
}
struct ListText { const wchar_t* title; const wchar_t* description; const wchar_t* placeholder; };
const ListText kLists[] = {
    {L"延迟结束的进程", L"游戏启动并等待上面的延迟后结束；只结束该进程本身", L"选择进程…"},
    {L"限制资源的进程", L"延迟后绑定到最后一个 CPU 核心，并降为极低 I/O 优先级", L"选择进程…"},
    {L"随游戏启动", L"在游戏所在的登录会话中启动；本程序目录内的文件保存为相对路径", L"选择程序…"},
    {L"游戏退出后关闭", L"只关闭同一登录会话中的程序；连发程序会先停止并释放按键", L"选择进程…"},
};
} // namespace

ServicePanel::ServicePanel(Store& store, std::wstring executable) : store_(store), executable_(std::move(executable)) {}
ServicePanel::~ServicePanel() { if (worker_.joinable()) worker_.join(); }

void ServicePanel::load() {
    try { options_ = store_.loadService(); } catch (...) { options_ = ServiceOptions{}; }
    dirty_ = false;
}
void ServicePanel::setOptions(const ServiceOptions& options) { options_ = options; dirty_ = true; }
std::vector<std::wstring>& ServicePanel::list(int index) {
    switch (index) {
    case Kill: return options_.kill;
    case Limit: return options_.limit;
    case AutoStart: return options_.autoStart;
    default: return options_.autoStop;
    }
}
void ServicePanel::changed(UiHost& host) { dirty_ = true; host.uiScheduleServiceSave(); host.uiInvalidate(); }
bool ServicePanel::flush(UiHost& host) {
    if (!dirty_) return true;
    try {
        store_.saveService(options_);
        options_ = store_.loadService(); // Show exactly what the service will read.
        dirty_ = false;
        host.uiInvalidate();
        return true;
    } catch (...) {
        host.uiMessage(L"无法写入服务设置，请检查 config.json 的写入权限。", true);
        return false;
    }
}
bool ServicePanel::refresh() {
    const auto next = svcctl::query(executable_);
    if (next == info_) return false;
    info_ = next;
    return true;
}
ServiceSummary ServicePanel::summarize(const svcctl::Info& info, bool busy) {
    if (busy) return {L"处理中", L"正在与 Windows 服务管理器通信，请稍候", ServiceTone::Busy};
    if (info.error) return {L"状态未知", L"无法读取服务状态（错误码 " + std::to_wstring(info.error) + L"）", ServiceTone::Stopped};
    // An old stand-alone service still needs replacing, installed or not.
    const std::wstring legacy = info.legacy.empty() ? L"" : L"检测到旧版服务 " + info.legacy.front() + L"，需要替换为本程序";
    if (!info.installed) {
        if (!legacy.empty()) return {L"待替换", legacy, ServiceTone::Update};
        return {L"未安装", L"安装后，打开 DNF 自动开启连发，退出游戏自动关闭", ServiceTone::Stopped};
    }
    if (!info.pointsHere) return {L"需更新", L"服务注册的是其他位置的程序，需要更新为当前程序", ServiceTone::Update};
    if (!legacy.empty()) return {L"待替换", legacy, ServiceTone::Update};
    switch (info.state) {
    case SERVICE_RUNNING: return {L"运行中", L"打开 DNF 自动开启连发，退出游戏自动关闭", ServiceTone::Running};
    case SERVICE_START_PENDING: return {L"启动中", L"服务正在启动", ServiceTone::Busy};
    case SERVICE_STOP_PENDING: return {L"停止中", L"服务正在停止", ServiceTone::Busy};
    default: return {L"已停止", L"服务未运行，打开 DNF 时不会自动开启连发", ServiceTone::Stopped};
    }
}
Color ServicePanel::toneColor(const Theme& t, ServiceTone tone) {
    switch (tone) {
    case ServiceTone::Running: return t.led;
    case ServiceTone::Update: return t.ledWarn;
    case ServiceTone::Busy: return t.combo;
    default: return t.ledBad;
    }
}

bool ServicePanel::addItem(int index, const std::wstring& raw, std::wstring& error) {
    auto value = trim(raw);
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') value = trim(value.substr(1, value.size() - 2));
    if (value.empty()) return false;
    const bool path = index == AutoStart;
    if (value.find_first_of(path ? L"\"<>|*?" : L"\\/:*?\"<>|") != std::wstring::npos) {
        error = path ? L"路径包含无效字符。" : L"请只填写进程名，例如 GameLoader.exe。";
        return false;
    }
    const auto key = svc::processKey(value);
    if (index == Kill || index == Limit) {
        if (key == svc::processKey(options_.gameProcess)) { error = L"不能把游戏进程本身加入这个列表。"; return false; }
        if (key == svc::processKey(executable_)) { error = L"连发程序请放在“游戏退出后关闭”中。"; return false; }
    }
    auto& items = list(index);
    for (const auto& item : items) {
        if (path ? lower(item) == lower(value) : svc::processKey(item) == key) { error = L"已在列表中。"; return false; }
    }
    items.push_back(value);
    dirty_ = true;
    if (path && !fs::isFile(svc::resolvePath(value, fs::directoryOf(executable_))))
        error = L"已添加，但文件目前不存在；游戏启动时会跳过它。";
    return true;
}
bool ServicePanel::locked(int index, const std::wstring& item) const {
    return index == AutoStart && fs::samePath(svc::resolvePath(item, fs::directoryOf(executable_)), executable_);
}
bool ServicePanel::removeItem(int index, size_t position) {
    auto& items = list(index);
    if (position >= items.size() || locked(index, items[position])) return false;
    items.erase(items.begin() + ptrdiff_t(position)); dirty_ = true;
    return true;
}

void ServicePanel::run(UiHost& host, const wchar_t* busyText, std::function<svcctl::Result()> operation) {
    if (busy_) return;
    if (!flush(host)) return;
    if (worker_.joinable()) worker_.join();
    busy_ = true; busyText_ = busyText; confirmUninstall_ = false;
    HWND window = host.uiWindow();
    worker_ = std::thread([this, window, operation] {
        svcctl::Result result;
        try { result = operation(); } catch (...) { result = {false, L"服务操作发生异常。"}; }
        { std::lock_guard<std::mutex> lock(mutex_); result_ = result; }
        PostMessageW(window, kServiceDoneMessage, 0, 0);
    });
    host.uiInvalidate();
}
void ServicePanel::finish(UiHost& host) {
    if (worker_.joinable()) worker_.join();
    busy_ = false;
    svcctl::Result result;
    { std::lock_guard<std::mutex> lock(mutex_); result = result_; }
    if (result.ok) host.uiHint(result.message); else host.uiMessage(result.message, true);
    refresh();
    host.uiInvalidate();
    if (quitAfter_) {
        quitAfter_ = false;
        // The new program was started and waits for this process to exit.
        if (updateChanged_) host.uiQuit();
    }
}

bool ServicePanel::chooseUpdate(const std::wstring& raw, std::wstring& error) {
    const auto path = trim(raw);
    if (path.empty()) return false;
    const auto full = fs::fullPath(path);
    if (fs::samePath(full, executable_)) { error = L"所选文件就是当前正在运行的程序。"; return false; }
    const auto info = update::inspect(full);
    if (!info.product) { error = L"所选文件不是 DAF 连发工具程序。"; return false; }
    if (update::sameContent(full, executable_)) { error = L"所选文件与当前程序完全相同，无需更新。"; return false; }
    pendingPath_ = full;
    pendingInfo_ = info;
    pendingOffer_ = update::classify(info.version, update::currentVersion(), false);
    return true;
}
void ServicePanel::pickUpdate(UiHost& host) {
    const auto chosen = pickProgramFile(host.uiWindow(), downloadsFolder(fs::directoryOf(executable_)), L"选择新版本的 DAF 连发工具程序");
    if (chosen.empty()) return;
    std::wstring error;
    if (!chooseUpdate(chosen, error)) { if (!error.empty()) host.uiMessage(error, true); }
    host.uiInvalidate();
}
void ServicePanel::confirmUpdate(UiHost& host) {
    if (pendingPath_.empty() || busy_) return;
    const auto source = pendingPath_, target = executable_;
    quitAfter_ = true; updateChanged_ = false;
    run(host, L"正在更新程序…", [this, source, target] {
        update::Options options;
        options.holdsInstance = true; // This client holds the single-instance lock; the new one waits for it.
        const auto outcome = update::replace(source, target, options);
        updateChanged_ = outcome.changed;
        return svcctl::Result{outcome.ok, outcome.message};
    });
    if (busy_) pendingPath_.clear(); else quitAfter_ = false;
}

void ServicePanel::openProcessPicker(UiHost& host, int index) {
    if (index == GameProcess) {
        picker_.open(host, L"选择游戏进程", L"服务检测到该进程启动后开始工作，默认 DNF.exe",
            [this, &host](const std::wstring& name) {
                const auto value = trim(name);
                if (value.empty() || value.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) { host.uiMessage(L"请填写游戏的进程名，例如 DNF.exe。", true); return; }
                if (value != options_.gameProcess) { options_.gameProcess = value; changed(host); }
            },
            [this](const std::wstring& name) { return svc::processKey(name) == svc::processKey(options_.gameProcess) ? std::wstring(L"当前") : std::wstring(); },
            true);
        return;
    }
    picker_.open(host, L"选择进程", std::wstring(L"添加到“") + kLists[index].title + L"”，可连续选择多个",
        [this, &host, index](const std::wstring& name) {
            std::wstring error;
            const bool added = addItem(index, name, error);
            if (added) { changed(host); host.uiHint(L"已添加 " + trim(name)); }
            else if (!error.empty()) host.uiMessage(error, true);
        },
        [this, index](const std::wstring& name) {
            const auto key = svc::processKey(name);
            for (const auto& item : list(index)) if (svc::processKey(item) == key) return std::wstring(L"已添加");
            if (index != AutoStop && key == svc::processKey(options_.gameProcess)) return std::wstring(L"游戏进程");
            if (index != AutoStop && key == svc::processKey(executable_)) return std::wstring(L"本程序");
            return std::wstring();
        },
        false);
}
void ServicePanel::pickProgram(UiHost& host) {
    const auto base = fs::directoryOf(executable_);
    const auto chosen = pickProgramFile(host.uiWindow(), base);
    if (chosen.empty()) return;
    std::wstring error;
    const bool added = addItem(AutoStart, relativeToDirectory(chosen, base), error);
    if (added) { changed(host); host.uiHint(L"已添加 " + relativeToDirectory(chosen, base)); }
    else if (!error.empty()) host.uiMessage(error, true);
}
bool ServicePanel::escape(UiHost& host) {
    if (!picker_.active()) return false;
    picker_.close(host);
    return true;
}
std::wstring ServicePanel::notice() const {
    if (info_.legacy.empty() || (info_.installed && info_.pointsHere)) return L"";
    return L"检测到旧版服务 " + info_.legacy.front() + L"，请点击连发开关左侧的“后台服务”，再点“" + (info_.installed ? L"更新并启动" : L"安装并启动") + L"”替换它。";
}
std::wstring ServicePanel::hoverText(int id) const {
    switch (id) {
    case IdInstall: return info_.legacy.empty() ? L"注册为自动启动的 Windows 服务（LocalSystem）并立即启动"
                                                 : L"注册本程序为服务，停止并删除旧版 " + info_.legacy.front() + L" 服务，再删除旧版服务程序和 bat 脚本";
    case IdStop: return L"停止后，游戏启动时不再自动打开连发、调整优先级或处理后台进程";
    case IdRestart: return L"设置修改后会自动生效，通常不需要重启";
    case IdUninstall: return L"删除 Windows 服务；程序文件、config.json 和日志保留";
    case IdLocked: return L"连发程序本身必须随游戏启动，不能移除";
    case IdLog: return L"服务日志：logs\\service.log，超过 128 KB 自动滚动，最多保留两个文件";
    case IdGame: return L"点击从正在运行的进程中选择；服务从该进程获取游戏会话，无需填写安装路径";
    case IdAdd + Kill: case IdAdd + Limit: case IdAdd + AutoStop: return L"从正在运行的进程中选择，也可以在搜索框输入进程名后按回车";
    case IdAdd + AutoStart: return L"选择要随游戏启动的程序文件";
    case IdPriorityAbove: return L"高于正常：后台争抢 CPU 时减少卡顿；从不使用“高”或“实时”";
    case IdUpdatePick: return L"选择下载好的新版本程序；也可以直接双击新版本文件，按提示更新";
    case IdUpdateConfirm: return L"停止后台服务和连发（先释放按键），用所选文件覆盖当前程序，再重启服务并打开新版本";
    case IdUpdateCancel: return L"不更新，保持当前程序";
    default: return L"";
    }
}

float ServicePanel::paintStatus(UiHost& host, float y) {
    auto& c = host.uiCanvas();
    const auto& t = host.uiTheme();
    host.uiSection(y, L"服务状态"); y += 27.4f;
    const float h = 124;
    const auto card = box(864, y, 392, h);
    c.fillRound(card, 12, t.surface2); c.insetRing(card, 12, t.line, 1);
    std::wstring title, detail;
    const auto tone = summarize(info_, busy_).tone;
    const Color dot = toneColor(t, tone);
    const bool glow = tone != ServiceTone::Busy; // Same LED colours as the main-screen chip.
    const bool running = info_.state == SERVICE_RUNNING;
    if (busy_) { title = busyText_; detail = L"请稍候，正在与 Windows 服务管理器通信"; }
    else if (info_.error) { title = L"无法读取服务状态"; detail = L"错误码 " + std::to_wstring(info_.error); }
    else if (!info_.installed) {
        title = L"未安装";
        detail = info_.legacy.empty() ? L"安装后随 Windows 启动，DNF 启动时自动打开连发"
                                      : L"检测到旧版服务 " + info_.legacy.front() + L"，安装时会自动替换";
    } else {
        switch (info_.state) {
        case SERVICE_RUNNING: title = L"运行中"; break;
        case SERVICE_START_PENDING: title = L"正在启动…"; break;
        case SERVICE_STOP_PENDING: title = L"正在停止…"; break;
        default: title = L"已停止"; break;
        }
        if (!info_.pointsHere) { detail = L"服务注册的是其他位置的程序，点击“更新并启动”改用当前程序"; }
        else if (!info_.legacy.empty()) detail = L"旧版服务 " + info_.legacy.front() + L" 仍然存在，点击“更新并启动”移除";
        else if (running) detail = L"进程 " + std::to_wstring(info_.pid) + L" · 开机自动运行 · 监视 " + options_.gameProcess;
        else detail = L"服务未运行，游戏启动时不会自动处理";
    }
    const float cy = y + 28;
    if (glow) { Color g = dot; g.a *= .6f; c.glow(884, cy, 3.5f, 11.5f, g); }
    c.fillCircle(884, cy, 3.5f, dot);
    c.text(title, sans(15, 600), 898, cy, t.text);
    if (busy_) host.uiSpinner(898 + c.textWidth(title, sans(15, 600)) + 10, cy);
    c.text(kServiceName, mono(11, 400), 1240, cy, t.text3, Align::Right);
    c.text(detail, sans(12), 881, y + 54, t.text2, Align::Left, 360);
    if (!busy_ && !info_.error) {
        const float by = y + 76;
        float x = 881;
        const bool needInstall = !info_.installed || !info_.pointsHere || !info_.legacy.empty();
        const auto exe = executable_;
        if (needInstall)
            x += host.uiButton(IdInstall, x, by, info_.installed ? L"更新并启动" : L"安装并启动", ButtonStyle::Primary,
                [this, &host, exe] {
                    const auto legacy = info_.legacyCommands;
                    run(host, L"正在安装服务…", [exe, legacy] {
                        // An old service in another folder keeps its settings there: bring them along first.
                        for (const auto& command : legacy) update::adoptLegacySettings(svcctl::commandExecutable(command), exe);
                        auto result = svcctl::install(exe);
                        if (!result.ok || legacy.empty() || !svcctl::query(exe).legacy.empty()) return result;
                        // The earlier package is gone: remove its service program and scripts.
                        std::wstring names;
                        for (const auto& command : legacy)
                            for (const auto& name : update::removeLegacyFiles(svcctl::commandExecutable(command))) names += (names.empty() ? L"" : L"、") + name;
                        if (!names.empty()) result.message += L"；已删除旧版文件：" + names;
                        return result;
                    });
                }) + 8;
        if (info_.installed && info_.pointsHere) {
            if (running) {
                x += host.uiButton(IdRestart, x, by, L"重启", ButtonStyle::Normal, [this, &host] { run(host, L"正在重启服务…", [] { return svcctl::restart(); }); }) + 8;
                x += host.uiButton(IdStop, x, by, L"停止", ButtonStyle::Normal, [this, &host] { run(host, L"正在停止服务…", [] { return svcctl::stop(); }); }) + 8;
            } else if (info_.state == SERVICE_STOPPED) {
                x += host.uiButton(IdStart, x, by, L"启动", needInstall ? ButtonStyle::Normal : ButtonStyle::Primary,
                    [this, &host] { run(host, L"正在启动服务…", [] { return svcctl::start(); }); }) + 8;
            }
        }
        if (info_.installed) {
            const std::wstring label = confirmUninstall_ ? L"再次点击确认卸载" : L"卸载";
            const float w = host.uiButtonWidth(label);
            host.uiButton(IdUninstall, 1239 - w, by, label, ButtonStyle::Danger, [this, &host] {
                if (!confirmUninstall_) { confirmUninstall_ = true; host.uiInvalidate(); return; }
                run(host, L"正在卸载服务…", [] { return svcctl::uninstall(); });
            });
        }
    }
    return y + h;
}

float ServicePanel::paintUpdate(UiHost& host, float y) {
    auto& c = host.uiCanvas();
    const auto& t = host.uiTheme();
    host.uiSection(y, L"程序版本"); y += 27.4f;
    const bool pending = !pendingPath_.empty();
    const float h = pending ? 134.f : 112.f;
    c.insetRing(box(864, y, 392, h), 12, t.line, 1);
    const std::wstring current = L"v" + update::text(update::currentVersion());
    if (!pending) {
        c.text(L"当前版本", sans(13, 500), 881, y + 22, t.text);
        c.text(current, mono(12.5f), 1239, y + 22, t.text2, Align::Right);
        c.text(fitPath(c, executable_, mono(11), 358), mono(11), 881, y + 44, t.text3);
        if (!busy_) host.uiButton(IdUpdatePick, 881, y + 64, L"从文件更新…", ButtonStyle::Normal, [this, &host] { pickUpdate(host); });
        return y + h;
    }
    const std::wstring target = L"v" + update::text(pendingInfo_.version);
    const std::wstring title = pendingOffer_ == update::Offer::Downgrade ? L"降级到 " + target
                             : pendingOffer_ == update::Offer::Reinstall ? L"重新覆盖 " + target + L"（版本号相同）"
                                                                          : L"更新到 " + target;
    c.text(title, sans(13, 500), 881, y + 22, pendingOffer_ == update::Offer::Downgrade ? t.danger : t.text);
    c.text(L"当前 " + current, sans(12.5f), 1239, y + 22, t.text3, Align::Right);
    c.text(fitPath(c, pendingPath_, mono(11), 358), mono(11), 881, y + 44, t.text3);
    c.text(L"服务与连发会停止几秒，完成后自动重新打开；设置保持不变", sans(11.5f), 881, y + 66, t.text2, Align::Left, 358);
    if (!busy_) {
        const float by = y + 86;
        const float w = host.uiButton(IdUpdateConfirm, 881, by, pendingOffer_ == update::Offer::Downgrade ? L"确认降级" : L"确认更新",
            ButtonStyle::Primary, [this, &host] { confirmUpdate(host); });
        host.uiButton(IdUpdateCancel, 881 + w + 8, by, L"取消", ButtonStyle::Normal, [this, &host] { cancelUpdate(); host.uiInvalidate(); });
    }
    return y + h;
}

float ServicePanel::paintGame(UiHost& host, float y) {
    auto& c = host.uiCanvas();
    const auto& t = host.uiTheme();
    host.uiSection(y, L"游戏检测"); y += 27.4f;
    c.insetRing(box(864, y, 392, 132), 12, t.line, 1);
    const auto label = [&](float ry, const wchar_t* text) { c.text(text, sans(13, 500), 881, ry + 22, t.text); };
    label(y, L"游戏进程");
    {
        // A select-style field: opens the running-process picker (typing a name is still possible there).
        const auto r = box(1239 - 150, y + 6, 150, 32);
        const bool hot = host.uiHovered(IdGame);
        c.fillRound(r, 8, hot ? t.surface2 : t.surface); c.insetRing(r, 8, hot ? t.text3 : t.line2, 1);
        c.text(options_.gameProcess, mono(12.5f), r.left + 10, y + 22, t.text, Align::Left, 150 - 40);
        c.icon(Icon::ChevronDown, r.right - 25, y + 14.5f, 15, hot ? t.text : t.text3);
        host.uiHit(r, IdGame, [this, &host] { openProcessPicker(host, GameProcess); });
    }
    c.fill(box(865, y + 43.5f, 390, 1), t.line);
    const auto unit = [&](float ry) { c.text(L"秒", sans(12), 1224, ry + 22, t.text3); };
    label(y + 44, L"检测间隔");
    host.uiStepper(IdPollDec, IdPollInc, 1074, y + 50, 140, std::to_wstring(options_.pollSeconds),
        [this, &host] { if (options_.pollSeconds > kMinPollSeconds) { --options_.pollSeconds; changed(host); } },
        [this, &host] { if (options_.pollSeconds < kMaxPollSeconds) { ++options_.pollSeconds; changed(host); } });
    unit(y + 44);
    c.fill(box(865, y + 87.5f, 390, 1), t.line);
    label(y + 88, L"启动后延迟处理");
    host.uiStepper(IdDelayDec, IdDelayInc, 1074, y + 94, 140, std::to_wstring(options_.actionDelaySeconds),
        [this, &host] { options_.actionDelaySeconds = options_.actionDelaySeconds >= 10 ? options_.actionDelaySeconds - 10 : 0; changed(host); },
        [this, &host] { options_.actionDelaySeconds = std::min(kMaxActionDelaySeconds, options_.actionDelaySeconds + 10); changed(host); });
    unit(y + 88);
    return y + 132;
}

float ServicePanel::paintOptimize(UiHost& host, float y) {
    auto& c = host.uiCanvas();
    const auto& t = host.uiTheme();
    host.uiSection(y, L"进程优化"); y += 27.4f;
    const float h = 44 + 44 + 60;
    c.insetRing(box(864, y, 392, h), 12, t.line, 1);
    c.text(L"保持游戏优先级", sans(13, 500), 881, y + 22, t.text);
    host.uiSwitch(IdPrioritySw, 1203, y + 12, options_.optimizeGamePriority, [this, &host] { options_.optimizeGamePriority = !options_.optimizeGamePriority; changed(host); });
    c.fill(box(865, y + 43.5f, 390, 1), t.line);
    c.setOpacity(options_.optimizeGamePriority ? 1.f : .45f);
    c.text(L"优先级", sans(13, 500), 881, y + 66, t.text);
    const float sy = y + 48;
    const auto seg = box(1088, sy, 168 - 17, 36);
    c.fillRound(seg, 9, t.surface2); c.insetRing(seg, 9, t.line, 1);
    // The selected pill slides between the two options.
    const float k = host.uiAnim(kIdBase + 90, options_.aboveNormalPriority ? 1.f : 0.f, 180);
    const auto pill = box(lerp(1091, 1154, k), sy + 3, lerp(60, 82, k), 30);
    c.shadow(pill, 7, rgb(0, .18f), 1, 2); c.fillRound(pill, 7, t.surface); c.ring(pill, 7, t.line2, 1);
    const auto segment = [&](int id, float x, float w, const std::wstring& text, bool selected, bool above) {
        const auto r = box(x, sy + 3, w, 30);
        c.text(text, sans(12.5f), x + w / 2, sy + 18, selected || host.uiHovered(id) ? t.text : t.text2, Align::Center);
        if (options_.optimizeGamePriority)
            host.uiHit(r, id, [this, &host, above] { if (options_.aboveNormalPriority != above) { options_.aboveNormalPriority = above; changed(host); } });
    };
    segment(IdPriorityNormal, 1091, 60, L"正常", !options_.aboveNormalPriority, false);
    segment(IdPriorityAbove, 1154, 82, L"高于正常", options_.aboveNormalPriority, true);
    c.setOpacity(1.f);
    c.fill(box(865, y + 87.5f, 390, 1), t.line);
    c.text(L"游戏退出后关闭 DNF 启动器", sans(13, 500), 881, y + 88 + 20, t.text);
    c.text(L"只在游戏运行过并退出 1 分钟后关闭，不影响下载与更新", sans(11.5f), 881, y + 88 + 40, t.text2, Align::Left, 310);
    host.uiSwitch(IdLauncherSw, 1203, y + 88 + 20, options_.closeLauncherAfterGame, [this, &host] { options_.closeLauncherAfterGame = !options_.closeLauncherAfterGame; changed(host); });
    return y + h;
}

float ServicePanel::paintList(UiHost& host, float y, int index) {
    auto& c = host.uiCanvas();
    const auto& t = host.uiTheme();
    const auto& text = kLists[index];
    auto& items = list(index);
    const float left = 881, width = 358, fieldW = 150;
    // Lay chips out first so the block height is known before anything is drawn.
    struct Chip { float x, y, w; std::wstring label; };
    std::vector<Chip> chips;
    float px = left, py = 0;
    for (const auto& item : items) {
        const float w = std::min(width, 10 + c.textWidth(item, mono(12)) + 2 + 20 + 3);
        if (px + w > left + width && px > left) { px = left; py += 34; }
        chips.push_back({px, py, w, item});
        px += w + 6;
    }
    if (px + fieldW > left + width && px > left) { px = left; py += 34; }
    const float fieldX = px, fieldY = py;
    const float contentH = py + 28;
    const float top = y + 15 + 37.5f + 12;
    const float h = 15 + 37.5f + 12 + contentH + 15;
    c.insetRing(box(864, y, 392, h), 12, t.line, 1);
    c.text(text.title, sans(13, 500), 881, y + 15 + 9.4f, t.text);
    c.text(text.description, sans(11.5f), 881, y + 15 + 18.85f + 2 + 8.35f, t.text2, Align::Left, width);
    for (size_t i = 0; i < chips.size(); ++i) {
        const auto& chip = chips[i];
        const auto r = box(chip.x, top + chip.y, chip.w, 28);
        c.keyShape(r, 7, t.capTop, t.capSide, 2); c.ring(r, 7, t.line2, 1);
        c.text(chip.label, mono(12), chip.x + 10, top + chip.y + 13, t.text, Align::Left, chip.w - 35);
        const auto remove = box(r.right - 23, top + chip.y + 3, 20, 20);
        const int id = IdRemove + index * 100 + int(i);
        if (locked(index, chip.label)) {
            // The client itself always starts with the game: shown with a lock, not removable.
            // A 1px outline body vanishes at chip size, so the body is filled and only the shackle is stroked.
            c.icon(Icon::Lock, remove.left + 3, remove.top + 2.5f, 14, t.text3);
            c.fillRound(box(remove.left + 6, remove.top + 8.5f, 8, 6.5f), 1.5f, t.text3);
            host.uiHit(remove, IdLocked, [] {});
            continue;
        }
        if (host.uiHovered(id)) c.fillRound(remove, 5, t.surface3);
        c.icon(Icon::Close, remove.left + 4, remove.top + 4, 12, host.uiHovered(id) ? t.text : t.text3);
        host.uiHit(remove, id, [this, &host, index, i] { removeItem(index, i); changed(host); });
    }
    {
        // Add button: running-process picker for names, file dialog for programs.
        const auto r = box(fieldX, top + fieldY, fieldW, 28);
        const int id = IdAdd + index;
        const bool hot = host.uiHovered(id);
        c.strokeRound(D2D1::RectF(r.left + .5f, r.top + .5f, r.right - .5f, r.bottom - .5f), 7, hot ? t.text3 : t.line2, 1, true);
        const Color fg = hot ? t.text : t.text2;
        const float tw = c.textWidth(text.placeholder, sans(12.5f));
        const float x0 = r.left + (fieldW - tw - 19) / 2;
        c.icon(Icon::Plus, x0, r.top + 6.5f, 15, fg);
        c.text(text.placeholder, sans(12.5f), x0 + 19, r.top + 14, fg);
        host.uiHit(r, id, [this, &host, index] {
            if (index == AutoStart) pickProgram(host); else openProcessPicker(host, index);
        });
    }
    return y + h;
}

float ServicePanel::paint(UiHost& host, float y) {
    auto& c = host.uiCanvas();
    const auto& t = host.uiTheme();
    y = paintStatus(host, y) + 24;
    y = paintUpdate(host, y) + 24;
    y = paintGame(host, y) + 24;
    y = paintOptimize(host, y) + 24;
    host.uiSection(y, L"进程列表"); y += 27.4f;
    for (int i = 0; i < ListCount; ++i) y = paintList(host, y, i) + 12;
    y += 8;
    const auto logPath = fs::join(fs::join(fs::directoryOf(executable_), kLogDirectory), kServiceLogName);
    host.uiLink(IdLog, 864, y + 10, L"打开服务日志", [logPath, &host] {
        const auto target = fs::isFile(logPath) ? logPath : fs::directoryOf(logPath);
        if (!fs::exists(target)) { host.uiHint(L"服务尚未写入日志，安装并启动服务后再查看。"); return; }
        ShellExecuteW(host.uiWindow(), L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    });
    c.text(dirty_ ? L"正在保存…" : L"修改自动保存到 config.json，服务约 1 秒内生效", sans(12), 1256, y + 10, t.text3, Align::Right);
    return y + 24;
}

} // namespace dafclient
