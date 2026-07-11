SendIP(keyCode){
    SendInput, {Blind}{%keyCode% DownTemp}
    if (AutoFireIsPulse10Test()) {
        AutoFireApplyPulseDelay(10)
    } else {
        Sleep, 1
    }
    SendInput, {Blind}{%keyCode% Up}
    if (AutoFireIsPulse10Test()) {
        AutoFireApplyPulseDelay(10)
    } else {
        Sleep, 1
    }
}
