#pragma once
// Recognises the DNF game window (window class + DNF.exe image name). Shared by the
// input controller (foreground checks) and the UI (where to place the in-game notice).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cwchar>

namespace dafclient {
inline bool isDnfWindow(HWND window) {
    if (!window) return false;
    wchar_t className[128]{};
    if (!GetClassNameW(window, className, 128)) return false;
    if (std::wcscmp(className, L"地下城与勇士")
        && std::wcscmp(className, L"Dungeon & Fighter")
        && std::wcscmp(className, L"Dungeon Fighter Online")) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    wchar_t path[32768]{};
    DWORD size = 32768;
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &size) != FALSE;
    CloseHandle(process);
    const auto* base = std::wcsrchr(path, L'\\');
    return ok && _wcsicmp(base ? base + 1 : path, L"DNF.exe") == 0;
}
// A visible, not minimised DNF window (the foreground one first), or nullptr.
inline HWND findDnfWindow() {
    const HWND foreground = GetForegroundWindow();
    if (isDnfWindow(foreground) && !IsIconic(foreground)) return foreground;
    HWND found = nullptr;
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        if (!IsWindowVisible(window) || IsIconic(window) || !isDnfWindow(window)) return TRUE;
        *reinterpret_cast<HWND*>(data) = window;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&found));
    return found;
}
} // namespace dafclient
