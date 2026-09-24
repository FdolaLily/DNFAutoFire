#pragma once

namespace dafclient {
// Physical ownership survives focus changes and configuration restarts. A
// repeat can be filtered without claiming a Down that previously passed through.
struct PhysicalPressState {
    struct Transition { bool changed, suppress; };
    bool down = false;
    bool swallowed = false;
    Transition observe(bool pressed, bool suppressByScope) {
        const bool changed = down != pressed;
        down = pressed;
        const bool suppress = swallowed || (pressed && suppressByScope);
        if (!pressed) swallowed = false;
        else if (changed && suppress) swallowed = true;
        return {changed, suppress};
    }
};
} // namespace dafclient
