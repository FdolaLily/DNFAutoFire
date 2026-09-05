; 一键奔跑默认使用全局方向键；方案可选择启用自己的方向键覆盖。
global _OneKeyRunActiveKeys := {}
global _OneKeyRunEnabled := false
global _OneKeyRunRegisteredKeys := []
global _OneKeyRunToggleRegisteredKey := ""
global _OneKeyRunRunningKeys := {}
global _OneKeyRunSuppressedKeys := {}
global _OneKeyRunLastRunReleaseTime := 0
global _OneKeyRunLastRunReleaseWasStable := false
global _OneKeyRunLastDirectionPressKey := ""
global _OneKeyRunLastDirectionPressTime := 0
global _OneKeyRunInputEpoch := 0
global _OneKeyRunKeyGenerations := {}
global _OneKeyRunPendingKeys := {}
global _OneKeyRunPressTimes := {}
global _OneKeyRunInputObserver := ""
global _OneKeyRunObservedDownKeys := {}
global _OneKeyRunLastOtherInputKey := ""
global _OneKeyRunLastOtherInputTime := 0
global _OneKeyRunRefreshGeneration := 0

OneKeyRunGetCurrentKeys(){
    global OneKeyRunUpKey
    global OneKeyRunDownKey
    global OneKeyRunLeftKey
    global OneKeyRunRightKey
    return [OneKeyRunUpKey, OneKeyRunDownKey, OneKeyRunLeftKey, OneKeyRunRightKey]
}

OneKeyRunGetPresetKeys(presetName){
    globalKeys := [LoadConfig("OneKeyRunUpKey", "Up")
        , LoadConfig("OneKeyRunDownKey", "Down")
        , LoadConfig("OneKeyRunLeftKey", "Left")
        , LoadConfig("OneKeyRunRightKey", "Right")]
    if (!LoadPreset(presetName, "OneKeyRunUsePresetKeys", false)) {
        return globalKeys
    }
    return [LoadPreset(presetName, "OneKeyRunUpKey", globalKeys[1])
        , LoadPreset(presetName, "OneKeyRunDownKey", globalKeys[2])
        , LoadPreset(presetName, "OneKeyRunLeftKey", globalKeys[3])
        , LoadPreset(presetName, "OneKeyRunRightKey", globalKeys[4])]
}

OneKeyRunGetDelay(value, defaultValue := 30){
    if value is not integer
        return defaultValue
    if (value < 1) {
        return 1
    }
    if (value > 1000) {
        return 1000
    }
    return value
}

OneKeyRunGetGuardDelay(value){
    delay := OneKeyRunGetDelay(value, 140)
    ; 将前几版默认写入的保护时间自动回落到新的状态机默认值。
    if (delay == 350 || delay == 200 || delay == 180) {
        return 140
    }
    return delay < 140 ? 140 : delay
}

OneKeyRunGetCommandWindow(){
    return 350
}

OneKeyRunCanReuseInitialTap(waitDelay){
    ; 超过游戏常见双击识别窗口时，改用一套新的完整双击。
    return waitDelay <= 250
}

OneKeyRunGetSequentialWaitDelay(elapsed, guardDelay){
    remaining := OneKeyRunGetCommandWindow() - elapsed
    if (remaining < 0) {
        remaining := 0
    }
    return remaining > guardDelay ? remaining : guardDelay
}

OneKeyRunGetSwitchDelay(guardDelay){
    ; 连续移动中的变向先立即保持原始方向，短暂观察技能输入后再补双击。
    return guardDelay < 90 ? guardDelay : 90
}

OneKeyRunNormalizeKey(key){
    if (key == "") {
        return ""
    }
    keyName := GetOriginKeyName(key)
    if (!InStr(keyName, "Num")) {
        keyName := Key2SC(keyName)
    }
    return Format("{:L}", keyName)
}

OneKeyRunContainsKey(keys, key){
    normalizedKey := OneKeyRunNormalizeKey(key)
    for _, configuredKey in keys {
        if (configuredKey != "" && OneKeyRunNormalizeKey(configuredKey) == normalizedKey) {
            return true
        }
    }
    return false
}

OneKeyRunResolvePhysicalKeyState(hookDown, hasVirtualKey, windowsAsyncState){
    if (!hookDown) {
        return false
    }
    ; 无法映射虚拟键时沿用 AHK 物理状态，避免未知按键被错误放行。
    if (!hasVirtualKey) {
        return true
    }
    return (windowsAsyncState & 0x8000) != 0
}

