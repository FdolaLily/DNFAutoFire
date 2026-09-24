#NoEnv
#NoTrayIcon
#SingleInstance, Off
SetBatchLines, -1
ListLines, Off
#Include %A_ScriptDir%\SingleInstance.ahk

; This helper is copied beside the production library into isolated test folders.
; It never opens the production mutex, shows a GUI, or sends keyboard input.
mode := A_Args[1]
mutexName := A_Args[2]
resultPath := A_Args[3]
if (InStr(mutexName, "Global\DNFAutoFire.SingleInstance.Test.") != 1)
    ExitApp, 2

try {
    if (mode == "probe") {
        result := SingleInstanceAlreadyRunning(mutexName) ? "EXISTS" : "EMPTY"
        FileAppend, %result%, %resultPath%
        ExitApp, 0
    }
    gate := DllCall("OpenEventW", "UInt", 0x100000, "Int", 0, "WStr", A_Args[4], "Ptr")
    release := DllCall("OpenEventW", "UInt", 0x100000, "Int", 0, "WStr", A_Args[5], "Ptr")
    if (!gate || !release)
        throw Exception("Cannot open test events", -1, A_LastError)
    readyPath := resultPath . ".ready"
    FileAppend, READY, %readyPath%
    if (DllCall("WaitForSingleObject", "Ptr", gate, "UInt", 30000, "UInt") != 0)
        throw Exception("Test start gate timed out")
    DllCall("CloseHandle", "Ptr", gate)
    global singletonHandle := SingleInstanceAcquire(mutexName)
    if (!singletonHandle) {
        FileAppend, DUPLICATE, %resultPath%
        ExitApp, 0
    }
    FileAppend, OWNER, %resultPath%
    if (DllCall("WaitForSingleObject", "Ptr", release, "UInt", 30000, "UInt") != 0)
        throw Exception("Test release event timed out")
    DllCall("CloseHandle", "Ptr", release)
    ; As in the client, keep the mutex handle until Windows exits the process.
    ExitApp, 0
} catch error {
    FileAppend, % "ERROR: " . error.Message . " " . error.Extra, %resultPath%
    ExitApp, 1
}
