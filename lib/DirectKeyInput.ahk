; 点击只读按键输入框后直接录入按键，复用 GetUserInputKey 的完整键范围。
global _DirectKeyInputControls := {}
global _DirectKeyInputCapturing := false
OnMessage(0x0201, "DirectKeyInputHandleClick")

RegisterDirectKeyInput(guiName, controlName, controlHwnd){
    global _DirectKeyInputControls
    _DirectKeyInputControls[controlHwnd] := { guiName: guiName, controlName: controlName }
}

DirectKeyInputHandleClick(wParam, lParam, msg, controlHwnd){
    global _DirectKeyInputControls
    global _DirectKeyInputCapturing
    if (_DirectKeyInputCapturing || !_DirectKeyInputControls.HasKey(controlHwnd)) {
        return
    }
    captureFn := Func("DirectKeyInputCapture").Bind(controlHwnd)
    SetTimer, %captureFn%, -1
}

DirectKeyInputCapture(controlHwnd){
    global _DirectKeyInputControls
    global _DirectKeyInputCapturing
    if (_DirectKeyInputCapturing || !_DirectKeyInputControls.HasKey(controlHwnd)) {
        return
    }
    _DirectKeyInputCapturing := true
    control := _DirectKeyInputControls[controlHwnd]
    guiName := control.guiName
    controlName := control.controlName
    GuiControlGet, oldValue, %guiName%:, %controlName%
    GuiControl, %guiName%:, %controlName%, 请按键...
    key := GetUserInputKey(true)
    if (key == "") {
        key := oldValue
    }
    GuiControl, %guiName%:, %controlName%, %key%
    _DirectKeyInputCapturing := false
}
