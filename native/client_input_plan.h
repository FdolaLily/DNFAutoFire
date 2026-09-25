#pragma once
#include "client_config.h"
#include "client_input_model.h"
#include "client_input_state.h"
#include <array>
#include <vector>

namespace dafclient {
// Keys the enabled profession helpers react to (旅人 / 战法 skill keys, 剑宗
// key), under the same conditions buildRules() gives them engine triggers.
inline std::vector<Key> professionTriggers(const Profile& profile) {
    std::vector<Key> result;
    const auto add = [&](const std::wstring& name) { const auto key = parseKey(name); if (key) result.push_back(key); };
    if (profile.lvRen && parseKey(profile.lvRenShotKey)) for (const auto& name : profile.lvRenSkillKeys) add(name);
    if (profile.zhanFa && isNumpadKey(profile.zhanFaShotKey)) for (const auto& name : profile.zhanFaSkillKeys) add(name);
    if (profile.jianZong) add(profile.jianZongSkillKey);
    return result;
}
// Why a configured run direction does not run: another enabled feature owns
// that physical key. Returns nullptr when the key is free for running.
inline const wchar_t* runKeyOverride(const Profile& profile, const std::wstring& name) {
    const auto key = parseKey(name);
    if (!key) return nullptr;
    if (profile.combo)
        for (const auto& combo : profile.combos) {
            const auto trigger = parseKey(combo.trigger);
            const bool playable = std::any_of(combo.steps.begin(), combo.steps.end(),
                [](const ComboStep& step) { return bool(parseKey(step.key)); });
            if (trigger && playable && trigger.physical_id() == key.physical_id()) return L"连招触发键";
        }
    for (const auto& trigger : professionTriggers(profile))
        if (trigger.physical_id() == key.physical_id()) return L"职业技能键";
    return nullptr;
}
// What a physical key Down means as a hotkey. Quick switch works in any window; the
// auto-fire power switch only while DNF is in the foreground; the run toggle only there
// and only while auto-fire runs (it also fires with extra modifiers held, like the old
// AHK `*` hotkey). One press fires at most one of them, in that order.
enum class HotkeyAction { None, QuickSwitch, Power, ToggleRun };
inline HotkeyAction hotkeyAction(const Hotkey& quick, const Hotkey& power, const Hotkey& run,
                                 unsigned physicalId, unsigned modifiers, bool inDnf, bool enabled) {
    const auto matches = [&](const Hotkey& hotkey, bool anyModifiers) {
        if (!hotkey || hotkey.key.physical_id() != physicalId) return false;
        return anyModifiers ? (modifiers & hotkey.modifiers) == hotkey.modifiers : modifiers == hotkey.modifiers;
    };
    if (matches(quick, false)) return HotkeyAction::QuickSwitch;
    if (inDnf && matches(power, false)) return HotkeyAction::Power;
    if (inDnf && enabled && matches(run, true)) return HotkeyAction::ToggleRun;
    return HotkeyAction::None;
}
inline InputPlan makeInputPlan(const Profile& profile, const Settings& settings, bool enabled) {
    InputPlan plan;
    const auto run = resolveRunSettings(profile, settings);
    plan.running = enabled && run.enabled;
    plan.pressMs = run.pressMs; plan.gapMs = run.gapMs; plan.guardMs = run.guardMs;
    if (plan.running)
        for (unsigned i = 0; i < 4; ++i)
            if (!runKeyOverride(profile, run.keys[i])) plan.directions[i] = parseKey(run.keys[i]).descriptor();
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
    // Legacy StartComboHotkeys ran after run bindings, so a playable combo's
    // trigger wins over a direction on the same physical key (runKeyOverride
    // above). Profession triggers win the same way: otherwise the run hook
    // would swallow the key before the autofire engine ever saw it.
    // Program-managed skills (manual autofire keys and profession triggers)
    // re-run a direction at once after release; see InputPlan::refreshKeys.
    if (enabled) {
        for (const auto& rule : buildRules(profile, settings)) {
            if (rule.manual) plan.refreshKeys[inputId(rule.descriptor)] = true;
            for (unsigned t = 0; t < rule.triggerCount; ++t) plan.refreshKeys[inputId(rule.triggers[t])] = true;
        }
    }
    return plan;
}

// What a restarted controller (run toggle, quick switch, settings change)
// carries over for keys that are still physically held. previous[id]: 0 = not
// a movement key of the previous plan, 1 = movement key the game did not hold
// at stop, 2 = movement key held Down in the game at stop.
struct Inheritance { std::vector<KeyCode> others, handBack, directions; };
inline Inheritance inheritHeld(const std::array<PhysicalPressState, 513>& physical,
                               const std::array<unsigned char, 513>& previous, const InputPlan& plan, bool fresh) {
    Inheritance result;
    // A stale ledger (no hook ran since) must never inject movement.
    if (!fresh) return result;
    std::array<bool, 513> direction{}, trigger{};
    if (plan.running) for (const auto key : plan.directions) if (key) direction[inputId(key)] = true;
    for (const auto& combo : plan.combos) trigger[inputId(combo.trigger)] = true;
    for (unsigned id = 0; id < physical.size(); ++id) {
        const auto& state = physical[id];
        if (!state.down || !state.code || inputId(state.code) != id) continue;
        if (direction[id]) result.directions.push_back(state.code);        // start a fresh hold
        else if (previous[id] == 2 && !trigger[id]) result.handBack.push_back(state.code); // keep walking
        else if (previous[id] == 0 || trigger[id]) result.others.push_back(state.code);   // defers running
    }
    return result;
}
} // namespace dafclient
