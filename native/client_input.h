#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "client_config.h"
#include "client_input_state.h"

namespace dafclient {
constexpr UINT kInputCommandMessage = WM_APP + 0x51;
// Exit is posted by the service-quit listener (client_main), not by the input hook.
// TogglePower / ToggleRun carry the foreground DNF window in LPARAM (for the in-game notice).
enum class InputCommand : unsigned { Start = 1, Stop, QuickSwitch, ToggleRun, Error, Exit, TogglePower };

// Lifecycle is owned by the UI thread. Start after AF_Start so this observer is
// first in the hook chain. Stop and join before releasing the engine pointer.
class Controller {
public:
    Controller() = default;
    ~Controller();
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    bool start(const Profile&, const Settings&, void* engine, HWND ui, bool enabled = true);
    bool stop();
    DWORD error() const;
private:
    struct Context;
    Context* context_ = nullptr;
    DWORD lastError_ = 0;
    // A held command key must stay latched across configuration restarts until
    // a real Up arrives; OS typematic is not a new toggle/quick-switch request.
    std::array<PhysicalPressState, 513> physical_{};
    std::array<bool, 256> virtualDown_{};
    // Movement keys the stopped controller held in the game (see inheritHeld)
    // and when it stopped; only an immediate restart inherits them.
    std::array<unsigned char, 513> previous_{};
    ULONGLONG previousAt_ = 0;
};
} // namespace dafclient
