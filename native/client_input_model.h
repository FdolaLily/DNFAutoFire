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
    // Skills the program itself drives (manual autofire keys, profession
    // triggers; combo triggers are implied). Releasing the last of them re-runs
    // a direction that was running at once. Any other key only allows a fresh,
    // guarded start after it is released.
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
//
// Invariants (tests/client_input_test.cpp checks them exhaustively):
// - At most one direction owns a double-tap stage; other held axes wait at Idle
//   with their Down held and join when the owner's pair completes.
// - Of two held opposite directions only the newer one reaches the game.
// - Once input stops changing, output stops changing (no endless re-tapping).
class InputModel {
public:
    static constexpr InputTick never = (std::numeric_limits<InputTick>::max)();
    // The player's own quick double tap already started DNF's run.
    static constexpr unsigned kManualTapMs = 250, kManualGapMs = 200;
    // A pair interleaved this often gives up and keeps walking instead of looping.
    static constexpr unsigned kMaxPairAttempts = 3;
    InputModel(InputPlan plan, InputSink& sink) : plan_(std::move(plan)), sink_(sink) {
        plan_.pressMs = (std::max)(plan_.pressMs, 1U);
        plan_.gapMs = (std::max)(plan_.gapMs, 1U);
        plan_.guardMs = (std::max)(plan_.guardMs, 1U);
        for (int i = 0; i < 4; ++i)
            if (plan_.directions[i]) directionId_[inputId(plan_.directions[i])] = true;
        for (const auto& combo : plan_.combos) managed_[inputId(combo.trigger)] = true;
        for (unsigned id = 0; id < managed_.size(); ++id) managed_[id] = managed_[id] || plan_.refreshKeys[id];
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
        if (handoff_[id]) {
            // A former direction handed back to the game across a controller
            // restart: it owes the game its Up and never counts as a skill.
            handoff_[id] = false;
            send(key, false);
            return;
        }
        const int direction = directionIndex(key);
        if (direction >= 0 && plan_.running) {
            if (down && foreground_) beginDirection(direction, now, false);
            else if (!down) releaseDirection(direction, now);
            return;
        }
        if (!foreground_ || failed_ || interrupted_) return;
        if (!down) { resumeAfterSkills(now); return; }
        interruptBySkill(id, now);
        for (std::size_t i = 0; i < plan_.combos.size(); ++i) {
            if (inputId(plan_.combos[i].trigger) != id) continue;
            // One press yields one sequence, irrespective of OS typematic.
            if (!comboBusy(i)) queue_.push_back(i);
            if (!comboActive_) beginNextCombo(now);
            break;
        }
    }
    // A key already held when this model starts (controller restart). Other
    // keys only defer running until released; directions start a fresh hold.
    // Adopt non-direction keys first so a held skill defers the directions.
    void adopt(KeyCode key, InputTick now) {
        const unsigned id = inputId(key);
        if (id >= physical_.size() || physical_[id]) return;
        physical_[id] = true;
        if (!foreground_ || failed_ || interrupted_) return;
        const int direction = directionIndex(key);
        if (direction >= 0 && plan_.running) beginDirection(direction, now, false);
        else if (direction < 0) deferToSkills();
    }
    // A key the previous plan held Down as a direction and this plan no longer
    // manages: keep the player's movement until the physical Up arrives.
    void handBack(KeyCode key, InputTick) {
        const unsigned id = inputId(key);
        if (id >= physical_.size() || physical_[id] || directionId_[id]) return;
        physical_[id] = true;
        handoff_[id] = true;
        if (foreground_) send(key, true);
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
                // A held skill (or any other key) or a combo still playing
                // defers the start until it ends; see resumeAfterSkills().
                if (busy()) { cancelPending(i); deferToSkills(); break; }
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
                else if (++state.attempts >= kMaxPairAttempts) {
                    // Something keeps interleaving other Downs: stop re-tapping
                    // and leave the direction held (walking) instead of looping.
                    state.stage = Stage::Idle;
                    state.refresh = false;
                } else {
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
        lastDirection_ = -1;
        lastMovementDown_ = -1;
        lastPress_ = 0;
        stableRelease_ = false;
        resume_ = Resume::None;
        resumeDir_ = -1;
        taps_ = {};
    }
    bool failed() const { return failed_; }
    bool interrupted() const { return interrupted_; }
    bool isRunning(int i) const { return dirs_[i].running; }
    bool outputDown(KeyCode key) const { return output_[inputId(key)] != 0; }
    bool comboActive() const { return comboActive_; }
    // Test hook: a direction is in a double-tap stage (Confirm/Gap/Tap).
    bool pending(int i) const { return dirs_[i].stage != Stage::Idle; }
private:
    enum class Stage { Idle, Confirm, GapFirst, Tap, GapLast };
    enum class Resume { None, Guard, Fast };
    struct Direction {
        bool active = false, desired = false, running = false, full = false;
        bool switching = false, refresh = false;
        int suppressedBy = -1;
        unsigned attempts = 0;
        Stage stage = Stage::Idle;
        std::uint64_t epoch = 0;
        InputTick due = never;
    };
    // Physical press/release and emitted Down/Up of one direction, used to
    // recognise the player's own double tap.
    struct Tap {
        bool pressed = false, released = false;
        InputTick press = 0, release = 0, down = 0, up = 0;
    };
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
        const int direction = directionIndex(key);
        if (direction >= 0) {
            const InputTick now = sink_.clock(0);
            (down ? taps_[direction].down : taps_[direction].up) = now;
        }
        // DNF keeps the character's horizontal facing until another Left/Right
        // Down reaches the game, including while running purely vertically.
        if (down && direction >= 0) {
            lastMovementDown_ = direction;
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
    bool usable(int i) const { return held(i) && dirs_[i].active && dirs_[i].suppressedBy < 0; }
    bool valid(int i, std::uint64_t epoch) const {
        return foreground_ && !failed_ && !interrupted_ && usable(i) && epoch == epoch_;
    }
    bool orthogonal(int a, int b) const { return a / 2 != b / 2; }
    // Any held key other than a movement key: skills, jump, items, chat.
    bool otherHeld() const {
        for (unsigned id = 0; id < physical_.size(); ++id)
            if (physical_[id] && !directionId_[id] && !handoff_[id]) return true;
        return false;
    }
    // A skill is still in progress: a key held or a combo playing / queued.
    bool busy() const { return otherHeld() || comboActive_ || !queue_.empty(); }
    int pendingOwner(int except) const {
        for (int j = 0; j < 4; ++j)
            if (j != except && dirs_[j].stage != Stage::Idle) return j;
        return -1;
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
        if (foreground_ && usable(i)) setDirectionOutput(i, true);
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
            if (j != i && orthogonal(i, j) && usable(j)) {
                dirs_[j].running = true;
                // The first completed axis owns the double-tap; the other axis
                // joins that session, including when pressed during its gap.
                dirs_[j].stage = Stage::Idle;
                dirs_[j].refresh = false;
                setDirectionOutput(j, true);
            }
        }
    }
    // Direction to re-run at once after managed skills: running, or in the
    // middle of such a recovery pair when the skill was pressed again.
    int runCandidate() const {
        const auto eligible = [&](int i) {
            return usable(i) && (dirs_[i].running || (dirs_[i].refresh && dirs_[i].stage != Stage::Idle));
        };
        if (lastDirection_ >= 0 && eligible(lastDirection_)) return lastDirection_;
        for (int i = 0; i < 4; ++i) if (eligible(i)) return i;
        return -1;
    }
    void interruptBySkill(unsigned id, InputTick) {
        // Fast only while every key since the run ended is program-managed;
        // any other key (jump, item, unlisted skill) means a guarded restart.
        if (!managed_[id]) resume_ = Resume::Guard;
        else if (resume_ == Resume::None) {
            resumeDir_ = runCandidate();
            resume_ = resumeDir_ >= 0 ? Resume::Fast : Resume::Guard;
        }
        advanceEpoch();
        // DNF ends a run when a skill starts; recovery decides how it resumes.
        for (auto& direction : dirs_) direction.running = false;
        stableRelease_ = false;
        for (auto& tap : taps_) tap.released = false;
    }
    // A start blocked by a held key (possibly pressed before focus returned,
    // so its Down was never seen here) still resumes once that key is up.
    void deferToSkills() { if (resume_ == Resume::None) resume_ = Resume::Guard; }
    // Runs when the last non-movement key is released and no combo is still
    // playing: a held direction runs again, at once after managed skills that
    // interrupted a run, otherwise after the usual guard.
    void resumeAfterSkills(InputTick now) {
        if (resume_ == Resume::None || busy()) return;
        const auto mode = resume_;
        const int preferred = resumeDir_;
        resume_ = Resume::None;
        resumeDir_ = -1;
        if (!plan_.running || !foreground_ || failed_ || interrupted_) return;
        for (int i = 0; i < 4; ++i) if (usable(i) && dirs_[i].running) return;
        int owner = -1;
        if (mode == Resume::Fast && preferred >= 0 && usable(preferred)) owner = preferred;
        else if (lastDirection_ >= 0 && usable(lastDirection_)) owner = lastDirection_;
        else for (int i = 0; i < 4 && owner < 0; ++i) if (usable(i)) owner = i;
        if (owner < 0) return;
        for (int j = 0; j < 4; ++j) if (j != owner) cancelPending(j);
        auto& state = dirs_[owner];
        state.epoch = epoch_;
        state.full = true;
        state.switching = false;
        state.attempts = 0;
        if (mode == Resume::Fast) {
            state.refresh = true;
            setDirectionOutput(owner, false);
            state.stage = Stage::GapFirst;
            state.due = now + ms(plan_.gapMs);
        } else {
            state.refresh = false;
            setDirectionOutput(owner, true);
            state.stage = Stage::Confirm;
            state.due = now + ms(plan_.guardMs);
        }
    }
    // The previous press of this direction was a short tap whose Down and Up
    // reached the game with nothing in between, and this press follows it
    // quickly: DNF sees the player's own double tap and already runs.
    bool manualDoubleTap(int i, InputTick now) const {
        const auto& tap = taps_[i];
        return tap.pressed && tap.released && lastMovementDown_ == i && !output_[inputId(plan_.directions[i])]
            && tap.release - tap.press <= ms(kManualTapMs) && now - tap.release <= ms(kManualGapMs)
            && tap.up >= tap.down && tap.up - tap.down <= ms(kManualTapMs) && now - tap.up <= ms(kManualGapMs)
            && !busy();
    }
    void beginDirection(int i, InputTick now, bool alreadyDown) {
        auto& state = dirs_[i];
        if (state.active && !alreadyDown) return;
        bool switching = stableRelease_ && now - lastRelease_ <= ms(250);
        bool orthoHeld = false, oppositeHeld = false, orthoRunning = false;
        for (int j = 0; j < 4; ++j) {
            if (j == i) continue;
            if (dirs_[j].running || (dirs_[j].refresh && dirs_[j].stage != Stage::Idle)) switching = true;
            if (!usable(j)) continue;
            if (orthogonal(i, j)) { orthoHeld = true; orthoRunning |= dirs_[j].running; }
            else oppositeHeld = true;
        }
        // Left -> Up run, release Left, then Right while Up is still held: the
        // character still faces Left, so a single Right Down only turns it and
        // drops to walking. A horizontal reversal needs its own double tap even
        // though the vertical axis keeps the session alive.
        const bool reverse = i >= 2 && facing_ == (i ^ 1);
        const bool session = switching && orthoRunning && !oppositeHeld && !reverse;
        const bool manual = !alreadyDown && manualDoubleTap(i, now);
        const InputTick elapsed = lastDirection_ >= 0 ? now - lastPress_ : ms(351);
        const bool sequential = !switching && lastDirection_ >= 0
            && lastDirection_ != i && elapsed <= ms(350);
        lastDirection_ = i;
        lastPress_ = now;
        if (!alreadyDown) taps_[i] = {true, false, now, 0, taps_[i].down, taps_[i].up};
        // Of two held opposite directions the newer one wins (DNF would get
        // both); the older one is restored when the newer is released.
        auto& opposite = dirs_[i ^ 1];
        if (opposite.active && held(i ^ 1) && opposite.suppressedBy < 0) {
            setDirectionOutput(i ^ 1, false);
            opposite.running = false;
            opposite.stage = Stage::Idle;
            opposite.refresh = false;
            opposite.suppressedBy = i;
        }
        if (!orthoHeld && !session) advanceEpoch();
        state.active = true;
        state.suppressedBy = -1;
        state.epoch = epoch_;
        state.refresh = false;
        state.switching = switching;
        state.attempts = 0;
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
        if (session || manual) { markRunning(i); return; }
        if (orthoHeld) {
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
        // A held orthogonal axis without a pending pair (its start was deferred
        // by a skill or gave up) no longer blocks this direction's own start;
        // the command window below still protects quick ↓→ style inputs.
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
        taps_[i].released = taps_[i].pressed;
        taps_[i].release = now;
        if (resumeDir_ == i) resumeDir_ = -1;
        for (int j = 0; j < 4; ++j) {
            auto& suppressed = dirs_[j];
            if (suppressed.suppressedBy != i) continue;
            suppressed.suppressedBy = -1;
            if (!foreground_ || !held(j) || !suppressed.active) continue;
            // Restoring Left after Right only turns the character: that single
            // Down ends the run, so no other axis may keep a stale session.
            if (j >= 2 && facing_ == (j ^ 1))
                for (auto& direction : dirs_) direction.running = false;
            setDirectionOutput(j, true);
            suppressed.epoch = epoch_;
            suppressed.refresh = false;
            suppressed.attempts = 0;
            // The restored older direction runs with its own pair, unless a
            // held skill defers it or another axis already owns a pair.
            if (busy()) deferToSkills();
            if (busy() || pendingOwner(j) >= 0) { suppressed.stage = Stage::Idle; continue; }
            suppressed.stage = Stage::Tap;
            suppressed.due = now + ms(plan_.pressMs);
        }
        if (!foreground_ || pendingOwner(-1) >= 0) return;
        if (busy()) { deferToSkills(); return; }
        int candidate = -1;
        for (int j = 0; j < 4; ++j) {
            if (!usable(j) || dirs_[j].running) continue;
            if (candidate >= 0) return;
            candidate = j;
        }
        if (candidate >= 0) beginDirection(candidate, now, true);
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
                // Run after a combo like after any managed skill, once its
                // trigger is released as well.
                if (!comboActive_) resumeAfterSkills(sink_.clock(now));
            } else {
                comboPhase_ = 0;
                comboDue_ = now + ms(steps[comboStep_].delayMs);
            }
        }
    }
    InputPlan plan_;
    InputSink& sink_;
    std::array<bool, 513> physical_{};
    std::array<bool, 513> directionId_{};
    std::array<bool, 513> managed_{};
    std::array<bool, 513> handoff_{};
    std::array<KeyCode, 513> output_{};
    std::array<Direction, 4> dirs_{};
    std::array<Tap, 4> taps_{};
    std::uint64_t epoch_ = 0;
    bool foreground_ = false, failed_ = false, interrupted_ = false, stableRelease_ = false;
    Resume resume_ = Resume::None;
    int resumeDir_ = -1;
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