OneKeyRunIsPhysicalKeyPressed(key){
    hookDown := GetKeyState(key, "P")
    if (!hookDown) {
        return false
    }
    vk := GetKeyVK(key)
    windowsAsyncState := vk
        ? DllCall("User32\GetAsyncKeyState", "Int", vk, "Short")
        : 0
    ; NumLock 状态切换等场景可能让 AHK 的 Hook 物理状态永久停在 Down。
    ; 同时核对 Windows 当前高位，只接受两个状态源均为按下的物理输入。
    return OneKeyRunResolvePhysicalKeyState(hookDown, vk != 0, windowsAsyncState)
}

OneKeyRunHasOtherInputPressed(key, physicalStateFn := ""){
    directionKeys := OneKeyRunGetCurrentKeys()
    oppositeKey := OneKeyRunGetOppositeKey(key)
    for _, configuredKey in GetAllKeys() {
        otherKey := OneKeyRunNormalizeKey(configuredKey)
        if (otherKey == "" || otherKey == key) {
            continue
        }
        isPressed := IsObject(physicalStateFn)
            ? physicalStateFn.Call(otherKey)
            : OneKeyRunIsPhysicalKeyPressed(otherKey)
        if (isPressed) {
            ; 同时按住的正交方向属于斜向移动，不应取消奔跑确认。
            if (OneKeyRunContainsKey(directionKeys, otherKey) && otherKey != oppositeKey) {
                continue
            }
            return true
        }
    }
    return false
}

OneKeyRunNextKeyGeneration(key){
    global _OneKeyRunKeyGenerations
    generation := _OneKeyRunKeyGenerations.HasKey(key)
        ? _OneKeyRunKeyGenerations[key] + 1
        : 1
    _OneKeyRunKeyGenerations[key] := generation
    return generation
}

OneKeyRunTokenMatches(key, expectedEpoch, expectedGeneration){
    global _OneKeyRunInputEpoch
    global _OneKeyRunKeyGenerations
    return _OneKeyRunInputEpoch == expectedEpoch
        && _OneKeyRunKeyGenerations.HasKey(key)
        && _OneKeyRunKeyGenerations[key] == expectedGeneration
}

OneKeyRunRegisterDirectionInput(){
    global _OneKeyRunInputEpoch
    _OneKeyRunInputEpoch += 1
    return _OneKeyRunInputEpoch
}

OneKeyRunNotifyOtherInput(key := ""){
    global _OneKeyRunEnabled
    global _OneKeyRunInputEpoch
    global _OneKeyRunLastOtherInputKey
    global _OneKeyRunLastOtherInputTime
    if (!_OneKeyRunEnabled) {
        return _OneKeyRunInputEpoch
    }
    normalizedKey := OneKeyRunNormalizeKey(key)
    if (normalizedKey != "" && OneKeyRunContainsKey(OneKeyRunGetCurrentKeys(), normalizedKey)) {
        return _OneKeyRunInputEpoch
    }
    ; InputHook 与按键热键可能同时报告同一次物理按下，短时间内去重。
    now := A_TickCount
    if (normalizedKey != ""
        && normalizedKey == _OneKeyRunLastOtherInputKey
        && now - _OneKeyRunLastOtherInputTime <= 20) {
        return _OneKeyRunInputEpoch
    }
    _OneKeyRunLastOtherInputKey := normalizedKey
    _OneKeyRunLastOtherInputTime := now
    _OneKeyRunInputEpoch += 1
    return _OneKeyRunInputEpoch
}

OneKeyRunGetObservedKey(vk, sc){
    keySpec := sc ? Format("vk{:02X}sc{:03X}", vk, sc) : Format("vk{:02X}", vk)
    return GetKeyName(keySpec)
}

OneKeyRunObserveKeyDown(inputHook, vk, sc){
    global _OneKeyRunEnabled
    global _OneKeyRunObservedDownKeys
    keyName := OneKeyRunGetObservedKey(vk, sc)
    if (keyName == "" || OneKeyRunContainsKey(OneKeyRunGetCurrentKeys(), keyName)) {
        return
    }
    normalizedKey := OneKeyRunNormalizeKey(keyName)
    if (_OneKeyRunObservedDownKeys.HasKey(normalizedKey)) {
        return
    }
    if (!_OneKeyRunEnabled || !WinActive("ahk_group DNF")) {
        _OneKeyRunObservedDownKeys[normalizedKey] := true
        return
    }
    OneKeyRunNotifyOtherInput(keyName)
    _OneKeyRunObservedDownKeys[normalizedKey] := true
}

OneKeyRunNotifyBlockingInput(key){
    global _OneKeyRunObservedDownKeys
    normalizedKey := OneKeyRunNormalizeKey(key)
    if (_OneKeyRunObservedDownKeys.HasKey(normalizedKey)) {
        return OneKeyRunGetInputEpoch()
    }
    ; 先占用该物理按键，避免 InputHook 与阻塞热键重复推进输入令牌。
    _OneKeyRunObservedDownKeys[normalizedKey] := true
    return OneKeyRunNotifyOtherInput(key)
}

