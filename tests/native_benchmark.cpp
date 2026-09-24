// No SendInput calls: this measures the production scheduler/timer with a
// simulated input sink. Game acceptance and keyboard-hook costs are separate.
#include "../native/schedule.h"
#include "../native/win_timer.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <stdexcept>
#include <numeric>

namespace {
using daf::Tick;

std::uint64_t cpu_100ns() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) {
        throw std::runtime_error("GetThreadTimes failed");
    }
    ULARGE_INTEGER k{}, u{};
    k.LowPart = kernel.dwLowDateTime;
    k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime;
    u.HighPart = user.dwHighDateTime;
    return k.QuadPart + u.QuadPart;
}

struct Metrics {
    std::vector<Tick> late;
    std::vector<Tick> intervals;
    std::array<Tick, daf::kMaxKeys> previous{};
    std::uint64_t downs = 0;
    void record(const daf::Edge& edge, Tick actual) {
        late.push_back((std::max)(Tick{0}, actual - edge.deadline));
        if (edge.down) {
            ++downs;
            if (previous[edge.key]) {
                intervals.push_back(actual - previous[edge.key]);
            }
            previous[edge.key] = actual;
        }
    }
};

double percentile(const std::vector<Tick>& sorted, double fraction) {
    if (sorted.empty()) return 0.0;
    const auto index = static_cast<std::size_t>(std::ceil(fraction * sorted.size())) - 1;
    return static_cast<double>(sorted[(std::min)(index, sorted.size() - 1)]) / 1000.0;
}

void report(const char* backend, std::size_t keys, int hold_ms, int spin_us,
            bool high_res, Metrics& m, Tick elapsed, std::uint64_t cpu,
            std::uint64_t skips) {
    std::sort(m.late.begin(), m.late.end());
    std::sort(m.intervals.begin(), m.intervals.end());
    const double seconds = static_cast<double>(elapsed) / 1000000.0;
    // Frequency from completed same-key intervals avoids counting the initial
    // immediate Down as an extra cycle, especially in short smoke runs.
    const double interval_sum = std::accumulate(m.intervals.begin(), m.intervals.end(), 0.0);
    const double frequency = interval_sum > 0.0 ? m.intervals.size() * 1000000.0 / interval_sum : 0.0;
    const double cpu_percent = (static_cast<double>(cpu) / 10.0) / elapsed * 100.0;
    std::printf("%s,%zu,%d,%d,%d,%.6f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%llu,%llu\n",
        backend, keys, hold_ms, spin_us, high_res ? 1 : 0, seconds, frequency,
        percentile(m.late, .50), percentile(m.late, .95), percentile(m.late, .99), percentile(m.late, 1.0),
        percentile(m.intervals, .50), percentile(m.intervals, .95), percentile(m.intervals, .99), percentile(m.intervals, 1.0),
        cpu_percent, static_cast<unsigned long long>(skips), static_cast<unsigned long long>(m.downs));
    std::fflush(stdout);
}

DWORD WINAPI signal_event(void* event) {
    Sleep(10);
    SetEvent(static_cast<HANDLE>(event));
    return 0;
}

void timer_checks() {
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE change = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!stop || !change) throw std::runtime_error("event creation failed");
    {
        daf::DeadlineWaiter waiter(stop, change);
        if (!waiter.valid()) throw std::runtime_error("timer creation failed");
        SetEvent(stop);
        if (waiter.wait_until(daf::qpc_us() - 1) != daf::WaitResult::stop) {
            throw std::runtime_error("stop must win over expired deadline");
        }
        ResetEvent(stop);
        SetEvent(change);
        if (waiter.wait_until(daf::qpc_us() - 1) != daf::WaitResult::change) {
            throw std::runtime_error("change must win over expired deadline");
        }
        HANDLE signal = CreateThread(nullptr, 0, signal_event, change, 0, nullptr);
        if (!signal) throw std::runtime_error("test thread creation failed");
        const auto result = waiter.wait_until(daf::kNever);
        WaitForSingleObject(signal, INFINITE);
        CloseHandle(signal);
        if (result != daf::WaitResult::change) throw std::runtime_error("idle wait did not wake on change");
        const Tick deadline = daf::qpc_us() + 2000;
        if (waiter.wait_until(deadline) != daf::WaitResult::deadline || daf::qpc_us() < deadline) {
            throw std::runtime_error("timer reported an early or incorrect deadline");
        }
    }
    CloseHandle(change);
    CloseHandle(stop);
}

