#NoEnv
#SingleInstance, Force
SetBatchLines, -1

#Include %A_ScriptDir%\..\core\AutoFireMode.ahk

AssertEqual(actual, expected, message) {
    if (actual != expected) {
        FileAppend, Test failed: %message%`nExpected: %expected%`nActual: %actual%`n, *
        ExitApp, 1
    }
}

twoKeys := ["F7", "Numpad3"]
AssertEqual(AutoFireGetStartDelay("F7", twoKeys), 0, "first arbitrary key must keep phase 0")
AssertEqual(AutoFireGetStartDelay("Numpad3", twoKeys), 8, "second arbitrary key must use phase 8")
threeKeys := ["Q", "Space", "Left"]
AssertEqual(AutoFireGetStartDelay("Q", threeKeys), 0, "first of three keys must use phase 0")
AssertEqual(AutoFireGetStartDelay("Space", threeKeys), 5, "second of three keys must use phase 5")
AssertEqual(AutoFireGetStartDelay("Left", threeKeys), 10, "third of three keys must use phase 10")

AssertEqual(AutoFireGetManagedTargetPhase("F7", twoKeys), 0, "first of two held keys must use phase 0")
AssertEqual(AutoFireGetManagedTargetPhase("Numpad3", twoKeys), 8, "second of two held keys must use fixed phase 8")
AssertEqual(AutoFireGetManagedTargetPhase("Q", threeKeys), 0, "first of three held keys must use phase 0")
AssertEqual(AutoFireGetManagedTargetPhase("Space", threeKeys), 8, "second of three held keys must use phase 8")
AssertEqual(AutoFireGetManagedTargetPhase("Left", threeKeys), 16, "third of three held keys must use phase 16")
fourKeys := ["Q", "Space", "Left", "F1"]
AssertEqual(AutoFireGetManagedTargetPhase("F1", fourKeys), -1, "four held keys must fall back to dynamic staggering")
AssertEqual(AutoFireGetAbsolutePhaseDelay(8, 20, 3), 5, "phase delay must advance to phase 8")
AssertEqual(AutoFireGetAbsolutePhaseDelay(16, 20, 18), 18, "phase delay must wrap deterministically")

DllCall("Winmm\timeBeginPeriod", "UInt", 1)
startTime := A_TickCount
Loop, 10
{
    AutoFireApplyPulseDelay(10)
}
pulseElapsed := A_TickCount - startTime
DllCall("Winmm\timeEndPeriod", "UInt", 1)
FileAppend, pulse_10x10ms_elapsed=%pulseElapsed%`n, *
if (pulseElapsed < 80 || pulseElapsed > 145) {
    FileAppend, Test failed: ten high-resolution 10ms waits took %pulseElapsed%ms`n, *
    ExitApp, 1
}

staggerStartTime := A_TickCount
Loop, 5
{
    AutoFireApplyStaggerDelay(8)
}
staggerElapsed := A_TickCount - staggerStartTime
FileAppend, stagger_5x8ms_elapsed=%staggerElapsed%`n, *
if (staggerElapsed < 20 || staggerElapsed > 100) {
    FileAppend, Test failed: five 8ms stagger waits took %staggerElapsed%ms`n, *
    ExitApp, 1
}

ExitApp, 0
