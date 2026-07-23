Gui ZhanFa:+ToolWindow +Owner%A_DefaultGui%
Gui ZhanFa:Add, ListBox, vZhanFaKeysListBox x8 y32 w80 h172
Gui ZhanFa:Add, Edit, vZhanFaShotKey hwndZhanFaShotKeyHwnd x96 y120 w80 h20 +ReadOnly -WantCtrlA
Gui ZhanFa:Add, Button, gZhanFaAddKey x96 y40 w80 h22, 添加技能键
Gui ZhanFa:Add, Button, gZhanFaDeleteKey x96 y70 w80 h22, 删除技能键
Gui ZhanFa:Add, Text, x8 y8 w80 h20 +0x200, 已添加技能键
Gui ZhanFa:Add, Text, x96 y100 w80 h20 +0x200, 炫纹发射键
Gui ZhanFa:Add, Button, gZhanFaSave x96 y150 w80 h27, 保存
Gui ZhanFa:Add, Button, gZhanFaHelp x158 y8 w18 h18, ?
Gui ZhanFa:Font, cRed
Gui ZhanFa:Add, Text, x8 y184 w168 h76, 说明：按住已添加的技能键时自动发射炫纹。`n限制：发射键必须是数字小键盘按键。`n提示：使用时先按住炫纹发射键，再按 Num Lock 关闭数字小键盘。
Gui ZhanFa:Font

RegisterDirectKeyInput("ZhanFa", "ZhanFaShotKey", ZhanFaShotKeyHwnd)

ShowGuiZhanFa(){
    Gui ZhanFa:Show, w184 h268, 战法自动炫纹
    ZhanFaLoadConfig()
    DisableGuiMain()
}

HideGuiZhanFa(){
    Gui ZhanFa:Hide
    EnableGuiMain()
}

ZhanFaGuiEscape(){
    HideGuiZhanFa()
}

ZhanFaGuiClose(){
    HideGuiZhanFa()
}

ZhanFaHelp(){
    MsgBox 0x2040, 战法自动炫纹, 1、添加需要触发自动炫纹的技能键。`n2、将炫纹发射键设置为数字小键盘按键。`n3、按住任一已添加的技能键时，工具会自动高速发射炫纹。`n4、使用时先按住炫纹发射键，再按 Num Lock 关闭数字小键盘。
}

global __ZhanFaSkillKeys := []

ZhanFaAddKey(){
    global __ZhanFaSkillKeys
    key := GetPressKey()
    if(IsValueInArray(key, __ZhanFaSkillKeys)){
        MsgBox 0x10, , 请勿重复添加按键
    }else{
        __ZhanFaSkillKeys.Push(key)
    }
    ZhanFaChangeListGui(__ZhanFaSkillKeys)
    GuiControl ZhanFa:ChooseString, ZhanFaKeysListBox, |%key%
}

ZhanFaDeleteKey(){
    global __ZhanFaSkillKeys
    global ZhanFaKeysListBox
    Gui ZhanFa:Submit, NoHide
    DeleteValueInArray(ZhanFaKeysListBox, __ZhanFaSkillKeys)
    ZhanFaChangeListGui(__ZhanFaSkillKeys)
}

ZhanFaSave(){
    if (ZhanFaSaveConfig()) {
        HideGuiZhanFa()
    }
}

; 战法功能模块修改列表
ZhanFaChangeListGui(keys){
    keysString := ""
    for k,v in keys
    {
        keysString := keysString . v . "|"
    }
    keysString := SubStr(keysString, 1, StrLen(keysString) - 1)
    GuiControl ZhanFa:, ZhanFaKeysListBox, |%keysString%
    GuiControl ZhanFa:Choose, ZhanFaKeysListBox, 1
}

; 战法功能模块保存配置
ZhanFaSaveConfig(){
    global __ZhanFaSkillKeys
    global ZhanFaShotKey
    Gui ZhanFa:Submit, NoHide
    if (!ZhanFaIsNumpadKey(ZhanFaShotKey)) {
        MsgBox 0x2030, 战法自动炫纹, 炫纹发射键必须是数字小键盘按键，请重新设置。
        return false
    }
    keysString := ""
    for k,v in __ZhanFaSkillKeys
    {
        keysString := keysString . v . "|"
    }
    keysString := SubStr(keysString, 1, StrLen(keysString) - 1)
    SavePreset(GetNowSelectPreset(),"ZhanFaSkillKeys", keysString)
    SavePreset(GetNowSelectPreset(),"ZhanFaShotKey", ZhanFaShotKey)
    return true
}

; 战法功能模块读取配置
ZhanFaLoadConfig(){
    global __ZhanFaSkillKeys
    nowSelectPreset := GetNowSelectPreset()
    shotKey := LoadPreset(GetNowSelectPreset(), "ZhanFaShotKey")
    if (!ZhanFaIsNumpadKey(shotKey)) {
        shotKey := ""
    }
    __ZhanFaSkillKeys := ZhanFaLoadKeys(GetNowSelectPreset())
    ZhanFaChangeListGui(__ZhanFaSkillKeys)
    GuiControl ZhanFa:, ZhanFaShotKey, %shotKey%
}
