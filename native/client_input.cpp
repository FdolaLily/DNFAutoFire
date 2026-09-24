#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "client_input.h"
#include "client_input_model.h"
#include "win_timer.h"
#include <atomic>
#include <cwchar>
#include <memory>
#include <new>

extern "C" int AF_PauseKey(void*, unsigned, unsigned, int);

namespace dafclient {
namespace {
constexpr unsigned kQueueSize = 512;
constexpr ULONG_PTR kInputMarker = 0x44414649; // Diagnostic tag only; no trust decisions.
bool isDnf(HWND window) {
    if (!window) return false;
    wchar_t className[128]{};
    if (!GetClassNameW(window, className, 128)) return false;
    if (std::wcscmp(className, L"地下城与勇士")
        && std::wcscmp(className, L"Dungeon & Fighter")
        && std::wcscmp(className, L"Dungeon Fighter Online")) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    wchar_t path[32768]{};
    DWORD size = 32768;
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &size) != FALSE;
    CloseHandle(process);
    const auto* base = std::wcsrchr(path, L'\\');
    return ok && _wcsicmp(base ? base + 1 : path, L"DNF.exe") == 0;
}
InputPlan makePlan(const Profile& profile, const Settings& settings, bool enabled) {
    InputPlan plan;
    const auto run = resolveRunSettings(profile, settings);
    plan.running = enabled && run.enabled;
    plan.pressMs = run.pressMs; plan.gapMs = run.gapMs; plan.guardMs = run.guardMs;
    if (plan.running)
        for (unsigned i = 0; i < 4; ++i) plan.directions[i] = parseKey(run.keys[i]).descriptor();
    if (enabled && profile.combo) {
        for (const auto& configured : profile.combos) {
            const auto trigger = parseKey(configured.trigger);
            if (!trigger) continue;
            InputCombo combo; combo.trigger = trigger.descriptor();
            for (const auto& configuredStep : configured.steps) {
                const auto key = parseKey(configuredStep.key);
                if (key) combo.steps.push_back({key.descriptor(), configuredStep.intervalMs});
            }
            if (!combo.steps.empty()) plan.combos.push_back(std::move(combo));
        }
    }
    // Legacy StartComboHotkeys ran after run bindings. Preserve that priority
    // when a user intentionally assigns the same physical key to both roles.
    for (const auto& combo : plan.combos)
        for (auto& direction : plan.directions)
            if (direction && inputId(direction) == inputId(combo.trigger)) direction = 0;
    // Same set as AF's physical callbacks: only manually managed autofire keys
    // can request post-skill run recovery, never arbitrary keyboard input.
    if (enabled) {
        for (const auto& rule : buildRules(profile, settings))
            if (rule.manual) plan.refreshKeys[inputId(rule.descriptor)] = true;
    }
    return plan;
}
} // namespace

struct Controller::Context final : InputSink {
    struct Event { KeyCode key = 0; bool down = false, focus = false; HWND target = nullptr; InputTick time = 0; };
    InputPlan plan;
    Hotkey quick, toggle;
    std::array<bool, 513> suppress{};
    std::array<bool, 256> virtualDown{};
    std::array<PhysicalPressState, 513> physical{}; // Hook-thread-only press pairing.
    std::array<KeyCode, 513> owned{}; // Successfully submitted Downs, retained through cleanup failures.
    Event queue[kQueueSize]{};
    std::atomic<unsigned> write{0}, read{0};
    std::atomic<HWND> target{nullptr};
    HWND workerTarget = nullptr;
    HWND ui = nullptr;
    void* engine = nullptr;
    bool enabled = false, blockWin = false;
    bool lostFocus = false; // Worker-only; expected focus races are cancellation.
    HANDLE stopEvent = nullptr, changed = nullptr, ready = nullptr;
    HANDLE hookThread = nullptr, workerThread = nullptr;
    DWORD hookThreadId = 0;
    HHOOK hook = nullptr;
    HWINEVENTHOOK foregroundHook = nullptr;
    std::atomic<DWORD> failure{0};
    static thread_local Context* local;

