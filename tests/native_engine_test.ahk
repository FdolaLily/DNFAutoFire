#NoEnv
#NoTrayIcon
#SingleInstance, Force
SetBatchLines, -1
ListLines, Off

; Integration test for real DLL loading/thread lifetime. This test never sends
; keyboard input. F24 is the only valid test output key; every worker stays idle.
global engine := 0
global module := 0
global assertions := 0
global startFn := 0, stopFn := 0, physicalFn := 0, errorFn := 0, statsFn := 0, pauseFn := 0
OnExit("Cleanup")

EnsureSafeForeground()
dllPath := A_ScriptDir . "\..\build\DNFAutoFireNative.dll"
module := DllCall("LoadLibraryW", "WStr", dllPath, "Ptr")
Assert(module != 0, "native DLL loads successfully")
startFn := Export("AF_Start")
stopFn := Export("AF_Stop")
physicalFn := Export("AF_Physical")
errorFn := Export("AF_Error")
statsFn := Export("AF_Stats")
pauseFn := Export("AF_PauseKey")
Gui, NativeTest:New, +HwndnotifyHwnd +ToolWindow -Caption

; One rule is 132 UInt values: descriptor/manual/delay/triggerCount/128 triggers.
VarSetCapacity(rules, 132 * 4 * 2, 0)
scan := GetKeySC("F24")
if (!scan)
    scan := 0x76
vk := 0x87
descriptor := scan | (vk << 16)
NumPut(descriptor, rules, 0, "UInt")
NumPut(1, rules, 4, "UInt")

Assert(DllCall(stopFn, "Ptr", 0, "Int") == 1, "stopping a null engine is harmless")
Assert(DllCall(physicalFn, "Ptr", 0, "UInt", scan, "UInt", vk, "Int") == -1, "no engine reports unowned physical state")
Assert(DllCall(errorFn, "Ptr", 0, "UInt") == 6, "null engine returns invalid-handle error")
VarSetCapacity(stats, 32, 0)
Assert(DllCall(statsFn, "Ptr", 0, "Ptr", &stats, "Int") == 0, "null engine cannot supply stats")
Assert(DllCall(pauseFn, "Ptr", 0, "UInt", scan, "UInt", vk, "Int", 1, "Int") == 1, "null engine needs no key handoff")

ExpectInvalid(0, 1, 10000, 10000, 0, notifyHwnd, "null rule buffer")
ExpectInvalid(&rules, 0, 10000, 10000, 0, notifyHwnd, "zero rules")
ExpectInvalid(&rules, 129, 10000, 10000, 0, notifyHwnd, "too many rules")
ExpectInvalid(&rules, 1, 999, 10000, 0, notifyHwnd, "Down below 1ms")
ExpectInvalid(&rules, 1, 100001, 10000, 0, notifyHwnd, "Down above 100ms")
ExpectInvalid(&rules, 1, 10000, 999, 0, notifyHwnd, "Up below 1ms")
ExpectInvalid(&rules, 1, 10000, 100001, 0, notifyHwnd, "Up above 100ms")
ExpectInvalid(&rules, 1, 10000, 10000, 251, notifyHwnd, "spin exceeds 250us")
ExpectInvalid(&rules, 1, 10000, 10000, 0, 0, "invalid notification window")
NumPut(0, rules, 0, "UInt")
ExpectInvalid(&rules, 1, 10000, 10000, 0, notifyHwnd, "zero scan and virtual key")
NumPut(0x200 | (vk << 16), rules, 0, "UInt")
ExpectInvalid(&rules, 1, 10000, 10000, 0, notifyHwnd, "out-of-range scan")
NumPut(scan | (0x100 << 16), rules, 0, "UInt")
ExpectInvalid(&rules, 1, 10000, 10000, 0, notifyHwnd, "out-of-range virtual key")
NumPut(descriptor, rules, 0, "UInt")
NumPut(129, rules, 12, "UInt")
ExpectInvalid(&rules, 1, 10000, 10000, 0, notifyHwnd, "too many trigger descriptors")
NumPut(0, rules, 12, "UInt")
NumPut(10000001, rules, 8, "UInt")
ExpectInvalid(&rules, 1, 10000, 10000, 0, notifyHwnd, "rule delay exceeds 10 seconds")
NumPut(0, rules, 8, "UInt")
NumPut(1, rules, 12, "UInt")
ExpectInvalid(&rules, 1, 10000, 10000, 0, notifyHwnd, "zero trigger descriptor")
NumPut(0, rules, 12, "UInt")
NumPut(scan | (0x86 << 16), rules, 132 * 4, "UInt")
NumPut(1, rules, 132 * 4 + 4, "UInt")
ExpectInvalid(&rules, 2, 10000, 10000, 0, notifyHwnd, "same physical scan cannot be owned by two aliases")

; Warm up the native runtime before comparing handle/thread counts.
engine := Start(&rules, notifyHwnd)
CheckIdle(engine, scan, vk)
Assert(DllCall(statsFn, "Ptr", engine, "Ptr", 0, "Int") == 0, "null stats output is rejected")
Assert(DllCall(pauseFn, "Ptr", engine, "UInt", scan, "UInt", vk, "Int", 1, "Int") == 1, "idle managed key pause is acknowledged")
CheckIdle(engine, scan, vk)
Assert(DllCall(pauseFn, "Ptr", engine, "UInt", scan, "UInt", vk, "Int", 0, "Int") == 1, "idle managed key resumes")
Assert(DllCall(pauseFn, "Ptr", engine, "UInt", 1, "UInt", 0x1B, "Int", 1, "Int") == 1, "unmanaged key needs no pause handoff")
StopEngine()
Sleep, 20
beforeHandles := HandleCount()
beforeThreads := ThreadCount()

