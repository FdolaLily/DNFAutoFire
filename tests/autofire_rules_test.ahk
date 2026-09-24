#NoEnv
#NoTrayIcon
#SingleInstance, Force
SetBatchLines, -1

#Include %A_ScriptDir%\..\core\KeyValidation.ahk
#Include %A_ScriptDir%\..\core\AutoFireRules.ahk

global LvRen := false
global ZhanFa := false
global JianZong := false
global ruleConfig := {}
global ruleAssertions := 0

GetNowSelectPreset(){
    return "selected-preset"
}

LoadPreset(presetName, key, defaultValue := ""){
    global ruleConfig
    AssertRule(presetName, "selected-preset", "rules must read the selected preset")
    return ruleConfig.HasKey(key) ? ruleConfig[key] : defaultValue
}

GetOriginKeyName(key){
    aliases := {Num6: "Numpad6", Num5: "Numpad5", NumLk: "NumLock", NumEnter: "NumpadEnter"}
    return aliases.HasKey(key) ? aliases[key] : key
}

AssertRule(actual, expected, message){
    global ruleAssertions
    ruleAssertions += 1
    if (actual != expected) {
        FileAppend, FAIL: %message%`nExpected: %expected%`nActual: %actual%`n, *
        ExitApp, 1
    }
}

rules := AutoFireBuildRules(["A", "sc01E", "Num6", "NumpadRight", "", "InvalidKeyName"])
AssertRule(rules.Length(), 2, "normal key aliases must share output owners")
AssertRule(rules[1].key, "A", "normal key order and first spelling must be retained")
AssertRule(rules[1].manual, true, "normal keys must suppress their physical originals")
AssertRule(rules[1].delayMs, 0, "normal repeats must have no activation delay")
AssertRule(rules[1].triggers.Length(), 0, "normal keys need no extension trigger")
AssertRule(rules[2].key, "Num6", "numeric/navigation aliases must merge by scan code")
AssertRule(AutoFireRuleKeyId("Numpad6") == AutoFireRuleKeyId("Right"), false, "extended arrow keys must stay separate")
AssertRule(AutoFireRuleKeyId("Pause") == AutoFireRuleKeyId("NumLock"), false, "Pause and NumLock must stay separate")
rules := AutoFireBuildRules(["Pause", "NumLk"])
AssertRule(rules.Length(), 2, "Pause and NumLock require separate owners")

LvRen := true
ruleConfig := {LvRenShotKey: "Num6", LvRenSkillKeys: "A|sc01E|B||InvalidKeyName|Numpad5|NumpadClear"}
rules := AutoFireBuildRules(["NumpadRight"])
AssertRule(rules.Length(), 1, "an extension must merge into a normal owner for the same scan code")
AssertRule(rules[1].manual, true, "extension merging must preserve ordinary autofire")
AssertRule(rules[1].triggers.Length(), 3, "extension triggers must be validated and deduplicated")
AssertRule(rules[1].triggers[1], "A", "trigger order must remain deterministic")
AssertRule(rules[1].triggers[2], "B", "distinct trigger sources must remain present")
rules := AutoFireBuildRules([])
AssertRule(rules[1].manual, false, "extension-only output must not swallow its own physical key")

ZhanFa := true
ruleConfig.ZhanFaShotKey := "NumpadRight"
ruleConfig.ZhanFaSkillKeys := "C|B"
rules := AutoFireBuildRules([])
AssertRule(rules.Length(), 1, "two professions targeting the same scan code must share one owner")
AssertRule(rules[1].triggers.Length(), 4, "multiple professions must union their trigger sources")
AssertRule(rules[1].triggers[4], "C", "a new profession must append distinct trigger keys")
LvRen := false
ruleConfig.ZhanFaShotKey := "A"
AssertRule(AutoFireBuildRules([]).Length(), 0, "ZhanFa non-numpad output must be rejected")
ruleConfig.ZhanFaShotKey := "NumLock"
AssertRule(AutoFireBuildRules([]).Length(), 0, "NumLock must not be accepted as a ZhanFa shot key")
ruleConfig.ZhanFaShotKey := "Num6"
AssertRule(AutoFireBuildRules([]).Length(), 1, "a GUI numpad alias must remain a valid shot key")
ruleConfig.ZhanFaSkillKeys := "||InvalidKeyName"
AssertRule(AutoFireBuildRules([]).Length(), 0, "empty valid trigger sets must not create unused owners")

ZhanFa := false
JianZong := true
ruleConfig := {JianZongSkillKey: "A", JianZongDelay: 350}
rules := AutoFireBuildRules(["sc01E", "B"])
AssertRule(rules.Length(), 2, "JianZong must reuse an existing normal key owner")
AssertRule(rules[1].manual, false, "JianZong must preserve the original first physical press")
AssertRule(rules[1].delayMs, 350, "JianZong must use its configured activation delay")
AssertRule(rules[1].triggers.Length(), 1, "JianZong must activate from its own physical skill key")
AssertRule(rules[1].triggers[1], "A", "the delayed trigger must be the skill key")
AssertRule(rules[2].manual, true, "JianZong must not change unrelated ordinary keys")
AssertRule(AutoFireRuleNormalizeDelay(0), 1, "activation delay must clamp to one millisecond")
AssertRule(AutoFireRuleNormalizeDelay(20000), 10000, "activation delay must clamp to ten seconds")
AssertRule(AutoFireRuleNormalizeDelay("bad"), 200, "invalid activation delay must use the existing default")
AssertRule(AutoFireRuleNormalizeDelay("1.5"), 200, "fractional activation delay must be rejected")

LvRen := true
ruleConfig.LvRenShotKey := "sc01E"
ruleConfig.LvRenSkillKeys := "B|A"
rules := AutoFireBuildRules(["A"])
AssertRule(rules.Length(), 1, "all sources must retain a single output owner")
AssertRule(rules[1].manual, false, "a shared JianZong output must still honor the activation delay")
AssertRule(rules[1].triggers.Length(), 2, "JianZong must retain other trigger sources without duplicates")
ruleConfig.JianZongSkillKey := "InvalidKeyName"
LvRen := false
AssertRule(AutoFireBuildRules([]).Length(), 0, "invalid JianZong key must not create a native rule")

FileAppend, autofire rule tests passed: %ruleAssertions% assertions; no keyboard input was injected`n, *
ExitApp, 0
