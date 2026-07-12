AutoFire(key){
    SetDNFWindowClass()
    keyCode := Key2NoVkSC(key)
    pressKey := Key2PressKey(key)
    pulseTimerActive := false
    loop {
        if(WinActive("ahk_group DNF")) {
            if (GetKeyState(pressKey, "P")) {
                pressedKeys := AutoFireCollectPressedKeys()
                startDelay := AutoFireGetStartDelay(key, pressedKeys)
                AutoFireApplyStaggerDelay(startDelay)
            }
            if (GetKeyState(pressKey, "P")) {
                DllCall("Winmm\timeBeginPeriod", "UInt", 1)
                pulseTimerActive := true
            }
            while, GetKeyState(pressKey, "P") {
                SendIP(keyCode)
            }
            if (pulseTimerActive) {
                DllCall("Winmm\timeEndPeriod", "UInt", 1)
                pulseTimerActive := false
            }
        }
        ; 未按下时降低轮询频率；按下后由 SendIP 按原始时序运行。
        Sleep, 5
    }
}

AutoFireCollectPressedKeys(){
    pressedKeys := []
    presetKeys := LoadPresetKeys(LoadLastPreset())
    for _, configuredKey in presetKeys {
        if (configuredKey == "") {
            continue
        }
        originKey := GetOriginKeyName(configuredKey)
        pressKey := Key2PressKey(originKey)
        if (GetKeyState(pressKey, "P")) {
            pressedKeys.Push(configuredKey)
        }
    }
    return pressedKeys
}
