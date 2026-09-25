#include "../native/client_input_model.h"
#include "../native/client_input_state.h"
#include <cstdlib>
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
    Rig ordinary; ordinary.run(); ordinary.key(b, true, 200); ordinary.key(b, false, 220); ordinary.tick(1000);
    check(ordinary.sink.edges.size() == 3, "unmanaged keys cannot initiate synthetic run recovery");
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
        rig.model.focus(false, 4000000);
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
} // namespace
int main() {
    alternatingDiagonals(); rapidAlternatingDiagonals(); customMovementTiming();
    holdAndCancel(); diagonalAndOpposite(); diagonalStartEdges(); commandWindowAndRecovery(); focusAndIdentity();
    comboTimingAndCancellation(); comboContentionAndFailures(); randomizedLifecycle();
    physicalPairingAcrossFocusAndRestart();
    std::cout << "PASS: " << assertions << " input state-machine assertions; no physical input emitted.\n";
}
