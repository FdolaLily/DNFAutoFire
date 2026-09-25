#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <limits>
#include <utility>
#include <vector>

namespace dafclient {
using InputTick = std::int64_t; // Monotonic microseconds.
using KeyCode = unsigned; // scan | (virtual_key << 16), same as engine rules.
inline unsigned inputId(KeyCode key) { return (key >> 16) == 0x13 ? 512 : key & 0x1ff; }
struct InputComboStep { KeyCode key = 0; unsigned delayMs = 0; };
struct InputCombo { KeyCode trigger = 0; std::vector<InputComboStep> steps; };
struct InputPlan {
    bool running = false;
    std::array<KeyCode, 4> directions{}; // Up, Down, Left, Right.
    unsigned pressMs = 30, gapMs = 30, guardMs = 140;
    std::vector<InputCombo> combos;
    std::array<bool, 513> refreshKeys{};
};
class InputSink {
public:
    virtual ~InputSink() = default;
    virtual bool send(KeyCode key, bool down) = 0;
    virtual bool pause(KeyCode key, bool paused) = 0;
    virtual InputTick clock(InputTick reference) const { return reference; }
    virtual bool focusLost() const { return false; }
};

// Deterministic state machine: no Windows callbacks, sleeps, keyboard text, or
// wall clock. Production supplies QPC timestamps; tests advance a virtual clock.
class InputModel {
public:
    static constexpr InputTick never = (std::numeric_limits<InputTick>::max)();
    InputModel(InputPlan plan, InputSink& sink) : plan_(std::move(plan)), sink_(sink) {
        plan_.pressMs = (std::max)(plan_.pressMs, 1U);
        plan_.gapMs = (std::max)(plan_.gapMs, 1U);
        plan_.guardMs = (std::max)(plan_.guardMs, 1U);
    }
    ~InputModel() = default; // Owner must call cancel() before destroying the sink.
    void focus(bool foreground, InputTick now) {
        if ((foreground_ || interrupted_) && !foreground) cancel(now);
        foreground_ = foreground;
        interrupted_ = false;
    }
    void physical(KeyCode key, bool down, InputTick now) {
        const unsigned id = inputId(key);
        if (id >= physical_.size() || physical_[id] == down) return;
        physical_[id] = down;
        const int direction = directionIndex(key);
        if (direction >= 0 && plan_.running) {
            if (down && foreground_) beginDirection(direction, now, false);
            else if (!down) releaseDirection(direction, now);
            return;
        }
        if (!foreground_ || failed_ || interrupted_) return;
        if (down) {
            advanceEpoch();
            if (plan_.refreshKeys[id]) {
                tickets_[id] = {refreshCandidate(), epoch_};
            }
            for (std::size_t i = 0; i < plan_.combos.size(); ++i) {
                if (inputId(plan_.combos[i].trigger) != id) continue;
                // One press yields one sequence, irrespective of OS typematic.
                if (!comboBusy(i)) queue_.push_back(i);
                if (!comboActive_) beginNextCombo(now);
                break;
            }
        } else {
            const auto ticket = tickets_[id];
            tickets_[id] = {};
            if (ticket.direction >= 0 && ticket.epoch == epoch_
                && refreshCandidate() == ticket.direction) {
                auto& state = dirs_[ticket.direction];
                state.stage = Stage::GapFirst;
                state.epoch = epoch_;
                state.full = true;
                state.refresh = true;
                setDirectionOutput(ticket.direction, false);
                state.due = now + ms(plan_.gapMs);
            }
        }
    }
    void tick(InputTick now) {
        if (!foreground_ || failed_ || interrupted_) return;
        // At most one transition per direction/tick: a delayed worker never
        // collapses a pulse's minimum hold/gap or bursts to catch up.
        for (int i = 0; i < 4 && !failed_ && !interrupted_; ++i) {
            auto& state = dirs_[i];
            if (state.stage == Stage::Idle || state.due > now) continue;
            if (!valid(i, state.epoch)) { cancelPending(i); continue; }
            if (comboLease_ && inputId(comboLease_) == inputId(plan_.directions[i])) continue;
            switch (state.stage) {
            case Stage::Confirm:
                if (!state.switching && otherHeld(i)) { cancelPending(i); break; }
                setDirectionOutput(i, false);
                state.stage = Stage::GapFirst;
                state.due = now + ms(plan_.gapMs);
                break;
            case Stage::GapFirst: {
                const bool repeated = lastMovementDown_ == i;
                setDirectionOutput(i, true);
                if (state.full || !repeated) {
                    state.stage = Stage::Tap;
                    state.due = now + ms(plan_.pressMs);
                } else markRunning(i);
                break;
            }
            case Stage::Tap:
                setDirectionOutput(i, false);
                state.stage = Stage::GapLast;
                state.due = now + ms(plan_.gapMs);
                break;
            case Stage::GapLast: {
                const bool repeated = lastMovementDown_ == i;
                setDirectionOutput(i, true);
                if (repeated) markRunning(i);
                else {
                    state.stage = Stage::Tap;
                    state.due = now + ms(plan_.pressMs);
                }
                break;
            }
            case Stage::Idle: break;
            }
        }
        tickCombo(now);
    }
    InputTick deadline() const {
        InputTick result = never;
        for (int i = 0; i < 4; ++i) {
            if (dirs_[i].stage == Stage::Idle) continue;
            if (comboLease_ && inputId(comboLease_) == inputId(plan_.directions[i])) continue;
            result = (std::min)(result, dirs_[i].due);
        }
        if (comboActive_) result = (std::min)(result, comboDue_);
        return failed_ || !foreground_ ? never : result;
    }
    void cancel(InputTick) {
        ++epoch_;
        queue_.clear();
        comboActive_ = false;
        // Never send a new Down during cleanup; only release owned outputs.
        for (unsigned id = 0; id < output_.size(); ++id) {
            if (output_[id]) send(output_[id], false);
        }
        if (comboLease_) {
            if (!sink_.pause(comboLease_, false)) failed_ = true;
            comboLease_ = 0;
        }
        dirs_ = {};
        tickets_ = {};
        lastDirection_ = -1;
        lastMovementDown_ = -1;
        lastPress_ = 0;
        stableRelease_ = false;
    }
    bool failed() const { return failed_; }
    bool interrupted() const { return interrupted_; }
    bool isRunning(int i) const { return dirs_[i].running; }
    bool outputDown(KeyCode key) const { return output_[inputId(key)] != 0; }
    bool comboActive() const { return comboActive_; }
private:
    enum class Stage { Idle, Confirm, GapFirst, Tap, GapLast };
    struct Direction {
        bool active = false, desired = false, running = false, full = false;
        bool switching = false, refresh = false;
        int suppressedBy = -1;
        Stage stage = Stage::Idle;
        std::uint64_t epoch = 0;
        InputTick due = never;
    };
    struct Ticket { int direction = -1; std::uint64_t epoch = 0; };
    static InputTick ms(unsigned value) { return InputTick(value) * 1000; }
    bool send(KeyCode key, bool down) {
        if (!key || (down && (failed_ || interrupted_))) return false;
        const auto id = inputId(key);
        if ((output_[id] != 0) == down) return true;
        if (!sink_.send(key, down)) {
            if (down && sink_.focusLost()) interrupted_ = true;
            else failed_ = true;
            return false;
        }
        output_[id] = down ? key : 0;
        // DNF keeps the character's horizontal facing until another Left/Right
        // Down reaches the game, including while running purely vertically.
        if (down) {
            const int direction = directionIndex(key);
            if (direction >= 0) lastMovementDown_ = direction;
            if (direction >= 2) facing_ = direction;
        }
        return true;
    }
    int directionIndex(KeyCode key) const {
        for (int i = 0; i < 4; ++i)
            if (plan_.directions[i] && inputId(plan_.directions[i]) == inputId(key)) return i;
        return -1;
    }
    bool held(int i) const { return plan_.directions[i] && physical_[inputId(plan_.directions[i])]; }
    bool valid(int i, std::uint64_t epoch) const {
        return foreground_ && !failed_ && !interrupted_ && held(i) && dirs_[i].active
            && dirs_[i].suppressedBy < 0 && epoch == epoch_;
    }
    bool orthogonal(int a, int b) const { return a / 2 != b / 2; }
    bool otherHeld(int direction) const {
        for (unsigned id = 0; id < physical_.size(); ++id) {
            if (!physical_[id]) continue;
            bool permitted = false;
            for (int i = 0; i < 4; ++i)
                if (plan_.directions[i] && inputId(plan_.directions[i]) == id
                    && (i == direction || orthogonal(i, direction))) permitted = true;
            if (!permitted) return true;
        }
        return false;
    }
    void setDirectionOutput(int i, bool down) {
        dirs_[i].desired = down;
        const auto key = plan_.directions[i];
        if (!comboLease_ || inputId(comboLease_) != inputId(key)) send(key, down);
    }
    void cancelPending(int i) {
        auto& state = dirs_[i];
        if (state.stage == Stage::Idle) return;
        if (state.refresh) state.running = false;
        state.stage = Stage::Idle;
        state.refresh = false;
        // A canceled gap must not strand a still-held physical direction at Up.
        if (foreground_ && held(i) && state.active && state.suppressedBy < 0)
            setDirectionOutput(i, true);
    }
    void advanceEpoch() {
        ++epoch_;
        for (int i = 0; i < 4; ++i) cancelPending(i);
    }
    void markRunning(int i) {
        dirs_[i].running = true;
        dirs_[i].stage = Stage::Idle;
        dirs_[i].refresh = false;
        for (int j = 0; j < 4; ++j) {
            if (j != i && orthogonal(i, j) && dirs_[j].active && held(j)
                && dirs_[j].suppressedBy < 0) {
                dirs_[j].running = true;
                // The first completed axis owns the double-tap; the other axis
                // joins that session, including when pressed during its gap.
                dirs_[j].stage = Stage::Idle;
                setDirectionOutput(j, true);
            }
        }
    }
    void beginDirection(int i, InputTick now, bool alreadyDown) {
        auto& state = dirs_[i];
        if (state.active && !alreadyDown) return;
        bool switching = stableRelease_ && now - lastRelease_ <= ms(250);
        bool orthoHeld = false, oppositeHeld = false, orthoRunning = false;
        for (int j = 0; j < 4; ++j) {
            if (j == i) continue;
            if (dirs_[j].running) switching = true;
            if (!dirs_[j].active || !held(j) || dirs_[j].suppressedBy >= 0) continue;
            if (orthogonal(i, j)) { orthoHeld = true; orthoRunning |= dirs_[j].running; }
            else oppositeHeld = true;
        }
        const bool chord = orthoHeld && (!oppositeHeld || switching);
        // Left -> Up run, release Left, then Right while Up is still held: the
        // character still faces Left, so a single Right Down only turns it and
        // drops to walking. A horizontal reversal needs its own double tap even
        // though the vertical axis keeps the session alive.
        const bool reverse = i >= 2 && facing_ == (i ^ 1);
        const bool session = switching && orthoRunning && !oppositeHeld && !reverse;
        if (!chord && !session) advanceEpoch();
        const InputTick elapsed = lastDirection_ >= 0 ? now - lastPress_ : ms(351);
        const bool sequential = !switching && lastDirection_ >= 0
            && lastDirection_ != i && elapsed <= ms(350);
        lastDirection_ = i;
        lastPress_ = now;
        state.active = true;
        state.suppressedBy = -1;
        state.epoch = epoch_;
        state.refresh = false;
        state.switching = switching;
        if (switching && dirs_[i ^ 1].active && held(i ^ 1)) {
            auto& opposite = dirs_[i ^ 1];
            setDirectionOutput(i ^ 1, false);
            opposite.running = false;
            opposite.stage = Stage::Idle;
            opposite.suppressedBy = i;
        }
        if (switching && !session) {
            // A reversal invalidates the whole previous diagonal session.
            // Leaving its vertical axis marked running lets the next key
            // inherit a stale run and cancel an unfinished horizontal tap.
            for (auto& direction : dirs_) {
                if (direction.running) { stableRelease_ = true; lastRelease_ = now; }
                direction.running = false;
            }
        }
        if (!alreadyDown) setDirectionOutput(i, true);
        if (session) { markRunning(i); return; }
        if (chord) {
            for (int j = 0; j < 4; ++j) {
                auto& first = dirs_[j];
                if (!orthogonal(i, j) || !valid(j, first.epoch)
                    || first.stage == Stage::Idle || first.running) continue;
                // A horizontal reversal owns the new pair; completing a
                // vertical tap must not skip the change of horizontal facing.
                if (reverse) { cancelPending(j); break; }
                // The new direction Down interrupts the first axis's double
                // tap (Right, Up, Right is not Right, Right). Keep diagonal
                // movement held, but send a fresh complete pair before either
                // axis may inherit running. A join during Tap/GapLast must
                // restart that pair too; its first Down is already too old.
                first.full = true;
                if (first.stage == Stage::Tap) {
                    setDirectionOutput(j, false);
                    first.due = now + ms(plan_.gapMs);
                }
                if (first.stage == Stage::Tap || first.stage == Stage::GapLast)
                    first.stage = Stage::GapFirst;
                state.stage = Stage::Idle;
                return;
            }
        }
        if (chord && sequential && !reverse) { state.stage = Stage::Idle; return; }
        const InputTick delay = switching ? ms(90) : sequential
            ? (std::max)(ms(plan_.guardMs), ms(350) - elapsed) : ms(plan_.guardMs);
        state.full = alreadyDown || (!switching && delay > ms(250));
        state.stage = Stage::Confirm;
        state.due = now + delay;
    }
    void releaseDirection(int i, InputTick now) {
        auto& state = dirs_[i];
        if (!state.active) return;
        if (state.running) { stableRelease_ = true; lastRelease_ = now; }
        setDirectionOutput(i, false);
        state = {};
        for (int j = 0; j < 4; ++j) {
            auto& suppressed = dirs_[j];
            if (suppressed.suppressedBy != i) continue;
            suppressed.suppressedBy = -1;
            if (!foreground_ || !held(j)) continue;
            setDirectionOutput(j, true);
            suppressed.epoch = epoch_;
            suppressed.stage = Stage::Tap;
            suppressed.due = now + ms(plan_.pressMs);
            suppressed.refresh = false;
        }
        int candidate = -1;
        for (int j = 0; j < 4; ++j) {
            const auto& direction = dirs_[j];
            if (!direction.active || !held(j) || direction.running
                || direction.stage != Stage::Idle || direction.suppressedBy >= 0) continue;
            if (candidate >= 0) return;
            candidate = j;
        }
        if (foreground_ && candidate >= 0) beginDirection(candidate, now, true);
    }
    int refreshCandidate() const {
        if (lastDirection_ >= 0 && dirs_[lastDirection_].running && held(lastDirection_))
            return lastDirection_;
        for (int i = 0; i < 4; ++i) if (dirs_[i].running && held(i)) return i;
        return -1;
    }
    bool comboBusy(std::size_t index) const {
        return (comboActive_ && comboIndex_ == index)
            || std::find(queue_.begin(), queue_.end(), index) != queue_.end();
    }
    void beginNextCombo(InputTick now) {
        if (queue_.empty() || failed_) return;
        comboIndex_ = queue_.front(); queue_.pop_front();
        comboStep_ = 0;
        comboPhase_ = 0;
        comboActive_ = !plan_.combos[comboIndex_].steps.empty();
        if (comboActive_) comboDue_ = now + ms(plan_.combos[comboIndex_].steps[0].delayMs);
    }
    void tickCombo(InputTick now) {
        if (!comboActive_ || failed_ || interrupted_ || comboDue_ > now) return;
        const auto& steps = plan_.combos[comboIndex_].steps;
        const auto key = steps[comboStep_].key;
        if (comboPhase_ == 0) {
            comboLease_ = key;
            if (!sink_.pause(key, true)) { failed_ = true; return; }
            // Preserve a held movement axis across a combo that uses that axis.
            send(key, false);
            send(key, true);
            comboPhase_ = 1;
            comboDue_ = sink_.clock(now) + ms(10);
        } else if (comboPhase_ == 1) {
            send(key, false);
            comboPhase_ = 2;
            comboDue_ = sink_.clock(now) + ms(10);
        } else {
            if (!sink_.pause(key, false)) { failed_ = true; return; }
            comboLease_ = 0;
            const int direction = directionIndex(key);
            if (direction >= 0 && dirs_[direction].desired && held(direction)) {
                send(plan_.directions[direction], true);
                if (dirs_[direction].stage != Stage::Idle)
                    dirs_[direction].due = (std::max)(dirs_[direction].due, sink_.clock(now) + ms(plan_.pressMs));
            }
            ++comboStep_;
            if (comboStep_ == steps.size()) {
                comboActive_ = false;
                beginNextCombo(now);
            } else {
                comboPhase_ = 0;
                comboDue_ = now + ms(steps[comboStep_].delayMs);
            }
        }
    }
    InputPlan plan_;
    InputSink& sink_;
    std::array<bool, 513> physical_{};
    std::array<KeyCode, 513> output_{};
    std::array<Ticket, 513> tickets_{};
    std::array<Direction, 4> dirs_{};
    std::uint64_t epoch_ = 0;
    bool foreground_ = false, failed_ = false, interrupted_ = false, stableRelease_ = false;
    int lastDirection_ = -1;
    int lastMovementDown_ = -1; // Actual output order, independent of physical presses/epochs.
    int facing_ = -1; // Last Left/Right Down sent to DNF; game state, kept across cancel().
    InputTick lastPress_ = 0, lastRelease_ = 0;
    std::deque<std::size_t> queue_;
    std::size_t comboIndex_ = 0, comboStep_ = 0;
    unsigned comboPhase_ = 0;
    bool comboActive_ = false;
    KeyCode comboLease_ = 0;
    InputTick comboDue_ = never;
};
} // namespace dafclient