void run_native(std::size_t keys, int hold_ms, int spin_us, Tick duration_us) {
    daf::DeadlineWaiter waiter(nullptr, nullptr, spin_us);
    if (!waiter.valid()) throw std::runtime_error("native timer unavailable");
    daf::Schedule schedule(keys, hold_ms * 1000, hold_ms * 1000);
    std::array<bool, daf::kMaxKeys> held{};
    for (std::size_t i = 0; i < keys; ++i) held[i] = true;
    std::array<daf::Edge, daf::kMaxKeys> edges{};
    Metrics metrics;
    metrics.late.reserve(static_cast<std::size_t>(duration_us / 1000) * keys + 16);
    metrics.intervals.reserve(static_cast<std::size_t>(duration_us / 1000) * keys / 2 + 16);
    const Tick start = daf::qpc_us();
    const auto cpu_start = cpu_100ns();
    schedule.set_held(held.data(), start);
    while (daf::qpc_us() - start < duration_us) {
        const auto waited = waiter.wait_until(schedule.next_deadline());
        if (waited != daf::WaitResult::deadline) throw std::runtime_error("unexpected timer wake");
        const auto count = schedule.due(daf::qpc_us(), edges.data(), edges.size());
        for (std::size_t i = 0; i < count; ++i) {
            const Tick actual = daf::qpc_us();
            metrics.record(edges[i], actual);
            schedule.commit(edges[i], actual);
        }
    }
    const auto cpu = cpu_100ns() - cpu_start;
    const Tick elapsed = daf::qpc_us() - start;
    report("native", keys, hold_ms, spin_us, waiter.high_resolution(), metrics, elapsed, cpu, schedule.skipped());
}

void run_legacy(int hold_ms, Tick duration_us) {
    Metrics metrics;
    timeBeginPeriod(1);
    const Tick start = daf::qpc_us();
    const auto cpu_start = cpu_100ns();
    Tick deadline = start;
    bool down = true;
    while (daf::qpc_us() - start < duration_us) {
        const Tick actual = daf::qpc_us();
        metrics.record(daf::Edge{0, down, deadline}, actual);
        deadline += hold_ms * 1000;
        down = !down;
        Sleep(static_cast<DWORD>(hold_ms));
    }
    const auto cpu = cpu_100ns() - cpu_start;
    const Tick elapsed = daf::qpc_us() - start;
    timeEndPeriod(1);
    report("relative_sleep", 1, hold_ms, 0, false, metrics, elapsed, cpu, 0);
}
} // namespace

int main(int argc, char** argv) {
    try {
        // Optional duration argument permits a fast smoke run; the normal
        // report uses two seconds for every configuration.
        const double duration_s = argc > 1 ? std::atof(argv[1]) : 2.0;
        if (duration_s <= 0.0 || duration_s > 60.0) return 2;
        const Tick duration_us = static_cast<Tick>(duration_s * 1000000.0);
        timer_checks();
        std::puts("backend,keys,hold_ms,spin_us,high_resolution,elapsed_s,hz_per_key,lateness_p50_ms,lateness_p95_ms,lateness_p99_ms,lateness_max_ms,interval_p50_ms,interval_p95_ms,interval_p99_ms,interval_max_ms,cpu_one_core_percent,skipped_cycles,down_events");
        // Optional targeted run: seconds keys hold_ms spin_us.
        if (argc == 5) {
            const int keys = std::atoi(argv[2]), hold = std::atoi(argv[3]), spin = std::atoi(argv[4]);
            if (keys < 1 || keys > static_cast<int>(daf::kMaxKeys) || hold < 1 || hold > 100 || spin < 0 || spin > 1000) return 2;
            run_native(static_cast<std::size_t>(keys), hold, spin, duration_us);
            return 0;
        }
        for (int hold : {1, 2, 5, 10}) run_legacy(hold, duration_us);
        for (int spin : {0, 50, 100}) {
            for (std::size_t keys : {std::size_t{1}, std::size_t{3}}) {
                for (int hold : {1, 2, 5, 10}) run_native(keys, hold, spin, duration_us);
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "benchmark failed: %s\n", error.what());
        return 1;
    }
}
