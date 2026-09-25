#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <userenv.h>
#include <wtsapi32.h>
#include "process_util.h"
#include "win_fs.h"
#include <cwctype>

namespace dafclient::svc {
namespace {
struct Handle {
    HANDLE h = nullptr;
    Handle() = default;
    explicit Handle(HANDLE value) : h(value == INVALID_HANDLE_VALUE ? nullptr : value) {}
    ~Handle() { if (h) CloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const { return h != nullptr; }
};
std::wstring lower(std::wstring s) { for (auto& c : s) c = wchar_t(towlower(c)); return s; }
}

std::vector<ProcessEntry> snapshotProcesses() {
    std::vector<ProcessEntry> result;
    Handle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snap) return result;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    for (BOOL ok = Process32FirstW(snap.h, &entry); ok; ok = Process32NextW(snap.h, &entry)) {
        ProcessEntry e;
        e.pid = entry.th32ProcessID; e.parent = entry.th32ParentProcessID; e.name = entry.szExeFile;
        if (!ProcessIdToSessionId(e.pid, &e.session)) e.session = 0;
        result.push_back(std::move(e));
    }
    return result;
}
const std::vector<ProcessEntry>& Snapshot::get() {
    if (!taken_) { entries_ = snapshotProcesses(); taken_ = true; }
    return entries_;
}

std::wstring processKey(const std::wstring& configured) {
    std::wstring name = configured;
    while (!name.empty() && iswspace(name.front())) name.erase(name.begin());
    while (!name.empty() && iswspace(name.back())) name.pop_back();
    const auto slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos) name = name.substr(slash + 1);
    name = lower(name);
    if (name.size() > 4 && name.compare(name.size() - 4, 4, L".exe") == 0) name.resize(name.size() - 4);
    return name;
}
bool matches(const ProcessEntry& entry, const std::wstring& key) { return !key.empty() && processKey(entry.name) == key; }
bool runningInSession(const std::vector<ProcessEntry>& entries, const std::wstring& key, DWORD session, DWORD pid) {
    for (const auto& e : entries) if (e.session == session && (!pid || e.pid == pid) && matches(e, key)) return true;
    return false;
}

std::wstring imagePath(DWORD pid) {
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process) return L"";
    std::wstring path(32768, L'\0');
    DWORD size = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process.h, 0, path.data(), &size)) return L"";
    path.resize(size);
    return path;
}
bool startTime(DWORD pid, unsigned long long& fileTime) {
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!process || !GetProcessTimes(process.h, &created, &exited, &kernel, &user)) return false;
    fileTime = (static_cast<unsigned long long>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    return true;
}
std::wstring resolvePath(const std::wstring& configured, const std::wstring& baseDirectory) {
    std::wstring text = configured;
    while (!text.empty() && iswspace(text.front())) text.erase(text.begin());
    while (!text.empty() && iswspace(text.back())) text.pop_back();
    if (text.size() >= 2 && text.front() == L'"' && text.back() == L'"') text = text.substr(1, text.size() - 2);
    const DWORD needed = ExpandEnvironmentStringsW(text.c_str(), nullptr, 0);
    if (needed) {
        std::wstring expanded(needed, L'\0');
        const DWORD n = ExpandEnvironmentStringsW(text.c_str(), expanded.data(), needed);
        if (n && n <= needed) { expanded.resize(n - 1); text = expanded; }
    }
    const bool absolute = (text.size() >= 3 && text[1] == L':' && (text[2] == L'\\' || text[2] == L'/')) ||
        (text.size() >= 2 && (text[0] == L'\\' || text[0] == L'/') && (text[1] == L'\\' || text[1] == L'/'));
    return fs::fullPath(absolute ? text : fs::join(baseDirectory, text));
}

