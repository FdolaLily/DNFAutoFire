#pragma once
// The "服务管理" drawer: service status and control (install / update, start, stop,
// restart, uninstall) plus every service option, drawn with the same tokens and
// widgets as the other drawers. ClientUi supplies the widgets through UiHost.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include "client_config.h"
#include "client_gfx.h"
#include "client_ui_host.h"
#include "client_ui_picker.h"
#include "app_update.h"
#include "service_control.h"

namespace dafclient {

// Posted to the main window when a background service operation has finished.
constexpr UINT kServiceDoneMessage = WM_APP + 0x53;

// One-line service state for the main screen (left of the power switch).
// LED colours: Running green, Update yellow (installed elsewhere / old service to
// replace), Stopped red (not installed, stopped or unreadable), Busy in progress.
enum class ServiceTone { Stopped, Running, Update, Busy };
struct ServiceSummary {
    std::wstring state;   // Short word after "后台服务", e.g. "运行中".
    std::wstring detail;  // Friendly sentence for the footer while hovered.
    ServiceTone tone = ServiceTone::Stopped;
};

class ServicePanel {
public:
    ServicePanel(Store& store, std::wstring executable);
    ~ServicePanel();
    ServicePanel(const ServicePanel&) = delete;
    ServicePanel& operator=(const ServicePanel&) = delete;

    void load();                                  // Service options from config.json.
    float paint(UiHost& host, float top);         // Drawer body; returns the content bottom.
    bool refresh();                               // Polls the SCM; true when the state changed.
    void finish(UiHost& host);                    // Handles kServiceDoneMessage.
    bool flush(UiHost& host);                     // Saves pending option edits now.
    void cancelConfirm() { confirmUninstall_ = false; }
    void paintOverlay(UiHost& host) { picker_.paint(host); } // Process picker, above the drawer.
    bool wheel(float notches) { return picker_.wheel(notches); }
    bool escape(UiHost& host);                    // Closes the picker; false when nothing was open.
    void closeOverlays(UiHost& host) { picker_.close(host); }
    bool pickerOpen() const { return picker_.active(); }
    bool busy() const { return busy_; }
    const svcctl::Info& info() const { return info_; }
    const ServiceOptions& options() const { return options_; }
    // Main-screen status chip; summarize() is pure so tests can feed synthetic states.
    ServiceSummary summary() const { return summarize(info_, busy_); }
    static ServiceSummary summarize(const svcctl::Info& info, bool busy);
    static gfx::Color toneColor(const gfx::Theme& theme, ServiceTone tone);
    std::wstring hoverText(int id) const;         // Footer hint for a hovered control.
    std::wstring notice() const;                  // Startup hint while an old stand-alone service remains.

    // Semantic operations (also used by tests).
    bool addItem(int list, const std::wstring& raw, std::wstring& error);
    bool removeItem(int list, size_t index);          // False for the locked client entry.
    bool locked(int list, const std::wstring& item) const; // autoStart entry that is this EXE.
    void setOptions(const ServiceOptions& options);
    void openProcessPicker(UiHost& host, int list);  // list: 0 kill, 1 limit, 3 autoStop, 4 game process.
    void pickProgram(UiHost& host);                  // File dialog for autoStart.
    // Program update from a picked EXE: choose (validated, shown for confirmation), then
    // confirm (stops service and client, swaps the file, reopens) or cancel.
    bool chooseUpdate(const std::wstring& path, std::wstring& error);
    const std::wstring& pendingUpdate() const { return pendingPath_; }
    void pickUpdate(UiHost& host);
    void confirmUpdate(UiHost& host);
    void cancelUpdate() { pendingPath_.clear(); }

    static constexpr int kIdBase = 7000, kIdEnd = ProcessPicker::kIdEnd, kUninstallId = kIdBase + 4;
private:
    enum List { Kill, Limit, AutoStart, AutoStop, ListCount, GameProcess = ListCount };
    std::vector<std::wstring>& list(int index);
    void changed(UiHost& host);
    void run(UiHost& host, const wchar_t* busyText, std::function<svcctl::Result()> operation);
    float paintStatus(UiHost& host, float y);
    float paintUpdate(UiHost& host, float y);
    float paintGame(UiHost& host, float y);
    float paintOptimize(UiHost& host, float y);
    float paintList(UiHost& host, float y, int index);

    Store& store_;
    std::wstring executable_;
    ServiceOptions options_;
    bool dirty_ = false, confirmUninstall_ = false, busy_ = false;
    std::wstring busyText_;
    svcctl::Info info_;
    std::thread worker_;
    std::mutex mutex_;
    svcctl::Result result_;
    std::wstring pendingPath_;             // Picked new program awaiting confirmation.
    update::ExeInfo pendingInfo_;
    update::Offer pendingOffer_ = update::Offer::None;
    bool quitAfter_ = false, updateChanged_ = false;
    ProcessPicker picker_;
};

} // namespace dafclient
