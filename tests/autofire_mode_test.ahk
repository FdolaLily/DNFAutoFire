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

AssertEqual(AutoFireIsPulse10Test("DNFAutoFire_pulse10_test.exe"), 1, "10ms pulse build must identify itself")
AssertEqual(AutoFireIsPulse10Test("DNFAutoFire_combined_test.exe"), 1, "combined build must enable pulse mode")
AssertEqual(AutoFireIsPulse10Test("DNFAutoFire.exe"), 1, "final build must enable pulse mode")
AssertEqual(AutoFireIsPulse10Test("DAF连发工具.ahk"), 0, "uncompiled source run must not silently enable pulse mode")

twoKeys := ["F7", "Numpad3"]
AssertEqual(AutoFireGetStartDelay("F7", twoKeys, "DNFAutoFire_combined_test.exe"), 0, "first arbitrary key must keep phase 0")
AssertEqual(AutoFireGetStartDelay("Numpad3", twoKeys, "DNFAutoFire_combined_test.exe"), 8, "second arbitrary key must use phase 8")
threeKeys := ["Q", "Space", "Left"]
AssertEqual(AutoFireGetStartDelay("Q", threeKeys, "DNFAutoFire_combined_test.exe"), 0, "first of three keys must use phase 0")
AssertEqual(AutoFireGetStartDelay("Space", threeKeys, "DNFAutoFire_combined_test.exe"), 5, "second of three keys must use phase 5")
AssertEqual(AutoFireGetStartDelay("Left", threeKeys, "DNFAutoFire_combined_test.exe"), 10, "third of three keys must use phase 10")
AssertEqual(AutoFireIsStaggerTest("DNFAutoFire_combined_test.exe"), 1, "combined build must enable staggering")
AssertEqual(AutoFireIsStaggerTest("DNFAutoFire.exe"), 1, "final build must enable staggering")
AssertEqual(AutoFireIsStaggerTest("DAF连发工具.ahk"), 0, "uncompiled source run must not silently enable staggering")

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