OneKeyRunObserveKeyUp(inputHook, vk, sc){
    global _OneKeyRunActiveKeys
    global _OneKeyRunObservedDownKeys
    keyName := OneKeyRunGetObservedKey(vk, sc)
    if (keyName == "") {
        return
    }
    normalizedKey := OneKeyRunNormalizeKey(keyName)
    if (OneKeyRunContainsKey(OneKeyRunGetCurrentKeys(), normalizedKey)) {
        ; 方向键在游戏外松开时，上下文热键不会触发；仅做状态和逻辑键清理。
        if (!WinActive("ahk_group DNF") && _OneKeyRunActiveKeys.HasKey(normalizedKey)) {
            OneKeyRunStop(normalizedKey)
        }
        return
    }
    if (_OneKeyRunObservedDownKeys.HasKey(normalizedKey)) {
        _OneKeyRunObservedDownKeys.Delete(normalizedKey)
    }
}

OneKeyRunStartInputObserver(){
    global _OneKeyRunInputObserver
    OneKeyRunStopInputObserver()
    ; I1 忽略本工具及连发子进程注入的 SendInput，只观察物理输入。
    observer := InputHook("V I1")
    observer.KeyOpt("{All}", "N")
    observer.OnKeyDown := Func("OneKeyRunObserveKeyDown")
    observer.OnKeyUp := Func("OneKeyRunObserveKeyUp")
    observer.Start()
    _OneKeyRunInputObserver := observer
}

OneKeyRunStopInputObserver(){
    global _OneKeyRunInputObserver
    if (IsObject(_OneKeyRunInputObserver)) {
        try {
            _OneKeyRunInputObserver.Stop()
        }
    }
    _OneKeyRunInputObserver := ""
}

OneKeyRunWaitForRunIntent(key, waitDelay, expectedEpoch, expectedGeneration){
    global _OneKeyRunEnabled
    startTime := A_TickCount
    loop {
        if (!_OneKeyRunEnabled || !WinActive("ahk_group DNF") || !GetKeyState(key, "P")) {
            return false
        }
        if (!OneKeyRunTokenMatches(key, expectedEpoch, expectedGeneration)) {
            return false
        }
        if (A_TickCount - startTime >= waitDelay) {
            return !OneKeyRunHasOtherInputPressed(key)
        }
        Sleep, 2
    }
}

OneKeyRunGetOppositeKey(key){
    keys := OneKeyRunGetCurrentKeys()
    upKey := OneKeyRunNormalizeKey(keys[1])
    downKey := OneKeyRunNormalizeKey(keys[2])
    leftKey := OneKeyRunNormalizeKey(keys[3])
    rightKey := OneKeyRunNormalizeKey(keys[4])
    if (key == upKey) {
        return downKey
    }
    if (key == downKey) {
        return upKey
    }
    if (key == leftKey) {
        return rightKey
    }
    if (key == rightKey) {
        return leftKey
    }
    return ""
}

OneKeyRunGetDirectionRelation(key, otherKey){
    if (key == otherKey) {
        return "same"
    }
    if (OneKeyRunGetOppositeKey(key) == otherKey) {
        return "opposite"
    }
    return OneKeyRunContainsKey(OneKeyRunGetCurrentKeys(), otherKey)
        ? "orthogonal"
        : "other"
}

OneKeyRunGetHeldDirectionRelation(key){
    global _OneKeyRunActiveKeys
    hasOrthogonal := false
    for activeKey, _ in _OneKeyRunActiveKeys {
        if (activeKey == key || !GetKeyState(activeKey, "P")) {
            continue
        }
        relation := OneKeyRunGetDirectionRelation(key, activeKey)
        if (relation == "opposite") {
            return relation
        }
        if (relation == "orthogonal") {
            hasOrthogonal := true
        }
    }
    return hasOrthogonal ? "orthogonal" : ""
}

OneKeyRunHasRunningOrthogonalDirection(key){
    global _OneKeyRunRunningKeys
    for runningKey, _ in _OneKeyRunRunningKeys {
        if (GetKeyState(runningKey, "P")
            && OneKeyRunGetDirectionRelation(key, runningKey) == "orthogonal") {
            return true
        }
    }
    return false
}

OneKeyRunMarkRunningSession(key){
    global _OneKeyRunActiveKeys
    global _OneKeyRunRunningKeys
    _OneKeyRunRunningKeys[key] := true
    ; 首轴真正进入奔跑后，仍按住的正交轴直接继承同一移动会话。
    for activeKey, _ in _OneKeyRunActiveKeys {
        if (activeKey != key
            && GetKeyState(activeKey, "P")
            && OneKeyRunGetDirectionRelation(key, activeKey) == "orthogonal") {
            _OneKeyRunRunningKeys[activeKey] := true
        }
    }
}

