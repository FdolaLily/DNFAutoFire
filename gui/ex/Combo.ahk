Gui Combo:+ToolWindow +Owner%A_DefaultGui%
Gui Combo:Add, Text, x8 y8 w120 h20 +0x200, 已添加连招
Gui Combo:Add, ListBox, vComboGroupsListBox gComboChangeList x8 y32 w210 h188

Gui Combo:Add, Text, x230 y8 w56 h20 +0x200, 触发键
Gui Combo:Add, Edit, vComboTriggerKey x286 y8 w64 h20 +ReadOnly -WantCtrlA
Gui Combo:Add, Button, gComboSetTriggerKey x356 y7 w70 h22, 设置

Gui Combo:Add, Text, x230 y40 w24 h20 +0x200, 1
Gui Combo:Add, Edit, vComboKey1 x254 y40 w64 h20 +ReadOnly -WantCtrlA
Gui Combo:Add, Button, gComboSetKey1 x324 y39 w50 h22, 设置
Gui Combo:Add, Edit, vComboInterval1 x382 y40 w56 h20 +Number, 0
Gui Combo:Add, Text, x442 y40 w28 h20 +0x200, ms

Gui Combo:Add, Text, x230 y68 w24 h20 +0x200, 2
Gui Combo:Add, Edit, vComboKey2 x254 y68 w64 h20 +ReadOnly -WantCtrlA
Gui Combo:Add, Button, gComboSetKey2 x324 y67 w50 h22, 设置
Gui Combo:Add, Edit, vComboInterval2 x382 y68 w56 h20 +Number, 0
Gui Combo:Add, Text, x442 y68 w28 h20 +0x200, ms

Gui Combo:Add, Text, x230 y96 w24 h20 +0x200, 3
Gui Combo:Add, Edit, vComboKey3 x254 y96 w64 h20 +ReadOnly -WantCtrlA
Gui Combo:Add, Button, gComboSetKey3 x324 y95 w50 h22, 设置
Gui Combo:Add, Edit, vComboInterval3 x382 y96 w56 h20 +Number, 0
Gui Combo:Add, Text, x442 y96 w28 h20 +0x200, ms

Gui Combo:Add, Text, x230 y124 w24 h20 +0x200, 4
Gui Combo:Add, Edit, vComboKey4 x254 y124 w64 h20 +ReadOnly -WantCtrlA
Gui Combo:Add, Button, gComboSetKey4 x324 y123 w50 h22, 设置
Gui Combo:Add, Edit, vComboInterval4 x382 y124 w56 h20 +Number, 0
Gui Combo:Add, Text, x442 y124 w28 h20 +0x200, ms

Gui Combo:Add, Text, x230 y152 w24 h20 +0x200, 5
Gui Combo:Add, Edit, vComboKey5 x254 y152 w64 h20 +ReadOnly -WantCtrlA
Gui Combo:Add, Button, gComboSetKey5 x324 y151 w50 h22, 设置
Gui Combo:Add, Edit, vComboInterval5 x382 y152 w56 h20 +Number, 0
Gui Combo:Add, Text, x442 y152 w28 h20 +0x200, ms

Gui Combo:Add, Button, gComboAddOrUpdate x230 y190 w70 h26, 添加/更新
Gui Combo:Add, Button, gComboDelete x306 y190 w50 h26, 删除
Gui Combo:Add, Button, gComboClearEditor x362 y190 w50 h26, 清空
Gui Combo:Add, Button, gComboSave x418 y190 w50 h26, 保存

global __ComboEditGroups := []
global __ComboSelectedIndex := 0

ShowGuiCombo(){
    Gui Combo:Show, w478 h228, 一键连招
    ComboLoadConfig(GetNowSelectPreset())
    DisableGuiMain()
}

HideGuiCombo(){
    Gui Combo:Hide
    EnableGuiMain()
}

ComboGuiEscape(){
    HideGuiCombo()
}

ComboGuiClose(){
    HideGuiCombo()
}

ComboSetTriggerKey(){
    key := GetPressKey()
    GuiControl Combo:, ComboTriggerKey, %key%
}

ComboSetKey1(){
    ComboSetStepKey(1)
}

ComboSetKey2(){
    ComboSetStepKey(2)
}

ComboSetKey3(){
    ComboSetStepKey(3)
}

ComboSetKey4(){
    ComboSetStepKey(4)
}

ComboSetKey5(){
    ComboSetStepKey(5)
}

ComboSetStepKey(index){
    key := GetPressKey()
    controlName := "ComboKey" . index
    GuiControl Combo:, %controlName%, %key%
}

