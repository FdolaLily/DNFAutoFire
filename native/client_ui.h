#pragma once
#include <windows.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "client_config.h"

namespace dafclient {

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
    bool create(bool visible = true);
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
    void setStatus(const std::wstring& text);
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
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
