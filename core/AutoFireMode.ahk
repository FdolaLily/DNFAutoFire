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
