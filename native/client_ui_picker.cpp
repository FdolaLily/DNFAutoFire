#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "client_ui_picker.h"
#include "client_ui_host.h"
#include "process_util.h"
#include "win_fs.h"
#include <shobjidl.h>
#include <algorithm>
#include <cwctype>
#include <map>

namespace dafclient {
using namespace gfx;
namespace {
enum : int { IdScrim = ProcessPicker::kIdBase, IdCard, IdClose, IdSearch, IdRefresh, IdRow = ProcessPicker::kIdBase + 20 };
constexpr float kLeft = 856, kTop = 124, kWidth = 408, kHeight = 660;
constexpr float kListTop = 262, kListBottom = kTop + kHeight - 12, kRow = 44;
D2D1_RECT_F box(float x, float y, float w, float h) { return D2D1::RectF(x, y, x + w, y + h); }
std::wstring lower(std::wstring s) { for (auto& c : s) c = wchar_t(towlower(c)); return s; }
std::wstring trim(std::wstring s) {
    while (!s.empty() && iswspace(s.front())) s.erase(s.begin());
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    return s;
}
bool hasExeImage(const std::wstring& name) {
    const auto n = lower(name);
    return n.size() > 4 && n.compare(n.size() - 4, 4, L".exe") == 0;
}
} // namespace

std::vector<RunningProcess> runningProcesses() {
    std::map<std::wstring, RunningProcess> byName;
    const DWORD self = GetCurrentProcessId();
    for (const auto& e : svc::snapshotProcesses()) {
        if (e.pid <= 4 || e.pid == self || !hasExeImage(e.name)) continue;
        auto& item = byName[lower(e.name)];
        if (!item.count) { item.name = e.name; item.background = true; }
        ++item.count;
        if (e.session != 0) item.background = false;
        if (item.path.empty()) item.path = svc::imagePath(e.pid);
    }
    std::vector<RunningProcess> result;
    result.reserve(byName.size());
    for (auto& entry : byName) result.push_back(std::move(entry.second)); // std::map keeps them sorted.
    return result;
}

void ProcessPicker::reload() { items_ = runningProcesses(); }

void ProcessPicker::open(UiHost& host, std::wstring title, std::wstring subtitle, Choose choose, Status status, bool closeOnChoose) {
    title_ = std::move(title); subtitle_ = std::move(subtitle);
    choose_ = std::move(choose); status_ = std::move(status);
    closeOnChoose_ = closeOnChoose; filter_.clear(); scroll_ = 0;
    reload();
    active_ = true;
    focusSearch(host); // Typing starts filtering at once.
    host.uiInvalidate();
}
void ProcessPicker::focusSearch(UiHost& host) {
    host.uiFocusText(IdSearch,
        [this, &host](const std::wstring& text, bool submitted) { if (submitted) submit(host, text); },
        [this, &host](const std::wstring& text) { setFilter(trim(text)); host.uiInvalidate(); });
}
void ProcessPicker::close(UiHost& host) {
    if (!active_) return;
    // Rows stay until the fade-out has finished; they are reloaded on the next open.
    active_ = false; choose_ = {}; status_ = {};
    host.uiCancelText();
    host.uiInvalidate();
}
std::vector<const RunningProcess*> ProcessPicker::visible() const {
    std::vector<const RunningProcess*> rows;
    const auto needle = lower(filter_);
    for (const auto& item : items_)
        if (needle.empty() || lower(item.name).find(needle) != std::wstring::npos || lower(item.path).find(needle) != std::wstring::npos)
            rows.push_back(&item);
    return rows;
}
void ProcessPicker::choose(UiHost& host, const std::wstring& name) {
    auto callback = choose_;
    if (callback) callback(name);
    if (closeOnChoose_) close(host); else host.uiInvalidate();
}
bool ProcessPicker::submit(UiHost& host, const std::wstring& raw) {
    const auto text = trim(raw);
    if (text.empty()) return false;
    // A search that narrows to one selectable process picks it; otherwise the text itself is used.
    std::vector<const RunningProcess*> selectable;
    for (const auto* row : visible()) if (!status_ || status_(row->name).empty()) selectable.push_back(row);
    const bool exact = std::any_of(selectable.begin(), selectable.end(), [&](const RunningProcess* r) { return lower(r->name) == lower(text); });
    const std::wstring name = selectable.size() == 1 && !exact ? selectable.front()->name : text;
    filter_.clear(); scroll_ = 0;
    choose(host, name);
    if (active_) focusSearch(host); // Keep typing to add the next one.
    return true;
}
bool ProcessPicker::wheel(float notches) {
    if (!active_) return false;
    if (notches == 0) return true;
    const float maxScroll = std::max(0.f, contentHeight_ - (kListBottom - kListTop));
    scroll_ = std::clamp(scroll_ - notches * kRow * 2, 0.f, maxScroll);
    return true;
}

void ProcessPicker::paint(UiHost& host) {
    // Fades in with a short rise; fades out without accepting clicks.
    const float a = host.uiAnim(IdCard, active_ ? 1.f : 0.f, 180);
    if (a <= 0.001f) return;
    struct Restore { UiHost& h; ~Restore() { h.uiLayer(1.f, 0.f); h.uiHitsEnabled(true); } } restore{host};
    host.uiHitsEnabled(active_);
    auto& c = host.uiCanvas();
    const auto& t = host.uiTheme();
    const auto body = box(840, 108, 440, 692);
    host.uiLayer(a, 0.f);
    c.fill(body, t.scrim);
    host.uiHit(body, IdScrim, [this, &host] { close(host); });
    host.uiLayer(a, (1.f - a) * 16.f);
    const auto card = box(kLeft, kTop, kWidth, kHeight);
    c.shadow(card, 12, t.shadow, 24, 64);
    c.fillRound(card, 12, t.surface); c.insetRing(card, 12, t.line2, 1);
    host.uiHit(card, IdCard, [] {});

    c.text(title_, sans(15, 600), kLeft + 16, kTop + 26, t.text);
    c.text(subtitle_, sans(12), kLeft + 16, kTop + 48, t.text2, Align::Left, kWidth - 64);
    const auto closeBox = box(kLeft + kWidth - 44, kTop + 12, 32, 32);
    if (host.uiHovered(IdClose)) c.fillRound(closeBox, 8, t.surface3);
    c.icon(Icon::Close, closeBox.left + 8.5f, closeBox.top + 8.5f, 15, host.uiHovered(IdClose) ? t.text : t.text2);
    host.uiHit(closeBox, IdClose, [this, &host] { close(host); });

    host.uiTextField(IdSearch, box(kLeft + 16, kTop + 66, kWidth - 32, 34), filter_, L"搜索进程，或输入进程名后按回车添加",
        [this, &host](const std::wstring& text, bool submitted) { if (submitted) submit(host, text); },
        [this, &host](const std::wstring& text) { setFilter(trim(text)); host.uiInvalidate(); });

    const auto rows = visible();
    std::vector<const RunningProcess*> selectable;
    for (const auto* row : rows) if (!status_ || status_(row->name).empty()) selectable.push_back(row);
    const bool exact = std::any_of(selectable.begin(), selectable.end(), [&](const RunningProcess* r) { return lower(r->name) == lower(filter_); });
    const std::wstring target = selectable.size() == 1 && !exact ? selectable.front()->name : filter_;
    const std::wstring summary = filter_.empty() ? L"正在运行 " + std::to_wstring(items_.size()) + L" 个程序 · 标“后台”的是服务进程"
                                                 : L"匹配 " + std::to_wstring(rows.size()) + L" 个 · 回车添加 " + target;
    c.text(summary, sans(11.5f), kLeft + 16, kTop + 122, t.text3, Align::Left, kWidth - 90);
    const Font refreshFont = sans(12);
    const float rw = c.textWidth(L"刷新", refreshFont);
    const auto refreshBox = box(kLeft + kWidth - 16 - rw - 8, kTop + 110, rw + 8, 24);
    c.text(L"刷新", refreshFont, refreshBox.left + 4, kTop + 122, host.uiHovered(IdRefresh) ? t.text : t.text2);
    host.uiHit(refreshBox, IdRefresh, [this, &host] { reload(); host.uiInvalidate(); });

    const auto list = D2D1::RectF(kLeft + 8, kListTop, kLeft + kWidth - 8, kListBottom);
    contentHeight_ = rows.size() * kRow;
    scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, contentHeight_ - (list.bottom - list.top)));
    c.fill(D2D1::RectF(list.left + 8, list.top - 1, list.right - 8, list.top), t.line);
    if (rows.empty()) {
        c.text(filter_.empty() ? L"没有读取到进程" : L"没有匹配的进程，按回车直接添加输入的名称", sans(12.5f),
            (list.left + list.right) / 2, list.top + 48, t.text3, Align::Center);
        return;
    }
    c.pushClip(list);
    const int first = int(scroll_ / kRow);
    for (int i = first; i < int(rows.size()); ++i) {
        const float y = list.top + i * kRow - scroll_;
        if (y >= list.bottom) break;
        const auto& row = *rows[size_t(i)];
        const std::wstring status = status_ ? status_(row.name) : L"";
        const bool available = status.empty();
        const int id = IdRow + i;
        const auto r = box(list.left, y + 2, list.right - list.left, kRow - 4);
        if (available && host.uiHovered(id)) c.fillRound(r, 8, t.surface2);
        c.setOpacity(available ? 1.f : .45f);
        const std::wstring tag = !available ? status
            : (row.background ? L"后台" : L"") + std::wstring(row.count > 1 ? (row.background ? L" · ×" : L"×") + std::to_wstring(row.count) : L"");
        const float tagW = tag.empty() ? 0 : c.textWidth(tag, sans(11.5f)) + 12;
        c.text(row.name, mono(12.5f), r.left + 12, y + 15, t.text, Align::Left, r.right - r.left - 24 - tagW);
        c.text(row.path.empty() ? L"路径不可读取" : row.path, sans(11), r.left + 12, y + 31, t.text3, Align::Left, r.right - r.left - 24 - tagW);
        if (!tag.empty()) c.text(tag, sans(11.5f), r.right - 12, y + kRow / 2, available ? t.text3 : t.accentText, Align::Right);
        c.setOpacity(1.f);
        // Hit areas are limited to the visible part of the list.
        const auto area = D2D1::RectF(r.left, std::max(r.top, list.top), r.right, std::min(r.bottom, list.bottom));
        if (available && area.bottom > area.top) {
            const std::wstring name = row.name;
            host.uiHit(area, id, [this, &host, name] { choose(host, name); });
        }
    }
    c.popClip();
}

