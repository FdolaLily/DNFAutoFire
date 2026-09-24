#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace daf {

using Tick = std::int64_t; // Monotonic QPC time, in microseconds.
constexpr std::size_t kMaxKeys = 128;
constexpr Tick kNever = (std::numeric_limits<Tick>::max)();

struct Edge {
    std::size_t key;
    bool down;
    Tick deadline;
};

struct State {
    bool held = false;
    bool down = false;
    Tick next = kNever;
    Tick last_up = -kNever / 4;
    Tick phase = 0;
    Tick planned_down = 0;
};

// No Win32 calls or allocation in the scheduling path. The caller supplies the
// physical-key snapshot and commits each edge using its actual submission time.
class Schedule {
public:
    Schedule(std::size_t key_count, Tick down_us, Tick up_us)
        : count_(key_count), down_us_(down_us), up_us_(up_us) {
        if (count_ > kMaxKeys || down_us_ <= 0 || up_us_ <= 0
            || down_us_ > kNever / 1024 || up_us_ > kNever / 1024) {
            throw std::invalid_argument("Invalid autofire schedule parameters");
        }
        period_ = down_us_ + up_us_;
        min_down_ = (std::max)(Tick{1}, down_us_ / 2);
        min_up_ = (std::max)(Tick{1}, up_us_ / 2);
    }

    bool set_held(const bool* held, Tick now) {
        bool changed = false;
        std::size_t active = 0;
        for (std::size_t i = 0; i < count_; ++i) {
            changed |= states_[i].held != held[i];
            active += held[i] ? 1 : 0;
        }
        if (!changed)
            return false;

        epoch_ = now;
        std::size_t index = 0;
        for (std::size_t i = 0; i < count_; ++i) {
            auto& s = states_[i];
            s.held = held[i];
            if (!s.held)
                continue;
            // 20 ms default: 0/8/16 ms for up to three keys. Four or more
            // retain the historical 80%-of-period spread. Integer us avoids
            // floating-point ceil errors at an exact phase boundary.
            s.phase = active <= 3 ? period_ * static_cast<Tick>(index) * 2 / 5
                : period_ * static_cast<Tick>(index) * 4 / (5 * static_cast<Tick>(active));
            ++index;
            // Do not postpone a Down's existing release on membership changes.
            if (!s.down)
                s.next = slot(s, (std::max)(now, add(s.last_up, min_up_)));
        }
        return true;
    }

    // Request releases; the caller must submit due() edges and commit them.
    // This keeps logical ownership until the physical Up has been submitted.
    void deactivate(Tick now) {
        epoch_ = now;
        for (std::size_t i = 0; i < count_; ++i)
            states_[i].held = false;
    }

    std::size_t due(Tick now, Edge* output, std::size_t capacity = kMaxKeys) const {
        std::size_t count = 0;
        // Releases precede activations; a key can have at most one edge in a pass.
        for (std::size_t i = 0; i < count_ && count < capacity; ++i) {
            const auto& s = states_[i];
            if (s.down && (!s.held || now >= s.next))
                output[count++] = {i, false, s.held ? s.next : now};
        }
        for (std::size_t i = 0; i < count_ && count < capacity; ++i) {
            const auto& s = states_[i];
            if (!s.down && s.held && now >= s.next)
                output[count++] = {i, true, s.next};
        }
        return count;
    }

    void commit(const Edge& edge, Tick actual) {
        if (edge.key >= count_)
            return;
        auto& s = states_[edge.key];
        if (edge.down == s.down || (edge.down && !s.held))
            return;
        s.down = edge.down;
        if (edge.down) {
            s.planned_down = edge.deadline;
            // Small wakeup jitter is absorbed by the next absolute deadline.
            // A badly late Down still receives half the requested pulse width.
            s.next = (std::max)(add(edge.deadline, down_us_), add(actual, min_down_));
        } else {
            s.last_up = actual;
            const Tick earliest = (std::max)(add(s.planned_down, period_), add(actual, min_up_));
            s.next = slot(s, earliest);
            if (s.held && s.next != kNever && s.next > s.planned_down) {
                const Tick cycles = (s.next - s.planned_down) / period_;
                if (cycles > 1)
                    skipped_ += static_cast<std::uint64_t>(cycles - 1);
            }
        }
    }

    Tick next_deadline() const {
        Tick result = kNever;
        for (std::size_t i = 0; i < count_; ++i) {
            const auto& s = states_[i];
            if (s.down && !s.held)
                return 0; // An explicit release is immediately due.
            if (s.down || s.held)
                result = (std::min)(result, s.next);
        }
        return result;
    }

    bool has_work() const { return next_deadline() != kNever; }
    std::uint64_t skipped() const { return skipped_; }
    std::size_t key_count() const { return count_; }
    Tick period() const { return period_; }
    Tick min_down() const { return min_down_; }
    Tick min_up() const { return min_up_; }
    const State& state(std::size_t key) const { return states_.at(key); }

private:
    static Tick add(Tick base, Tick positive) {
        return base > kNever - positive ? kNever : base + positive;
    }

    Tick slot(const State& s, Tick earliest) const {
        const Tick origin = add(epoch_, s.phase);
        if (earliest <= origin)
            return origin;
        const Tick distance = earliest - origin;
        const Tick cycles = distance / period_ + (distance % period_ != 0);
        if (cycles > (kNever - origin) / period_)
            return kNever;
        return origin + cycles * period_;
    }

    std::array<State, kMaxKeys> states_{};
    std::size_t count_;
    Tick down_us_, up_us_, period_, min_down_, min_up_;
    Tick epoch_ = 0;
    std::uint64_t skipped_ = 0;
};

} // namespace daf
