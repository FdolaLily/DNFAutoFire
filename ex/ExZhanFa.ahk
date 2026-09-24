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
