// Stand-in for the AHK-era client used by app_update_test: carries the old version
// names and, like a compiled AHK script, a hidden top-level "AutoHotkey" main window
// that exits on WM_CLOSE (exit code 7). "--ignore-close" models a script that hangs.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cwchar>

namespace {
bool ignoreClose = false;
LRESULT CALLBACK proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CLOSE) { if (!ignoreClose) DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(7); return 0; }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

int wmain(int argc, wchar_t** argv) {
    ignoreClose = argc > 1 && std::wcscmp(argv[1], L"--ignore-close") == 0;
    WNDCLASSW cls{}; cls.lpfnWndProc = proc; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"AutoHotkey";
    RegisterClassW(&cls);
    if (!CreateWindowExW(0, cls.lpszClassName, L"DNFAutoFire.exe - AutoHotkey", WS_OVERLAPPEDWINDOW, 0, 0, 10, 10, nullptr, nullptr, cls.hInstance, nullptr))
        return 2;
    SetTimer(nullptr, 0, 60000, nullptr); // Never outlives a failed test by more than a minute.
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_TIMER && !message.hwnd) return 3;
        DispatchMessageW(&message);
    }
    return int(message.wParam);
}
