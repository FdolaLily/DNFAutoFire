#NoEnv
#SingleInstance, Force
SetBatchLines, -1
#Include %A_ScriptDir%\..\core\AutoFireInput.ahk

Assert(actual, expected, message){
    if (actual != expected) {
        FileAppend, Test failed: %message%`nExpected: %expected%`nActual: %actual%`n, *
        ExitApp, 1
    }
}

for _, name in ["A", "Left", "NumpadEnter", "Numpad1", "RCtrl", "LShift", "Pause"] {
    input := new AutoFireInput(name)
    down := input.GetAddress("Down")
    up := input.GetAddress("Up")
    k := input.KeyboardOffset
    Assert(input.Size, A_PtrSize == 8 ? 40 : 28, "INPUT size")
    Assert(NumGet(down + 0, 0, "UInt"), 1, "keyboard type")
    Assert(NumGet(down + input.ExtraInfoOffset, 0, "UPtr"), 0xFFC3D44D, "AHK SendLevel 0 marker")
    Assert(NumGet(up + input.ExtraInfoOffset, 0, "UPtr"), 0xFFC3D44D, "up marker")
    flags := NumGet(down + k + 4, 0, "UInt")
    Assert(NumGet(up + k + 4, 0, "UInt"), flags | 2, "matching key-up flags")
    Assert(NumGet(down + k + 8, 0, "UInt"), 0, "Windows assigned timestamp")
    if (name == "Pause") {
        Assert(flags, 0, "Pause uses VK mode")
        Assert(NumGet(down + k, 0, "UShort"), 0x13, "Pause VK")
    } else {
        Assert(flags & 8, 8, "scan code mode")
        Assert(flags & 1, input.ScanCode & 0x100 ? 1 : 0, "extended scan flag")
        Assert(NumGet(down + k, 0, "UShort"), 0, "scan mode leaves VK zero")
        Assert(NumGet(down + k + 2, 0, "UShort"), input.ScanCode & 0xFF, "scan low byte")
    }
}
invalidRejected := false
try invalid := new AutoFireInput("NotARealKeyName")
catch error
    invalidRejected := true
Assert(invalidRejected, true, "invalid key must be rejected")

; 真正的 native SendInput 集成测试：只使用 F24，底层 I0 sink 吞掉 Down/Up，
; 上层 I1 observer 应忽略 level 0。前台为 DNF 或不能确定前台进程时跳过。
global sinkCount := 0
global observerCount := 0
sink := InputHook("I0 L0 V")
sink.KeyOpt("{F24}", "NS")
sink.OnKeyDown := Func("CountSink")
sink.OnKeyUp := Func("CountSink")
observer := InputHook("I1 L0 V")
observer.KeyOpt("{F24}", "N")
observer.OnKeyDown := Func("CountObserver")
observer.OnKeyUp := Func("CountObserver")
Assert(observer.MinSendLevel, 1, "physical observer ignores SendLevel 0")
sink.Start()
observer.Start()
Assert(sink.InProgress && observer.InProgress, true, "input filters must be running before injection")
testInput := new AutoFireInput("F24")
if (SafeForegroundForFilterTest()) {
    Assert(testInput.Send(true), true, "native F24 down inserted")
    Assert(testInput.Send(false), true, "native F24 up inserted")
    WaitForInputCallbacks()
    Assert(sinkCount, 2, "I0 sink captures and suppresses both native F24 edges")
    Assert(observerCount, 0, "I1 excludes native SendLevel 0 edges")
    FileAppend, native marker integration passed: sink=2 observer_I1=0`n, *
    ; 正对照只改为 AHK SendLevel 1 标记，证明 I1 的回调确实工作；F24 仍由 sink 吞掉。
    if (SafeForegroundForFilterTest()) {
        NumPut(AutoFireInputMarker() - 1, testInput.GetAddress("Down") + testInput.ExtraInfoOffset, 0, "UPtr")
        NumPut(AutoFireInputMarker() - 1, testInput.GetAddress("Up") + testInput.ExtraInfoOffset, 0, "UPtr")
        Assert(testInput.Send(true), true, "positive-control F24 down inserted")
        Assert(testInput.Send(false), true, "positive-control F24 up inserted")
        WaitForInputCallbacks()
        Assert(sinkCount, 4, "sink suppresses the positive-control pair")
        Assert(observerCount, 2, "I1 observes SendLevel 1 positive control")
        FileAppend, marker positive control passed: sink=4 observer_I1=2`n, *
    }
} else {
    FileAppend, native marker integration SKIPPED: foreground is DNF or could not be identified`n, *
}
observer.Stop()
sink.Stop()
FileAppend, autofire input tests passed`n, *
ExitApp, 0

CountSink(hook, vk, sc){
    global sinkCount
    if (vk == 0x87) {
        sinkCount += 1
    }
}

CountObserver(hook, vk, sc){
    global observerCount
    if (vk == 0x87) {
        observerCount += 1
    }
}

WaitForInputCallbacks(){
    ; AHK posts InputHook callbacks to its script thread; this only pumps messages.
    Loop, 10 {
        Sleep, 5
    }
}

SafeForegroundForFilterTest(){
    WinGet, processName, ProcessName, A
    WinGetClass, className, A
    return processName != "" && processName != "DNF.exe"
        && className != "地下城与勇士" && className != "Dungeon & Fighter"
        && className != "Dungeon Fighter Online"
}
