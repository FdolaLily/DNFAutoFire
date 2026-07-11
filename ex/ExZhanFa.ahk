ExZhanFa(){
    SetDNFWindowClass()
    presetName := LoadLastPreset()
    if(LoadPreset(LoadLastPreset(),"ZhanFaState")){
        ShotKey := LoadPreset(LoadLastPreset(), "ZhanFaShotKey")
        if (!ZhanFaIsNumpadKey(ShotKey)) {
            return
        }
        SkillKeys := ZhanFaLoadKeys(LoadLastPreset())
        keyCode := Key2NoVkSC(ShotKey)
        pressKeys := []
        for _, key in SkillKeys{
            pressKeys.Push(Key2PressKey(key))
        }
        loop {
            if(WinActive("ahk_group DNF")) {
                isNeedSend := false
                for _, pressKey in pressKeys{
                    if (GetKeyState(pressKey, "P")) {
                        isNeedSend := true
                        break
                    }
                }
                if (isNeedSend) {
                    SendIP(keyCode)
                }
            }
            ; 保留原版额外的抬起采样窗口，避免下一轮 Down 过早覆盖 Up。
            Sleep, 1
        }
    }
}

; 读取预设的连发按键
ZhanFaLoadKeys(presetName){
    skillKeysConfig := LoadPreset(presetName, "ZhanFaSkillKeys")
    keys := []
    loop, Parse, skillKeysConfig, `|
    {
        keys.Push(A_LoopField)
    }
    return keys
}
