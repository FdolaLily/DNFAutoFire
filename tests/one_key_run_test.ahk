#NoEnv
#SingleInstance, Force

#Include %A_ScriptDir%\..\core\KeyConvert.ahk
#Include %A_ScriptDir%\..\core\OneKeyRun.ahk

LoadPreset(presetName, type, default := "") {
    return default
}

SavePreset(presetName, type, value) {
}

GetNowSelectPreset() {
    return "test"
}

GetAllKeys() {
    return ["W", "S", "A", "D", "Space", "Numpad5"]
}

LoadConfig(type, default := "") {
    return default
}

SaveConfig(type, value) {
}

LoadLastPreset() {
    return "test"
}

ShowTip(text) {
}

GetOriginKeyName(key) {
    return key
}

IsValueInArray(value, array) {
    for _, element in array {
        if (element == value) {
            return true
        }
    }
    return false
}

AssertEqual(actual, expected, message) {
    if (actual != expected) {
        FileAppend, Test failed: %message%`nExpected: %expected%`nActual: %actual%`n, *
        ExitApp, 1
    }
}

AppendTestEvent(events, eventName) {
    events.Push(eventName)
}

GetTestPhysicalKeyState(states, key) {
    return states.HasKey(key) ? states[key] : false
}

AssertAtomicCommitOrder() {
    events := []
    timerFn := Func("AppendTestEvent").Bind(events, "SecondDirection")
    SetTimer, %timerFn%, -10
    Critical, On
    events.Push("FirstUp")
    Sleep, 30
    events.Push("FirstDown")
    Critical, Off
    Sleep, 30
    actual := ""
    for _, eventName in events {
        actual .= (actual == "" ? "" : ",") . eventName
    }
    AssertEqual(actual, "FirstUp,FirstDown,SecondDirection", "run commit must finish before a diagonal direction thread can interrupt it")
}

runKeys := ["W", "S", "A", "D"]
OneKeyRunUpKey := "W"
OneKeyRunDownKey := "S"
OneKeyRunLeftKey := "A"
OneKeyRunRightKey := "D"
AssertEqual(OneKeyRunContainsKey(runKeys, "W"), true, "custom Up key must enable one-key running")
AssertEqual(OneKeyRunContainsKey(runKeys, "S"), true, "custom Down key must enable one-key running")
AssertEqual(OneKeyRunContainsKey(runKeys, "A"), true, "custom Left key must enable one-key running")
AssertEqual(OneKeyRunContainsKey(runKeys, "D"), true, "custom Right key must enable one-key running")
AssertEqual(OneKeyRunContainsKey(runKeys, "Up"), false, "physical arrow keys must not be hard-coded")
AssertEqual(OneKeyRunContainsKey(runKeys, "Q"), false, "ordinary configured keys must keep autofire")
AssertEqual(OneKeyRunGetDelay(45), 45, "configured run delay must be preserved")
AssertEqual(OneKeyRunGetDelay(0), 1, "run delay must have a safe lower bound")
AssertEqual(OneKeyRunGetDelay(1001), 1000, "run delay must have a safe upper bound")
AssertEqual(OneKeyRunGetGuardDelay(100), 140, "legacy guard delay must be raised to the state-machine minimum")
AssertEqual(OneKeyRunGetGuardDelay(180), 140, "previous balanced guard delay must migrate to the new value")
AssertEqual(OneKeyRunGetGuardDelay(200), 140, "older balanced guard delay must migrate to the new value")
AssertEqual(OneKeyRunGetGuardDelay(350), 140, "previous forced guard delay must migrate to the balanced value")
AssertEqual(OneKeyRunGetGuardDelay(420), 420, "configured safe guard delay must be preserved")
AssertEqual(OneKeyRunGetCommandWindow(), 350, "direction command window must stay independent from run confirmation")
AssertEqual(OneKeyRunCanReuseInitialTap(140), true, "short confirmation must reuse the original direction press")
AssertEqual(OneKeyRunCanReuseInitialTap(300), false, "long confirmation must use a fresh double tap")
AssertEqual(OneKeyRunGetSequentialWaitDelay(100, 140), 250, "second direction must wait until the command window closes")
AssertEqual(OneKeyRunGetSequentialWaitDelay(300, 140), 140, "second direction must still satisfy the hold threshold")
AssertEqual(OneKeyRunGetSwitchDelay(140), 90, "continuous movement switch must use the short asynchronous confirmation")
AssertEqual(OneKeyRunGetSwitchDelay(60), 60, "custom shorter guard must remain effective")
AssertEqual(OneKeyRunResolvePhysicalKeyState(true, true, 0), false, "stale AHK physical state must not block running after Windows reports key up")
AssertEqual(OneKeyRunResolvePhysicalKeyState(true, true, 0x8000), true, "a key held in both physical state sources must remain blocking")
AssertEqual(OneKeyRunResolvePhysicalKeyState(false, true, 0x8000), false, "injected Windows key state must not impersonate physical input")
AssertEqual(OneKeyRunResolvePhysicalKeyState(true, false, 0), true, "an unmapped physical key must preserve the conservative AHK state")
defaultKeys := OneKeyRunGetPresetKeys("test")
AssertEqual(defaultKeys[1], "Up", "preset without override must use the global Up key")
AssertEqual(defaultKeys[2], "Down", "preset without override must use the global Down key")
AssertEqual(OneKeyRunGetOppositeKey(OneKeyRunNormalizeKey("A")), OneKeyRunNormalizeKey("D"), "Left and Right must be opposite")
AssertEqual(OneKeyRunGetOppositeKey(OneKeyRunNormalizeKey("W")), OneKeyRunNormalizeKey("S"), "Up and Down must be opposite")
AssertEqual(OneKeyRunGetDirectionRelation(OneKeyRunNormalizeKey("W"), OneKeyRunNormalizeKey("D")), "orthogonal", "Up and Right must form a diagonal chord")
AssertEqual(OneKeyRunGetDirectionRelation(OneKeyRunNormalizeKey("W"), OneKeyRunNormalizeKey("S")), "opposite", "Up and Down must remain a command/opposite pair")

testKey := OneKeyRunNormalizeKey("W")
otherDirection := OneKeyRunNormalizeKey("D")
staleNum5States := {}
staleNum5States[OneKeyRunNormalizeKey("Numpad5")] := false
AssertEqual(OneKeyRunHasOtherInputPressed(testKey, Func("GetTestPhysicalKeyState").Bind(staleNum5States)), false, "stale Numpad5 state must not cancel run confirmation")
heldNum5States := {}
heldNum5States[OneKeyRunNormalizeKey("Numpad5")] := true
AssertEqual(OneKeyRunHasOtherInputPressed(testKey, Func("GetTestPhysicalKeyState").Bind(heldNum5States)), true, "a physically held Numpad5 must still cancel run confirmation")
orthogonalStates := {}
orthogonalStates[OneKeyRunNormalizeKey("D")] := true
AssertEqual(OneKeyRunHasOtherInputPressed(testKey, Func("GetTestPhysicalKeyState").Bind(orthogonalStates)), false, "a held orthogonal direction must remain eligible for diagonal running")
oppositeStates := {}
oppositeStates[OneKeyRunNormalizeKey("S")] := true
AssertEqual(OneKeyRunHasOtherInputPressed(testKey, Func("GetTestPhysicalKeyState").Bind(oppositeStates)), true, "a held opposite direction must still cancel run confirmation")
global _OneKeyRunInputEpoch := 0
global _OneKeyRunKeyGenerations := {}
global _OneKeyRunEnabled := true
generation := OneKeyRunNextKeyGeneration(testKey)
epoch := OneKeyRunRegisterDirectionInput()
AssertEqual(OneKeyRunTokenMatches(testKey, epoch, generation), true, "fresh pending token must be valid")
OneKeyRunNotifyOtherInput("Space")
AssertEqual(OneKeyRunTokenMatches(testKey, epoch, generation), false, "even a short command key event must permanently cancel the pending run")
OneKeyRunStartInputObserver()
AssertEqual(IsObject(_OneKeyRunInputObserver), true, "physical input observer must start successfully")
OneKeyRunStopInputObserver()
AssertEqual(IsObject(_OneKeyRunInputObserver), false, "physical input observer must stop cleanly")
switchGeneration := OneKeyRunNextKeyGeneration(testKey)
switchEpoch := OneKeyRunRegisterDirectionInput()
global _OneKeyRunPendingKeys := {}
_OneKeyRunPendingKeys[testKey] := switchGeneration
switchScheduleStart := A_TickCount
OneKeyRunScheduleDirectionSwitch(testKey, switchEpoch, switchGeneration, 90)
AssertEqual(A_TickCount - switchScheduleStart < 30, true, "continuous movement switch scheduling must not block the hotkey thread")
Sleep, 120
AssertEqual(_OneKeyRunPendingKeys.HasKey(testKey), false, "invalid deferred switch must clean up its pending state")

global _OneKeyRunRunningKeys := {}
global _OneKeyRunPressTimes := {}
global _OneKeyRunLastRunReleaseTime := 0
global _OneKeyRunLastRunReleaseWasStable := false
_OneKeyRunRunningKeys[testKey] := true
_OneKeyRunPressTimes[testKey] := 500
AssertEqual(OneKeyRunIsStableRun(testKey, 501), true, "a committed run must immediately establish the movement session")
AssertEqual(OneKeyRunIsDirectionSwitch(otherDirection, 501), true, "confirmed movement must not wait another 350ms before switching")
_OneKeyRunRunningKeys := {}
_OneKeyRunPressTimes := {}
_OneKeyRunLastRunReleaseWasStable := true
_OneKeyRunLastRunReleaseTime := 800
AssertEqual(OneKeyRunIsDirectionSwitch(otherDirection, 1000), true, "recent release of a stable run must keep switching responsive")
AssertEqual(OneKeyRunIsDirectionSwitch(otherDirection, 1051), false, "old run release must not mask a direction command")
AssertAtomicCommitOrder()

ExitApp, 0
