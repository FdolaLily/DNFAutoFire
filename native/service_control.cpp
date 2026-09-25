#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "service_control.h"
#include "app_ids.h"
#include "win_fs.h"
#include <cwctype>

namespace dafclient::svcctl {
namespace {
struct ScHandle {
    SC_HANDLE h = nullptr;
    explicit ScHandle(SC_HANDLE value = nullptr) : h(value) {}
    ~ScHandle() { if (h) CloseServiceHandle(h); }
    ScHandle(const ScHandle&) = delete;
    ScHandle& operator=(const ScHandle&) = delete;
    explicit operator bool() const { return h != nullptr; }
};
std::wstring lower(std::wstring s) { for (auto& c : s) c = wchar_t(towlower(c)); return s; }
std::wstring errorText(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring text;
    if (n && buffer) {
        text.assign(buffer, n);
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ' || text.back() == L'。')) text.pop_back();
    }
    if (buffer) LocalFree(buffer);
    return text.empty() ? L"错误码 " + std::to_wstring(code) : text + L"（" + std::to_wstring(code) + L"）";
}
Result fail(DWORD code, const std::wstring& what) { return {false, code ? what + L"：" + errorText(code) : what}; }
bool currentStatus(SC_HANDLE service, SERVICE_STATUS_PROCESS& status) {
    DWORD needed = 0;
    return QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE*>(&status), sizeof(status), &needed) != FALSE;
}
bool waitFor(SC_HANDLE service, DWORD state, DWORD timeoutMs) {
    const auto deadline = GetTickCount64() + timeoutMs;
    SERVICE_STATUS_PROCESS status{};
    while (currentStatus(service, status)) {
        if (status.dwCurrentState == state) return true;
        if (GetTickCount64() >= deadline) return false;
        Sleep(200);
    }
    return false;
}
// Stops a service and waits; true when it is stopped (or was not running).
bool stopAndWait(SC_HANDLE service, DWORD timeoutMs) {
    SERVICE_STATUS_PROCESS status{};
    if (!currentStatus(service, status)) return false;
    if (status.dwCurrentState == SERVICE_STOPPED) return true;
    if (status.dwCurrentState != SERVICE_STOP_PENDING) {
        SERVICE_STATUS ignored{};
        if (!ControlService(service, SERVICE_CONTROL_STOP, &ignored) && GetLastError() != ERROR_SERVICE_NOT_ACTIVE) return false;
    }
    return waitFor(service, SERVICE_STOPPED, timeoutMs);
}
ScHandle manager(DWORD access) { return ScHandle(OpenSCManagerW(nullptr, nullptr, access)); }
Result startAndWait(SC_HANDLE service) {
    if (!StartServiceW(service, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) return fail(GetLastError(), L"服务启动失败");
    if (!waitFor(service, SERVICE_RUNNING, 15000)) {
        SERVICE_STATUS_PROCESS status{};
        currentStatus(service, status);
        if (status.dwCurrentState == SERVICE_STOPPED)
            return {false, L"服务启动后立即停止，请查看 logs\\service.log（退出码 " + std::to_wstring(status.dwWin32ExitCode) + L"）"};
        return {false, L"服务启动超时，请稍后查看状态"};
    }
    return {true, L""};
}
} // namespace

std::wstring serviceCommand(const std::wstring& executable) {
    return L"\"" + executable + L"\" " + kServiceArgument;
}
namespace {
// Splits "\"<exe>\" args" or "<exe>.exe args" into the executable and the rest.
bool splitCommand(const std::wstring& command, std::wstring& path, std::wstring& rest) {
    std::wstring text = command;
    while (!text.empty() && iswspace(text.front())) text.erase(text.begin());
    if (!text.empty() && text.front() == L'"') {
        const auto end = text.find(L'"', 1);
        if (end == std::wstring::npos) return false;
        path = text.substr(1, end - 1); rest = text.substr(end + 1);
        return !path.empty();
    }
    const auto lowerText = lower(text);
    const auto exe = lowerText.find(L".exe");
    if (exe == std::wstring::npos) return false;
    path = text.substr(0, exe + 4); rest = text.substr(exe + 4);
    return true;
}
} // namespace
std::wstring commandExecutable(const std::wstring& command) {
    std::wstring path, rest;
    return splitCommand(command, path, rest) ? path : std::wstring();
}
bool commandPointsTo(const std::wstring& command, const std::wstring& executable) {
    std::wstring path, rest;
    if (!splitCommand(command, path, rest)) return false;
    // The service argument must be present as its own token.
    bool service = false;
    for (size_t at = 0; (at = lower(rest).find(kServiceArgument, at)) != std::wstring::npos; ++at) {
        const size_t end = at + std::wcslen(kServiceArgument);
        if ((at == 0 || iswspace(rest[at - 1])) && (end == rest.size() || iswspace(rest[end]))) { service = true; break; }
    }
    return service && fs::samePath(path, executable);
}

Info query(const std::wstring& executable) {
    Info info;
    ScHandle scm = manager(SC_MANAGER_CONNECT);
    if (!scm) { info.error = GetLastError(); return info; }
    const auto binaryOf = [](SC_HANDLE service) {
        DWORD needed = 0;
        QueryServiceConfigW(service, nullptr, 0, &needed);
        if (!needed) return std::wstring();
        std::vector<BYTE> buffer(needed);
        auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
        if (!QueryServiceConfigW(service, config, needed, &needed) || !config->lpBinaryPathName) return std::wstring();
        return std::wstring(config->lpBinaryPathName);
    };
    for (const wchar_t* legacy : kLegacyServiceNames) {
        ScHandle old(OpenServiceW(scm.h, legacy, SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG));
        if (!old) old.h = OpenServiceW(scm.h, legacy, SERVICE_QUERY_STATUS);
        if (!old) continue;
        info.legacy.push_back(legacy);
        info.legacyCommands.push_back(binaryOf(old.h));
    }
    ScHandle service(OpenServiceW(scm.h, kServiceName, SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG));
    if (!service) {
        const DWORD error = GetLastError();
        if (error != ERROR_SERVICE_DOES_NOT_EXIST) info.error = error;
        return info;
    }
    info.installed = true;
    SERVICE_STATUS_PROCESS status{};
    if (currentStatus(service.h, status)) { info.state = status.dwCurrentState; info.pid = status.dwProcessId; }
    DWORD needed = 0;
    QueryServiceConfigW(service.h, nullptr, 0, &needed);
    if (needed) {
        std::vector<BYTE> buffer(needed);
        auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
        if (QueryServiceConfigW(service.h, config, needed, &needed)) {
            info.startType = config->dwStartType;
            info.command = config->lpBinaryPathName ? config->lpBinaryPathName : L"";
            info.pointsHere = commandPointsTo(info.command, executable);
        }
    }
    return info;
}

Result install(const std::wstring& executable) {
    ScHandle scm = manager(SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    if (!scm) return fail(GetLastError(), L"无法打开服务管理器，请以管理员身份运行");
    // Earlier stand-alone services would run the same automation twice.
    for (const wchar_t* legacy : kLegacyServiceNames) {
        ScHandle old(OpenServiceW(scm.h, legacy, SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE));
        if (!old) continue;
        if (!stopAndWait(old.h, 20000)) return {false, std::wstring(L"旧版服务 ") + legacy + L" 未能在 20 秒内停止，未作任何更改"};
        if (!DeleteService(old.h)) {
            const DWORD error = GetLastError();
            if (error != ERROR_SERVICE_MARKED_FOR_DELETE) return fail(error, std::wstring(L"无法删除旧版服务 ") + legacy);
        }
    }
    const auto command = serviceCommand(executable);
    ScHandle service(OpenServiceW(scm.h, kServiceName, SERVICE_ALL_ACCESS));
    if (service) {
        if (!stopAndWait(service.h, 20000)) return {false, L"等待服务停止超时，未更新服务"};
        if (!ChangeServiceConfigW(service.h, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
                command.c_str(), nullptr, nullptr, nullptr, L"LocalSystem", L"", kServiceDisplayName))
            return fail(GetLastError(), L"服务更新失败");
    } else {
        if (GetLastError() != ERROR_SERVICE_DOES_NOT_EXIST) return fail(GetLastError(), L"无法打开服务");
        service.h = CreateServiceW(scm.h, kServiceName, kServiceDisplayName, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
            SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, command.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr);
        if (!service) {
            if (GetLastError() == ERROR_SERVICE_MARKED_FOR_DELETE) return {false, L"旧服务仍在删除中，请关闭“服务”管理窗口后重试"};
            return fail(GetLastError(), L"服务安装失败");
        }
    }
    wchar_t description[512]; wcscpy_s(description, kServiceDescription);
    SERVICE_DESCRIPTIONW text{description};
    ChangeServiceConfig2W(service.h, SERVICE_CONFIG_DESCRIPTION, &text);
    // Restart after an unexpected exit (twice), then stay stopped; counters reset daily.
    SC_ACTION actions[3] = {{SC_ACTION_RESTART, 5000}, {SC_ACTION_RESTART, 30000}, {SC_ACTION_NONE, 0}};
    SERVICE_FAILURE_ACTIONSW failure{}; failure.dwResetPeriod = 86400; failure.cActions = 3; failure.lpsaActions = actions;
    ChangeServiceConfig2W(service.h, SERVICE_CONFIG_FAILURE_ACTIONS, &failure);
    const auto started = startAndWait(service.h);
    if (!started.ok) return {false, L"服务已安装，但" + started.message};
    return {true, L"服务已安装并启动，开机后自动运行"};
}

bool refreshLabels() {
    ScHandle scm = manager(SC_MANAGER_CONNECT);
    ScHandle service(scm ? OpenServiceW(scm.h, kServiceName, SERVICE_CHANGE_CONFIG) : nullptr);
    if (!service) return false;
    wchar_t description[512]; wcscpy_s(description, kServiceDescription);
    SERVICE_DESCRIPTIONW text{description};
    const bool named = ChangeServiceConfigW(service.h, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, kServiceDisplayName) != FALSE;
    return ChangeServiceConfig2W(service.h, SERVICE_CONFIG_DESCRIPTION, &text) != FALSE && named;
}

Result start() {
    ScHandle scm = manager(SC_MANAGER_CONNECT);
    ScHandle service(scm ? OpenServiceW(scm.h, kServiceName, SERVICE_START | SERVICE_QUERY_STATUS) : nullptr);
    if (!service) return fail(GetLastError(), L"无法打开服务");
    const auto result = startAndWait(service.h);
    return result.ok ? Result{true, L"服务已启动"} : result;
}
Result stop() {
    ScHandle scm = manager(SC_MANAGER_CONNECT);
    ScHandle service(scm ? OpenServiceW(scm.h, kServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS) : nullptr);
    if (!service) return fail(GetLastError(), L"无法打开服务");
    if (!stopAndWait(service.h, 20000)) return fail(GetLastError(), L"服务停止失败或超时");
    return {true, L"服务已停止，游戏启动时不再自动处理"};
}
Result restart() {
    ScHandle scm = manager(SC_MANAGER_CONNECT);
    ScHandle service(scm ? OpenServiceW(scm.h, kServiceName, SERVICE_STOP | SERVICE_START | SERVICE_QUERY_STATUS) : nullptr);
    if (!service) return fail(GetLastError(), L"无法打开服务");
    if (!stopAndWait(service.h, 20000)) return fail(GetLastError(), L"等待服务停止超时");
    const auto result = startAndWait(service.h);
    return result.ok ? Result{true, L"服务已重启"} : result;
}
Result stopNamed(const wchar_t* name) {
    ScHandle scm = manager(SC_MANAGER_CONNECT);
    ScHandle service(scm ? OpenServiceW(scm.h, name, SERVICE_STOP | SERVICE_QUERY_STATUS) : nullptr);
    if (!service) return fail(GetLastError(), std::wstring(L"无法打开服务 ") + name);
    if (!stopAndWait(service.h, 20000)) return fail(GetLastError(), std::wstring(L"服务 ") + name + L" 停止失败或超时");
    return {true, L""};
}
Result startNamed(const wchar_t* name) {
    ScHandle scm = manager(SC_MANAGER_CONNECT);
    ScHandle service(scm ? OpenServiceW(scm.h, name, SERVICE_START | SERVICE_QUERY_STATUS) : nullptr);
    if (!service) return fail(GetLastError(), std::wstring(L"无法打开服务 ") + name);
    return startAndWait(service.h);
}
Result uninstall() {
    ScHandle scm = manager(SC_MANAGER_CONNECT);
    ScHandle service(scm ? OpenServiceW(scm.h, kServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE) : nullptr);
    if (!service) return fail(GetLastError(), L"无法打开服务");
    if (!stopAndWait(service.h, 20000)) return fail(GetLastError(), L"服务停止失败或超时，未卸载");
    if (!DeleteService(service.h) && GetLastError() != ERROR_SERVICE_MARKED_FOR_DELETE) return fail(GetLastError(), L"服务卸载失败");
    return {true, L"服务已卸载，程序文件和配置未删除"};
}

} // namespace dafclient::svcctl
