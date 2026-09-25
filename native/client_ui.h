#pragma once
#include <windows.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "client_config.h"

namespace dafclient {

// A duplicate launch posts this registered message to the running client's main
// window, which then shows itself with a friendly notice instead of failing silently.
constexpr wchar_t kMainWindowClass[] = L"DAF.Native.Client";
constexpr wchar_t kShowRunningMessage[] = L"DNFAutoFire.Client.ShowRunning.{B797BFB2-305A-44DC-9E06-76D6CC424419}";

struct UiCallbacks {
    std::function<bool(const Profile&, const Settings&)> start;
    std::function<bool()> stop;
    // Called after every committed (automatically saved) change; the runtime
    // re-applies it, keeping the current running state.
    std::function<void(const Profile&, const Settings&)> settingsChanged;
    std::function<void()> quit;
    std::function<void(const std::wstring&)> reportError;
};

// The UI only edits configuration. All physical input and synthetic output
// belongs to the controller, including global hotkeys.
class ClientUi {
public:
    ClientUi(HINSTANCE instance, Store& store, UiCallbacks callbacks);
    ~ClientUi();
    ClientUi(const ClientUi&) = delete;
    ClientUi& operator=(const ClientUi&) = delete;
    // visible: real client with tray icon; showWindow=false keeps the main
    // window hidden (tray only) so an auto-started client never flashes.
    bool create(bool visible = true, bool showWindow = true);
    HWND window() const;
    const Profile& currentProfile() const;
    const Settings& settings() const;
    void show();
    void quickSwitch();
    bool start(bool saveFirst = true);
    bool stop();
    void toggle();
    void toggleRun();
    void setRunning(bool running);
    void setStatus(const std::wstring& text);     // Error line in the footer.
    void showHint(const std::wstring& text);      // Friendly (non-error) footer line.
    void quit();                                  // Stop, release keys and leave the message loop.
    bool processMessage(MSG& message);

    // Semantic operations behind the mouse UI; also used by automated tests.
    bool keyEnabled(const std::wstring& key) const;
    void setKeyEnabled(const std::wstring& key, bool enabled);
    void setTiming(unsigned downMs, unsigned upMs);
    void setRunScope(bool perProfile);
    std::vector<std::wstring> runKeys() const;
    bool selectProfile(const std::wstring& name);
    bool cloneProfile();
    bool renameProfile(const std::wstring& name);
    bool flush();                 // Commits a pending automatic save immediately.
    std::wstring status() const;  // Last status or error line shown in the footer.
    bool running() const;
    // Service drawer (lists: 0 kill, 1 limit, 2 autoStart, 3 autoStop).
    const ServiceOptions& serviceOptions() const;
    bool addServiceItem(int list, const std::wstring& value);
    bool removeServiceItem(int list, size_t index);   // False for the client's own autoStart entry.
    bool flushService();
    void openServicePage();
    std::wstring serviceNotice() const;           // Non-empty while an old stand-alone service still runs.
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
