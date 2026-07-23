Gui OneKeyRun:+ToolWindow +Owner%A_DefaultGui%
Gui OneKeyRun:Add, Text, x8 y10 w48 h20 +0x200, 向上键
Gui OneKeyRun:Add, Edit, vOneKeyRunUpKey hwndOneKeyRunUpKeyHwnd x58 y10 w136 h20 +ReadOnly -WantCtrlA
Gui OneKeyRun:Add, Text, x8 y40 w48 h20 +0x200, 向下键
Gui OneKeyRun:Add, Edit, vOneKeyRunDownKey hwndOneKeyRunDownKeyHwnd x58 y40 w136 h20 +ReadOnly -WantCtrlA
Gui OneKeyRun:Add, Text, x8 y70 w48 h20 +0x200, 向左键
Gui OneKeyRun:Add, Edit, vOneKeyRunLeftKey hwndOneKeyRunLeftKeyHwnd x58 y70 w136 h20 +ReadOnly -WantCtrlA
Gui OneKeyRun:Add, Text, x8 y100 w48 h20 +0x200, 向右键
Gui OneKeyRun:Add, Edit, vOneKeyRunRightKey hwndOneKeyRunRightKeyHwnd x58 y100 w136 h20 +ReadOnly -WantCtrlA

Gui OneKeyRun:Add, Text, x8 y134 w90 h20 +0x200, 按键脉冲(ms)
Gui OneKeyRun:Add, Edit, vOneKeyRunPressDelay x104 y134 w90 h20 +Number
Gui OneKeyRun:Add, Text, x8 y164 w90 h20 +0x200, 双击间隔(ms)
Gui OneKeyRun:Add, Edit, vOneKeyRunGapDelay x104 y164 w90 h20 +Number
Gui OneKeyRun:Add, Text, x8 y194 w90 h20 +0x200, 搓招保护(ms)
Gui OneKeyRun:Add, Edit, vOneKeyRunGuardDelay x104 y194 w90 h20 +Number
Gui OneKeyRun:Add, Text, x8 y224 w90 h20 +0x200, 游戏内开关键
Gui OneKeyRun:Add, Edit, vOneKeyRunToggleHotKey hwndOneKeyRunToggleHotKeyHwnd x104 y224 w90 h20 +ReadOnly -WantCtrlA

Gui OneKeyRun:Add, CheckBox, vOneKeyRunUsePresetKeys gOneKeyRunChangeKeyScope x8 y256 h20 w16
Gui OneKeyRun:Add, Text, x28 y258 w166 h20, 当前方案单独配置方向键
Gui OneKeyRun:Add, Button, gOneKeyRunSave x8 y288 w90 h28, 保存
Gui OneKeyRun:Add, Button, gOneKeyRunHelp x104 y288 w90 h28, 使用说明

RegisterDirectKeyInput("OneKeyRun", "OneKeyRunUpKey", OneKeyRunUpKeyHwnd)
RegisterDirectKeyInput("OneKeyRun", "OneKeyRunDownKey", OneKeyRunDownKeyHwnd)
RegisterDirectKeyInput("OneKeyRun", "OneKeyRunLeftKey", OneKeyRunLeftKeyHwnd)
RegisterDirectKeyInput("OneKeyRun", "OneKeyRunRightKey", OneKeyRunRightKeyHwnd)
RegisterDirectKeyInput("OneKeyRun", "OneKeyRunToggleHotKey", OneKeyRunToggleHotKeyHwnd)

ShowGuiOneKeyRun(){
    OneKeyRunLoadConfig()
    Gui OneKeyRun:Show, w202 h326, 一键奔跑设置
    DisableGuiMain()
}

HideGuiOneKeyRun(){
    Gui OneKeyRun:Hide
    EnableGuiMain()
}

OneKeyRunGuiEscape(){
    HideGuiOneKeyRun()
}

OneKeyRunGuiClose(){
    HideGuiOneKeyRun()
}

OneKeyRunHelp(){
    MsgBox 0x2040, 一键奔跑, 1、默认方向键对所有连发方案生效。`n2、勾选“当前方案单独配置方向键”后，四个方向键只覆盖当前方案。`n3、初次按下立即行走；持续单方向超过保护时间后，仅补一次抬起/按下进入奔跑。`n4、按键脉冲和双击间隔默认 30ms，搓招保护最低 140ms。`n5、首次启动前，350ms 内的不同方向优先按顺序搓招处理。`n6、奔跑确认后进入连续移动会话：正交方向立即继承奔跑，相反方向以 90ms 异步短确认切换，不阻塞快速乱动。`n7、技能释放只恢复已确认的奔跑方向。
}

OneKeyRunChangeKeyScope(){
    global OneKeyRunUsePresetKeys
    Gui OneKeyRun:Submit, NoHide
    OneKeyRunLoadDirectionControls(OneKeyRunUsePresetKeys)
}

OneKeyRunSave(){
    if (OneKeyRunSaveConfig()) {
        HideGuiOneKeyRun()
    }
}