ComboReadEditor(){
    global ComboTriggerKey
    global ComboKey1, ComboKey2, ComboKey3, ComboKey4, ComboKey5
    global ComboInterval1, ComboInterval2, ComboInterval3, ComboInterval4, ComboInterval5
    Gui Combo:Submit, NoHide
    keys := [ComboKey1, ComboKey2, ComboKey3, ComboKey4, ComboKey5]
    intervals := [ComboInterval1, ComboInterval2, ComboInterval3, ComboInterval4, ComboInterval5]
    return ComboCreateGroup(ComboTriggerKey, keys, intervals)
}

ComboAddOrUpdate(){
    global __ComboEditGroups
    group := ComboReadEditor()
    if (group == "") {
        MsgBox 0x2010, , 请设置触发键和至少一个后续按键
        return
    }

    selectedIndex := ComboGetSelectedIndex()
    ComboUpsertGroup(__ComboEditGroups, group, selectedIndex)
    ComboChangeListGui(__ComboEditGroups)
    ComboClearEditor()
}

ComboDelete(){
    global __ComboEditGroups
    selectedIndex := ComboGetSelectedIndex()
    if (selectedIndex > 0) {
        __ComboEditGroups.Delete(selectedIndex)
        ComboChangeListGui(__ComboEditGroups)
        ComboClearEditor()
    }
}

ComboClearEditor(){
    global __ComboSelectedIndex
    __ComboSelectedIndex := 0
    GuiControl Combo:Choose, ComboGroupsListBox, 0
    GuiControl Combo:, ComboTriggerKey,
    loop, 5 {
        keyControl := "ComboKey" . A_Index
        intervalControl := "ComboInterval" . A_Index
        GuiControl Combo:, %keyControl%,
        GuiControl Combo:, %intervalControl%, 0
    }
}

ComboGetSelectedIndex(){
    global __ComboSelectedIndex
    global __ComboEditGroups
    if (__ComboSelectedIndex > 0 && __ComboSelectedIndex <= __ComboEditGroups.Length()) {
        return __ComboSelectedIndex
    }
    return 0
}

ComboFindSelectedIndex(){
    global __ComboEditGroups
    global ComboGroupsListBox
    Gui Combo:Submit, NoHide
    if (ComboGroupsListBox == "") {
        return 0
    }
    for index, group in __ComboEditGroups {
        if (ComboSummarizeGroup(group) == ComboGroupsListBox) {
            return index
        }
    }
    return 0
}

ComboChangeListGui(groups){
    global __ComboSelectedIndex
    items := ""
    for _, group in groups {
        items .= ComboSummarizeGroup(group) . "|"
    }
    GuiControl Combo:, ComboGroupsListBox, |%items%
    __ComboSelectedIndex := 0
}

ComboLoadEditor(group){
    trigger := ComboGetTrigger(group)
    GuiControl Combo:, ComboTriggerKey, %trigger%
    steps := ComboGetSteps(group)
    loop, 5 {
        key := ""
        interval := 0
        if (A_Index <= steps.Length()) {
            key := steps[A_Index].key
            interval := steps[A_Index].interval
        }
        keyControl := "ComboKey" . A_Index
        intervalControl := "ComboInterval" . A_Index
        GuiControl Combo:, %keyControl%, %key%
        GuiControl Combo:, %intervalControl%, %interval%
    }
}

ComboChangeList(){
    global __ComboEditGroups
    global __ComboSelectedIndex
    selectedIndex := ComboFindSelectedIndex()
    __ComboSelectedIndex := selectedIndex
    if (selectedIndex > 0) {
        ComboLoadEditor(__ComboEditGroups[selectedIndex])
    }
}

ComboCloneGroups(groups){
    clonedGroups := []
    for _, group in groups {
        clonedGroups.Push(group)
    }
    return clonedGroups
}

ComboSave(){
    ComboSaveConfig(GetNowSelectPreset())
    HideGuiCombo()
}

ComboSaveConfig(presetName){
    global _ComboGroups
    global __ComboEditGroups
    _ComboGroups := ComboCloneGroups(__ComboEditGroups)
    SavePresetCombos(presetName, _ComboGroups)
}

ComboLoadConfig(presetName){
    global _ComboGroups
    global __ComboEditGroups
    _ComboGroups := LoadPresetCombos(presetName)
    __ComboEditGroups := ComboCloneGroups(_ComboGroups)
    ComboChangeListGui(__ComboEditGroups)
    ComboClearEditor()
}
