#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <algorithm>
#include <new>
#include <cwchar>
#include "schedule.h"
#include "engine_api.h"
#include "win_timer.h"

namespace {
constexpr UINT kPhysicalMessage = WM_APP + 0x31;
// AHK v1.1.37.02 keyboard_mouse.h: KEY_IGNORE_LEVEL(0).
constexpr ULONG_PTR kAhkSendLevelZero = 0xFFC3D44D;
// Unassigned VK used by the AHK version's `vkFFscXX`: DNF still sees the scan
// code, but chat/IME text input gets no WM_CHAR from autofire pulses.
constexpr WORD kTextlessVk = 0xFF;
struct Key {
    unsigned scan, vk, trigger_count = 0, delay_us = 0;
    bool manual = true;
    unsigned triggers[daf::kMaxKeys]{};
    daf::Tick held_since = 0;
    INPUT down{}, up{};
};
unsigned physical_id(unsigned descriptor) {
    return (descriptor >> 16) == VK_PAUSE ? 512 : descriptor & 0x1FF;
}

struct Engine {
    Key keys[daf::kMaxKeys]{};
    std::atomic<bool> physical[513]{};
    std::atomic<bool> paused[daf::kMaxKeys]{};
    std::atomic<unsigned long long> pause_generation[daf::kMaxKeys]{};
    std::atomic<unsigned long long> pause_ack[daf::kMaxKeys]{};
    size_t count = 0;
    unsigned long long down_us = 10000, up_us = 10000;
    unsigned spin_us = 0;
    HWND notify = nullptr;
    ULONG_PTR cookie = 0;
    HANDLE stop = nullptr, changed = nullptr, ready = nullptr;
    HANDLE hook_thread = nullptr, worker_thread = nullptr;
    DWORD hook_thread_id = 0;
    HHOOK keyboard_hook = nullptr;
    HWINEVENTHOOK foreground_hook = nullptr;
    std::atomic<HWND> target{nullptr};
    std::atomic<DWORD> error{0};
    std::atomic<bool> running{false};
    std::atomic<unsigned long long> edges{0}, skipped{0};
    std::atomic<bool> high_resolution{false};
    std::atomic<bool> cleanup_failed{false};
    HMODULE self_reference = nullptr;

    ~Engine() {
        if (hook_thread) CloseHandle(hook_thread);
        if (worker_thread) CloseHandle(worker_thread);
        if (stop) CloseHandle(stop);
        if (changed) CloseHandle(changed);
        if (ready) CloseHandle(ready);
        if (self_reference) FreeLibrary(self_reference);
    }
    void fail(DWORD code) { error.store(code ? code : ERROR_WRITE_FAULT); }
};

// Both callbacks run only on the dedicated hook thread; no AHK callbacks.
thread_local Engine* hook_engine = nullptr;

LRESULT CALLBACK keyboard_proc(int code, WPARAM message, LPARAM value) {
    auto* e = hook_engine;
    if (code == HC_ACTION && e && e->running.load(std::memory_order_relaxed)) {
        const auto* input = reinterpret_cast<KBDLLHOOKSTRUCT*>(value);
        if (!(input->flags & LLKHF_INJECTED)) {
            const unsigned scan = input->scanCode | ((input->flags & LLKHF_EXTENDED) ? 0x100 : 0);
            const bool down = !(input->flags & LLKHF_UP);
            const auto physical = input->vkCode == VK_PAUSE ? 512 : scan & 0x1FF;
            const bool changed = e->physical[physical].exchange(down) != down;
            if (changed) SetEvent(e->changed);
            for (size_t i = 0; i < e->count; ++i) {
                const auto& key = e->keys[i];
                // Pause has E1 semantics; distinguish it from NumLock by VK.
                if ((key.vk == VK_PAUSE ? input->vkCode != VK_PAUSE
                     : key.scan != scan || input->vkCode == VK_PAUSE)) continue;
                if (!key.manual) {
                    // Delayed sword skill: preserve the first physical Down, but
                    // suppress OS typematic repeats which would bypass its delay.
                    const HWND target = e->target.load();
                    if (key.delay_us && down && !changed && target && GetForegroundWindow() == target)
                        return 1;
                    break;
                }
                // Always forward Up, including a key already held when the engine started.
                if (changed || !down) {
                    const WPARAM packed = key.scan | (input->vkCode << 16) | (down ? 0x01000000 : 0);
                    if (!PostMessageW(e->notify, kPhysicalMessage, packed, e->cookie)) {
                        e->fail(GetLastError());
                        SetEvent(e->stop);
                    }
                }
                // Like the AHK `$*scXX` blocking hotkey, the first physical Down
                // passes through once so a chat box receives exactly one
                // character; only OS typematic repeats are swallowed. Pulses are
                // vkFF + scan code: the game reads the scan code, while
                // TranslateMessage cannot turn VK 0xFF into WM_CHAR text.
                // Never swallow a physical Up: its first Down may have passed
                // through outside DNF or before this engine was started. The
                // scheduler still releases only its own injected Down states.
                const HWND target = e->target.load();
                if (target && GetForegroundWindow() == target && down && !changed) return 1;
                break;
            }
        }
    }
    return CallNextHookEx(nullptr, code, message, value);
}

void CALLBACK foreground_proc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    if (hook_engine) SetEvent(hook_engine->changed);
}

