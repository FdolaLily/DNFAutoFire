#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace daf {

// QPC is monotonic and independent of timer resolution changes.
inline std::int64_t qpc_us() noexcept {
    static const std::int64_t frequency = [] {
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        return frequency.QuadPart;
    }();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    // Divide before multiplying to avoid long-running uptime overflow. Preserve
    // integer microseconds so the timer and scheduler share exactly one domain.
    return (counter.QuadPart / frequency) * 1000000
         + (counter.QuadPart % frequency) * 1000000 / frequency;
}

enum class WaitResult { deadline, stop, change, error };

// The caller owns stop/change handles; this object owns its timer only.
// A native worker thread has no AHK message queue to pump. A change event wakes
// it whenever the host publishes a new physical-key/configuration snapshot.
class DeadlineWaiter {
public:
    explicit DeadlineWaiter(HANDLE stop, HANDLE change = nullptr,
                            double spin_us = 0.0) noexcept
        : spin_us_(static_cast<std::int64_t>(std::clamp(spin_us, 0.0, 1000.0))) {
        if (stop) {
            stop_index_ = count_;
            handles_[count_++] = stop;
        }
        if (change) {
            change_index_ = count_;
            handles_[count_++] = change;
        }
        external_count_ = count_;
        // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION: Windows 10 1803 and later.
        timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002,
                                       TIMER_MODIFY_STATE | SYNCHRONIZE);
        high_resolution_ = timer_ != nullptr;
        if (!timer_) {
            timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
            if (timer_) {
                resolution_active_ = timeBeginPeriod(1) == TIMERR_NOERROR;
            }
        }
        if (!timer_) {
            last_error_ = GetLastError();
            return;
        }
        timer_index_ = count_;
        handles_[count_++] = timer_;
    }

    ~DeadlineWaiter() noexcept {
        if (timer_) {
            CancelWaitableTimer(timer_);
            CloseHandle(timer_);
        }
        if (resolution_active_) {
            timeEndPeriod(1);
        }
    }

    DeadlineWaiter(const DeadlineWaiter&) = delete;
    DeadlineWaiter& operator=(const DeadlineWaiter&) = delete;

    bool valid() const noexcept { return timer_ != nullptr; }
    bool high_resolution() const noexcept { return high_resolution_; }
    DWORD last_error() const noexcept { return last_error_; }

    WaitResult wait_until(std::int64_t deadline_us) noexcept {
        if (!timer_) {
            last_error_ = ERROR_INVALID_HANDLE;
            return WaitResult::error;
        }
        if (deadline_us == (std::numeric_limits<std::int64_t>::max)()) {
            // An idle scheduler has no deadline and needs no periodic wakeup.
            if (!external_count_) {
                last_error_ = ERROR_INVALID_PARAMETER;
                return WaitResult::error;
            }
            const DWORD result = WaitForMultipleObjects(external_count_, handles_, FALSE, INFINITE);
            return external_result(result);
        }
        for (;;) {
            // Stop/change takes precedence even when a deadline is overdue.
            const auto external = poll_external();
            if (external != WaitResult::deadline) {
                return external;
            }
            std::int64_t remaining = deadline_us - qpc_us();
            if (remaining <= 0) {
                return WaitResult::deadline;
            }
            if (spin_us_ > 0 && remaining <= spin_us_) {
                // Optional bounded final spin, disabled by default. It never
                // promotes the thread to realtime priority or busy-waits idle.
                unsigned polls = 0;
                do {
                    YieldProcessor();
                    if ((++polls & 31U) == 0) {
                        const auto interrupted = poll_external();
                        if (interrupted != WaitResult::deadline) {
                            return interrupted;
                        }
                    }
                } while (qpc_us() < deadline_us);
                return poll_external();
            }
            remaining -= spin_us_;
            // Negative due times are relative 100ns intervals. Recalculate from
            // the same QPC deadline after every wakeup; never chain Sleep calls.
            LARGE_INTEGER due{};
            // Saturation is only relevant for deliberately distant deadlines.
            const auto bounded = (std::min)(remaining, (std::numeric_limits<std::int64_t>::max)() / 10);
            due.QuadPart = -bounded * 10;
            if (!SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
                last_error_ = GetLastError();
                return WaitResult::error;
            }
            const DWORD result = WaitForMultipleObjects(count_, handles_, FALSE, INFINITE);
            if (result == WAIT_FAILED) {
                last_error_ = GetLastError();
                return WaitResult::error;
            }
            if (result == WAIT_OBJECT_0 + stop_index_) {
                return WaitResult::stop;
            }
            if (result == WAIT_OBJECT_0 + change_index_) {
                return WaitResult::change;
            }
            if (result != WAIT_OBJECT_0 + timer_index_) {
                last_error_ = ERROR_INVALID_DATA;
                return WaitResult::error;
            }
            // A timer signal is a wakeup hint: verify QPC before declaring due.
        }
    }

private:
    WaitResult poll_external() noexcept {
        if (!external_count_) {
            return WaitResult::deadline;
        }
        const DWORD result = WaitForMultipleObjects(external_count_, handles_, FALSE, 0);
        if (result == WAIT_TIMEOUT) {
            return WaitResult::deadline;
        }
        return external_result(result);
    }

    WaitResult external_result(DWORD result) noexcept {
        if (result == WAIT_OBJECT_0 + stop_index_) {
            return WaitResult::stop;
        }
        if (result == WAIT_OBJECT_0 + change_index_) {
            return WaitResult::change;
        }
        last_error_ = result == WAIT_FAILED ? GetLastError() : ERROR_INVALID_DATA;
        return WaitResult::error;
    }

    HANDLE handles_[3]{};
    HANDLE timer_ = nullptr;
    DWORD count_ = 0;
    DWORD external_count_ = 0;
    DWORD stop_index_ = MAXDWORD - 1;
    DWORD change_index_ = MAXDWORD - 1;
    DWORD timer_index_ = MAXDWORD - 1;
    DWORD last_error_ = 0;
    bool high_resolution_ = false;
    bool resolution_active_ = false;
    std::int64_t spin_us_ = 0;
};

} // namespace daf