OneKeyRunIsStableRun(key, now := ""){
    global _OneKeyRunRunningKeys
    ; 完成双击提交后即进入连续移动会话，不再要求同方向额外保持 350ms。
    return _OneKeyRunRunningKeys.HasKey(key)
}

OneKeyRunIsDirectionSwitch(key, now := ""){
    global _OneKeyRunRunningKeys
    global _OneKeyRunLastRunReleaseTime
    global _OneKeyRunLastRunReleaseWasStable
    if (now == "") {
        now := A_TickCount
    }
    for runningKey, _ in _OneKeyRunRunningKeys {
        if (runningKey != key) {
            return true
        }
    }
    return _OneKeyRunLastRunReleaseWasStable
        && _OneKeyRunLastRunReleaseTime > 0
        && now - _OneKeyRunLastRunReleaseTime <= 250
}

OneKeyRunClearPending(key, generation){
    global _OneKeyRunPendingKeys
    if (_OneKeyRunPendingKeys.HasKey(key) && _OneKeyRunPendingKeys[key] == generation) {
        _OneKeyRunPendingKeys.Delete(key)
    }
}

OneKeyRunCanContinue(key, expectedEpoch, expectedGeneration){
    global _OneKeyRunEnabled
    return _OneKeyRunEnabled
        && WinActive("ahk_group DNF")
        && GetKeyState(key, "P")
        && OneKeyRunTokenMatches(key, expectedEpoch, expectedGeneration)
}

OneKeyRunPromoteHeldDirection(key){
    global _OneKeyRunActiveKeys
    global _OneKeyRunEnabled
    global _OneKeyRunPendingKeys
    global _OneKeyRunRunningKeys
    global _OneKeyRunSuppressedKeys
    Critical, On
    if (!_OneKeyRunEnabled
        || !WinActive("ahk_group DNF")
        || !GetKeyState(key, "P")
        || !_OneKeyRunActiveKeys.HasKey(key)
        || _OneKeyRunPendingKeys.HasKey(key)
        || _OneKeyRunRunningKeys.HasKey(key)
        || _OneKeyRunSuppressedKeys.HasKey(key)) {
        Critical, Off
        return
    }
    ; 该键的原始 Down 仍然有效，只重新建立确认流程，不重复发送第一次 Down。
    _OneKeyRunActiveKeys.Delete(key)
    Critical, Off
    OneKeyRunStart(key, true)
}

OneKeyRunQueueHeldDirectionPromotion(){
    global _OneKeyRunActiveKeys
    global _OneKeyRunPendingKeys
    global _OneKeyRunRunningKeys
    global _OneKeyRunSuppressedKeys
    candidates := []
    for activeKey, _ in _OneKeyRunActiveKeys {
        if (GetKeyState(activeKey, "P")
            && !_OneKeyRunPendingKeys.HasKey(activeKey)
            && !_OneKeyRunRunningKeys.HasKey(activeKey)
            && !_OneKeyRunSuppressedKeys.HasKey(activeKey)) {
            candidates.Push(activeKey)
        }
    }
    if (candidates.Length() != 1) {
        return
    }
    timerFn := Func("OneKeyRunPromoteHeldDirection").Bind(candidates[1])
    SetTimer, %timerFn%, -1
}

OneKeyRunCompleteDirectionSwitch(key, expectedEpoch, expectedGeneration){
    global OneKeyRunGapDelay
    global _OneKeyRunPendingKeys
    global _OneKeyRunSuppressedKeys
    if (!_OneKeyRunPendingKeys.HasKey(key)
        || _OneKeyRunPendingKeys[key] != expectedGeneration
        || !OneKeyRunCanContinue(key, expectedEpoch, expectedGeneration)) {
        OneKeyRunClearPending(key, expectedGeneration)
        return
    }
    gapDelay := OneKeyRunGetDelay(OneKeyRunGapDelay)
    ; 定时器只负责短确认，最终 Up→Down 保持原子，避免在 gap 中丢轴。
    Critical, On
    if (!OneKeyRunCanContinue(key, expectedEpoch, expectedGeneration)) {
        Critical, Off
        OneKeyRunClearPending(key, expectedGeneration)
        return
    }
    SendInput, {Blind}{%key% Up}
    Sleep, %gapDelay%
    if (!OneKeyRunCanContinue(key, expectedEpoch, expectedGeneration)) {
        Critical, Off
        OneKeyRunClearPending(key, expectedGeneration)
        return
    }
    SendInput, {Blind}{%key% DownTemp}
    OneKeyRunMarkRunningSession(key)
    if (_OneKeyRunSuppressedKeys.HasKey(key)) {
        _OneKeyRunSuppressedKeys.Delete(key)
    }
    OneKeyRunClearPending(key, expectedGeneration)
    Critical, Off
}