DWORD WINAPI hook_main(void* context) {
    auto* e = static_cast<Engine*>(context);
    hook_engine = e;
    MSG message{};
    PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE); // establish queue before ready
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&keyboard_proc), &module);
    e->keyboard_hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_proc, module, 0);
    if (!e->keyboard_hook) e->fail(GetLastError());
    e->foreground_hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        nullptr, foreground_proc, 0, 0, WINEVENT_OUTOFCONTEXT);
    if (!e->foreground_hook) e->fail(GetLastError());
    SetEvent(e->ready);
    while (!e->error.load()) {
        const int result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) break;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (e->keyboard_hook) UnhookWindowsHookEx(e->keyboard_hook);
    if (e->foreground_hook) UnhookWinEvent(e->foreground_hook);
    hook_engine = nullptr;
    return 0;
}

bool is_dnf(HWND window) {
    if (!window) return false;
    wchar_t name[128]{};
    if (!GetClassNameW(window, name, 128)) return false;
    if (std::wcscmp(name, L"地下城与勇士") && std::wcscmp(name, L"Dungeon & Fighter")
        && std::wcscmp(name, L"Dungeon Fighter Online")) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    wchar_t path[32768]{};
    DWORD length = 32768;
    const bool read = QueryFullProcessImageNameW(process, 0, path, &length) != FALSE;
    CloseHandle(process);
    const wchar_t* base = std::wcsrchr(path, L'\\');
    return read && _wcsicmp(base ? base + 1 : path, L"DNF.exe") == 0;
}

bool emit(Engine* e, size_t index, bool down) {
    auto& key = e->keys[index];
    if (SendInput(1, down ? &key.down : &key.up, sizeof(INPUT)) != 1) {
        e->fail(GetLastError());
        return false;
    }
    e->edges.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool requested(Engine* e, const Key& key) {
    if (key.manual && e->physical[physical_id(key.scan | (key.vk << 16))].load()) return true;
    for (unsigned j = 0; j < key.trigger_count; ++j)
        if (e->physical[key.triggers[j]].load()) return true;
    return false;
}

DWORD WINAPI worker_main(void* context) {
    auto* e = static_cast<Engine*>(context);
    // Normal process priority; no affinity or realtime priority that could starve DNF.
    daf::DeadlineWaiter waiter(e->stop, e->changed, e->spin_us);
    if (!waiter.valid()) { e->fail(waiter.last_error()); e->running.store(false); return 0; }
    e->high_resolution.store(waiter.high_resolution());
    daf::Schedule schedule(e->count, e->down_us, e->up_us);
    HWND previous = nullptr;
    DWORD previous_pid = 0;
    bool focused = false;
    daf::Edge due[daf::kMaxKeys];
    bool held[daf::kMaxKeys]{};
    while (WaitForSingleObject(e->stop, 0) != WAIT_OBJECT_0 && !e->error.load()) {
        HWND foreground = GetForegroundWindow();
        DWORD pid = 0;
        if (foreground) GetWindowThreadProcessId(foreground, &pid);
        if (foreground != previous || pid != previous_pid) {
            previous = foreground;
            previous_pid = pid;
            focused = is_dnf(foreground);
            e->target.store(focused ? foreground : nullptr);
        }
        auto now = daf::qpc_us();
        for (size_t i = 0; i < e->count; ++i) {
            auto& key = e->keys[i];
            const bool active = requested(e, key);
            if (!focused || !active) key.held_since = 0;
            else if (!key.held_since) key.held_since = now;
            held[i] = focused && active && !e->paused[i].load()
                && now - key.held_since >= key.delay_us;
        }
        schedule.set_held(held, now);
        const auto count = schedule.due(now, due);
        for (size_t i = 0; i < count; ++i) {
            const auto& edge = due[i];
            if (edge.down && (WaitForSingleObject(e->stop, 0) == WAIT_OBJECT_0
                || !requested(e, e->keys[edge.key]) || e->paused[edge.key].load()
                || GetForegroundWindow() != foreground)) continue;
            if (!emit(e, edge.key, edge.down)) break;
            schedule.commit(edge, daf::qpc_us());
        }
        e->skipped.store(schedule.skipped(), std::memory_order_relaxed);
        for (size_t i = 0; i < e->count; ++i) {
            const auto generation = e->pause_generation[i].load();
            if (e->paused[i].load() && !schedule.state(i).down)
                e->pause_ack[i].store(generation);
        }
        if (e->error.load()) break;
        // Foreground event is the normal wakeup. A 20ms watchdog also handles lost events.
        auto deadline = std::min(schedule.next_deadline(), daf::qpc_us() + 20000);
        for (size_t i = 0; i < e->count; ++i) {
            const auto& key = e->keys[i];
            if (key.held_since && key.delay_us && now < key.held_since + key.delay_us)
                deadline = std::min(deadline, key.held_since + key.delay_us);
        }
        const auto result = waiter.wait_until(deadline);
        if (result == daf::WaitResult::stop) break;
        if (result == daf::WaitResult::error) { e->fail(waiter.last_error()); break; }
    }
    e->target.store(nullptr);
    // Own only keys actually injected Down; physical release/focus loss/stop share this path.
    schedule.deactivate(daf::qpc_us());
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        const auto count = schedule.due(daf::qpc_us(), due);
        if (!count) break;
        for (size_t i = 0; i < count; ++i)
            if (emit(e, due[i].key, false)) schedule.commit(due[i], daf::qpc_us());
        if (attempt != 2) Sleep(1);
    }
    for (size_t i = 0; i < e->count; ++i)
        if (schedule.state(i).down) e->cleanup_failed.store(true);
    e->running.store(false);
    return 0;
}

