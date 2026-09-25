#pragma once

namespace dafclient {
// Physical ownership survives focus changes and configuration restarts. A
// repeat can be filtered without claiming a Down that previously passed through.
struct PhysicalPressState {
    struct Transition { bool changed, suppress; };
    bool down = false;
    bool swallowed = false;
    unsigned code = 0; // Last scan | (vk << 16) seen for this physical key.
    Transition observe(bool pressed, bool suppressByScope) {
        const bool changed = down != pressed;
        down = pressed;
        const bool suppress = swallowed || (pressed && suppressByScope);
        if (!pressed) swallowed = false;
        else if (changed && suppress) swallowed = true;
        return {changed, suppress};
    }
    // The hook never sees some Ups (secure desktop for Ctrl+Alt+Del, Win+L or
    // UAC; another hook consuming them). Forget such a press so it neither
    // blocks running forever nor makes the next real press look like a repeat;
    // a late real Up then simply passes through. Returns whether it was down.
    bool forget() {
        const bool was = down;
        down = false;
        swallowed = false;
        return was;
    }
    // Only keys this hook passed through can be checked against the system's
    // async key state; for swallowed keys that state reflects injected edges.
    bool stale(bool asyncDown) const { return down && !swallowed && !asyncDown; }
};
} // namespace dafclient
