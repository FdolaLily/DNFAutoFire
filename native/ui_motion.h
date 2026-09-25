#pragma once
// Time-based transitions for the self-drawn UI. Each animated property is a key
// whose value eases toward a target; a key seen for the first time starts settled,
// so nothing animates on the first frame. Honours the Windows "animation effects"
// accessibility switch (SPI_GETCLIENTAREAANIMATION).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <limits>
#include <unordered_map>

namespace dafclient {

class Motion {
public:
    // Eased value (ease-out cubic) moving toward target over ms milliseconds.
    float value(int key, float target, float ms = 160.f) {
        const double now = clock();
        auto found = tweens_.find(key);
        if (found == tweens_.end() || !enabled_) {
            tweens_[key] = Tween{target, target, target, now, ms};
            return target;
        }
        Tween& t = found->second;
        if (!(t.to == target)) { t.from = t.current; t.to = target; t.start = now; t.ms = ms; } // NaN after jump().
        if (t.current == t.to) return t.to;
        const double p = t.ms > 0 ? (now - t.start) / t.ms : 1.0;
        if (p >= 1.0) { t.current = t.to; return t.to; }
        const float e = float(1.0 - (1.0 - p) * (1.0 - p) * (1.0 - p));
        t.current = t.from + (t.to - t.from) * e;
        pending_ = true;
        return t.current;
    }
    // Restarts key from value; the next value() call eases from there to its target.
    void jump(int key, float value) {
        tweens_[key] = Tween{value, std::numeric_limits<float>::quiet_NaN(), value, clock(), 0.f};
    }
    // Continuous effects (spinners) request another frame without a tween.
    void keepAlive() { pending_ = true; }
    // Call at the start of each frame; pending() then tells whether another frame is needed.
    void beginFrame() { pending_ = false; enabled_ = systemEnabled(); }
    bool pending() const { return pending_; }
    static bool systemEnabled() {
        BOOL on = TRUE;
        return !SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0) || on;
    }
private:
    struct Tween { float from, to, current; double start; float ms; };
    static double clock() {
        static const double frequency = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return double(f.QuadPart); }();
        LARGE_INTEGER c{}; QueryPerformanceCounter(&c);
        return double(c.QuadPart) * 1000.0 / frequency;
    }
    std::unordered_map<int, Tween> tweens_;
    bool pending_ = false, enabled_ = true;
};

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

} // namespace dafclient