bool stop(Engine* e) {
    if (!e) return true;
    if (e->stop) SetEvent(e->stop);
    if (e->worker_thread && WaitForSingleObject(e->worker_thread, 3000) != WAIT_OBJECT_0) return false;
    e->running.store(false);
    if (e->hook_thread) {
        PostThreadMessageW(e->hook_thread_id, WM_QUIT, 0, 0);
        if (WaitForSingleObject(e->hook_thread, 3000) != WAIT_OBJECT_0) return false;
    }
    return true;
}
}

#ifdef DAF_STATIC_ENGINE
#define API extern "C"
#else
#define API extern "C" __declspec(dllexport)
#endif
// Per rule: descriptor, manual, delay_us, trigger_count, 128 trigger descriptors.
// descriptor: low 16 bits AHK scan code, high 16 bits virtual key.
API void* AF_Start(const unsigned* descriptors, unsigned count, unsigned long long down_us,
                   unsigned long long up_us, unsigned spin_us, HWND notify, ULONG_PTR cookie) {
    if (!descriptors || !count || count > daf::kMaxKeys || !down_us || down_us > daf::kNever / 1024
        || !up_us || up_us > daf::kNever / 1024 || spin_us > 250 || !IsWindow(notify)) {
        SetLastError(ERROR_INVALID_PARAMETER); return nullptr;
    }
    auto* e = new (std::nothrow) Engine;
    if (!e) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return nullptr; }
    e->count = count; e->down_us = down_us; e->up_us = up_us; e->spin_us = spin_us;
    e->notify = notify; e->cookie = cookie;
#ifndef DAF_STATIC_ENGINE
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(&keyboard_proc), &e->self_reference)) {
        delete e; return nullptr;
    }
