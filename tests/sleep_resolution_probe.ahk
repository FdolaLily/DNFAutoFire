#NoEnv
#SingleInstance, Force
SetBatchLines, -1

startTime := A_TickCount
Loop, 100
{
    Sleep, 1
}
defaultElapsed := A_TickCount - startTime

startTime := A_TickCount
Loop, 100
{
    DllCall("Sleep", "UInt", 1)
}
defaultWin32Elapsed := A_TickCount - startTime

DllCall("Winmm\timeBeginPeriod", "UInt", 1)
startTime := A_TickCount
Loop, 100
{
    DllCall("Sleep", "UInt", 1)
}
highResolutionWin32Elapsed := A_TickCount - startTime
DllCall("Winmm\timeEndPeriod", "UInt", 1)

FileAppend, ahk_default_ms=%defaultElapsed% win32_default_ms=%defaultWin32Elapsed% win32_high_res_ms=%highResolutionWin32Elapsed%`n, *

DllCall("Winmm\timeBeginPeriod", "UInt", 1)
delays := [1, 2, 4, 6, 8, 10, 12, 14, 15]
for _, delay in delays
{
    startTime := A_TickCount
    Loop, 20
    {
        DllCall("Sleep", "UInt", delay)
    }
    elapsed := A_TickCount - startTime
    average := elapsed / 20.0
    FileAppend, requested_ms=%delay% average_ms=%average% total_ms=%elapsed%`n, *
}
DllCall("Winmm\timeEndPeriod", "UInt", 1)
ExitApp, 0
