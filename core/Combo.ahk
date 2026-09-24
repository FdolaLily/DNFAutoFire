global _ComboGroups := []
global _ComboHotkeyTriggers := []
global _ComboGeneration := 0

ComboNormalizeInterval(value) {
    value := value + 0
    if (value < 0) {
        return 0
    }
    return Floor(value)
}

ComboCreateGroup(trigger, keys, intervals) {
    trigger := Trim(trigger)
    if (trigger == "") {
        return ""
    }

    steps := ""
    stepCount := 0
    for index, key in keys {
        key := Trim(key)
        if (key == "") {
            continue
        }

        interval := ComboNormalizeInterval(intervals[index])
        steps .= key . "," . interval . ";"
        stepCount++
        if (stepCount >= 5) {
            break
        }
    }

    if (stepCount == 0) {
        return ""
    }

    return trigger . ">" . SubStr(steps, 1, StrLen(steps) - 1)
}

ComboLoadGroups(serialized) {
    groups := []
    loop, Parse, serialized, |
    {
        group := Trim(A_LoopField)
        if (ComboIsValidGroup(group)) {
            groups.Push(group)
        }
    }
    return groups
}

ComboSerializeGroups(groups) {
    serialized := ""
    for _, group in groups {
        if (ComboIsValidGroup(group)) {
            serialized .= group . "|"
        }
    }
    if (serialized == "") {
        return ""
    }
    return SubStr(serialized, 1, StrLen(serialized) - 1)
}

ComboIsValidGroup(group) {
    parts := StrSplit(group, ">")
    if (parts.Length() != 2 || Trim(parts[1]) == "" || Trim(parts[2]) == "") {
        return false
    }

    stepText := parts[2]
    stepCount := 0
    loop, Parse, stepText, `;
    {
        step := Trim(A_LoopField)
        if (step == "") {
            continue
        }
        stepParts := StrSplit(step, ",")
        if (stepParts.Length() < 1 || Trim(stepParts[1]) == "") {
            continue
        }
        stepCount++
    }
    return stepCount > 0
}

ComboGetTrigger(group) {
    return StrSplit(group, ">")[1]
}

ComboGetSteps(group) {
    parts := StrSplit(group, ">")
    steps := []
    if (parts.Length() != 2) {
        return steps
    }

    stepText := parts[2]
    loop, Parse, stepText, `;
    {
        step := Trim(A_LoopField)
        if (step == "") {
            continue
        }
        stepParts := StrSplit(step, ",")
        key := Trim(stepParts[1])
        interval := stepParts.Length() >= 2 ? ComboNormalizeInterval(stepParts[2]) : 0
        if (key != "") {
            steps.Push({ key: key, interval: interval })
        }
    }
    return steps
}

ComboSummarizeGroup(group) {
    trigger := ComboGetTrigger(group)
    text := trigger . " -> "
    steps := ComboGetSteps(group)
    for index, step in steps {
        text .= step.key . "(" . step.interval . "ms)"
        if (index < steps.Length()) {
            text .= ", "
        }
    }
    return text
}

ComboFindTriggerIndex(groups, trigger) {
    trigger := Trim(trigger)
    if (trigger == "") {
        return 0
    }

    for index, group in groups {
        if (ComboGetTrigger(group) == trigger) {
            return index
        }
    }
    return 0
}

ComboUpsertGroup(ByRef groups, group, preferredIndex := 0) {
    if (!ComboIsValidGroup(group)) {
        return
    }

    trigger := ComboGetTrigger(group)
    targetIndex := preferredIndex
    if (targetIndex > 0 && targetIndex <= groups.Length()) {
        groups[targetIndex] := group
    } else {
        targetIndex := ComboFindTriggerIndex(groups, trigger)
        if (targetIndex > 0) {
            groups[targetIndex] := group
        } else {
            groups.Push(group)
            targetIndex := groups.Length()
        }
    }

    index := groups.Length()
    while (index > 0) {
        if (index != targetIndex && ComboGetTrigger(groups[index]) == trigger) {
            groups.Delete(index)
            if (index < targetIndex) {
                targetIndex--
            }
        }
        index--
    }
}

