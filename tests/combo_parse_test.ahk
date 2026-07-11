#NoEnv
#SingleInstance, Force

GetOriginKeyName(key) {
    return key
}

Key2SC(key) {
    return key
}

Key2NoVkSC(key) {
    return key
}

SendIP(keyCode) {
}

SetOriginalBlocking(key) {
}

SetOriginalDirect(key) {
}

#Include %A_ScriptDir%\..\core\Combo.ahk

AssertEqual(actual, expected, message) {
    if (actual != expected) {
        FileAppend, Test failed: %message%`nExpected: %expected%`nActual: %actual%`n, *
        ExitApp, 1
    }
}

group := ComboCreateGroup("F1", ["A", "Space", "", "D", "E", "F"], [10, 20, 30, -1, "bad", 60])
AssertEqual(group, "F1>A,10;Space,20;D,0;E,0;F,60", "ComboCreateGroup should keep at most five non-empty steps")

groups := ComboLoadGroups(group . "|Q>W,15;E,25")
AssertEqual(groups.Length(), 2, "ComboLoadGroups should parse multiple groups")
AssertEqual(ComboFindTriggerIndex(groups, "Q"), 2, "ComboFindTriggerIndex should find an existing trigger")
AssertEqual(ComboFindTriggerIndex(groups, "E"), 0, "ComboFindTriggerIndex should return zero for a missing trigger")
ComboUpsertGroup(groups, "Q>R,30", 0)
AssertEqual(groups.Length(), 2, "ComboUpsertGroup should replace duplicate triggers")
AssertEqual(groups[2], "Q>R,30", "ComboUpsertGroup should update the existing group")

global _ComboGroups := groups
global Combo := false
AssertEqual(ComboIsTriggerKey("F1"), 0, "Combo trigger should not reserve normal autofire keys when disabled")
Combo := true
AssertEqual(ComboIsTriggerKey("F1"), 1, "Combo trigger should reserve normal autofire keys when enabled")

serialized := ComboSerializeGroups(groups)
AssertEqual(serialized, "F1>A,10;Space,20;D,0;E,0;F,60|Q>R,30", "ComboSerializeGroups should round-trip")

summary := ComboSummarizeGroup("Q>W,15;E,25")
AssertEqual(summary, "Q -> W(15ms), E(25ms)", "ComboSummarizeGroup should be readable")

ExitApp, 0