    ~Context() {
        if (hookThread) CloseHandle(hookThread);
        if (workerThread) CloseHandle(workerThread);
        if (stopEvent) CloseHandle(stopEvent);
        if (changed) CloseHandle(changed);
        if (ready) CloseHandle(ready);
    }
    void fail(DWORD code) {
        DWORD empty = 0;
        if (failure.compare_exchange_strong(empty, code ? code : ERROR_WRITE_FAULT))
            PostMessageW(ui, kInputCommandMessage, static_cast<WPARAM>(InputCommand::Error), failure.load());
        if (stopEvent) SetEvent(stopEvent);
    }
    void push(const Event& event) {
        const auto current = write.load(std::memory_order_relaxed);
        const auto next = (current + 1) % kQueueSize;
        if (next == read.load(std::memory_order_acquire)) { fail(ERROR_BUFFER_OVERFLOW); return; }
        queue[current] = event;
        write.store(next, std::memory_order_release);
        SetEvent(changed);
    }
    bool pop(Event& event) {
        const auto current = read.load(std::memory_order_relaxed);
        if (current == write.load(std::memory_order_acquire)) return false;
        event = queue[current];
        read.store((current + 1) % kQueueSize, std::memory_order_release);
        return true;
    }
    void foreground() {
        const auto candidate = GetForegroundWindow();
        const auto verified = isDnf(candidate) ? candidate : nullptr;
        if (target.exchange(verified) != verified)
            push({0, false, true, verified, daf::qpc_us()});
    }
    unsigned modifiers() const {
        unsigned mods = 0;
        if (virtualDown[VK_MENU] || virtualDown[VK_LMENU] || virtualDown[VK_RMENU]) mods |= MOD_ALT;
        if (virtualDown[VK_CONTROL] || virtualDown[VK_LCONTROL] || virtualDown[VK_RCONTROL]) mods |= MOD_CONTROL;
        if (virtualDown[VK_SHIFT] || virtualDown[VK_LSHIFT] || virtualDown[VK_RSHIFT]) mods |= MOD_SHIFT;
        if (virtualDown[VK_LWIN] || virtualDown[VK_RWIN]) mods |= MOD_WIN;
        return mods;
    }
    bool matches(const Hotkey& hotkey, KeyCode key, bool wildcard = false) const {
        if (!hotkey || hotkey.key.physical_id() != inputId(key)) return false;
        // The old run-toggle hotkey uses *; quick-switch uses exact modifiers.
        return wildcard ? (modifiers() & hotkey.modifiers) == hotkey.modifiers : modifiers() == hotkey.modifiers;
    }
    void command(InputCommand command) {
        if (!PostMessageW(ui, kInputCommandMessage, static_cast<WPARAM>(command), 0)) fail(GetLastError());
    }
    static LRESULT CALLBACK keyboard(int code, WPARAM message, LPARAM parameter) {
        auto* self = local;
        if (code != HC_ACTION || !self) return CallNextHookEx(nullptr, code, message, parameter);
        const auto* event = reinterpret_cast<KBDLLHOOKSTRUCT*>(parameter);
        // Synthetic input never becomes a physical press, even with a forged tag.
        if (event->flags & LLKHF_INJECTED) return CallNextHookEx(nullptr, code, message, parameter);
        const KeyCode key = (event->scanCode | ((event->flags & LLKHF_EXTENDED) ? 0x100 : 0))
            | (event->vkCode << 16);
        const auto id = inputId(key);
        const bool down = !(event->flags & LLKHF_UP);
        const bool changed = self->physical[id].down != down;
        if (event->vkCode < 256) self->virtualDown[event->vkCode] = down;
        // The foreground notification can be queued behind the physical event.
        // Refresh verification before swallowing anything in a different window.
        if (GetForegroundWindow() != self->target.load()) self->foreground();
        const auto target = self->target.load();
        const bool inDnf = target && GetForegroundWindow() == target;
        bool suppressByScope = down && inDnf && self->suppress[id];
        const bool quick = changed && down && self->matches(self->quick, key);
        if (quick) self->command(InputCommand::QuickSwitch);
        if (!quick && changed && down && self->enabled && inDnf && self->matches(self->toggle, key, true)) {
            self->command(InputCommand::ToggleRun);
            suppressByScope = true;
        }
        if (down && inDnf && self->blockWin && (event->vkCode == VK_LWIN || event->vkCode == VK_RWIN)) suppressByScope = true;
        const auto transition = self->physical[id].observe(down, suppressByScope);
        if (changed && !self->failure.load()) self->push({key, down, false, target, daf::qpc_us()});
        if (transition.suppress && !self->failure.load()) return 1;
        // Observe before the autofire engine consumes manual skill keys.
        return CallNextHookEx(nullptr, code, message, parameter);
    }
    static void CALLBACK foregroundCallback(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
        if (local) local->foreground();
    }
    static DWORD WINAPI hookMain(void* argument) {
        auto* self = static_cast<Context*>(argument);
        local = self;
        MSG message{};
        PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
        self->hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard, GetModuleHandleW(nullptr), 0);
        self->foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
            nullptr, foregroundCallback, 0, 0, WINEVENT_OUTOFCONTEXT);
        if (!self->hook || !self->foregroundHook) self->fail(GetLastError());
        self->foreground();
        SetEvent(self->ready);
        while (!self->failure.load()) {
            const int result = GetMessageW(&message, nullptr, 0, 0);
            if (result <= 0) { if (result < 0) self->fail(GetLastError()); break; }
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if (self->hook) UnhookWindowsHookEx(self->hook);
        if (self->foregroundHook) UnhookWinEvent(self->foregroundHook);
        local = nullptr;
        return 0;
    }
    bool send(KeyCode key, bool down) override {
        // Down is confined to the verified foreground DNF process. Owned Up
        // cleanup must still run after focus loss so the Windows state clears.
        lostFocus = false;
        if (down && (WaitForSingleObject(stopEvent, 0) != WAIT_TIMEOUT
            || !workerTarget || target.load() != workerTarget
            || GetForegroundWindow() != workerTarget || !isDnf(workerTarget))) {
            lostFocus = true;
            return false;
        }
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        const unsigned scan = key & 0x1ff, vk = key >> 16;
        if (vk == VK_PAUSE) input.ki.wVk = static_cast<WORD>(vk);
        else {
            input.ki.wScan = static_cast<WORD>(scan & 0xff);
            input.ki.dwFlags = KEYEVENTF_SCANCODE | ((scan & 0x100) ? KEYEVENTF_EXTENDEDKEY : 0);
        }
        if (!down) input.ki.dwFlags |= KEYEVENTF_KEYUP;
        input.ki.dwExtraInfo = kInputMarker;
        if (SendInput(1, &input, sizeof(INPUT)) != 1) { fail(GetLastError()); return false; }
        owned[inputId(key)] = down ? key : 0;
        return true;
    }
    bool releaseOwned() {
        for (unsigned attempt = 0; attempt < 3; ++attempt) {
            bool clean = true;
            for (const auto key : owned) {
                if (key && !send(key, false)) clean = false;
            }
            if (clean) return true;
            if (attempt != 2) Sleep(1); // Cleanup only; never in a hook callback.
        }
        return false;
    }
    bool pause(KeyCode key, bool paused) override {
        if (!engine) return true;
        const bool ok = AF_PauseKey(engine, key & 0x1ff, key >> 16, paused ? 1 : 0) != 0;
        if (!ok) fail(ERROR_TIMEOUT);
        return ok;
    }
    InputTick clock(InputTick) const override { return daf::qpc_us(); }
    bool focusLost() const override { return lostFocus; }
    static DWORD WINAPI workerMain(void* argument) {
        auto* self = static_cast<Context*>(argument);
        std::unique_ptr<InputModel> owner;
        try {
        owner = std::make_unique<InputModel>(self->plan, *self);
        auto& model = *owner;
        daf::DeadlineWaiter waiter(self->stopEvent, self->changed);
        if (!waiter.valid()) self->fail(waiter.last_error());
        const auto synchronizeFocus = [&] {
            const HWND verified = self->target.load();
            const HWND effective = verified && GetForegroundWindow() == verified ? verified : nullptr;
            if (self->workerTarget != effective) model.focus(false, daf::qpc_us());
            self->workerTarget = effective;
            // Also recover if a fast out-and-back transition was observed only
            // by this worker; a repeated WinEvent for the same HWND is optional.
            model.focus(effective != nullptr, daf::qpc_us());
        };
        while (!self->failure.load() && WaitForSingleObject(self->stopEvent, 0) == WAIT_TIMEOUT) {
            Event event;
            while (self->pop(event)) {
                if (event.focus) {
                    // Every verified foreground transition invalidates work,
                    // including a pair of transitions drained in one batch.
                    model.focus(false, event.time);
                    self->workerTarget = nullptr;
                }
                synchronizeFocus();
                if (!event.focus) {
                    // Never replay an outside-app physical Down into DNF just
                    // because focus changed while the worker queue was pending.
                    if (event.target != self->workerTarget) model.focus(false, event.time);
                    model.physical(event.key, event.down, event.time);
                    if (event.target != self->workerTarget) synchronizeFocus();
                }
                if (model.interrupted()) model.focus(false, daf::qpc_us());
                if (model.failed()) break;
            }
            synchronizeFocus();
            model.tick(daf::qpc_us());
            if (model.interrupted()) model.focus(false, daf::qpc_us());
            if (model.failed()) { self->fail(ERROR_WRITE_FAULT); break; }
            const auto wait = waiter.wait_until(model.deadline());
            if (wait == daf::WaitResult::stop) break;
            if (wait == daf::WaitResult::error) { self->fail(waiter.last_error()); break; }
        }
        } catch (const std::bad_alloc&) {
            self->fail(ERROR_NOT_ENOUGH_MEMORY);
        } catch (...) {
            self->fail(ERROR_INVALID_DATA);
        }
        // Nothing may unwind across the Windows thread entry point. Cancellation
        // is allocation-free and retains ownership until each Up succeeds.
        if (owner) {
            owner->cancel(daf::qpc_us());
            if (owner->failed()) self->fail(ERROR_WRITE_FAULT);
        }
        self->releaseOwned();
        return 0;
    }
};
thread_local Controller::Context* Controller::Context::local = nullptr;