OneKeyRunScheduleDirectionSwitch(key, expectedEpoch, expectedGeneration, delay){
    timerFn := Func("OneKeyRunCompleteDirectionSwitch")
        .Bind(key, expectedEpoch, expectedGeneration)
    period := -(delay < 1 ? 1 : delay)
    SetTimer, %timerFn%, %period%
}

; 将一次物理长按转换为“点按一次，再按住一次”。
OneKeyRunStart(key, logicalDownAlready := false){
    global _OneKeyRunActiveKeys
    global _OneKeyRunEnabled
    global OneKeyRunGapDelay
    global OneKeyRunGuardDelay
    global OneKeyRunPressDelay
    global _OneKeyRunRunningKeys
    global _OneKeyRunSuppressedKeys
    global _OneKeyRunLastRunReleaseTime
    global _OneKeyRunLastDirectionPressKey
    global _OneKeyRunLastDirectionPressTime
    global _OneKeyRunPendingKeys
    global _OneKeyRunPressTimes
    Critical, On
    if (!_OneKeyRunEnabled || _OneKeyRunActiveKeys.HasKey(key)) {
        Critical, Off
        return
    }
    now := A_TickCount
    _OneKeyRunActiveKeys[key] := true
    generation := OneKeyRunNextKeyGeneration(key)
    _OneKeyRunPressTimes[key] := now
    gapDelay := OneKeyRunGetDelay(OneKeyRunGapDelay)
    guardDelay := OneKeyRunGetGuardDelay(OneKeyRunGuardDelay)
    pressDelay := OneKeyRunGetDelay(OneKeyRunPressDelay)
    isDirectionSwitch := OneKeyRunIsDirectionSwitch(key, now)
    directionRelation := OneKeyRunGetHeldDirectionRelation(key)
    isOrthogonalSession := isDirectionSwitch
        && directionRelation == "orthogonal"
        && OneKeyRunHasRunningOrthogonalDirection(key)
    isOrthogonalChord := !isDirectionSwitch && directionRelation == "orthogonal"
    ; 正交方向仍同时按住时属于斜向移动，共用当前输入令牌，不取消首轴确认。
    expectedEpoch := (isOrthogonalChord || isOrthogonalSession)
        ? OneKeyRunGetInputEpoch()
        : OneKeyRunRegisterDirectionInput()
    sequenceElapsed := _OneKeyRunLastDirectionPressTime > 0
        ? now - _OneKeyRunLastDirectionPressTime
        : OneKeyRunGetCommandWindow() + 1
    isSequentialCommand := !isDirectionSwitch
        && _OneKeyRunLastDirectionPressKey != ""
        && _OneKeyRunLastDirectionPressKey != key
        && sequenceElapsed <= OneKeyRunGetCommandWindow()
    _OneKeyRunLastDirectionPressKey := key
    _OneKeyRunLastDirectionPressTime := now
    _OneKeyRunPendingKeys[key] := generation
    Critical, Off

    oppositeKey := OneKeyRunGetOppositeKey(key)
    ; 正交方向继续保持；只有相反方向会暂时压制，避免两键互相抵消。
    if (isDirectionSwitch && oppositeKey != "" && _OneKeyRunRunningKeys.HasKey(oppositeKey)) {
        SendInput, {Blind}{%oppositeKey% Up}
        _OneKeyRunRunningKeys.Delete(oppositeKey)
        _OneKeyRunSuppressedKeys[oppositeKey] := key
    }

    if (!logicalDownAlready) {
        SendInput, {Blind}{%key% DownTemp}
    }
    if (isOrthogonalSession) {
        ; 已确认奔跑中追加正交轴时直接继承会话，不等待、不再次双击。
        OneKeyRunMarkRunningSession(key)
        OneKeyRunClearPending(key, generation)
        return
    }
    if (isOrthogonalChord && isSequentialCommand) {
        ; 第二轴立即保持并返回，让首轴的 140ms 确认线程继续运行。
        ; 后续空格/技能输入仍会推进 epoch，从而取消首轴补发。
        OneKeyRunClearPending(key, generation)
        return
    }
    if (isDirectionSwitch) {
        ; 相反方向或 Grace 内重新起步使用异步短确认，热键线程立即返回。
        switchDelay := OneKeyRunGetSwitchDelay(guardDelay)
        OneKeyRunScheduleDirectionSwitch(key, expectedEpoch, generation, switchDelay)
        return
    } else {
        ; 第二方向先保留原始输入；若之后持续按住且没有技能输入，窗口结束后仍可升级奔跑。
        waitDelay := isSequentialCommand
            ? OneKeyRunGetSequentialWaitDelay(sequenceElapsed, guardDelay)
            : guardDelay
        if (!OneKeyRunWaitForRunIntent(key, waitDelay, expectedEpoch, generation)) {
            OneKeyRunClearPending(key, generation)
            return
        }
    }
    ; 从抬起到最终再次按下必须作为一个原子提交。
    ; 否则第二个方向恰好在 gap 内进入时，会让首方向停在 Up 状态，造成斜向移动丢轴。
    Critical, On
    if (!OneKeyRunCanContinue(key, expectedEpoch, generation)) {
        Critical, Off
        OneKeyRunClearPending(key, generation)
        return
    }
    SendInput, {Blind}{%key% Up}
    if (!OneKeyRunCanContinue(key, expectedEpoch, generation)) {
        Critical, Off
        OneKeyRunClearPending(key, generation)
        if (!GetKeyState(key, "P") && _OneKeyRunActiveKeys.HasKey(key)) {
            _OneKeyRunActiveKeys.Delete(key)
        }
        return
    }

    Sleep, %gapDelay%
    if (!OneKeyRunCanContinue(key, expectedEpoch, generation)) {
        Critical, Off
        OneKeyRunClearPending(key, generation)
        return
    }

    reuseInitialTap := isDirectionSwitch || OneKeyRunCanReuseInitialTap(waitDelay)
    if (!reuseInitialTap) {
        ; 较长等待已超出双击识别窗口，此时再发送一套完整双击。
        SendInput, {Blind}{%key% DownTemp}
        Sleep, %pressDelay%
        if (!OneKeyRunCanContinue(key, expectedEpoch, generation)) {
            SendInput, {Blind}{%key% Up}
            Critical, Off
            OneKeyRunClearPending(key, generation)
            return
        }
        SendInput, {Blind}{%key% Up}
        Sleep, %gapDelay%
        if (!OneKeyRunCanContinue(key, expectedEpoch, generation)) {
            Critical, Off
            OneKeyRunClearPending(key, generation)
            return
        }
    }

    SendInput, {Blind}{%key% DownTemp}
    OneKeyRunMarkRunningSession(key)
    OneKeyRunClearPending(key, generation)
    if (_OneKeyRunSuppressedKeys.HasKey(key)) {
        _OneKeyRunSuppressedKeys.Delete(key)
    }
    Critical, Off
}