Launch launchInSession(const std::wstring& application, DWORD session, bool elevate) {
    Launch result;
    HANDLE raw = nullptr;
    if (!WTSQueryUserToken(session, &raw)) { result.error = GetLastError(); result.step = L"WTSQueryUserToken"; return result; }
    Handle user(raw), linked, primary;
    HANDLE source = user.h;
    TOKEN_ELEVATION_TYPE type = TokenElevationTypeDefault; DWORD length = 0;
    if (elevate && GetTokenInformation(user.h, TokenElevationType, &type, sizeof(type), &length)) {
        if (type == TokenElevationTypeLimited) {
            TOKEN_LINKED_TOKEN link{};
            if (GetTokenInformation(user.h, TokenLinkedToken, &link, sizeof(link), &length) && link.LinkedToken) {
                linked.h = link.LinkedToken; source = linked.h; result.elevated = true;
            }
        } else if (type == TokenElevationTypeFull) result.elevated = true;
    }
    if (!DuplicateTokenEx(source, MAXIMUM_ALLOWED, nullptr, SecurityImpersonation, TokenPrimary, &primary.h)) {
        result.error = GetLastError(); result.step = L"DuplicateTokenEx"; return result;
    }
    void* environment = nullptr;
    DWORD flags = 0;
    if (CreateEnvironmentBlock(&environment, primary.h, FALSE)) flags |= CREATE_UNICODE_ENVIRONMENT;
    else environment = nullptr;
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    wchar_t desktop[] = L"winsta0\\default";
    startup.lpDesktop = desktop;
    std::wstring command = L"\"" + application + L"\"";
    const std::wstring directory = fs::directoryOf(application);
    PROCESS_INFORMATION info{};
    const BOOL ok = CreateProcessAsUserW(primary.h, application.c_str(), command.data(), nullptr, nullptr, FALSE,
        flags, environment, directory.c_str(), &startup, &info);
    const DWORD error = ok ? 0 : GetLastError();
    if (environment) DestroyEnvironmentBlock(environment);
    if (!ok) { result.error = error; result.step = L"CreateProcessAsUserW"; return result; }
    CloseHandle(info.hThread); CloseHandle(info.hProcess);
    result.pid = info.dwProcessId;
    return result;
}

bool waitExit(DWORD pid, DWORD waitMs) {
    Handle process(OpenProcess(SYNCHRONIZE, FALSE, pid));
    if (!process) return GetLastError() == ERROR_INVALID_PARAMETER; // No such process any more.
    return WaitForSingleObject(process.h, waitMs) == WAIT_OBJECT_0;
}
bool terminate(DWORD pid, DWORD waitMs, DWORD& error) {
    error = 0;
    Handle process(OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid));
    if (!process) { error = GetLastError(); return error == ERROR_INVALID_PARAMETER; }
    if (!TerminateProcess(process.h, 1)) {
        error = GetLastError();
        if (WaitForSingleObject(process.h, 0) == WAIT_OBJECT_0) return true; // Exited meanwhile.
        return false;
    }
    return WaitForSingleObject(process.h, waitMs) == WAIT_OBJECT_0;
}
bool restrictToLastCpu(DWORD pid, DWORD& error) {
    error = 0;
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION, FALSE, pid));
    if (!process) { error = GetLastError(); return false; }
    DWORD_PTR mask = 0, system = 0;
    if (!GetProcessAffinityMask(process.h, &mask, &system)) { error = GetLastError(); return false; }
    if (!mask) return true;
    int highest = 63;
    while (highest > 0 && !(mask & (DWORD_PTR(1) << highest))) --highest;
    if (!SetProcessAffinityMask(process.h, DWORD_PTR(1) << highest)) { error = GetLastError(); return false; }
    return true;
}
bool setVeryLowIoPriority(DWORD pid, long& status) {
    using SetInformation = long(WINAPI*)(HANDLE, int, void*, ULONG);
    static const auto setInformation = reinterpret_cast<SetInformation>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationProcess")));
    status = 0;
    if (!setInformation) { status = -1; return false; }
    Handle process(OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid));
    if (!process) { status = long(GetLastError()); return false; }
    int priority = 0; // IoPriorityVeryLow
    status = setInformation(process.h, 0x21 /* ProcessIoPriority */, &priority, sizeof(priority));
    return status == 0;
}

} // namespace dafclient::svc
