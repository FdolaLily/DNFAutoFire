#include "../native/client_input_model.h"
#include "../native/client_input_plan.h"
#include "../native/client_input_state.h"
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <string>

using namespace dafclient;
namespace {
constexpr KeyCode up = 0x26'0148, down = 0x28'0150, left = 0x25'014b, right = 0x27'014d;
constexpr KeyCode a = 0x41'001e, b = 0x42'0030, c = 0x43'002e, trigger = 0x70'003b;
std::size_t assertions = 0;
void check(bool condition, const char* text) {
    ++assertions;
    if (!condition) { std::cerr << "FAIL: " << text << '\n'; std::exit(1); }
}
struct FakeSink : InputSink {
    struct Edge { KeyCode key; bool down; InputTick time; };
    std::vector<Edge> edges;
    std::vector<std::pair<KeyCode, bool>> pauses;
    InputTick now = 0, pauseDelay = 0;
    bool failDown = false, failUp = false, failPause = false, lostFocus = false;
    bool send(KeyCode key, bool down) override {
        if (down && (failDown || lostFocus)) return false;
        if (!down && failUp) return false;
        edges.push_back({key, down, now}); return true;
    }
    bool pause(KeyCode key, bool value) override {
        pauses.push_back({key, value});
        if (value) now += pauseDelay;
        return !value || !failPause;
    }
    InputTick clock(InputTick) const override { return now; }
    bool focusLost() const override { return lostFocus; }
};
InputPlan movement() {
    InputPlan plan; plan.running = true; plan.directions = {up, down, left, right};
    plan.refreshKeys[inputId(a)] = true;
    return plan;
}
struct Rig {
    FakeSink sink;
    InputModel model;
    explicit Rig(InputPlan plan = movement()) : model(std::move(plan), sink) { model.focus(true, 0); }
    void key(KeyCode key, bool down, InputTick ms) { sink.now = ms * 1000; model.physical(key, down, sink.now); }
    void tick(InputTick ms) { sink.now = ms * 1000; model.tick(sink.now); }
    void run(KeyCode key = right) { this->key(key, true, 0); tick(140); tick(170); }
    void edge(std::size_t index, KeyCode key, bool down) {
        check(sink.edges.size() > index, "expected edge exists");
        check(sink.edges[index].key == key && sink.edges[index].down == down, "edge has correct identity and direction");
    }
};
void holdAndCancel() {
    Rig rig; rig.key(right, true, 0); rig.tick(139);
    check(rig.sink.edges.size() == 1, "140ms guard emits no premature double tap");
    rig.tick(140); rig.edge(1, right, false);
    rig.tick(169); check(rig.sink.edges.size() == 2, "configured 30ms gap is respected");
    rig.tick(170); rig.edge(2, right, true);
    check(rig.model.isRunning(3), "long hold enters running session");
    rig.key(right, true, 180); check(rig.sink.edges.size() == 3, "typematic never double-triggers");
    rig.key(right, false, 200); rig.edge(3, right, false);
    check(rig.model.deadline() == InputModel::never, "released direction has no timer");
    Rig tap; tap.key(right, true, 0); tap.key(right, false, 139); tap.tick(1000);
    check(tap.sink.edges.size() == 2, "short command tap does not become a run");
    Rig skill; skill.key(right, true, 0); skill.key(a, true, 100); skill.tick(1000);
    check(skill.sink.edges.size() == 1 && !skill.model.isRunning(3), "skill input cancels pending confirmation while preserving movement");
    Rig gap; gap.key(right, true, 0); gap.tick(140); gap.key(a, true, 150); gap.tick(1000);
    check(gap.sink.edges.size() == 3 && gap.model.outputDown(right), "cancellation inside a gap restores the still-held axis");
}
void diagonalAndOpposite() {
    Rig rig; rig.key(right, true, 0); rig.key(up, true, 50); rig.tick(140); rig.tick(170);
    check(!rig.model.isRunning(3) && !rig.model.isRunning(0), "interrupted initial tap cannot confirm diagonal running");
    rig.tick(200); rig.tick(230);
    check(rig.model.isRunning(3) && rig.model.isRunning(0), "orthogonal chord inherits first axis running session");
    check(rig.sink.edges.size() == 6, "first diagonal axis sends a fresh complete double tap");
    Rig late; late.run(); late.key(up, true, 200);
    check(late.model.isRunning(0) && late.model.deadline() == InputModel::never, "late orthogonal axis joins without another timer");
    Rig gap; gap.key(right, true, 0); gap.tick(140); gap.key(up, true, 150); gap.tick(170);
    check(!gap.model.isRunning(3), "direction added during gap interrupts the initial double tap");
    gap.tick(200); gap.tick(230);
    check(gap.model.outputDown(right) && gap.model.outputDown(up), "adding diagonal axis during gap cannot strand either axis");
    Rig opposite; opposite.run(left); opposite.key(right, true, 200);
    opposite.edge(3, left, false); opposite.edge(4, right, true);
    opposite.tick(289); check(opposite.sink.edges.size() == 5, "reverse direction preserves 90ms observation window");
    opposite.tick(290); opposite.tick(320);
    check(opposite.model.isRunning(3) && !opposite.model.outputDown(left), "reverse direction runs while old direction is suppressed");
    opposite.key(right, false, 350); opposite.edge(7, right, false); opposite.edge(8, left, true);
    opposite.tick(380); opposite.tick(410);
    check(opposite.model.isRunning(2) && opposite.model.outputDown(left), "releasing reverse restores still-physically-held old direction");
    // Left run -> Up joins -> Left released (running up, still facing Left) -> Right.
    Rig turn; turn.run(left); turn.key(up, true, 200); turn.key(left, false, 400); turn.key(right, true, 600);
    turn.edge(5, right, true);
    check(turn.model.outputDown(up) && !turn.model.isRunning(3), "reversal against facing does not join the vertical session with one press");
    turn.tick(689); check(turn.sink.edges.size() == 6, "horizontal reversal keeps the 90ms observation window");
    turn.tick(690); turn.edge(6, right, false); turn.tick(720); turn.edge(7, right, true);
    check(turn.model.isRunning(3) && turn.model.isRunning(0) && turn.model.outputDown(up), "horizontal reversal double-taps while vertical axis stays held");
    Rig same; same.run(left); same.key(up, true, 200); same.key(left, false, 400); same.key(left, true, 600);
    check(same.model.isRunning(2) && same.model.deadline() == InputModel::never, "same-facing horizontal still joins the vertical session");
}
void diagonalStartEdges() {
    // Check the emitted sequence, not just the model's running flags. Every
    // initial diagonal run needs two consecutive Down edges on the same axis
    // after the last chord member arrived, with the configured hold and gap.
    const KeyCode keys[] = {up, down, left, right};
    for (int first = 0; first < 4; ++first) {
        for (int second = 0; second < 4; ++second) {
            if (first / 2 == second / 2) continue;
            for (unsigned guard : {140U, 150U, 300U}) {
                for (unsigned join : {0U, 50U, guard - 1, guard + 10, guard + 40, guard + 70}) {
                    auto plan = movement(); plan.guardMs = guard;
                    Rig rig(plan); rig.key(keys[first], true, 0);
                    for (unsigned time = 0; time <= guard + 200; ++time) {
                        if (time == join) rig.key(keys[second], true, time);
                        rig.tick(time);
                        if (time < join || !rig.model.isRunning(first)) continue;
                        check(rig.model.isRunning(second), "completed diagonal start confirms both axes");
                        check(rig.model.outputDown(keys[first]) && rig.model.outputDown(keys[second]),
                            "completed diagonal start holds both movement axes");
                        // A join after a completed single-axis run deliberately
                        // inherits it without synthesizing another double tap.
                        const unsigned singleCompletion = guard + (guard > 250 ? 90 : 30);
                        if (join < singleCompletion) {
                            std::vector<FakeSink::Edge> downs;
                            for (const auto& edge : rig.sink.edges) if (edge.down) downs.push_back(edge);
                            check(downs.size() >= 4, "diagonal start emits a complete fresh pair");
                            const auto& penultimate = downs[downs.size() - 2];
                            const auto& last = downs.back();
                            check(penultimate.key == keys[first] && last.key == keys[first],
                                "two final Down edges use one direction without an intervening chord key");
                            check(penultimate.time >= InputTick(join) * 1000
                                && last.time - penultimate.time >= InputTick(plan.pressMs + plan.gapMs) * 1000,
                                "fresh diagonal double tap respects configured press and gap");
                        }
                        break;
                    }
                    check(rig.model.isRunning(first) && rig.model.isRunning(second), "every chord timing eventually runs");
                }
            }
        }
    }
    Rig skill; skill.key(right, true, 0); skill.key(up, true, 50); skill.tick(140);
    skill.tick(170); skill.key(a, true, 180); skill.tick(1000);
    check(!skill.model.isRunning(3) && !skill.model.isRunning(0)
        && skill.model.outputDown(right) && skill.model.outputDown(up), "skill cancels fresh pair while preserving diagonal walking");
    Rig release; release.key(right, true, 0); release.key(up, true, 50); release.tick(140);
    release.key(right, false, 150); release.tick(500); release.tick(530); release.tick(560); release.tick(590);
    check(!release.model.outputDown(right) && release.model.isRunning(0), "releasing leading axis transfers start to remaining held direction");
    Rig focus; focus.key(right, true, 0); focus.key(up, true, 50); focus.tick(140); focus.tick(170);
    focus.model.focus(false, 180000); focus.tick(1000);
    check(!focus.model.outputDown(right) && !focus.model.outputDown(up)
        && focus.model.deadline() == InputModel::never, "focus loss cancels diagonal pair and releases both axes");
}
void commandWindowAndRecovery() {
    Rig sequence; sequence.key(right, true, 0); sequence.key(right, false, 10); sequence.key(up, true, 50);
    sequence.tick(349); check(sequence.sink.edges.size() == 3, "sequential command waits through 350ms command window");
    sequence.tick(350); sequence.tick(380); sequence.tick(410); sequence.tick(440);
    check(sequence.model.isRunning(0) && sequence.sink.edges.size() == 7, "long guard uses a new complete double tap");
    Rig refresh; refresh.run(); refresh.key(a, true, 200); refresh.key(a, false, 220);
    refresh.edge(3, right, false); refresh.tick(250); refresh.tick(280); refresh.tick(310);
    check(refresh.model.isRunning(3) && refresh.sink.edges.size() == 7, "managed skill release restores confirmed running direction");
    Rig stale; stale.run(); stale.key(a, true, 200); stale.key(b, true, 210); stale.key(a, false, 220); stale.tick(1000);
    check(stale.sink.edges.size() == 3, "other input invalidates stale skill-release recovery");
    // Any other key also ends DNF's run, but it only earns a guarded restart:
    // nothing is sent before the guard elapses after its release.
    Rig ordinary; ordinary.run(); ordinary.key(b, true, 200); ordinary.key(b, false, 220);
    for (InputTick time = 220; time < 360; ++time) ordinary.tick(time);
    check(ordinary.sink.edges.size() == 3 && !ordinary.model.isRunning(3), "unmanaged key waits a full guard before restarting the run");
    for (InputTick time = 360; time <= 1000; ++time) ordinary.tick(time);
    check(ordinary.sink.edges.size() == 7 && ordinary.model.isRunning(3), "unmanaged key release restarts a still-held run with a fresh pair");
    ordinary.edge(3, right, false); ordinary.edge(4, right, true); ordinary.edge(5, right, false); ordinary.edge(6, right, true);
}
void alternatingDiagonals() {
    const KeyCode keys[] = {up, down, left, right};
    for (bool horizontalFirst : {false, true}) {
        for (bool releaseHorizontalFirst : {false, true}) {
            // All keys released first, rolling overlap, and four-key overlap.
            for (int overlap = 0; overlap < 3; ++overlap) {
                for (int spacing : {0, 10, 100, 160}) {
                    Rig rig;
                    InputTick now = 0;
                    const auto advance = [&](InputTick until) {
                        while (now < until) rig.tick(++now);
                    };
                    for (int cycle = 0; cycle < 8; ++cycle) {
                        const int horizontal = cycle % 2 ? 3 : 2;
                        const int vertical = cycle % 2 ? 1 : 0;
                        const int first = horizontalFirst ? horizontal : vertical;
                        const int second = horizontalFirst ? vertical : horizontal;
                        const int oldFirst = releaseHorizontalFirst ? horizontal ^ 1 : vertical ^ 1;
                        const int oldSecond = releaseHorizontalFirst ? vertical ^ 1 : horizontal ^ 1;
                        const auto start = now;
                        if (cycle && overlap == 0) {
                            rig.key(keys[oldFirst], false, now); rig.key(keys[oldSecond], false, now);
                        }
                        rig.key(keys[first], true, now);
                        advance(start + spacing);
                        if (cycle && overlap == 1) rig.key(keys[oldFirst], false, now);
                        rig.key(keys[second], true, now);
                        if (cycle && overlap) {
                            advance(now + 10);
                            rig.key(keys[oldFirst], false, now); rig.key(keys[oldSecond], false, now);
                        }
                        advance(now + 500);
                        check(rig.model.isRunning(horizontal) && rig.model.isRunning(vertical),
                            "alternating opposite diagonals confirms both new axes");
                        check(rig.model.outputDown(keys[horizontal]) && rig.model.outputDown(keys[vertical])
                            && !rig.model.outputDown(keys[horizontal ^ 1]) && !rig.model.outputDown(keys[vertical ^ 1]),
                            "rolling diagonal change holds only the two current axes");
                        bool paired = false;
                        KeyCode last = 0;
                        for (const auto& edge : rig.sink.edges) {
                            if (!edge.down || edge.time < start * 1000) continue;
                            if (last == edge.key && (last == keys[horizontal] || last == keys[vertical])) paired = true;
                            last = edge.key;
                        }
                        check(paired, "each opposite diagonal switch actually emits an uninterrupted double tap");
                    }
                }
            }
        }
    }
}
void rapidAlternatingDiagonals() {
    // Switch again before the previous confirmation/pulse finished, then
    // settle. Rolling overlap must not leave stale suppression or run flags.
    for (bool horizontalFirst : {false, true}) {
        for (InputTick dwell : {20, 70, 120, 180, 250}) {
            Rig rig;
            InputTick now = 0;
            for (int cycle = 0; cycle < 20; ++cycle) {
                const auto horizontal = cycle % 2 ? right : left;
                const auto vertical = cycle % 2 ? down : up;
                rig.key(horizontalFirst ? horizontal : vertical, true, now);
                rig.tick(++now);
                rig.key(horizontalFirst ? vertical : horizontal, true, now);
                if (cycle) {
                    rig.key(cycle % 2 ? left : right, false, now);
                    rig.key(cycle % 2 ? up : down, false, now);
                }
                const auto until = now + dwell;
                while (now < until) rig.tick(++now);
            }
            const auto until = now + 500;
            while (now < until) rig.tick(++now);
            check(rig.model.isRunning(3) && rig.model.isRunning(1), "rapid repeated diagonals eventually run on final held chord");
            check(rig.model.outputDown(right) && rig.model.outputDown(down)
                && !rig.model.outputDown(left) && !rig.model.outputDown(up), "rapid repeated diagonals retain no old output axis");
            rig.model.focus(false, now * 1000);
            check(!rig.model.outputDown(right) && !rig.model.outputDown(down), "rapid diagonal cancellation releases current chord");
        }
    }
    Rig paused; paused.run(left); paused.key(left, false, 300);
    paused.key(up, true, 1000); paused.key(right, true, 1050);
    for (InputTick time = 1050; time <= 1600; ++time) paused.tick(time);
    check(paused.model.isRunning(0) && paused.model.isRunning(3),
        "vertical-first reversal after the old session expires keeps a pending start owner");
}
void customMovementTiming() {
    auto plan = movement(); plan.guardMs = 1; plan.pressMs = 1; plan.gapMs = 1;
    Rig fast(plan); fast.key(right, true, 0);
    check(fast.model.deadline() == 1000, "1ms guard is not raised to 140ms");
    fast.tick(1); fast.tick(2);
    check(fast.model.isRunning(3), "positive custom guard and gap are used by the actual state machine");
    plan.guardMs = 2000; plan.pressMs = 3000; plan.gapMs = 4000;
    Rig slow(plan); slow.key(right, true, 0);
    check(slow.model.deadline() == 2000000, "guard beyond 1000ms is preserved");
    slow.tick(2000);
    check(slow.model.deadline() == 6000000, "gap beyond 1000ms is preserved");
    slow.tick(6000);
    check(slow.model.deadline() == 9000000, "pulse beyond 1000ms is preserved");
    plan.guardMs = (std::numeric_limits<unsigned>::max)();
    Rig large(plan); large.key(right, true, 0);
    check(large.model.deadline() == InputTick(plan.guardMs) * 1000, "millisecond conversion cannot overflow at storage maximum");
}
void focusAndIdentity() {
    Rig rig; rig.run(); rig.model.focus(false, 200000); rig.tick(1000);
    check(!rig.model.outputDown(right) && rig.sink.edges.size() == 4, "focus loss releases owned movement exactly once");
    rig.model.focus(true, 1100000); rig.tick(1200);
    check(rig.sink.edges.size() == 4, "returning focus never resumes stale held movement");
    rig.key(right, false, 1300); rig.key(right, true, 1400); rig.tick(1540); rig.tick(1570);
    check(rig.model.isRunning(3), "fresh physical press works after focus recovery");
    auto plan = movement(); plan.directions[0] = 0x67'0047;
    Rig numpad(plan); numpad.key(0x67'0047, true, 0); numpad.key(0x24'0047, false, 50); numpad.tick(500);
    check(numpad.sink.edges.size() == 2 && !numpad.model.outputDown(0x67'0047), "NumLock changing VK still releases same physical scan code");
    check(inputId(0x13'0045) != inputId(0x90'0045), "Pause and NumLock retain independent physical identities");
    Rig race; race.sink.lostFocus = true; race.key(right, true, 0);
    check(race.model.interrupted() && !race.model.failed(), "foreground race is cancellation, not an input error");
    race.model.focus(false, 0); check(race.sink.edges.empty(), "failed foreground validation injects no events");
}
InputPlan comboPlan() {
    InputPlan plan = movement();
    plan.combos.push_back({trigger, {{a, 25}, {b, 35}}});
    return plan;
}
void comboTimingAndCancellation() {
    Rig rig(comboPlan()); rig.key(trigger, true, 0); rig.tick(24);
    check(rig.sink.edges.empty(), "combo honors delay before its first step");
    rig.tick(25); rig.edge(0, a, true); rig.tick(34);
    check(rig.sink.edges.size() == 1, "combo pulse lasts at least 10ms");
    rig.tick(35); rig.edge(1, a, false); rig.tick(45);
    check(rig.sink.pauses.size() == 2 && !rig.sink.pauses[1].second, "engine lease released only after 10ms post-up gap");
    rig.key(trigger, false, 46); rig.tick(79); check(rig.sink.edges.size() == 2, "second step interval starts after preceding pulse and gap");
    rig.tick(80); rig.tick(90); rig.tick(100);
    check(!rig.model.comboActive() && rig.sink.edges.size() == 4, "trigger release does not truncate the chosen combo");
    Rig cancel(comboPlan()); cancel.key(trigger, true, 0); cancel.tick(25); cancel.model.focus(false, 26000); cancel.tick(1000);
    check(cancel.sink.edges.size() == 2 && !cancel.sink.pauses.back().second, "focus loss cancels combo, releases key and native lease");
    Rig delayed(comboPlan()); delayed.sink.pauseDelay = 50000; delayed.key(trigger, true, 0); delayed.tick(25);
    check(delayed.model.deadline() == 85000, "handoff latency does not shorten 10ms combo press");
    delayed.tick(84); check(delayed.sink.edges.size() == 1, "lease acknowledgement cannot make Up immediately overdue");
    delayed.tick(85); check(delayed.sink.edges.size() == 2, "pulse releases after actual Down timestamp");
    Rig stalled(comboPlan()); stalled.key(trigger, true, 0); stalled.tick(1000); stalled.tick(1000);
    check(stalled.sink.edges.size() == 1, "late scheduling never bursts Down/Up in one timestamp");
}
void comboContentionAndFailures() {
    auto plan = comboPlan(); plan.combos.push_back({c, {{b, 0}}});
    Rig serial(plan); serial.key(trigger, true, 0); serial.key(c, true, 1);
    serial.tick(25); serial.tick(35); serial.tick(45); serial.tick(80); serial.tick(90); serial.tick(100); serial.tick(100);
    serial.edge(4, b, true);
    check(serial.sink.pauses.size() == 5, "concurrent combos serialize ownership of shared skill keys");
    auto axisPlan = movement(); axisPlan.combos.push_back({trigger, {{right, 0}}});
    Rig axis(axisPlan); axis.run(); axis.key(trigger, true, 200); axis.tick(200); axis.tick(210); axis.tick(220);
    check(axis.model.outputDown(right), "combo using a movement key restores its still-held direction");
    Rig failed(comboPlan()); failed.sink.failPause = true; failed.key(trigger, true, 0); failed.tick(25); failed.model.cancel(26000);
    check(failed.model.failed() && failed.sink.edges.empty(), "failed handoff never emits combo Down");
    check(failed.sink.pauses.size() == 2 && !failed.sink.pauses.back().second, "failed acknowledgement still releases the engine pause request");
    Rig upFailure; upFailure.key(right, true, 0); upFailure.sink.failUp = true; upFailure.key(right, false, 10);
    check(upFailure.model.failed() && upFailure.model.outputDown(right), "failed Up remains owned for cleanup retry");
    upFailure.sink.failUp = false; upFailure.model.cancel(20000);
    check(!upFailure.model.outputDown(right), "cleanup retries owned Up after a transient send failure");
}
void randomizedLifecycle() {
    const KeyCode keys[] = {up, down, left, right, a, b, trigger};
    std::uint32_t random = 1777;
    for (int run = 0; run < 200; ++run) {
        Rig rig(comboPlan()); std::array<bool, 7> held{};
        for (int step = 0; step < 500; ++step) {
            random = random * 1664525u + 1013904223u;
            const auto index = random % 7;
            held[index] = !held[index];
            rig.key(keys[index], held[index], step * 7);
            rig.tick(step * 7);
            check(!rig.model.failed(), "randomized legal input does not fail the model");
        }
        for (InputTick time = 3500; time <= 4500; ++time) rig.tick(time);
        const auto settled = rig.sink.edges.size();
        for (InputTick time = 4501; time <= 7500; ++time) rig.tick(time);
        check(rig.sink.edges.size() == settled, "randomized input settles: output stops once input stops");
        rig.model.focus(false, 8000000);
        std::array<bool, 513> outputs{};
        for (const auto& edge : rig.sink.edges) {
            check(outputs[inputId(edge.key)] != edge.down, "owned output edges alternate without duplicate Down or stray Up");
            outputs[inputId(edge.key)] = edge.down;
        }
        for (const bool down : outputs) check(!down, "focus cleanup balances every emitted Down");
    }
}
void physicalPairingAcrossFocusAndRestart() {
    PhysicalPressState outside;
    auto result = outside.observe(true, false);
    check(result.changed && !result.suppress, "first Down outside DNF passes through");
    result = outside.observe(true, true);
    check(!result.changed && result.suppress, "inside-DNF typematic can be suppressed without a new physical press");
    result = outside.observe(false, true);
    check(result.changed && !result.suppress, "Up for an originally passed-through Down must also pass through");

    PhysicalPressState toggle;
    result = toggle.observe(true, true);
    check(result.changed && result.suppress, "first toggle Down is consumed and fires one command");
    auto restarted = toggle; // Controller stop/start transfers this exact state.
    result = restarted.observe(true, true);
    check(!result.changed && result.suppress, "holding toggle across restart never fires a second command");
    result = restarted.observe(false, false);
    check(result.changed && result.suppress, "Up for an owned Down remains consumed even outside its original scope");
    result = restarted.observe(true, true);
    check(result.changed && result.suppress, "a new press after a real Up can fire the next toggle");

    PhysicalPressState windowsKey;
    windowsKey.observe(true, false); // Win initially pressed outside DNF.
    result = windowsKey.observe(false, true);
    check(!result.suppress, "enabling block-Win mid-press never swallows the unowned Up");
}

// ---------------------------------------------------------------- 2026-09-25 audit
// Each case below reproduced a gap found by exhaustive search of the previous
// state machine (see docs/更新日志.md, v0.3.0.x "一键奔跑穷举修复").
void tickRange(Rig& rig, InputTick from, InputTick to) { for (InputTick time = from; time <= to; ++time) rig.tick(time); }
struct Step { InputTick at; KeyCode key; bool down; };
InputTick play(Rig& rig, std::initializer_list<Step> steps) {
    InputTick now = 0;
    for (const auto& step : steps) { tickRange(rig, now, step.at); rig.key(step.key, step.down, step.at); rig.tick(step.at); now = step.at + 1; }
    return now;
}
// Settle for 1s, then count edges over the next 3s.
std::size_t edgesAfterSettling(Rig& rig, InputTick from) {
    tickRange(rig, from, from + 1000);
    const auto before = rig.sink.edges.size();
    tickRange(rig, from + 1001, from + 4000);
    return rig.sink.edges.size() - before;
}
void noEndlessRetap() {
    auto plan = movement(); plan.guardMs = 150;
    Rig three(plan);
    auto end = play(three, {{20, right, true}, {200, down, true}, {220, left, true}, {270, up, true}, {290, up, false}});
    check(edgesAfterSettling(three, end) == 0, "held directions plus a vertical flick settle instead of re-tapping forever");
    check(three.model.outputDown(left) && three.model.outputDown(down) && !three.model.outputDown(right) && !three.model.outputDown(up),
        "newest horizontal and the held vertical remain; the older opposite is suppressed");
    check(three.model.isRunning(2) && three.model.isRunning(1), "the settled diagonal runs");
    Rig restored(plan);
    end = play(restored, {{20, right, true}, {200, left, true}, {380, left, false}, {400, down, true}, {420, up, true}, {440, up, false}});
    check(edgesAfterSettling(restored, end) == 0, "reversal, restore and a vertical flick settle instead of re-tapping forever");
    check(restored.model.isRunning(3) && restored.model.isRunning(1) && restored.model.outputDown(right) && restored.model.outputDown(down),
        "the restored diagonal runs");
    for (int i = 0; i < 4; ++i) check(!restored.model.pending(i), "no direction is left in a double-tap stage");
}
void skillRepressDuringRecovery() {
    Rig rig; rig.run();
    rig.key(a, true, 200); rig.key(a, false, 220); rig.tick(230);
    rig.key(a, true, 240); rig.key(a, false, 260);
    tickRange(rig, 261, 600);
    check(rig.model.isRunning(3) && rig.model.outputDown(right), "re-pressing the skill during its recovery pair still recovers the run");
    const auto& edges = rig.sink.edges; const auto n = edges.size();
    check(n == 9 && edges[n - 4].key == right && !edges[n - 4].down && edges[n - 4].time == 260000 && edges[n - 1].down,
        "recovery restarts with a complete pair from the final release");
    Rig two = [] { auto plan = movement(); plan.refreshKeys[inputId(b)] = true; return Rig(plan); }();
    two.run(); two.key(a, true, 200); two.key(a, false, 220); two.tick(230); two.key(b, true, 240); two.key(b, false, 260);
    tickRange(two, 261, 600);
    check(two.model.isRunning(3), "another managed skill during the recovery pair still recovers the run");
}
void runAfterSkillRelease() {
    Rig held; held.key(a, true, 0); held.key(right, true, 50); tickRange(held, 51, 299);
    check(held.sink.edges.size() == 1 && !held.model.isRunning(3), "a direction pressed during a held skill only walks");
    held.key(a, false, 300); tickRange(held, 300, 439);
    check(held.sink.edges.size() == 1, "running after the skill still waits the full guard");
    tickRange(held, 440, 600);
    check(held.model.isRunning(3) && held.sink.edges.size() == 5, "releasing the skill starts the held direction's run with a fresh pair");
    Rig early; early.key(right, true, 0); early.key(a, true, 80); early.key(a, false, 200); tickRange(early, 200, 500);
    check(early.model.isRunning(3), "a skill inside the guard no longer leaves the held direction walking");
    Rig unseen; unseen.model.focus(false, 0); unseen.key(b, true, 10); unseen.model.focus(true, 20000);
    unseen.key(right, true, 100); tickRange(unseen, 100, 400);
    check(!unseen.model.isRunning(3) && unseen.model.outputDown(right), "a key held from before focus returned defers running");
    unseen.key(b, false, 400); tickRange(unseen, 400, 700);
    check(unseen.model.isRunning(3), "releasing a key whose Down was never seen still lets the held direction run");
}
void managedSkillsTogether() {
    auto plan = movement(); plan.refreshKeys[inputId(b)] = true;
    Rig rig(plan); rig.run(); rig.key(a, true, 200); rig.key(b, true, 250); rig.key(b, false, 300); tickRange(rig, 300, 499);
    check(rig.sink.edges.size() == 3, "releasing one managed skill while another is held sends nothing");
    rig.key(a, false, 500); tickRange(rig, 500, 700);
    check(rig.model.isRunning(3) && rig.sink.edges.size() == 7 && rig.sink.edges[3].time == 500000,
        "the run recovers once the last managed skill is released");
    Rig diagonal; diagonal.run(); diagonal.key(up, true, 200);
    check(diagonal.model.isRunning(0), "orthogonal axis joins the run");
    diagonal.key(a, true, 300); diagonal.key(up, false, 350); diagonal.key(a, false, 400); tickRange(diagonal, 400, 600);
    bool pair = false;
    for (const auto& edge : diagonal.sink.edges) if (edge.key == right && !edge.down && edge.time == 400000) pair = true;
    check(pair && diagonal.model.isRunning(3), "the remaining axis re-runs after its partner was released during the skill");
    diagonal.key(up, true, 700);
    check(diagonal.model.isRunning(0) && diagonal.model.deadline() == InputModel::never, "an axis re-added to the recovered run joins it");
    Rig combo(comboPlan()); combo.run(); combo.key(trigger, true, 200); combo.key(trigger, false, 210);
    tickRange(combo, 200, 299);
    bool quiet = true;
    for (const auto& edge : combo.sink.edges) if (edge.key == right && edge.time >= 200000) quiet = false;
    check(quiet && !combo.model.isRunning(3), "no movement edges while the combo plays");
    tickRange(combo, 300, 500);
    check(combo.model.isRunning(3), "the run recovers after the combo finishes");
    bool recovered = false;
    for (const auto& edge : combo.sink.edges) if (edge.key == right && !edge.down && edge.time == 300000) recovered = true;
    check(recovered, "recovery starts right after the combo's last pulse");
}
void playerDoubleTap() {
    Rig own; own.key(right, true, 0); own.key(right, false, 80); own.key(right, true, 150);
    check(own.model.isRunning(3) && own.model.deadline() == InputModel::never && own.sink.edges.size() == 3,
        "the player's own quick double tap counts as a started run");
    tickRange(own, 150, 600);
    check(own.sink.edges.size() == 3, "nothing is injected after the player's own double tap");
    Rig slow; slow.key(right, true, 0); slow.key(right, false, 300); slow.key(right, true, 350);
    check(!slow.model.isRunning(3) && slow.model.deadline() != InputModel::never, "a long first press is not a double tap");
    Rig late; late.key(right, true, 0); late.key(right, false, 80); late.key(right, true, 300);
    check(!late.model.isRunning(3) && late.model.deadline() != InputModel::never, "a re-press after 200ms is not a double tap");
    Rig skill; skill.key(right, true, 0); skill.key(right, false, 80); skill.key(a, true, 100); skill.key(a, false, 110); skill.key(right, true, 150);
    check(!skill.model.isRunning(3), "a skill between the taps breaks the double tap");
    Rig other; other.key(right, true, 0); other.key(right, false, 80); other.key(up, true, 100); other.key(up, false, 120); other.key(right, true, 150);
    check(!other.model.isRunning(3), "another direction between the taps breaks the double tap");
}
void oppositeFromIdle() {
    Rig rig; rig.key(right, true, 0); rig.key(left, true, 50);
    check(!rig.model.outputDown(right) && rig.model.outputDown(left), "the newer opposite direction replaces the older one");
    tickRange(rig, 50, 500);
    check(rig.model.isRunning(2) && !rig.model.outputDown(right), "the newer opposite direction runs");
    rig.key(left, false, 600); tickRange(rig, 600, 800);
    check(rig.model.isRunning(3) && rig.model.outputDown(right) && !rig.model.outputDown(left), "releasing it restores and runs the older direction");
    Rig vertical; vertical.key(up, true, 0); vertical.key(down, true, 30); tickRange(vertical, 30, 500);
    check(vertical.model.isRunning(1) && !vertical.model.outputDown(up), "vertical opposites follow the same rule");
}
void restartHandoff() {
    Rig adopted; adopted.model.adopt(c, 0); adopted.model.adopt(right, 0);
    check(adopted.sink.edges.size() == 1 && adopted.model.outputDown(right), "a direction held across a restart walks at once");
    tickRange(adopted, 0, 299);
    check(!adopted.model.isRunning(3), "the still-held toggle hotkey defers the run");
    adopted.key(c, false, 300); tickRange(adopted, 300, 600);
    check(adopted.model.isRunning(3), "releasing the toggle hotkey runs the direction held across the restart");
    InputPlan partial = movement(); partial.directions[3] = 0; // Right is no longer a run key.
    Rig handed(partial); handed.model.handBack(right, 0);
    check(handed.model.outputDown(right) && handed.sink.edges.size() == 1, "a former direction still held keeps walking after the restart");
    handed.key(up, true, 10); tickRange(handed, 10, 300);
    check(handed.model.isRunning(0), "a handed-back movement key is not a skill and does not block running");
    handed.key(right, false, 400);
    check(!handed.model.outputDown(right) && handed.sink.edges.back().key == right && !handed.sink.edges.back().down,
        "its physical release ends the handed-back Down");
    Rig lost(partial); lost.model.handBack(right, 0); lost.model.focus(false, 50000);
    check(!lost.model.outputDown(right), "focus loss releases a handed-back key");
    lost.model.focus(true, 60000); const auto count = lost.sink.edges.size(); lost.key(right, false, 100);
    check(lost.sink.edges.size() == count, "its later physical Up sends nothing more");

    std::array<PhysicalPressState, 513> held{};
    std::array<unsigned char, 513> previous{};
    for (const auto key : {right, left, up, c, trigger}) { held[inputId(key)].observe(true, true); held[inputId(key)].code = key; }
    previous[inputId(right)] = 2; previous[inputId(left)] = 1; previous[inputId(up)] = 2;
    InputPlan next = comboPlan(); next.directions = {up, down, 0, 0};
    const auto inherited = inheritHeld(held, previous, next, true);
    check(inherited.directions.size() == 1 && inherited.directions[0] == up, "a held key that is a direction of the new plan restarts as one");
    check(inherited.handBack.size() == 1 && inherited.handBack[0] == right, "an old direction held in game but no longer managed is handed back");
    check(inherited.others.size() == 2, "held skills and combo triggers defer running; an old unheld direction is ignored");
    const auto stale = inheritHeld(held, previous, next, false);
    check(stale.directions.empty() && stale.handBack.empty() && stale.others.empty(), "a stale ledger inherits nothing");
}
void missedPhysicalUps() {
    PhysicalPressState passed; passed.observe(true, false);
    check(passed.stale(false) && !passed.stale(true), "a pass-through key whose Up was missed is detected from async state");
    PhysicalPressState owned; owned.observe(true, true);
    check(!owned.stale(false), "a swallowed key's async state is never trusted");
    check(owned.forget() && !owned.down && !owned.swallowed, "a secure-desktop switch forgets even swallowed presses");
    const auto late = owned.observe(false, true);
    check(!late.changed && !late.suppress, "the late real Up after forgetting passes through and is no new event");
    const auto again = owned.observe(true, true);
    check(again.changed && again.suppress, "the next real press is a new press again");
}
// Simplified DNF: a Down that follows the same key's short Down/Up (press and
// gap <= 250ms, no other movement Down between) runs; a horizontal Down against
// the facing without a double tap, a skill, or releasing all movement walks.
struct EmulatedGame {
    bool out[4]{}, hadDown[4]{}, upAfterDown[4]{}, running = false;
    int facing = -1, lastDownKey = -1;
    InputTick downAt[4]{}, upAt[4]{};
    void edge(int d, bool isDown, InputTick t) {
        if (isDown) {
            const bool pair = hadDown[d] && upAfterDown[d] && lastDownKey == d && upAt[d] - downAt[d] <= 250000 && t - upAt[d] <= 250000;
            if (d >= 2 && facing >= 0 && facing != d && !pair) running = false;
            if (pair) running = true;
            if (d >= 2) facing = d;
            out[d] = hadDown[d] = true; upAfterDown[d] = false; downAt[d] = t; lastDownKey = d;
        } else {
            out[d] = false; upAfterDown[d] = true; upAt[d] = t;
            if (!out[0] && !out[1] && !out[2] && !out[3]) running = false;
        }
    }
};
// Every key sequence up to a length, with representative gaps, must settle into
// the one-key-run contract. `a` is a managed skill, `b` any other key.
void exhaustiveSequences() {
    const KeyCode keys[] = {up, down, left, right, a, b};
    std::size_t sequences = 0;
    const auto simulate = [&](const int* index, const int* gaps, int n) {
        ++sequences;
        auto plan = movement(); plan.guardMs = 150;
        Rig rig(plan);
        bool held[6]{}; std::size_t order[4]{}; std::vector<InputTick> skills;
        InputTick now = 0;
        const auto advance = [&](InputTick until) {
            for (InputTick due; (due = rig.model.deadline()) <= until;) { rig.sink.now = due; rig.model.tick(due); }
        };
        for (int e = 0; e < n; ++e) {
            now += InputTick(gaps[e]) * 1000; advance(now);
            const int k = index[e]; held[k] = !held[k];
            if (held[k] && k < 4) order[k] = std::size_t(e) + 1;
            if (held[k] && k >= 4) skills.push_back(now);
            rig.sink.now = now; rig.model.physical(keys[k], held[k], now); rig.model.tick(now);
        }
        advance(now + 1000000);
        const auto settled = rig.sink.edges.size();
        advance(now + 4000000);
        check(rig.sink.edges.size() == settled, "exhaustive: output stops once input stops");
        std::array<bool, 513> outputs{};
        EmulatedGame game; std::size_t s = 0;
        for (const auto& edge : rig.sink.edges) {
            while (s < skills.size() && skills[s] <= edge.time) { game.running = false; ++s; }
            check(outputs[inputId(edge.key)] != edge.down, "exhaustive: edges alternate without duplicate Down or stray Up");
            outputs[inputId(edge.key)] = edge.down;
            for (int d = 0; d < 4; ++d) if (edge.key == keys[d]) game.edge(d, edge.down, edge.time);
        }
        if (s < skills.size()) game.running = false;
        bool want[4], anyRun = false;
        for (int d = 0; d < 4; ++d) want[d] = held[d];
        for (int d = 0; d < 4; d += 2) if (held[d] && held[d + 1]) want[order[d] > order[d + 1] ? d + 1 : d] = false;
        for (int d = 0; d < 4; ++d) {
            check(held[d] || !rig.model.outputDown(keys[d]), "exhaustive: a released direction is never left Down");
            check(held[d] || !rig.model.isRunning(d), "exhaustive: only held directions are marked running");
            check(want[d] == rig.model.outputDown(keys[d]), "exhaustive: exactly the newest held directions reach the game");
            anyRun |= rig.model.isRunning(d);
        }
        if (held[4] || held[5]) { check(!anyRun, "exhaustive: no run is claimed while a skill key is held"); return; }
        for (int d = 0; d < 4; ++d) check(!want[d] || rig.model.isRunning(d), "exhaustive: directions held alone end up running");
        check(!anyRun || game.running, "exhaustive: a claimed run is one DNF would also be running");
    };
    int index[8], gaps[8];
    const int mixedGaps[] = {0, 40, 100, 170, 400};
    for (int n = 1; n <= 4; ++n) {
        long total = 1; for (int i = 0; i < n; ++i) total *= 6 * 5;
        for (long code = 0; code < total; ++code) {
            long x = code;
            for (int i = 0; i < n; ++i) { index[i] = int(x % 6); x /= 6; gaps[i] = mixedGaps[x % 5]; x /= 5; }
            simulate(index, gaps, n);
        }
    }
    const int rollGaps[] = {20, 50, 100, 180};
    for (int n = 1; n <= 5; ++n) {
        long total = 1; for (int i = 0; i < n; ++i) total *= 4 * 4;
        for (long code = 0; code < total; ++code) {
            long x = code;
            for (int i = 0; i < n; ++i) { index[i] = int(x % 4); x /= 4; gaps[i] = rollGaps[x % 4]; x /= 4; }
            simulate(index, gaps, n);
        }
    }
    check(sequences > 1900000, "exhaustive search covered every sequence");
}
// Hotkeys seen by the hook: quick switch anywhere; the auto-fire power switch only in the
// DNF foreground (running or not); the run toggle only there while auto-fire runs.
void hotkeyDispatch() {
    constexpr unsigned alt = 0x1, ctrl = 0x2, shift = 0x4; // MOD_ALT / MOD_CONTROL / MOD_SHIFT
    const Hotkey quick{Key{L"Tilde", 0x29, 0xC0}, alt}, power{Key{L"F12", 0x58, 0x7B}, alt}, run{Key{L"F10", 0x44, 0x79}, 0};
    const unsigned f12 = 0x58, f10 = 0x44, tilde = 0x29;
    const auto act = [&](unsigned id, unsigned mods, bool inDnf, bool enabled) { return hotkeyAction(quick, power, run, id, mods, inDnf, enabled); };
    check(act(f12, alt, true, false) == HotkeyAction::Power, "power hotkey starts auto-fire from the game");
    check(act(f12, alt, true, true) == HotkeyAction::Power, "power hotkey stops auto-fire from the game");
    check(act(f12, alt, false, true) == HotkeyAction::None, "power hotkey is ignored outside DNF");
    check(act(f12, 0, true, true) == HotkeyAction::None && act(f12, alt | ctrl, true, true) == HotkeyAction::None, "power hotkey needs exact modifiers");
    check(act(tilde, alt, false, false) == HotkeyAction::QuickSwitch, "quick switch works in any window");
    check(act(f10, 0, true, true) == HotkeyAction::ToggleRun && act(f10, shift, true, true) == HotkeyAction::ToggleRun, "run toggle keeps AHK * modifiers");
    check(act(f10, 0, true, false) == HotkeyAction::None && act(f10, 0, false, true) == HotkeyAction::None, "run toggle needs running auto-fire in DNF");
    const Hotkey none{};
    check(hotkeyAction(quick, none, run, f12, alt, true, true) == HotkeyAction::None, "cleared power hotkey never fires");
    const Hotkey altF10{Key{L"F10", 0x44, 0x79}, alt};
    check(hotkeyAction(quick, altF10, run, f10, alt, true, true) == HotkeyAction::Power, "power wins over the wildcard run toggle");
    check(hotkeyAction(Hotkey{Key{L"F12", 0x58, 0x7B}, alt}, power, run, f12, alt, true, true) == HotkeyAction::QuickSwitch, "one press fires one hotkey");
}
} // namespace
int main() {
    alternatingDiagonals(); rapidAlternatingDiagonals(); customMovementTiming();
    holdAndCancel(); diagonalAndOpposite(); diagonalStartEdges(); commandWindowAndRecovery(); focusAndIdentity();
    comboTimingAndCancellation(); comboContentionAndFailures(); randomizedLifecycle();
    physicalPairingAcrossFocusAndRestart();
    noEndlessRetap(); skillRepressDuringRecovery(); runAfterSkillRelease(); managedSkillsTogether();
    playerDoubleTap(); oppositeFromIdle(); restartHandoff(); missedPhysicalUps(); hotkeyDispatch(); exhaustiveSequences();
    std::cout << "PASS: " << assertions << " input state-machine assertions; no physical input emitted.\n";
}
