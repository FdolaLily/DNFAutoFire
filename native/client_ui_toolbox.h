#pragma once
// "游戏工具箱" drawer: the functions of DNF专用工具箱8.0.bat inside the client.
// The game root is detected automatically (remembered path, running DNF, launcher,
// registry, common folders); when that fails the user picks it. File operations
// run on a worker thread and require DNF to be closed, as the script did.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "client_config.h"
#include "client_ui_host.h"
#include "game_toolbox.h"

namespace dafclient {

constexpr UINT kToolboxDoneMessage = WM_APP + 0x54;

class ToolboxPanel {
public:
    explicit ToolboxPanel(Store& store);
    ~ToolboxPanel();
    ToolboxPanel(const ToolboxPanel&) = delete;
    ToolboxPanel& operator=(const ToolboxPanel&) = delete;

    void opened(UiHost& host);                   // Drawer shown: detect once, refresh states.
    float paint(UiHost& host, float top);
    void finish(UiHost& host);                   // Handles kToolboxDoneMessage.
    void cancelConfirm(int id) { if (id != confirm_) confirm_ = 0; }
    std::wstring hoverText(int id) const;
    const std::wstring& directory() const { return directory_; }
    bool busy() const { return busy_; }
    // Uses a folder chosen by the user; false (with message) when it has no DNF.exe.
    bool useDirectory(UiHost* host, const std::wstring& directory, const std::wstring& source);

    static constexpr int kIdBase = 8000, kIdEnd = 8100;
private:
    struct Result { bool ok = true, detection = false; std::wstring message; toolbox::Detection found; };
    void run(UiHost& host, const wchar_t* busyText, std::function<Result()> job);
    void detect(UiHost& host);
    void chooseFolder(UiHost& host);
    void refreshStates();
    float paintDirectory(UiHost& host, float y);
    float paintComponents(UiHost& host, float y);
    float paintCleaning(UiHost& host, float y);
    bool confirmed(UiHost& host, int id, const wchar_t* prompt); // Second click confirms.

    Store& store_;
    std::wstring directory_, source_, busyText_;
    bool detected_ = false, busy_ = false;
    int confirm_ = 0;
    std::vector<toolbox::Item> items_;
    std::vector<toolbox::State> states_;
    std::thread worker_;
    std::mutex mutex_;
    Result result_;
};

} // namespace dafclient