Controller::~Controller() { stop(); }
bool Controller::start(const Profile& profile, const Settings& settings, void* engine, HWND ui, bool enabled) {
    if (!stop()) return false;
    lastError_ = 0;
    auto* context = new (std::nothrow) Context;
    if (!context) { lastError_ = ERROR_NOT_ENOUGH_MEMORY; return false; }
    context_ = context;
    context->physical = physical_;
    context->virtualDown = virtualDown_;
    try {
    context->plan = makePlan(profile, settings, enabled);
    context->quick = parseHotkey(settings.quickSwitchHotkey);
    context->toggle = parseHotkey(settings.oneKeyRun.toggleHotkey);
    context->enabled = enabled; context->blockWin = settings.blockWin;
    context->engine = engine; context->ui = ui;
    for (auto key : context->plan.directions) if (key) context->suppress[inputId(key)] = true;
    for (const auto& combo : context->plan.combos) context->suppress[inputId(combo.trigger)] = true;
    context->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    context->changed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    context->ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!context->stopEvent || !context->changed || !context->ready) context->fail(GetLastError());
    if (!context->failure.load()) {
        context->workerThread = CreateThread(nullptr, 0, Context::workerMain, context, 0, nullptr);
        if (!context->workerThread) context->fail(GetLastError());
    }
    if (!context->failure.load()) {
        context->hookThread = CreateThread(nullptr, 0, Context::hookMain, context, 0, &context->hookThreadId);
        if (!context->hookThread) context->fail(GetLastError());
        else if (WaitForSingleObject(context->ready, 5000) != WAIT_OBJECT_0) context->fail(ERROR_TIMEOUT);
    }
    if (!context->failure.load()) return true;
    } catch (const std::bad_alloc&) {
        context->fail(ERROR_NOT_ENOUGH_MEMORY);
    } catch (...) {
        context->fail(ERROR_INVALID_DATA);
    }
    stop();
    return false;
}
bool Controller::stop() {
    auto* context = context_;
    if (!context) return true;
    if (context->stopEvent) SetEvent(context->stopEvent);
    if (context->hookThreadId) PostThreadMessageW(context->hookThreadId, WM_QUIT, 0, 0);
    // Retain all state if a join fails: the root must keep the engine alive too.
    if ((context->hookThread && WaitForSingleObject(context->hookThread, 5000) != WAIT_OBJECT_0)
        || (context->workerThread && WaitForSingleObject(context->workerThread, 5000) != WAIT_OBJECT_0)) {
        lastError_ = ERROR_TIMEOUT;
        return false;
    }
    physical_ = context->physical;
    virtualDown_ = context->virtualDown;
    if (!context->releaseOwned()) {
        lastError_ = context->failure.load() ? context->failure.load() : ERROR_WRITE_FAULT;
        return false; // Retain the owned-key ledger; do not silently reconfigure.
    }
    lastError_ = context->failure.load();
    delete context;
    context_ = nullptr;
    return true;
}
DWORD Controller::error() const { return context_ ? context_->failure.load() : lastError_; }
} // namespace dafclient