Loop, 100 {
    EnsureSafeForeground()
    ; Include manual and non-manual output rules; both expose real physical state.
    NumPut(Mod(A_Index, 2), rules, 4, "UInt")
    engine := Start(&rules, notifyHwnd)
    Sleep, 2
    CheckIdle(engine, scan, vk)
    StopEngine()
}
Sleep, 20
afterHandles := HandleCount()
afterThreads := ThreadCount()
Assert(afterHandles <= beforeHandles + 2, "100 stop cycles do not leak native handles")
Assert(afterThreads <= beforeThreads, "100 stop cycles join every native thread")
Gui, NativeTest:Destroy
Assert(DllCall("FreeLibrary", "Ptr", module, "Int") != 0, "DLL unloads after all threads stop")
module := 0
FileAppend, PASS: %assertions% native DLL assertions; 101 idle start/stop cycles; handles %beforeHandles%->%afterHandles%; threads %beforeThreads%->%afterThreads%; no keyboard input sent.`n, *
ExitApp, 0

Assert(condition, message) {
    global assertions
    assertions += 1
    if (!condition) {
        FileAppend, FAIL: %message% (Win32 error %A_LastError%)`n, *
        ExitApp, 1
    }
}

Export(name) {
    global module
    address := DllCall("GetProcAddress", "Ptr", module, "AStr", name, "Ptr")
    Assert(address != 0, "export " . name . " exists")
    return address
}

EnsureSafeForeground() {
    foreground := DllCall("GetForegroundWindow", "Ptr")
    WinGet, processName, ProcessName, ahk_id %foreground%
    if (processName = "DNF.exe") {
        FileAppend, SKIP: DNF is foreground; native integration test sends no input and will not run during gameplay.`n, *
        ExitApp, 0
    }
}

Start(buffer, notify) {
    global startFn
    EnsureSafeForeground()
    context := DllCall(startFn, "Ptr", buffer, "UInt", 1, "UInt", 10000
        , "UInt", 10000, "UInt", 0, "Ptr", notify, "UPtr", 0xA17E, "Ptr")
    Assert(context != 0, "idle engine starts")
    return context
}

ExpectInvalid(buffer, count, down, up, spin, notify, message) {
    global startFn, stopFn
    context := DllCall(startFn, "Ptr", buffer, "UInt", count, "UInt", down
        , "UInt", up, "UInt", spin, "Ptr", notify, "UPtr", 0, "Ptr")
    code := A_LastError
    if (context)
        DllCall(stopFn, "Ptr", context, "Int")
    Assert(!context && code == 87, "invalid parameter rejected: " . message)
}

CheckIdle(context, scan, vk) {
    global physicalFn, errorFn, statsFn
    EnsureSafeForeground()
    Assert(DllCall(physicalFn, "Ptr", context, "UInt", scan, "UInt", vk, "Int") == 0, "managed F24 remains physically released")
    Assert(DllCall(physicalFn, "Ptr", context, "UInt", 0x01, "UInt", 0x1B, "Int") == -1, "unmanaged Escape preserves fallback physical lookup")
    Assert(DllCall(physicalFn, "Ptr", context, "UInt", scan, "UInt", 0x13, "Int") == -1, "Pause identity does not alias an ordinary scan")
    Assert(DllCall(errorFn, "Ptr", context, "UInt") == 0, "native threads remain free of errors")
    VarSetCapacity(values, 32, 0)
    Assert(DllCall(statsFn, "Ptr", context, "Ptr", &values, "Int") == 1, "native stats are readable")
    Assert(NumGet(values, 0, "UInt64") == 0, "idle worker emitted zero input edges")
    Assert(NumGet(values, 8, "UInt64") == 0, "idle worker skipped zero pulse slots")
    Assert(NumGet(values, 24, "UInt64") == 1, "idle native worker is alive")
}

StopEngine() {
    global engine, stopFn
    stopped := DllCall(stopFn, "Ptr", engine, "Int")
    if (stopped)
        engine := 0
    Assert(stopped == 1, "native worker and hook threads stop and join")
}

HandleCount() {
    success := DllCall("GetProcessHandleCount", "Ptr", DllCall("GetCurrentProcess", "Ptr"), "UInt*", count, "Int")
    Assert(success != 0, "process handle count is available")
    return count
}

ThreadCount() {
    snapshot := DllCall("CreateToolhelp32Snapshot", "UInt", 4, "UInt", 0, "Ptr")
    Assert(snapshot != -1 && snapshot != 0, "thread snapshot opens")
    VarSetCapacity(entry, 28, 0)
    NumPut(28, entry, 0, "UInt")
    count := 0
    valid := DllCall("Thread32First", "Ptr", snapshot, "Ptr", &entry, "Int")
    pid := DllCall("GetCurrentProcessId", "UInt")
    while (valid) {
        if (NumGet(entry, 12, "UInt") == pid)
            count += 1
        NumPut(28, entry, 0, "UInt")
        valid := DllCall("Thread32Next", "Ptr", snapshot, "Ptr", &entry, "Int")
    }
    DllCall("CloseHandle", "Ptr", snapshot)
    return count
}

Cleanup(reason, exitCode) {
    global engine, module, stopFn
    if (engine && stopFn) {
        if (DllCall(stopFn, "Ptr", engine, "Int"))
            engine := 0
    }
    ; A failed join must keep the DLL mapped until process termination.
    if (!engine && module) {
        DllCall("FreeLibrary", "Ptr", module)
        module := 0
    }
}
