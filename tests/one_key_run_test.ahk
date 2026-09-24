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
    return ["W", "S", "A", "D", "Space", "Numpad5", "Numpad6"]
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

GetTestNumpadPhysicalState(key) {
    return OneKeyRunIsDualStateNumpadSC(GetKeySC(key)) ? OneKeyRunIsPhysicalKeyPressed(key) : false
}

CountObservedKeyDown(observer, vk, sc) {
    global observedEventCount
    global observedEventVK
    if (vk == observedEventVK) {
        observedEventCount += 1
    }
    OneKeyRunObserveKeyDown(observer, vk, sc)
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

; Replay the real hardware identity sequence without sending anything to the game.
; VK changes with NumLock; SC must pair the press and release in either direction.
global _OneKeyRunEnabled := false
for _, pair in [[0x67, 0x24, 0x47], [0x68, 0x26, 0x48], [0x69, 0x21, 0x49]
    , [0x64, 0x25, 0x4B], [0x65, 0x0C, 0x4C], [0x66, 0x27, 0x4D]
    , [0x61, 0x23, 0x4F], [0x62, 0x28, 0x50], [0x63, 0x22, 0x51]
    , [0x60, 0x2D, 0x52], [0x6E, 0x2E, 0x53]] {
    numericKey := OneKeyRunGetObservedKey(pair[1], pair[3])
    navigationKey := OneKeyRunGetObservedKey(pair[2], pair[3])
    physicalId := OneKeyRunPhysicalInputId(numericKey)
    AssertEqual(physicalId, OneKeyRunPhysicalInputId(navigationKey), "NumLock aliases must share the physical input identity")
    OneKeyRunObserveKeyDown("", pair[1], pair[3])
    AssertEqual(OneKeyRunIsPhysicalKeyPressed(numericKey), true, "a real numpad press must block even without a Windows virtual down state")
    OneKeyRunObserveKeyUp("", pair[2], pair[3])
    AssertEqual(OneKeyRunIsPhysicalKeyPressed(numericKey), false, "navigation key-up must release the numeric physical state")
    AssertEqual(_OneKeyRunObservedDownKeys.HasKey(physicalId), false, "navigation key-up must also remove the numeric notification latch")
    OneKeyRunObserveKeyDown("", pair[2], pair[3])
    OneKeyRunObserveKeyUp("", pair[1], pair[3])
    AssertEqual(OneKeyRunIsPhysicalKeyPressed(navigationKey), false, "numeric key-up must release the navigation physical state")
}
AssertEqual(OneKeyRunIsDualStateNumpadSC(GetKeySC("Right")), false, "extended Right arrow must not be confused with Numpad6")
OneKeyRunObserveKeyDown("", 0x66, 0x4D)
OneKeyRunObserveKeyUp("", 0x27, 0x14D)
AssertEqual(OneKeyRunIsPhysicalKeyPressed("Numpad6"), true, "releasing the dedicated Right arrow must not release Numpad6")
OneKeyRunObserveKeyUp("", 0x27, 0x4D)
AssertEqual(OneKeyRunHasOtherInputPressed(testKey, Func("GetTestNumpadPhysicalState")), false, "released NumLock latch must no longer cancel run intent")

OneKeyRunStartInputObserver()
observerBeforeReset := _OneKeyRunInputObserver
OneKeyRunObserveKeyDown("", 0x66, 0x4D)
ResetOneKeyRunState()
AssertEqual(_OneKeyRunInputObserver.InProgress, true, "preset/toggle reset must keep receiving physical releases")
AssertEqual(OneKeyRunIsPhysicalKeyPressed("Numpad6"), true, "reset must preserve a genuinely held physical key")
OneKeyRunObserveKeyUp("", 0x27, 0x4D)
OneKeyRunStartInputObserver()
AssertEqual(_OneKeyRunInputObserver == observerBeforeReset, true, "reenabling must reuse the continuously running observer")
AssertEqual(OneKeyRunIsPhysicalKeyPressed("Numpad6"), false, "release while disabled must remain released after reenabling")
OneKeyRunStopInputObserver()
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
previousEpoch := _OneKeyRunInputEpoch
previousRefresh := _OneKeyRunRefreshGeneration
ResetOneKeyRunState()
AssertEqual(_OneKeyRunInputEpoch > previousEpoch, true, "reset must not reuse an old input generation")
AssertEqual(_OneKeyRunRefreshGeneration > previousRefresh, true, "reset must invalidate suspended run refreshes")

; The lower input hook consumes every test event before it reaches other apps.
; Level 1 deliberately passes the production I1 filter; normal autofire stays at 0.
WinGet, testForegroundProcess, ProcessName, A
if (testForegroundProcess == "" || testForegroundProcess == "DNF.exe") {
    FileAppend, Pure one-key-run regression passed; live InputHook integration SKIPPED while DNF is foreground or inaccessible.`n, *
    ExitApp, 0
}
_OneKeyRunEnabled := false
inputSink := InputHook("L0")
inputSink.KeyOpt("{All}", "S")
inputSink.Start()
OneKeyRunStartInputObserver()
observedEventCount := 0
observedEventVK := 0x41
_OneKeyRunInputObserver.OnKeyDown := Func("CountObservedKeyDown")
SetKeyDelay, -1, -1
SendLevel, 1
Loop, 1100 {
    SendEvent, {Blind}a
    Sleep, -1
}
Sleep, 50
AssertEqual(_OneKeyRunInputObserver.InProgress, true, "observer must survive more than 1023 text characters")
AssertEqual(_OneKeyRunInputObserver.Input, "", "observer must not accumulate typed text")
AssertEqual(observedEventCount >= 1100, true, "all long-session input events must remain observable")
SendEvent, {Blind}{vk66sc04D Down}
Sleep, 30
AssertEqual(OneKeyRunIsPhysicalKeyPressed("Numpad6"), true, "InputHook must record the actual numeric scan-code event")
ResetOneKeyRunState()
SendEvent, {Blind}{vk27sc04D Up}
Sleep, 30
AssertEqual(OneKeyRunIsPhysicalKeyPressed("Numpad6"), false, "InputHook must pair an alias key-up after reset")
observedEventCount := 0
observedEventVK := 0x66
SendLevel, 0
SendEvent, {Blind}{vk66sc04D Down}{vk66sc04D Up}
Sleep, 30
AssertEqual(observedEventCount, 0, "normal autofire injections must not reach the physical observer")
AssertEqual(OneKeyRunIsPhysicalKeyPressed("Numpad6"), false, "injected autofire must not recreate a physical numpad latch")
OneKeyRunStopInputObserver()
inputSink.Stop()

ExitApp, 0
