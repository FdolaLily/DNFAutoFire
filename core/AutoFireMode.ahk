AutoFireGetStartDelay(key, pressedKeys){
    if (!IsObject(pressedKeys)) {
        return 0
    }
    keyCount := pressedKeys.Length()
    if (keyCount < 2) {
        return 0
    }
    for index, pressedKey in pressedKeys {
        if (pressedKey == key) {
            return Floor((index - 1) * 16 / keyCount)
        }
    }
    return 0
}

AutoFireGetManagedTargetPhase(key, pressedKeys, cycleMs := 20){
    if (!IsObject(pressedKeys) || pressedKeys.Length() < 2 || pressedKeys.Length() > 3) {
        return -1
    }
    for index, pressedKey in pressedKeys {
        if (pressedKey == key) {
            return Mod((index - 1) * 8, cycleMs)
        }
    }
    return -1
}

AutoFireGetAbsolutePhaseDelay(targetPhase, cycleMs := 20, nowMs := ""){
    if (targetPhase < 0 || cycleMs <= 0) {
        return 0
    }
    if (nowMs == "") {
        nowMs := DllCall("Winmm\timeGetTime", "UInt")
    }
    normalizedTarget := Mod(targetPhase, cycleMs)
    currentPhase := Mod(nowMs, cycleMs)
    return Mod(normalizedTarget - currentPhase + cycleMs, cycleMs)
}

AutoFireGetTransitionDelay(key, pressedKeys){
    if (IsObject(pressedKeys) && pressedKeys.Length() >= 2 && pressedKeys.Length() <= 3) {
        targetPhase := AutoFireGetManagedTargetPhase(key, pressedKeys)
        return AutoFireGetAbsolutePhaseDelay(targetPhase)
    }
    return AutoFireGetStartDelay(key, pressedKeys)
}

AutoFireApplyStaggerDelay(delay){
    if (delay <= 0) {
        return
    }
    DllCall("Winmm\timeBeginPeriod", "UInt", 1)
    DllCall("Sleep", "UInt", delay)
    DllCall("Winmm\timeEndPeriod", "UInt", 1)
}

AutoFireApplyPulseDelay(delay){
    if (delay > 0) {
        DllCall("Sleep", "UInt", delay)
    }
}
