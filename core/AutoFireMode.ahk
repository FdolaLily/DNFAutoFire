AutoFireIsStaggerTest(scriptName := ""){
    if (scriptName == "") {
        scriptName := A_ScriptName
    }
    return InStr(scriptName, "_stagger_test") > 0 || InStr(scriptName, "_combined_test") > 0
}

AutoFireIsPulse10Test(scriptName := ""){
    if (scriptName == "") {
        scriptName := A_ScriptName
    }
    return InStr(scriptName, "_pulse10_test") > 0 || InStr(scriptName, "_combined_test") > 0
}

AutoFireGetStartDelay(key, pressedKeys, scriptName := ""){
    if (!AutoFireIsStaggerTest(scriptName) || !IsObject(pressedKeys)) {
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
