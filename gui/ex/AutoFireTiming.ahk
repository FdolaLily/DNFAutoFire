global _AutoFireTimingPresetName := ""

Gui AutoFireTiming:+ToolWindow +OwnerMain
Gui AutoFireTiming:Add, Text, vAutoFireTimingPresetLabel x16 y12 w368 h24 +0x200
Gui AutoFireTiming:Add, Text, x16 y48 w128 h24 +0x200, 按下持续时间 (ms)
Gui AutoFireTiming:Add, Edit, vAutoFireTimingDownMs gAutoFireTimingUpdateRate x160 y48 w88 h24 +Number
Gui AutoFireTiming:Add, Text, x16 y84 w128 h24 +0x200, 抬起持续时间 (ms)
Gui AutoFireTiming:Add, Edit, vAutoFireTimingUpMs gAutoFireTimingUpdateRate x160 y84 w88 h24 +Number
Gui AutoFireTiming:Add, Text, vAutoFireTimingRate x16 y122 w368 h24 +0x200
Gui AutoFireTiming:Add, Text, x16 y154 w368 h76, 每项可设 1 至 100 毫秒。默认 10 + 10ms，理论输出 50Hz。`n1 + 1ms 为 500Hz 实验设置。`n理论输出频率不代表 DNF 实际识别次数；过短的按下或抬起可能被游戏漏采样。
Gui AutoFireTiming:Add, Text, x16 y236 w368 h40, 应用于当前方案的普通连发和职业连发。保存后停止连发，请重新启动以应用新时序。
Gui AutoFireTiming:Add, Button, gAutoFireTimingDefaults x16 y286 w104 h30, 恢复默认
Gui AutoFireTiming:Add, Button, gAutoFireTimingSave x280 y286 w104 h30, 保存

ShowGuiAutoFireTiming(){
    global _AutoFireTimingPresetName
    presetName := GetNowSelectPreset()
    if (Trim(presetName) == "") {
        MsgBox 0x2010, 连发时序, 请先保存或读取一个方案。
        return
    }
    _AutoFireTimingPresetName := presetName
    timing := AutoFireLoadTiming(presetName)
    GuiControl AutoFireTiming:, AutoFireTimingPresetLabel, % "当前方案：" . presetName
    GuiControl AutoFireTiming:, AutoFireTimingDownMs, % timing.downMs
    GuiControl AutoFireTiming:, AutoFireTimingUpMs, % timing.upMs
    AutoFireTimingUpdateRate()
    DisableGuiMain()
    Gui AutoFireTiming:Show, w400 h330, 连发时序设置
}

HideGuiAutoFireTiming(){
    Gui AutoFireTiming:Hide
    EnableGuiMain()
}

AutoFireTimingGuiEscape(){
    HideGuiAutoFireTiming()
}

AutoFireTimingGuiClose(){
    HideGuiAutoFireTiming()
}

AutoFireTimingIsValid(value){
    if value is not integer
        return false
    return value >= 1 && value <= 100
}

AutoFireTimingUpdateRate(){
    GuiControlGet, downMs, AutoFireTiming:, AutoFireTimingDownMs
    GuiControlGet, upMs, AutoFireTiming:, AutoFireTimingUpMs
    if (AutoFireTimingIsValid(downMs) && AutoFireTimingIsValid(upMs)) {
        label := "理论输出：" . Round(1000.0 / (downMs + upMs), 2) . " Hz / 键"
    } else {
        label := "请输入 1 至 100 的整数毫秒。"
    }
    GuiControl AutoFireTiming:, AutoFireTimingRate, %label%
}

AutoFireTimingDefaults(){
    GuiControl AutoFireTiming:, AutoFireTimingDownMs, 10
    GuiControl AutoFireTiming:, AutoFireTimingUpMs, 10
    AutoFireTimingUpdateRate()
}

AutoFireTimingSave(){
    global _AutoFireTimingPresetName
    presetName := _AutoFireTimingPresetName
    GuiControlGet, downMs, AutoFireTiming:, AutoFireTimingDownMs
    GuiControlGet, upMs, AutoFireTiming:, AutoFireTimingUpMs
    if (!AutoFireTimingIsValid(downMs) || !AutoFireTimingIsValid(upMs)) {
        MsgBox 0x2010, 连发时序, 按下和抬起持续时间均须为 1 至 100 的整数毫秒。
        return false
    }
    if (presetName == "" || GetNowSelectPreset() != presetName) {
        MsgBox 0x2010, 连发时序, 当前方案已切换，请重新打开时序设置后保存。
        return false
    }
    StopAutoFire()
    if (GetNowSelectPreset() != presetName) {
        MsgBox 0x2010, 连发时序, 当前方案已切换，请重新打开时序设置后保存。
        return false
    }
    SavePreset(presetName, "AutoFireDownMs", downMs + 0)
    SavePreset(presetName, "AutoFireUpMs", upMs + 0)
    HideGuiAutoFireTiming()
    return true
}
