#include "../native/schedule.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>

using daf::Edge;
using daf::Schedule;
using daf::Tick;
using Held = std::array<bool, daf::kMaxKeys>;
using Edges = std::array<Edge, daf::kMaxKeys>;

static std::size_t assertions = 0;

static void check(bool condition, const char* description) {
    ++assertions;
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        std::exit(1);
    }
}

static Held held(std::initializer_list<std::size_t> keys) {
    Held result{};
    for (const auto key : keys)
        result.at(key) = true;
    return result;
}

static std::size_t commit_due(Schedule& schedule, Tick now, Edges& events) {
    const auto count = schedule.due(now, events.data());
    for (std::size_t i = 0; i < count; ++i)
        schedule.commit(events[i], now);
    return count;
}

static void single(Schedule& schedule, Tick now, std::size_t key, bool down) {
    Edges events;
    const auto count = commit_due(schedule, now, events);
    check(count == 1, "exactly one edge must be ready");
    check(events[0].key == key, "ready edge has expected key identity");
    check(events[0].down == down, "ready edge has expected polarity");
}

static void normal_grid() {
    for (const Tick duration : {10000, 5000, 2000, 1000}) {
        Schedule schedule(1, duration, duration);
        check(schedule.set_held(held({0}).data(), 100000), "initial press changes membership");
        Edges events;
        for (Tick cycle = 0; cycle < 10000; ++cycle) {
            const Tick planned = 100000 + cycle * duration * 2;
            const Tick down_time = planned + cycle % 3 * duration / 20;
            const Tick up_time = planned + duration + cycle % 4 * duration / 25;
            check(schedule.due(down_time, events.data()) == 1, "normal Down is due");
            check(events[0].down && events[0].deadline == planned, "Down stays on absolute phase grid");
            schedule.commit(events[0], down_time);
            check(schedule.next_deadline() == planned + duration, "small lateness does not postpone the Up deadline");
            check(schedule.due(up_time, events.data()) == 1, "normal Up is due");
            check(!events[0].down && events[0].deadline == planned + duration, "Up retains its absolute deadline");
            schedule.commit(events[0], up_time);
            check(schedule.next_deadline() == planned + duration * 2, "10000 jittered cycles do not accumulate drift");
        }
        check(schedule.skipped() == 0, "ordinary lateness must not skip pulses");
    }
}

static void stalled_worker() {
    for (const Tick duration : {10000, 1000}) {
        Schedule schedule(1, duration, duration);
        schedule.set_held(held({0}).data(), 0);
        const Tick late_down = duration * 19 / 10;
        single(schedule, late_down, 0, true);
        const Tick safe_up = late_down + duration / 2;
        check(schedule.next_deadline() == safe_up, "late Down retains half-target minimum width");
        Edges events;
        check(schedule.due(safe_up - 1, events.data()) == 0, "late Down cannot immediately collapse to Up");
        single(schedule, safe_up, 0, false);
        check(schedule.due(safe_up, events.data()) == 0, "late Up cannot immediately collapse to Down");
        check(schedule.next_deadline() == duration * 4, "late edge skips an expired pulse slot");
        check(schedule.skipped() == 1, "missed slot is recorded");
        check(schedule.next_deadline() >= safe_up + schedule.min_up(), "late Up retains minimum release window");
    }

    Schedule schedule(1, 10000, 10000);
    schedule.set_held(held({0}).data(), 0);
    single(schedule, 0, 0, true);
    single(schedule, 45000, 0, false);
    check(schedule.next_deadline() == 60000, "long stall drops overdue cycles");
    check(schedule.skipped() == 2, "long stall accounts for two skipped cycles");
    Edges events;
    check(schedule.due(59999, events.data()) == 0, "no backlog replay after long stall");
    single(schedule, 60000, 0, true);
    single(schedule, 1000000000, 0, false);
    check(schedule.next_deadline() >= 1000005000, "very long stalls retain release window");
    check(schedule.due(1000000000, events.data()) == 0, "very long stalls cannot replay a burst");
}

static void phases_and_membership() {
    Schedule schedule(4, 10000, 10000);
    auto input = held({0, 1, 2});
    schedule.set_held(input.data(), 100000);
    check(schedule.state(0).next == 100000, "three-key first phase is zero");
    check(schedule.state(1).next == 108000, "three-key second phase is 8ms");
    check(schedule.state(2).next == 116000, "three-key third phase is 16ms");
    check(!schedule.set_held(input.data(), 105000), "same identity set retains epoch");
    check(schedule.state(1).next == 108000, "unchanged members do not shift deadlines");
    input = held({0, 1, 2, 3});
    schedule.set_held(input.data(), 200000);
    check(schedule.state(3).next == 212000, "four-key dynamic staggering stays scaled to the cycle");

    Schedule fast(3, 1000, 1000);
    fast.set_held(held({0, 1, 2}).data(), 0);
    check(fast.state(1).next == 800 && fast.state(2).next == 1600, "fast phases preserve fractional milliseconds");

    Schedule swapped(3, 10000, 10000);
    swapped.set_held(held({0, 1}).data(), 0);
    single(swapped, 0, 0, true);
    single(swapped, 8000, 1, true);
    check(swapped.set_held(held({0, 2}).data(), 9000), "same key count with different members changes schedule");
    check(swapped.state(0).next == 10000, "member change preserves an existing Down's release deadline");
    check(swapped.state(2).next == 17000, "new member receives phase from configuration order");
    check(swapped.next_deadline() == 0, "removed Down key requires immediate release");
    single(swapped, 9000, 1, false);
    single(swapped, 10000, 0, false);
    single(swapped, 17000, 2, true);
}

