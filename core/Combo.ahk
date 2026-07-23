global _ComboGroups := []
global _ComboHotkeyTriggers := []

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

ComboSendKey(guiKey) {
    originKey := GetOriginKeyName(guiKey)
    SendIP(Key2NoVkSC(originKey))
}

ComboRun(group) {
    if (!WinActive("ahk_group DNF")) {
        return
    }

    trigger := ComboGetTrigger(group)
    pressKey := ComboKeyToHotkey(trigger)
    steps := ComboGetSteps(group)

    for _, step in steps {
        if (step.interval > 0) {
            Sleep, % step.interval
        }
        ComboSendKey(step.key)
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