OneKeyRunStop(key){
    global _OneKeyRunActiveKeys
    global _OneKeyRunEnabled
    global _OneKeyRunRunningKeys
    global _OneKeyRunSuppressedKeys
    global _OneKeyRunLastRunReleaseTime
    global _OneKeyRunLastRunReleaseWasStable
    global _OneKeyRunLastDirectionPressKey
    global _OneKeyRunLastDirectionPressTime
    global _OneKeyRunPendingKeys
    global _OneKeyRunPressTimes
    global OneKeyRunPressDelay
    global OneKeyRunGapDelay
    generation := OneKeyRunNextKeyGeneration(key)
    OneKeyRunClearPending(key, generation - 1)
    SendInput, {Blind}{%key% Up}
    if (_OneKeyRunRunningKeys.HasKey(key)) {
        _OneKeyRunLastRunReleaseWasStable := OneKeyRunIsStableRun(key)
        _OneKeyRunRunningKeys.Delete(key)
        _OneKeyRunLastRunReleaseTime := A_TickCount
    }
    ; 被其他方向压制的键如果已物理松开，不再尝试恢复。
    if (_OneKeyRunSuppressedKeys.HasKey(key)) {
        _OneKeyRunSuppressedKeys.Delete(key)
    }
    if (_OneKeyRunActiveKeys.HasKey(key)) {
        _OneKeyRunActiveKeys.Delete(key)
    }
    if (_OneKeyRunPressTimes.HasKey(key)) {
        _OneKeyRunPressTimes.Delete(key)
    }

    pressDelay := OneKeyRunGetDelay(OneKeyRunPressDelay)
    gapDelay := OneKeyRunGetDelay(OneKeyRunGapDelay)
    for suppressedKey, suppressorKey in _OneKeyRunSuppressedKeys {
        if (suppressorKey != key) {
            continue
        }
        _OneKeyRunSuppressedKeys.Delete(suppressedKey)
        if (!_OneKeyRunEnabled || !GetKeyState(suppressedKey, "P")) {
            continue
        }
        ; 相反的新方向松开后，恢复仍被物理按住的旧方向。
        SendInput, {Blind}{%suppressedKey% DownTemp}
        Sleep, %pressDelay%
        SendInput, {Blind}{%suppressedKey% Up}
        Sleep, %gapDelay%
        if (GetKeyState(suppressedKey, "P")) {
            SendInput, {Blind}{%suppressedKey% DownTemp}
            _OneKeyRunRunningKeys[suppressedKey] := true
        }
    }
    OneKeyRunQueueHeldDirectionPromotion()
}