#endif
    for (size_t i = 0; i < count; ++i) {
        auto& key = e->keys[i];
        const auto* record = descriptors + i * (4 + daf::kMaxKeys);
        key.scan = record[0] & 0xFFFF;
        key.vk = record[0] >> 16;
        key.manual = record[1] != 0;
        key.delay_us = record[2];
        key.trigger_count = record[3];
        if (key.trigger_count > daf::kMaxKeys || key.delay_us > 10000000) {
            delete e; SetLastError(ERROR_INVALID_PARAMETER); return nullptr;
        }
        for (unsigned j = 0; j < key.trigger_count; ++j) {
            const auto descriptor = record[j + 4];
            if (!(descriptor & 0xFFFF) || (descriptor & 0xFFFF) > 0x1FF
                || !(descriptor >> 16) || (descriptor >> 16) > 0xFF) {
                delete e; SetLastError(ERROR_INVALID_PARAMETER); return nullptr;
            }
            key.triggers[j] = physical_id(descriptor);
        }
        if (!key.scan || key.scan > 0x1FF || !key.vk || key.vk > 0xFF) {
            delete e; SetLastError(ERROR_INVALID_PARAMETER); return nullptr;
        }
        for (size_t j = 0; j < i; ++j) {
            if (e->keys[j].scan == key.scan && (e->keys[j].vk == VK_PAUSE) == (key.vk == VK_PAUSE)) {
                delete e; SetLastError(ERROR_INVALID_PARAMETER); return nullptr;
            }
        }
        key.down.type = INPUT_KEYBOARD;
        key.down.ki.dwExtraInfo = kAhkSendLevelZero;
        if (key.vk == VK_PAUSE) key.down.ki.wVk = VK_PAUSE;
        else {
            // AHK `vkFFscXX`: no KEYEVENTF_SCANCODE, so Windows keeps VK 0xFF
            // instead of mapping the scan code to a character key.
            key.down.ki.wVk = kTextlessVk;
            key.down.ki.wScan = key.scan & 0xFF;
            key.down.ki.dwFlags = (key.scan & 0x100) ? KEYEVENTF_EXTENDEDKEY : 0;
        }
        key.up = key.down;
        key.up.ki.dwFlags |= KEYEVENTF_KEYUP;
    }
    e->stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    e->changed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    e->ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!e->stop || !e->changed || !e->ready) { const auto error = GetLastError(); delete e; SetLastError(error); return nullptr; }
    e->running.store(true);
    e->hook_thread = CreateThread(nullptr, 0, hook_main, e, 0, &e->hook_thread_id);
    if (!e->hook_thread || WaitForSingleObject(e->ready, 3000) != WAIT_OBJECT_0 || e->error.load()) {
        const auto error = e->error.load() ? e->error.load() : ERROR_DLL_INIT_FAILED;
        if (stop(e)) delete e;
        SetLastError(error); return nullptr;
    }
    e->worker_thread = CreateThread(nullptr, 0, worker_main, e, 0, nullptr);
    if (!e->worker_thread) { const auto error = GetLastError(); if (stop(e)) delete e; SetLastError(error); return nullptr; }
    return e;
}

API int AF_Stop(void* context) {
    auto* e = static_cast<Engine*>(context);
    if (!stop(e)) return 0; // retain live memory/module if a thread cannot join
    const bool clean = !e || !e->cleanup_failed.load();
    delete e;
    return clean ? 1 : -1; // joined, but an owned Up could not be submitted
}

API int AF_Physical(void* context, unsigned scan, unsigned vk) {
    auto* e = static_cast<Engine*>(context);
    if (!e) return -1;
    for (size_t i = 0; i < e->count; ++i) {
        if (e->keys[i].scan == scan && (vk == VK_PAUSE) == (e->keys[i].vk == VK_PAUSE))
            return e->physical[physical_id(scan | (vk << 16))].load() ? 1 : 0;
    }
    return -1;
}

API unsigned AF_Error(void* context) {
    auto* e = static_cast<Engine*>(context);
    return e ? e->error.load() : ERROR_INVALID_HANDLE;
}

// Hand a single key to a synchronous combo pulse after the scheduler's Up commits.
API int AF_PauseKey(void* context, unsigned scan, unsigned vk, int pause) {
    auto* e = static_cast<Engine*>(context);
    if (!e) return 1;
    for (size_t i = 0; i < e->count; ++i) {
        if (e->keys[i].scan != scan || (e->keys[i].vk == VK_PAUSE) != (vk == VK_PAUSE)) continue;
        e->paused[i].store(pause != 0);
        const auto generation = e->pause_generation[i].fetch_add(1) + 1;
        SetEvent(e->changed);
        if (!pause) return 1;
        for (unsigned wait = 0; wait < 200; ++wait) {
            if (e->pause_ack[i].load() == generation) return 1;
            if (!e->running.load() || e->error.load()) return 0;
            Sleep(1);
        }
        return 0;
    }
    return 1;
}

API int AF_Stats(void* context, unsigned long long* output) {
    auto* e = static_cast<Engine*>(context);
    if (!e || !output) return 0;
    output[0] = e->edges.load(); output[1] = e->skipped.load();
    output[2] = e->high_resolution.load() ? 1 : 0;
    output[3] = e->running.load() ? 1 : 0;
    return 1;
}
