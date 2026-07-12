SendIP(keyCode){
    SendInput, {Blind}{%keyCode% DownTemp}
    AutoFireApplyPulseDelay(10)
    SendInput, {Blind}{%keyCode% Up}
    AutoFireApplyPulseDelay(10)
}