SetOneKeyRunBlocking(key){
    global _OneKeyRunEnabled
    global _OneKeyRunRegisteredKeys
    keyName := OneKeyRunNormalizeKey(key)
    if (keyName == "" || IsValueInArray(keyName, _OneKeyRunRegisteredKeys)) {
        return
    }
    downHotkey := "$*" . keyName
    upHotkey := downHotkey . " Up"
    downFn := Func("OneKeyRunStart").Bind(keyName)
    upFn := Func("OneKeyRunStop").Bind(keyName)
    Hotkey, IfWinActive, ahk_group DNF
    try {
        Hotkey, %downHotkey%, %downFn%, On
        Hotkey, %upHotkey%, %upFn%, On
        _OneKeyRunRegisteredKeys.Push(keyName)
        _OneKeyRunEnabled := true
    } finally {
        Hotkey, IfWinActive
    }
}

ResetOneKeyRunState(){
    global _OneKeyRunActiveKeys
    global _OneKeyRunEnabled
    global _OneKeyRunRegisteredKeys
    global _OneKeyRunRunningKeys
    global _OneKeyRunSuppressedKeys
    global _OneKeyRunLastRunReleaseTime
    global _OneKeyRunLastRunReleaseWasStable
    global _OneKeyRunLastDirectionPressKey
    global _OneKeyRunLastDirectionPressTime
    global _OneKeyRunInputEpoch
    global _OneKeyRunKeyGenerations
    global _OneKeyRunPendingKeys
    global _OneKeyRunPressTimes
    global _OneKeyRunLastOtherInputKey
    global _OneKeyRunLastOtherInputTime
    global _OneKeyRunObservedDownKeys
    global _OneKeyRunRefreshGeneration
    _OneKeyRunEnabled := false
    OneKeyRunStopInputObserver()
    Hotkey, IfWinActive, ahk_group DNF
    for _, keyName in _OneKeyRunRegisteredKeys {
        downHotkey := "$*" . keyName
        upHotkey := downHotkey . " Up"
        SendInput, {Blind}{%keyName% Up}
        try {
            Hotkey, %downHotkey%, Off
        }
        try {
            Hotkey, %upHotkey%, Off
        }
    }
    Hotkey, IfWinActive
    _OneKeyRunActiveKeys := {}
    _OneKeyRunRegisteredKeys := []
    _OneKeyRunRunningKeys := {}
    _OneKeyRunSuppressedKeys := {}
    _OneKeyRunLastRunReleaseTime := 0
    _OneKeyRunLastRunReleaseWasStable := false
    _OneKeyRunLastDirectionPressKey := ""
    _OneKeyRunLastDirectionPressTime := 0
    _OneKeyRunInputEpoch := 0
    _OneKeyRunKeyGenerations := {}
    _OneKeyRunPendingKeys := {}
    _OneKeyRunPressTimes := {}
    _OneKeyRunObservedDownKeys := {}
    _OneKeyRunLastOtherInputKey := ""
    _OneKeyRunLastOtherInputTime := 0
    _OneKeyRunRefreshGeneration := 0
}

OneKeyRunRegisterToggleHotkey(){
    global OneKeyRunToggleHotKey
    global _OneKeyRunToggleRegisteredKey
    if (OneKeyRunToggleHotKey == "") {
        return
    }
    hotkeyName := "$*" . OneKeyRunToggleHotKey
    fn := Func("OneKeyRunToggle")
    Hotkey, IfWinActive, ahk_group DNF
    try {
        Hotkey, %hotkeyName%, %fn%, On
        _OneKeyRunToggleRegisteredKey := hotkeyName
    } finally {
        Hotkey, IfWinActive
    }
}

OneKeyRunUnregisterToggleHotkey(){
    global _OneKeyRunToggleRegisteredKey
    if (_OneKeyRunToggleRegisteredKey == "") {
        return
    }
    Hotkey, IfWinActive, ahk_group DNF
    try {
        Hotkey, %_OneKeyRunToggleRegisteredKey%, Off
    }
    Hotkey, IfWinActive
    _OneKeyRunToggleRegisteredKey := ""
}

OneKeyRunApplyRuntime(){
    global OneKeyRun
    global _OneKeyRunEnabled
    ResetOneKeyRunState()
    if (OneKeyRun) {
        for _, runKey in OneKeyRunGetCurrentKeys() {
            if (runKey != "") {
                SetOneKeyRunBlocking(runKey)
            }
        }
        if (_OneKeyRunEnabled) {
            OneKeyRunStartInputObserver()
        }
    }
    GuiControl Main:, OneKeyRun, %OneKeyRun%
}