std::wstring pickProgramFile(HWND owner, const std::wstring& folder, const std::wstring& title) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, reinterpret_cast<void**>(&dialog))))
        return L"";
    const COMDLG_FILTERSPEC filters[] = {{L"程序 (*.exe)", L"*.exe"}, {L"所有文件 (*.*)", L"*.*"}};
    dialog->SetFileTypes(2, filters);
    dialog->SetTitle(title.c_str());
    FILEOPENDIALOGOPTIONS options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options)))
        dialog->SetOptions(options | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
    IShellItem* start = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_IShellItem, reinterpret_cast<void**>(&start)))) {
        dialog->SetFolder(start);
        start->Release();
    }
    std::wstring result;
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->Show(owner)) && SUCCEEDED(dialog->GetResult(&item))) {
        wchar_t* path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) { result = path; CoTaskMemFree(path); }
        item->Release();
    }
    dialog->Release();
    return result;
}

std::wstring pickFolder(HWND owner, const std::wstring& title, const std::wstring& folder) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, reinterpret_cast<void**>(&dialog))))
        return L"";
    dialog->SetTitle(title.c_str());
    FILEOPENDIALOGOPTIONS options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options)))
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
    IShellItem* start = nullptr;
    if (!folder.empty() && SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_IShellItem, reinterpret_cast<void**>(&start)))) {
        dialog->SetFolder(start);
        start->Release();
    }
    std::wstring result;
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->Show(owner)) && SUCCEEDED(dialog->GetResult(&item))) {
        wchar_t* path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) { result = path; CoTaskMemFree(path); }
        item->Release();
    }
    dialog->Release();
    return result;
}

std::wstring relativeToDirectory(const std::wstring& path, const std::wstring& baseDirectory) {
    auto base = fs::fullPath(baseDirectory);
    if (!base.empty() && base.back() != L'\\') base += L'\\';
    const auto full = fs::fullPath(path);
    if (full.size() > base.size() && lower(full.substr(0, base.size())) == lower(base)) return full.substr(base.size());
    return full;
}

} // namespace dafclient
