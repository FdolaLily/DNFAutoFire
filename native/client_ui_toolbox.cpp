#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "client_ui_toolbox.h"
#include "client_ui_picker.h"
#include "win_fs.h"
#include <shellapi.h>
#include <algorithm>

namespace dafclient {
using namespace gfx;
namespace {
enum : int {
    IdChoose = ToolboxPanel::kIdBase, IdDetect, IdOpen, IdDisable, IdRestore, IdClean, IdReset, IdRow = ToolboxPanel::kIdBase + 40
};
D2D1_RECT_F box(float x, float y, float w, float h) { return D2D1::RectF(x, y, x + w, y + h); }
std::wstring joinLabels(const std::vector<std::wstring>& labels) {
    std::wstring out;
    for (const auto& l : labels) { if (!out.empty()) out += L"、"; out += l; }
    return out;
}
constexpr wchar_t kCloseGame[] = L"请先关闭 DNF 再执行文件操作。";
}

ToolboxPanel::ToolboxPanel(Store& store) : store_(store) {}
ToolboxPanel::~ToolboxPanel() { if (worker_.joinable()) worker_.join(); }

void ToolboxPanel::refreshStates() {
    items_ = toolbox::items(toolbox::userRoots(directory_));
    states_.clear();
    for (const auto& item : items_) states_.push_back(toolbox::status(item));
}

void ToolboxPanel::opened(UiHost& host) {
    if (!detected_) { detected_ = true; detect(host); return; }
    // The remembered folder may have been moved or deleted since.
    if (!directory_.empty() && !toolbox::isGameDirectory(directory_)) { detect(host); return; }
    refreshStates();
}

void ToolboxPanel::run(UiHost& host, const wchar_t* busyText, std::function<Result()> job) {
    if (busy_) return;
    if (worker_.joinable()) worker_.join();
    busy_ = true; busyText_ = busyText; confirm_ = 0;
    HWND window = host.uiWindow();
    worker_ = std::thread([this, window, job] {
        Result result;
        try { result = job(); } catch (...) { result = {false, false, L"操作发生异常，未完成。", {}}; }
        { std::lock_guard<std::mutex> lock(mutex_); result_ = result; }
        PostMessageW(window, kToolboxDoneMessage, 0, 0);
    });
    host.uiInvalidate();
}

void ToolboxPanel::finish(UiHost& host) {
    if (worker_.joinable()) worker_.join();
    busy_ = false;
    Result result;
    { std::lock_guard<std::mutex> lock(mutex_); result = result_; }
    if (result.detection) {
        if (!result.found.directory.empty()) {
            directory_ = result.found.directory; source_ = result.found.source;
            try { store_.saveGameDirectory(directory_); } catch (...) {}
            host.uiHint(L"已识别游戏目录（" + source_ + L"）：" + directory_);
        } else {
            directory_.clear(); source_.clear();
            host.uiMessage(L"未能自动找到游戏目录，请点击“选择目录”，选择包含 DNF.exe 的游戏根目录。", true);
        }
    } else if (!result.message.empty()) {
        if (result.ok) host.uiHint(result.message); else host.uiMessage(result.message, true);
    }
    refreshStates();
    host.uiInvalidate();
}

void ToolboxPanel::detect(UiHost& host) {
    std::wstring remembered;
    try { remembered = store_.loadGameDirectory(); } catch (...) {}
    run(host, L"正在识别游戏目录…", [remembered] {
        Result r; r.detection = true; r.found = toolbox::detectGameDirectory(remembered); return r;
    });
}

bool ToolboxPanel::useDirectory(UiHost* host, const std::wstring& directory, const std::wstring& source) {
    if (!toolbox::isGameDirectory(directory)) {
        if (host) host->uiMessage(L"该目录中没有 DNF.exe，请选择游戏根目录（同时包含 DNF.exe、start 和 Pandora）。", true);
        return false;
    }
    directory_ = fs::fullPath(directory); source_ = source; detected_ = true;
    try { store_.saveGameDirectory(directory_); } catch (...) {}
    refreshStates();
    if (host) { host->uiHint(L"游戏目录已设为 " + directory_); host->uiInvalidate(); }
    return true;
}

void ToolboxPanel::chooseFolder(UiHost& host) {
    const auto chosen = pickFolder(host.uiWindow(), L"选择 DNF 游戏根目录（包含 DNF.exe）", directory_);
    if (!chosen.empty()) useDirectory(&host, chosen, L"手动选择");
}

bool ToolboxPanel::confirmed(UiHost& host, int id, const wchar_t* prompt) {
    if (confirm_ == id) { confirm_ = 0; return true; }
    confirm_ = id;
    host.uiHint(prompt);
    host.uiInvalidate();
    return false;
}

std::wstring ToolboxPanel::hoverText(int id) const {
    switch (id) {
    case IdChoose: return L"手动选择游戏根目录：同时包含 DNF.exe、start 和 Pandora 的文件夹";
    case IdDetect: return L"依次查找：上次目录、正在运行的 DNF、DNF 启动器、注册表、常见安装位置";
    case IdOpen: return L"在资源管理器中打开游戏目录";
    case IdDisable: return L"把组件改名备份为 .dnf-toolbox-disabled 并放置占位，与工具箱 8.0 脚本完全兼容";
    case IdRestore: return L"恢复由本工具（或 8.0 脚本）禁用的组件";
    case IdClean: return L"删除游戏日志、崩溃记录和用户缓存，保留 DNF.cfg；不删除任何下载或更新文件";
    case IdReset: return L"清空 LocalLow\\DNF 全部用户缓存（包括 DNF.cfg 画面设置），用于黑屏或连不上服务器";
    default: return L"";
    }
}

float ToolboxPanel::paintDirectory(UiHost& host, float y) {
    auto& c = host.uiCanvas();
    const auto& t = host.uiTheme();
    host.uiSection(y, L"游戏目录"); y += 27.4f;
    const float h = 118;
    const auto card = box(864, y, 392, h);
    c.fillRound(card, 12, t.surface2); c.insetRing(card, 12, t.line, 1);
    const bool detecting = busy_ && busyText_ == L"正在识别游戏目录…";
    const bool found = !directory_.empty();
    c.icon(Icon::Folder, 881, y + 17, 18, found ? t.accentText : t.text3);
    if (detecting) {
        c.text(busyText_, sans(14, 600), 907, y + 26, t.text);
        host.uiSpinner(907 + c.textWidth(busyText_, sans(14, 600)) + 10, y + 26);
        c.text(L"正在查找运行中的 DNF、启动器、注册表和常见安装位置", sans(12), 881, y + 52, t.text2, Align::Left, 360);
    } else if (found) {
        c.text(directory_, mono(12.5f), 907, y + 26, t.text, Align::Left, 330);
        c.text(L"来源：" + source_, sans(12), 881, y + 52, t.text2, Align::Left, 360);
    } else {
        c.text(L"未找到游戏目录", sans(14, 600), 907, y + 26, t.text);
        c.text(L"自动识别失败，请选择包含 DNF.exe 的游戏根目录", sans(12), 881, y + 52, t.text2, Align::Left, 360);
    }
    if (!busy_) {
        float x = 881;
        const float by = y + 72;
        x += host.uiButton(IdChoose, x, by, L"选择目录…", found ? ButtonStyle::Normal : ButtonStyle::Primary, [this, &host] { chooseFolder(host); }) + 8;
        x += host.uiButton(IdDetect, x, by, L"重新识别", ButtonStyle::Normal, [this, &host] {
            try { store_.saveGameDirectory(L""); } catch (...) {}
            detect(host);
        }) + 8;
        if (found) host.uiButton(IdOpen, x, by, L"打开", ButtonStyle::Normal, [this, &host] {
            ShellExecuteW(host.uiWindow(), L"open", directory_.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        });
    }
    return y + h;
}

float ToolboxPanel::paintComponents(UiHost& host, float y) {
    auto& c = host.uiCanvas();
    const auto& t = host.uiTheme();
    host.uiSection(y, L"无用组件"); y += 27.4f;
    const bool ready = !directory_.empty() && !busy_;
    unsigned disabled = 0, present = 0;
    for (const auto s : states_) { if (s == toolbox::State::Disabled) ++disabled; if (s != toolbox::State::NotInstalled) ++present; }
    host.uiHitsEnabled(ready);
    c.setOpacity(ready ? 1.f : .45f);
    float x = 864;
    x += host.uiButton(IdDisable, x, y, L"禁用无用组件", ButtonStyle::Primary, [this, &host] {
        const auto roots = toolbox::userRoots(directory_);
        run(host, L"正在禁用组件…", [roots] {
            if (toolbox::gameRunning()) return Result{false, false, kCloseGame, {}};
            const auto r = toolbox::disableAll(roots);
            std::wstring text = L"已禁用 " + std::to_wstring(r.changed) + L" 项";
            if (r.unchanged) text += L"，" + std::to_wstring(r.unchanged) + L" 项原本已禁用";
            if (r.notInstalled) text += L"，" + std::to_wstring(r.notInstalled) + L" 项未安装";
            if (!r.problems.empty()) return Result{false, false, text + L"；未能处理：" + joinLabels(r.problems) + L"（已有备份或文件被占用）", {}};
            return Result{true, false, text + L"。下载与更新组件未改动。", {}};
        });
    }) + 8;
    host.uiButton(IdRestore, x, y, L"恢复全部", ButtonStyle::Normal, [this, &host] {
        const auto roots = toolbox::userRoots(directory_);
        run(host, L"正在恢复组件…", [roots] {
            if (toolbox::gameRunning()) return Result{false, false, kCloseGame, {}};
            const auto r = toolbox::restoreAll(roots);
            std::wstring text = L"已恢复 " + std::to_wstring(r.changed) + L" 项";
            if (r.noBackup) text += L"，" + std::to_wstring(r.noBackup) + L" 项没有本工具的备份";
            if (!r.problems.empty()) return Result{false, false, text + L"；未能恢复：" + joinLabels(r.problems) + L"（原位置已被占用）", {}};
            return Result{true, false, text + L"。", {}};
        });
    });
    c.setOpacity(1.f);
    host.uiHitsEnabled(true);
    c.text(directory_.empty() ? L"先设置游戏目录" : L"已禁用 " + std::to_wstring(disabled) + L" / " + std::to_wstring(present) + L" 项",
        sans(12), 1256, y + 16, t.text3, Align::Right);
    y += 44;
    // Component list, grouped as in the script.
    const float rowH = 32;
    size_t gameRows = 0;
    for (const auto& item : items_) if (item.group == toolbox::Group::Game) ++gameRows;
    const float listH = 12 + (gameRows ? 26 + gameRows * rowH : 0) + 26 + (items_.size() - gameRows) * rowH + 8;
    c.insetRing(box(864, y, 392, listH), 12, t.line, 1);
    float ry = y + 12;
    toolbox::Group current = toolbox::Group::Tencent;
    bool first = true;
    for (size_t i = 0; i < items_.size(); ++i) {
        const auto& item = items_[i];
        if (first || item.group != current) {
            current = item.group; first = false;
            c.text(current == toolbox::Group::Game ? L"游戏目录" : L"腾讯公共组件（%APPDATA%\\Tencent）", sans(11.5f, 500), 881, ry + 12, t.text3);
            ry += 26;
        }
        const auto state = i < states_.size() ? states_[i] : toolbox::State::NotInstalled;
        const Color tagColor = state == toolbox::State::Disabled ? t.accentText : state == toolbox::State::Normal ? t.text : t.text3;
        const wchar_t* tag = state == toolbox::State::Disabled ? L"已禁用" : state == toolbox::State::Normal ? L"未禁用" : L"未安装";
        c.text(item.label, sans(13), 881, ry + rowH / 2, state == toolbox::State::NotInstalled ? t.text3 : t.text, Align::Left, 280);
        // A small LED mirrors the keycaps: lit when the component is disabled.
        const float lx = 1239 - c.textWidth(tag, sans(12)) - 10;
        const float lit = host.uiAnim(IdRow + int(i), state == toolbox::State::Disabled ? 1.f : 0.f, 260);
        c.fillCircle(lx, ry + rowH / 2, 3, mix(t.ledOff, t.led, lit));
        c.text(tag, sans(12), 1239, ry + rowH / 2, tagColor, Align::Right);
        ry += rowH;
    }
    y += listH + 10;
    c.text(L"不会改动下载与更新组件：BackgroundDownloader、Tencentdl、TenioDL、TesService、", sans(11.5f), 864, y + 8, t.text3, Align::Left, 392);
    c.text(L"QQDownload、QQMiniDL、DeskUpdate、TXPTOP、TXFTN。", sans(11.5f), 864, y + 26, t.text3, Align::Left, 392);
    return y + 36;
}

float ToolboxPanel::paintCleaning(UiHost& host, float y) {
    auto& c = host.uiCanvas();
    const auto& t = host.uiTheme();
    host.uiSection(y, L"清理与修复"); y += 27.4f;
    const float rowH = 64;
    c.insetRing(box(864, y, 392, rowH * 2), 12, t.line, 1);
    const bool ready = !directory_.empty() && !busy_;
    const auto row = [&](float ry, int id, const wchar_t* title, const wchar_t* detail, const wchar_t* label, const wchar_t* confirmLabel,
                         ButtonStyle style, std::function<void()> click) {
        c.text(title, sans(13, 500), 881, ry + 22, t.text);
        c.text(detail, sans(11.5f), 881, ry + 42, t.text2, Align::Left, 250);
        const std::wstring text = confirm_ == id ? confirmLabel : label;
        const float w = host.uiButtonWidth(text);
        host.uiHitsEnabled(ready);
        c.setOpacity(ready ? 1.f : .45f);
        host.uiButton(id, 1239 - w, ry + 16, text, confirm_ == id ? ButtonStyle::Danger : style, std::move(click));
        c.setOpacity(1.f);
        host.uiHitsEnabled(true);
    };
    row(y, IdClean, L"清理日志与缓存", L"游戏日志、崩溃记录和用户缓存，保留 DNF.cfg", L"清理", L"确认清理", ButtonStyle::Normal, [this, &host] {
        if (!confirmed(host, IdClean, L"再次点击“确认清理”删除日志与缓存（保留 DNF.cfg）。")) return;
        const auto roots = toolbox::userRoots(directory_);
        run(host, L"正在清理…", [roots] {
            if (toolbox::gameRunning()) return Result{false, false, kCloseGame, {}};
            const auto r = toolbox::cleanLogsAndCache(roots);
            std::wstring text = L"已删除 " + std::to_wstring(r.deleted) + L" 个日志与缓存文件，DNF.cfg 与下载文件保留";
            if (r.failed) text += L"；" + std::to_wstring(r.failed) + L" 个文件正在使用，未删除";
            return Result{r.failed == 0, false, text + L"。", {}};
        });
    });
    c.fill(box(865, y + rowH - .5f, 390, 1), t.line);
    row(y + rowH, IdReset, L"修复黑屏 / 连接服务器", L"清空全部用户缓存，包括 DNF.cfg 画面设置", L"修复", L"确认修复", ButtonStyle::Normal, [this, &host] {
        if (!confirmed(host, IdReset, L"再次点击“确认修复”清空 LocalLow\\DNF（DNF.cfg 画面设置会恢复默认）。")) return;
        const auto roots = toolbox::userRoots(directory_);
        run(host, L"正在清空用户缓存…", [roots] {
            if (toolbox::gameRunning()) return Result{false, false, kCloseGame, {}};
            const auto r = toolbox::resetUserCache(roots);
            std::wstring text = L"已清空 DNF 用户缓存（" + std::to_wstring(r.deleted) + L" 个文件）";
            if (r.failed) text += L"；" + std::to_wstring(r.failed) + L" 个文件正在使用，未删除";
            return Result{r.failed == 0, false, text + L"。", {}};
        });
    });
    return y + rowH * 2;
}

float ToolboxPanel::paint(UiHost& host, float y) {
    y = paintDirectory(host, y) + 24;
    y = paintComponents(host, y) + 24;
    y = paintCleaning(host, y) + 16;
    if (busy_) {
        host.uiCanvas().text(busyText_, sans(12), 864, y + 8, host.uiTheme().text3);
        host.uiSpinner(864 + host.uiCanvas().textWidth(busyText_, sans(12)) + 8, y + 8);
    }
    return y + 16;
}

} // namespace dafclient