OneKeyRunSaveConfig(){
    global OneKeyRunUpKey
    global OneKeyRunDownKey
    global OneKeyRunLeftKey
    global OneKeyRunRightKey
    global OneKeyRunPressDelay
    global OneKeyRunGapDelay
    global OneKeyRunGuardDelay
    global OneKeyRunToggleHotKey
    global OneKeyRunUsePresetKeys
    Gui OneKeyRun:Submit, NoHide

    if (OneKeyRunUpKey == "" || OneKeyRunDownKey == "" || OneKeyRunLeftKey == "" || OneKeyRunRightKey == "") {
        MsgBox 0x2010, , 请完整设置上、下、左、右四个方向键
        return false
    }
    if (OneKeyRunPressDelay < 1 || OneKeyRunPressDelay > 1000 || OneKeyRunGapDelay < 1 || OneKeyRunGapDelay > 1000 || OneKeyRunGuardDelay < 140 || OneKeyRunGuardDelay > 1000) {
        MsgBox 0x2010, , 按键延迟须为 1 至 1000 毫秒，搓招保护须为 140 至 1000 毫秒
        return false
    }
    if (OneKeyRunToggleHotKey == "") {
        MsgBox 0x2010, , 请设置游戏内开关热键
        return false
    }

    presetName := GetNowSelectPreset()
    SavePreset(presetName, "OneKeyRunUsePresetKeys", OneKeyRunUsePresetKeys)
    if (OneKeyRunUsePresetKeys) {
        SavePreset(presetName, "OneKeyRunUpKey", OneKeyRunUpKey)
        SavePreset(presetName, "OneKeyRunDownKey", OneKeyRunDownKey)
        SavePreset(presetName, "OneKeyRunLeftKey", OneKeyRunLeftKey)
        SavePreset(presetName, "OneKeyRunRightKey", OneKeyRunRightKey)
    } else {
        SaveConfig("OneKeyRunUpKey", OneKeyRunUpKey)
        SaveConfig("OneKeyRunDownKey", OneKeyRunDownKey)
        SaveConfig("OneKeyRunLeftKey", OneKeyRunLeftKey)
        SaveConfig("OneKeyRunRightKey", OneKeyRunRightKey)
    }
    SaveConfig("OneKeyRunPressDelay", OneKeyRunPressDelay)
    SaveConfig("OneKeyRunGapDelay", OneKeyRunGapDelay)
    SaveConfig("OneKeyRunGuardDelay", OneKeyRunGuardDelay)
    SaveConfig("OneKeyRunToggleHotKey", OneKeyRunToggleHotKey)
    return true
}

OneKeyRunLoadConfig(){
    global OneKeyRunUpKey
    global OneKeyRunDownKey
    global OneKeyRunLeftKey
    global OneKeyRunRightKey
    global OneKeyRunPressDelay
    global OneKeyRunGapDelay
    global OneKeyRunGuardDelay
    global OneKeyRunToggleHotKey
    global OneKeyRunUsePresetKeys
    presetName := GetNowSelectPreset()
    OneKeyRunUsePresetKeys := LoadPreset(presetName, "OneKeyRunUsePresetKeys", false)
    GuiControl OneKeyRun:, OneKeyRunUsePresetKeys, %OneKeyRunUsePresetKeys%
    OneKeyRunLoadDirectionControls(OneKeyRunUsePresetKeys)
    OneKeyRunPressDelay := OneKeyRunGetDelay(LoadConfig("OneKeyRunPressDelay", 30))
    OneKeyRunGapDelay := OneKeyRunGetDelay(LoadConfig("OneKeyRunGapDelay", 30))
    OneKeyRunGuardDelay := OneKeyRunGetGuardDelay(LoadConfig("OneKeyRunGuardDelay", 140))
    OneKeyRunToggleHotKey := LoadConfig("OneKeyRunToggleHotKey", "F10")
    GuiControl OneKeyRun:, OneKeyRunPressDelay, %OneKeyRunPressDelay%
    GuiControl OneKeyRun:, OneKeyRunGapDelay, %OneKeyRunGapDelay%
    GuiControl OneKeyRun:, OneKeyRunGuardDelay, %OneKeyRunGuardDelay%
    GuiControl OneKeyRun:, OneKeyRunToggleHotKey, %OneKeyRunToggleHotKey%
}

OneKeyRunLoadDirectionControls(usePresetKeys){
    global OneKeyRunUpKey
    global OneKeyRunDownKey
    global OneKeyRunLeftKey
    global OneKeyRunRightKey
    presetName := GetNowSelectPreset()
    globalKeys := [LoadConfig("OneKeyRunUpKey", "Up")
        , LoadConfig("OneKeyRunDownKey", "Down")
        , LoadConfig("OneKeyRunLeftKey", "Left")
        , LoadConfig("OneKeyRunRightKey", "Right")]
    if (usePresetKeys) {
        OneKeyRunUpKey := LoadPreset(presetName, "OneKeyRunUpKey", globalKeys[1])
        OneKeyRunDownKey := LoadPreset(presetName, "OneKeyRunDownKey", globalKeys[2])
        OneKeyRunLeftKey := LoadPreset(presetName, "OneKeyRunLeftKey", globalKeys[3])
        OneKeyRunRightKey := LoadPreset(presetName, "OneKeyRunRightKey", globalKeys[4])
    } else {
        OneKeyRunUpKey := globalKeys[1]
        OneKeyRunDownKey := globalKeys[2]
        OneKeyRunLeftKey := globalKeys[3]
        OneKeyRunRightKey := globalKeys[4]
    }
    GuiControl OneKeyRun:, OneKeyRunUpKey, %OneKeyRunUpKey%
    GuiControl OneKeyRun:, OneKeyRunDownKey, %OneKeyRunDownKey%
    GuiControl OneKeyRun:, OneKeyRunLeftKey, %OneKeyRunLeftKey%
    GuiControl OneKeyRun:, OneKeyRunRightKey, %OneKeyRunRightKey%
}