OneKeyRunToggle(){
    global OneKeyRun
    OneKeyRun := !OneKeyRun
    SaveConfig("OneKeyRunState", OneKeyRun)
    OneKeyRunApplyRuntime()
    ShowTip(OneKeyRun ? "一键奔跑已开启" : "一键奔跑已关闭")
}

OneKeyRunStartForPreset(){
    OneKeyRunRegisterToggleHotkey()
    OneKeyRunApplyRuntime()
}

OneKeyRunShutdown(){
    ResetOneKeyRunState()
    OneKeyRunUnregisterToggleHotkey()
}

OneKeyRunGetInputEpoch(){
    global _OneKeyRunInputEpoch
    return _OneKeyRunInputEpoch
}

OneKeyRunGetRefreshCandidate(){
    global _OneKeyRunEnabled
    global _OneKeyRunRunningKeys
    global _OneKeyRunLastDirectionPressKey
    if (!_OneKeyRunEnabled || !WinActive("ahk_group DNF")) {
        return ""
    }
    heldRunningKeys := []
    for runningKey, _ in _OneKeyRunRunningKeys {
        if (GetKeyState(runningKey, "P")) {
            heldRunningKeys.Push(runningKey)
        }
    }
    if (heldRunningKeys.Length() == 0) {
        return ""
    }
    ; 已确认的斜向会话可选择最近方向重触发一次，另一轴继续保持。
    if (_OneKeyRunLastDirectionPressKey != ""
        && IsValueInArray(_OneKeyRunLastDirectionPressKey, heldRunningKeys)) {
        return _OneKeyRunLastDirectionPressKey
    }
    return heldRunningKeys[1]
}

OneKeyRunRefreshCanContinue(key, expectedEpoch, expectedGeneration, refreshGeneration){
    global _OneKeyRunEnabled
    global _OneKeyRunRunningKeys
    global _OneKeyRunRefreshGeneration
    return _OneKeyRunEnabled
        && WinActive("ahk_group DNF")
        && _OneKeyRunRefreshGeneration == refreshGeneration
        && _OneKeyRunRunningKeys.HasKey(key)
        && GetKeyState(key, "P")
        && OneKeyRunTokenMatches(key, expectedEpoch, expectedGeneration)
        && OneKeyRunGetRefreshCandidate() == key
}

OneKeyRunCancelRunningState(key){
    global _OneKeyRunRunningKeys
    if (_OneKeyRunRunningKeys.HasKey(key)) {
        _OneKeyRunRunningKeys.Delete(key)
    }
}

; 仅恢复主进程已经确认的奔跑方向，避免把搓招方向误判为奔跑。
OneKeyRunRefreshRunningDirection(key, expectedEpoch := ""){
    global OneKeyRunPressDelay
    global OneKeyRunGapDelay
    global _OneKeyRunKeyGenerations
    global _OneKeyRunRefreshGeneration
    if (key == "" || !_OneKeyRunKeyGenerations.HasKey(key)) {
        return
    }
    if (expectedEpoch == "") {
        expectedEpoch := OneKeyRunGetInputEpoch()
    }
    expectedGeneration := _OneKeyRunKeyGenerations[key]
    _OneKeyRunRefreshGeneration += 1
    refreshGeneration := _OneKeyRunRefreshGeneration
    if (!OneKeyRunRefreshCanContinue(key, expectedEpoch, expectedGeneration, refreshGeneration)) {
        return
    }
    pressDelay := OneKeyRunGetDelay(OneKeyRunPressDelay)
    gapDelay := OneKeyRunGetDelay(OneKeyRunGapDelay)

    SendInput, {Blind}{%key% Up}
    Sleep, %gapDelay%
    if (!OneKeyRunRefreshCanContinue(key, expectedEpoch, expectedGeneration, refreshGeneration)) {
        OneKeyRunCancelRunningState(key)
        return
    }
    SendInput, {Blind}{%key% DownTemp}
    Sleep, %pressDelay%
    if (!OneKeyRunRefreshCanContinue(key, expectedEpoch, expectedGeneration, refreshGeneration)) {
        SendInput, {Blind}{%key% Up}
        OneKeyRunCancelRunningState(key)
        return
    }
    SendInput, {Blind}{%key% Up}
    Sleep, %gapDelay%
    if (!OneKeyRunRefreshCanContinue(key, expectedEpoch, expectedGeneration, refreshGeneration)) {
        OneKeyRunCancelRunningState(key)
        return
    }
    SendInput, {Blind}{%key% DownTemp}
}