static void release_and_focus() {
    Schedule schedule(2, 10000, 10000);
    schedule.set_held(held({0, 1}).data(), 0);
    single(schedule, 0, 0, true);
    single(schedule, 8000, 1, true);
    schedule.deactivate(8250);
    check(schedule.has_work(), "focus loss retains ownership until Up is submitted");
    Edges events;
    const auto count = commit_due(schedule, 8250, events);
    check(count == 2, "focus loss releases every logical Down");
    for (std::size_t i = 0; i < count; ++i)
        check(!events[i].down, "focus loss never generates Down");
    check(!schedule.has_work() && schedule.next_deadline() == daf::kNever, "focus loss becomes idle after releases");
    check(schedule.due(1000000, events.data()) == 0, "inactive schedule emits no later edges");
    schedule.set_held(held({0}).data(), 8500);
    check(schedule.next_deadline() >= 13250, "immediate reactivation retains previous release window");
    check(schedule.due(8500, events.data()) == 0, "reactivation cannot collapse the release window");

    Schedule released(1, 10000, 10000);
    released.set_held(held({0}).data(), 0);
    single(released, 0, 0, true);
    released.set_held(held({}).data(), 1);
    single(released, 1, 0, false);
    check(!released.has_work(), "physical release overrides minimum Down width");
    check(released.skipped() == 0, "explicit cancellation is not a missed cycle");
}

static void ordering_and_capacity() {
    Schedule schedule(3, 10000, 10000);
    schedule.set_held(held({0, 1}).data(), 0);
    single(schedule, 0, 0, true);
    single(schedule, 8000, 1, true);
    schedule.set_held(held({1, 2}).data(), 10000);
    single(schedule, 10000, 0, false);
    Edges events;
    check(schedule.due(18000, events.data()) == 2, "Up and Down can share a wakeup");
    check(!events[0].down && events[1].down, "Up is ordered before Down");
    check(schedule.due(18000, events.data(), 1) == 1 && !events[0].down, "bounded output prioritizes releases");
    check(schedule.due(18000, nullptr, 0) == 0, "zero-capacity collection writes nothing");

    Schedule full(daf::kMaxKeys, 10000, 10000);
    Held all;
    all.fill(true);
    full.set_held(all.data(), 0);
    check(commit_due(full, 16000, events) == daf::kMaxKeys, "all 128 configured keys fit the fixed buffer");
    full.deactivate(16001);
    check(commit_due(full, 16001, events) == daf::kMaxKeys, "all 128 keys can be released in one pass");
    check(!full.has_work(), "128-key shutdown leaves no ownership");
}

static void stress_invariants() {
    for (const Tick duration : {10000, 1000}) {
        Schedule schedule(6, duration, duration);
        std::array<bool, 6> sink_down{};
        std::array<Tick, 6> sink_changed;
        sink_changed.fill(-1000000000);
        Tick now = 0;
        std::uint64_t seed = 1729;
        Edges events;
        for (int step = 0; step < 50000; ++step) {
            seed = seed * 48271 % 2147483647;
            now += duration / 10 + duration * static_cast<Tick>(seed % 400) / 100;
            if (step % 13 == 0) {
                Held input{};
                for (std::size_t key = 0; key < 6; ++key)
                    input[key] = seed / (key + 1) % 3 != 0;
                schedule.set_held(input.data(), now);
            }
            const auto count = schedule.due(now, events.data());
            Held seen{};
            for (std::size_t i = 0; i < count; ++i) {
                const auto& event = events[i];
                const auto& state = schedule.state(event.key);
                check(!seen[event.key], "at most one edge per key per pass");
                seen[event.key] = true;
                check(event.down != sink_down[event.key], "output edges strictly alternate");
                if (event.down) {
                    check(state.held, "only currently held keys receive Down");
                    check(now - sink_changed[event.key] >= schedule.min_up(), "irregular wakeups preserve minimum Up window");
                    check(event.deadline <= now, "no Down is emitted early");
                } else if (state.held) {
                    check(now - sink_changed[event.key] >= schedule.min_down(), "irregular wakeups preserve minimum Down window");
                    check(event.deadline <= now, "normal Up is never emitted early");
                }
                sink_down[event.key] = event.down;
                sink_changed[event.key] = now;
                schedule.commit(event, now);
            }
        }
        schedule.deactivate(now);
        commit_due(schedule, now, events);
        for (std::size_t key = 0; key < 6; ++key)
            check(!schedule.state(key).down, "stress cleanup releases every remaining Down");
    }
}

static void bounds_and_long_uptime() {
    Schedule empty(0, 1000, 1000);
    Edges events;
    check(!empty.has_work() && empty.due(0, events.data()) == 0, "empty configuration stays idle");
    for (const auto parameters : {std::array<Tick, 3>{129, 1000, 1000}, {1, 0, 1000}, {1, 1000, -1}}) {
        bool threw = false;
        try {
            Schedule invalid(static_cast<std::size_t>(parameters[0]), parameters[1], parameters[2]);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "invalid capacity and pulse widths are rejected");
    }
    // Beyond 32-bit millisecond uptime: no timeGetTime rollover or precision loss.
    const Tick epoch = 9000000000000;
    Schedule long_running(1, 1000, 1000);
    long_running.set_held(held({0}).data(), epoch);
    single(long_running, epoch, 0, true);
    single(long_running, epoch + 1000, 0, false);
    check(long_running.next_deadline() == epoch + 2000, "64-bit timeline retains microsecond precision after long uptime");
}

int main() {
    normal_grid();
    stalled_worker();
    phases_and_membership();
    release_and_focus();
    ordering_and_capacity();
    stress_invariants();
    bounds_and_long_uptime();
    std::cout << "PASS: " << assertions << " deterministic scheduler assertions; no keyboard input sent.\n";
}