ComboIsTriggerKey(trigger) {
    global Combo
    global _ComboGroups
    if (!Combo) {
        return false
    }
    return ComboFindTriggerIndex(_ComboGroups, trigger) > 0
}

ComboKeyToHotkey(key) {
    keyName := GetOriginKeyName(key)
    if (!InStr(keyName, "Num")) {
        keyName := Key2SC(keyName)
    }
    return keyName
}

ComboSendKey(guiKey, generation := "") {
    global _ComboGeneration
    if (generation == "")
        generation := _ComboGeneration
    originKey := GetOriginKeyName(guiKey)
    if (!WinActive("ahk_group DNF"))
        return
    leaseCritical := A_IsCritical
    Critical, On
    lease := AutoFireNativeLease()
    try {
        if (!AutoFireNativePauseKey(originKey, true, lease))
            throw Exception("无法安全交接连招按键。")
        if (generation != _ComboGeneration || !AutoFireNativeLeaseValid(lease) || !WinActive("ahk_group DNF"))
            return
        priorCritical := A_IsCritical
        Critical, On
        try {
            if (generation != _ComboGeneration || !AutoFireNativeLeaseValid(lease) || !WinActive("ahk_group DNF"))
                return
            input := new AutoFireInput(originKey)
            try {
                if (!input.Send(true))
                    throw Exception("连招按下发送失败：" . input.LastError)
                DllCall("Sleep", "UInt", 10)
            } finally {
                if (!input.Send(false))
                    throw Exception("连招抬起发送失败：" . input.LastError)
            }
        } finally {
            Critical, %priorCritical%
        }
        DllCall("Sleep", "UInt", 10)
    } finally {
        AutoFireNativePauseKey(originKey, false, lease)
        Critical, %leaseCritical%
    }
}

ComboRun(group) {
    global _ComboGeneration
    if (!WinActive("ahk_group DNF")) {
        return
    }

    trigger := ComboGetTrigger(group)
    pressKey := ComboKeyToHotkey(trigger)
    steps := ComboGetSteps(group)
    generation := _ComboGeneration

    for _, step in steps {
        if (step.interval > 0) {
            Sleep, % step.interval
        }
        if (generation != _ComboGeneration || !WinActive("ahk_group DNF"))
            return
        ComboSendKey(step.key, generation)
    }

    KeyWait, %pressKey%
}

StartComboHotkeys() {
    global Combo
    global _ComboGroups
    global _ComboHotkeyTriggers
    StopComboHotkeys()
    if (!Combo) {
        return
    }

    registeredHotkeys := "|"
    Hotkey, IfWinActive, ahk_group DNF
    try {
        for _, group in _ComboGroups {
            trigger := ComboGetTrigger(group)
            if (trigger == "") {
                continue
            }

            hotkeyName := ComboKeyToHotkey(trigger)
            if (InStr(registeredHotkeys, "|" . hotkeyName . "|")) {
                continue
            }
            registeredHotkeys .= hotkeyName . "|"
            fn := Func("ComboRun").Bind(group)
            Hotkey, $*%hotkeyName%, %fn%, On
            _ComboHotkeyTriggers.Push(trigger)
        }
    } finally {
        Hotkey, IfWinActive
    }
}

StopComboHotkeys() {
    global _ComboHotkeyTriggers
    global _ComboGeneration
    _ComboGeneration++
    Hotkey, IfWinActive, ahk_group DNF
    for _, trigger in _ComboHotkeyTriggers {
        hotkeyName := ComboKeyToHotkey(trigger)
        try {
            Hotkey, $*%hotkeyName%, Off
        }
    }
    Hotkey, IfWinActive
    _ComboHotkeyTriggers := []
}
