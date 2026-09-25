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
    check(rig.model.isRunning(3) && rig.model.isRunning(0), "orthogonal chord inherits first axis running session");
    check(rig.sink.edges.size() == 4, "only the first diagonal axis gets a double tap");
    Rig late; late.run(); late.key(up, true, 200);
    check(late.model.isRunning(0) && late.model.deadline() == InputModel::never, "late orthogonal axis joins without another timer");
    Rig gap; gap.key(right, true, 0); gap.tick(140); gap.key(up, true, 150); gap.tick(170);
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
    holdAndCancel(); diagonalAndOpposite(); commandWindowAndRecovery(); focusAndIdentity();
    comboTimingAndCancellation(); comboContentionAndFailures(); randomizedLifecycle();
    physicalPairingAcrossFocusAndRestart();
    std::cout << "PASS: " << assertions << " input state-machine assertions; no physical input emitted.\n";
}
