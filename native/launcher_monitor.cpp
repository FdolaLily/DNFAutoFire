#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "launcher_monitor.h"
#include "json.h"
#include "win_fs.h"
#include <algorithm>
#include <cwctype>

namespace dafclient::svc {
namespace {
std::wstring lower(std::wstring s) { for (auto& c : s) c = wchar_t(towlower(c)); return s; }
bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool identifier(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

// Finds every `"key" : <value>` occurrence; returns [valueStart, valueEnd) spans.
// Boolean values must be true/false; string values a complete JSON string literal.
struct Span { size_t begin, end; };
std::vector<Span> valueSpans(const std::string& text, const std::string& key, bool stringValue) {
    std::vector<Span> spans;
    const std::string token = "\"" + key + "\"";
    for (size_t at = text.find(token); at != std::string::npos; at = text.find(token, at + 1)) {
        size_t p = at + token.size();
        while (p < text.size() && space(text[p])) ++p;
        if (p >= text.size() || text[p] != ':') continue;
        ++p;
        while (p < text.size() && space(text[p])) ++p;
        if (stringValue) {
            if (p >= text.size() || text[p] != '"') continue;
            size_t q = p + 1;
            while (q < text.size() && text[q] != '"') q += text[q] == '\\' ? 2 : 1;
            if (q >= text.size()) continue;
            spans.push_back({p, q + 1});
        } else {
            for (const char* word : {"true", "false"}) {
                const size_t n = std::char_traits<char>::length(word);
                if (text.compare(p, n, word) == 0 && (p + n >= text.size() || !identifier(text[p + n]))) { spans.push_back({p, p + n}); break; }
            }
        }
    }
    return spans;
}
bool settingsState(const json::Value& root, bool& showOnExit, std::wstring& closeAction) {
    const json::Value& settings = root.at(L"app_settings");
    const json::Value* show = settings.find(L"showMainWindowOnGameExit");
    const json::Value* close = settings.find(L"MainWindowCloseAction");
    if (!settings.isObject() || !show || !show->isBool() || !close || !close->isString()) return false;
    showOnExit = show->boolean(true); closeAction = close->str();
    return true;
}
std::wstring widenLossy(const std::string& s) { try { return json::widen(s); } catch (...) { return L"?"; } }
}

LauncherConfigStatus rewriteLauncherConfig(const std::string& in, std::string& out, std::string& error) {
    json::Value root;
    try { root = json::parse(in); } catch (const std::exception& e) { error = e.what(); return LauncherConfigStatus::Retry; }
    bool show = true; std::wstring action;
    if (!settingsState(root, show, action)) { error = "the launcher config does not contain valid exit-window settings"; return LauncherConfigStatus::Retry; }
    const bool showOk = !show, actionOk = action == L"minimize";
    if (showOk && actionOk) return LauncherConfigStatus::AlreadyConfigured;
    const auto showSpans = valueSpans(in, "showMainWindowOnGameExit", false);
    const auto actionSpans = valueSpans(in, "MainWindowCloseAction", true);
    if (showSpans.size() != 1 || actionSpans.size() != 1) {
        error = "the launcher exit-window settings could not be uniquely located"; return LauncherConfigStatus::Retry;
    }
    // Replace the later span first so the earlier offsets stay valid.
    std::vector<std::pair<Span, std::string>> edits;
    if (!showOk) edits.push_back({showSpans[0], "false"});
    if (!actionOk) edits.push_back({actionSpans[0], "\"minimize\""});
    std::sort(edits.begin(), edits.end(), [](const auto& a, const auto& b) { return a.first.begin > b.first.begin; });
    std::string updated = in;
    for (const auto& edit : edits) updated.replace(edit.first.begin, edit.first.end - edit.first.begin, edit.second);
    try {
        const auto verify = json::parse(updated);
        if (!settingsState(verify, show, action) || show || action != L"minimize") { error = "the launcher config update could not be verified"; return LauncherConfigStatus::Retry; }
    } catch (const std::exception& e) { error = e.what(); return LauncherConfigStatus::Retry; }
    out = std::move(updated);
    return LauncherConfigStatus::Updated;
}

std::wstring urlHost(const std::wstring& url) {
    const auto scheme = url.find(L"://");
    if (scheme == std::wstring::npos || scheme == 0) return L"";
    for (size_t i = 0; i < scheme; ++i) if (!iswalpha(url[i]) && !(i && (iswdigit(url[i]) || url[i] == L'+' || url[i] == L'-' || url[i] == L'.'))) return L"";
    std::wstring authority = url.substr(scheme + 3, url.find_first_of(L"/?#", scheme + 3) - (scheme + 3));
    const auto at = authority.rfind(L'@');
    if (at != std::wstring::npos) authority = authority.substr(at + 1);
    if (!authority.empty() && authority.front() == L'[') return lower(authority.substr(0, authority.find(L']') + 1));
    return lower(authority.substr(0, authority.find(L':')));
}
bool isDnfUpdateManifest(const std::string& filelistJson) {
    try {
        const auto root = json::parse(filelistJson);
        const json::Value& files = root.at(L"filelist");
        if (!files.isArray()) return false;
        for (const auto& entry : files.items())
            for (const wchar_t* field : {L"url", L"bkurl", L"curl", L"bkcurl"})
                if (urlHost(entry.at(field).string()) == L"dnf.gcloudcdn.qq.com") return true;
    } catch (...) {}
    return false;
}
bool launcherCloseDue(bool gameWasObserved, bool gameRunning, unsigned long long msSinceGameExit) {
    // A launcher only opened for downloads or updates (the game never ran) is left alone.
    return !gameRunning && gameWasObserved && msSinceGameExit >= kLauncherCloseDelayMs;
}

bool LauncherMonitor::isLauncher(const std::wstring& path) {
    if (lower(fs::fileName(path)) != L"client.exe") return false;
    const auto directory = fs::directoryOf(path);
    const auto manifest = fs::join(directory, L"filelist.json");
    const auto stamp = fs::stamp(manifest);
    auto& cached = identities_[lower(path)];
    if (stamp.exists && cached.manifestTime == stamp.writeTime + stamp.size) return cached.launcher;
    cached.manifestTime = stamp.exists ? stamp.writeTime + stamp.size : 0;
    cached.launcher = false;
    if (!stamp.exists || stamp.size > 1024 * 1024) return false;
    if (!fs::isFile(fs::join(directory, L"resources\\app.asar")) || !fs::isFile(fs::join(directory, L"uninstall.json"))) return false;
    try { cached.launcher = isDnfUpdateManifest(fs::read(manifest, 1024 * 1024)); } catch (...) { cached.launcher = false; }
    return cached.launcher;
}

LauncherConfigStatus LauncherMonitor::configure(const std::wstring& executable, std::wstring& configPath, std::string& error) {
    configPath.clear();
    std::wstring directory = fs::directoryOf(fs::fullPath(executable));
    for (int level = 0; level < 3 && !directory.empty(); ++level) {
        const auto candidate = fs::join(directory, L"AppData\\config.json");
        if (fs::isFile(candidate)) { configPath = candidate; break; }
        const auto parent = fs::directoryOf(directory);
        if (parent == directory || parent == L".") break;
        directory = parent;
    }
    if (configPath.empty()) { error = "no AppData\\config.json was found near the launcher executable"; return LauncherConfigStatus::Retry; }
    const auto stamp = fs::stamp(configPath);
    if (!stamp.exists || stamp.size == 0 || stamp.size > 1024 * 1024) { error = "the launcher config file size is invalid"; return LauncherConfigStatus::Retry; }
    std::string text, updated;
    try { text = fs::read(configPath, 1024 * 1024); } catch (const std::exception& e) { error = e.what(); return LauncherConfigStatus::Retry; }
    const auto status = rewriteLauncherConfig(text, updated, error);
    if (status != LauncherConfigStatus::Updated) return status;
    // Replace in place so the launcher's file keeps its ACL and attributes.
    const auto temporary = configPath + L"." + std::to_wstring(GetTickCount64()) + L".tmp";
    try { fs::writeAtomic(temporary, updated); } catch (const std::exception& e) { error = e.what(); return LauncherConfigStatus::Retry; }
    if (!ReplaceFileW(configPath.c_str(), temporary.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
        error = "ReplaceFile failed: " + std::to_string(GetLastError());
        DeleteFileW(temporary.c_str());
        return LauncherConfigStatus::Retry;
    }
    try {
        std::string again, ignored, verifyError;
        again = fs::read(configPath, 1024 * 1024);
        if (rewriteLauncherConfig(again, ignored, verifyError) != LauncherConfigStatus::AlreadyConfigured) {
            error = "the launcher config update could not be verified"; return LauncherConfigStatus::Retry;
        }
    } catch (const std::exception& e) { error = e.what(); return LauncherConfigStatus::Retry; }
    return LauncherConfigStatus::Updated;
}

int LauncherMonitor::stopVerified(const std::vector<Candidate>& group, const Key& key) {
    int stopped = 0;
    for (const auto& candidate : group) {
        HANDLE process = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, candidate.pid);
        if (!process) continue;
        DWORD session = 0;
        unsigned long long started = 0;
        std::wstring path(32768, L'\0'); DWORD size = DWORD(path.size());
        const bool identified = WaitForSingleObject(process, 0) == WAIT_TIMEOUT &&
            ProcessIdToSessionId(candidate.pid, &session) && session == key.session &&
            QueryFullProcessImageNameW(process, 0, path.data(), &size) &&
            (path.resize(size), lower(fs::fullPath(path)) == key.path) &&
            startTime(candidate.pid, started) && started == candidate.started &&
            isLauncher(path);
        if (identified && TerminateProcess(process, 1)) ++stopped;
        CloseHandle(process);
    }
    return stopped;
}

void LauncherMonitor::step(const ServiceOptions& options, Snapshot& snapshot, unsigned long long now) {
    if (!options.closeLauncherAfterGame) {
        if (enabled_) log_.info(L"DNF 启动器监控已按配置关闭");
        enabled_ = false; launchers_.clear();
        return;
    }
    if (!enabled_) log_.info(L"DNF 启动器监控已按配置开启");
    enabled_ = true;
    const auto& entries = snapshot.get();
    std::map<Key, std::vector<Candidate>> groups;
    for (const auto& e : entries) {
        if (e.session == 0 || lower(e.name) != L"client.exe") continue;
        const auto path = imagePath(e.pid);
        if (path.empty() || !isLauncher(path)) continue;
        unsigned long long started = 0;
        if (!startTime(e.pid, started)) continue;
        const Key key{lower(fs::fullPath(path)), e.session};
        groups[key].push_back({key, e.pid, started});
    }
    for (auto it = launchers_.begin(); it != launchers_.end();) {
        if (groups.count(it->first)) { ++it; continue; }
        // The launcher closed: make sure it did not restore its settings on the way out.
        std::wstring configPath; std::string error;
        const auto status = configure(it->first.path, configPath, error);
        if (status == LauncherConfigStatus::Updated) log_.info(L"启动器关闭后已重新设置其退出行为：" + configPath);
        else if (status == LauncherConfigStatus::Retry) log_.warn(L"启动器关闭后无法核对其设置：" + widenLossy(error));
        it = launchers_.erase(it);
    }
    const auto gameKey = processKey(options.gameProcess);
    for (const auto& [key, group] : groups) {
        auto found = launchers_.find(key);
        if (found == launchers_.end()) {
            found = launchers_.emplace(key, State{}).first;
            log_.info(L"检测到 DNF 启动器（会话 " + std::to_wstring(key.session) + L"）：" + key.path);
        }
        State& state = found->second;
        if (!state.configHandled && now >= state.nextConfigAttempt) {
            std::wstring configPath; std::string error;
            const auto status = configure(key.path, configPath, error);
            if (status == LauncherConfigStatus::Updated) {
                state.configHandled = true;
                log_.info(L"已设置启动器在游戏退出后保持最小化到托盘：" + configPath);
            } else if (status == LauncherConfigStatus::AlreadyConfigured) state.configHandled = true;
            else {
                state.nextConfigAttempt = now + 5000;
                log_.warn(L"暂时无法设置启动器的退出行为，稍后重试：" + widenLossy(error));
            }
        }
        const bool gameRunning = runningInSession(entries, gameKey, key.session);
        if (gameRunning) { state.gameWasObserved = true; state.gameExited = false; }
        else if (state.gameWasObserved && !state.gameExited) {
            state.gameExited = true; state.gameExitedAt = now;
            log_.info(L"会话 " + std::to_wstring(key.session) + L" 中的游戏已退出，一分钟后仍在运行的 DNF 启动器将被关闭");
        }
        if (!launcherCloseDue(state.gameWasObserved, gameRunning, state.gameExited ? now - state.gameExitedAt : 0)) continue;
        if (now < state.nextCloseAttempt) continue;
        state.nextCloseAttempt = now + 10000;
        const int stopped = stopVerified(group, key);
        if (stopped > 0) log_.info(L"游戏退出后已结束 " + std::to_wstring(stopped) + L" 个通过身份校验的 DNF 启动器进程：" + key.path);
        else log_.warn(L"DNF 启动器应当关闭，但没有进程通过最终身份校验，稍后重试：" + key.path);
    }
}

} // namespace dafclient::svc
