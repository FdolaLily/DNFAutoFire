; Build one output owner per physical key. No configuration reads occur in the
; native scheduling loop; trigger aliases share the same physical identity.
AutoFireRuleKeyId(key){
    if (Trim(key) == "") {
        return ""
    }
    originKey := GetOriginKeyName(key)
    scan := GetKeySC(originKey)
    vk := GetKeyVK(originKey)
    if (!scan || !vk) {
        return ""
    }
    return Format("sc{:03X}", scan) . (vk == 0x13 ? ":pause" : ":key")
}

AutoFireRuleEnsure(rules, key, manual := false){
    keyId := AutoFireRuleKeyId(key)
    if (keyId == "") {
        return ""
    }
    for _, rule in rules {
        if (AutoFireRuleKeyId(rule.key) == keyId) {
            if (manual) {
                rule.manual := true
            }
            return rule
        }
    }
    rule := {key: key, manual: !!manual, delayMs: 0, triggers: []}
    rules.Push(rule)
    return rule
}

AutoFireRuleAddTrigger(rule, key){
    keyId := AutoFireRuleKeyId(key)
    if (!IsObject(rule) || keyId == "") {
        return
    }
    for _, existing in rule.triggers {
        if (AutoFireRuleKeyId(existing) == keyId) {
            return
        }
    }
    rule.triggers.Push(key)
}

AutoFireRuleAddConfiguredTriggers(rules, outputKey, triggerText){
    if (AutoFireRuleKeyId(outputKey) == "") {
        return
    }
    ; Avoid creating a disabled output owner for an empty/invalid trigger list.
    for _, key in StrSplit(triggerText, "|") {
        key := Trim(key)
        if (AutoFireRuleKeyId(key) != "") {
            rule := AutoFireRuleEnsure(rules, outputKey)
            AutoFireRuleAddTrigger(rule, key)
        }
    }
}

AutoFireRuleNormalizeDelay(value){
    if value is not integer
        return 200
    return value < 1 ? 1 : (value > 10000 ? 10000 : value + 0)
}

AutoFireBuildRules(normalKeys){
    global LvRen
    global ZhanFa
    global JianZong
    rules := []
    if (IsObject(normalKeys)) {
        for _, key in normalKeys {
            AutoFireRuleEnsure(rules, key, true)
        }
    }
    presetName := GetNowSelectPreset()
    if (LvRen) {
        AutoFireRuleAddConfiguredTriggers(rules
            , LoadPreset(presetName, "LvRenShotKey")
            , LoadPreset(presetName, "LvRenSkillKeys"))
    }
    if (ZhanFa) {
        outputKey := LoadPreset(presetName, "ZhanFaShotKey")
        if (ZhanFaIsNumpadKey(GetOriginKeyName(outputKey))) {
            AutoFireRuleAddConfiguredTriggers(rules, outputKey
                , LoadPreset(presetName, "ZhanFaSkillKeys"))
        }
    }
    if (JianZong) {
        skillKey := LoadPreset(presetName, "JianZongSkillKey")
        rule := AutoFireRuleEnsure(rules, skillKey)
        if (IsObject(rule)) {
            ; The skill's first hardware press remains visible to DNF. Delayed
            ; repeats must not be bypassed by selecting it as an ordinary key.
            rule.manual := false
            rule.delayMs := AutoFireRuleNormalizeDelay(LoadPreset(presetName, "JianZongDelay", 200))
            AutoFireRuleAddTrigger(rule, skillKey)
        }
    }
    return rules
}
