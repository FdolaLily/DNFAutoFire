; 原生 DLL 只加载到本工具进程。游戏侧使用标准 Win32 SendInput。
AutoFireNativeStart(rules, timing){
    global _AutoFireNativeHandle, _AutoFireNativeModule, _AutoFireNativeCookie
    global _AutoFireNativeRefresh, _AutoFireNativeKeys
    if (!rules.Length())
        return
    priorCritical := A_IsCritical
    Critical, On
    try {
    if (A_PtrSize != 8)
        throw Exception("原生连发需要 64 位版本。")
    AutoFireNativeLoad()
    _AutoFireNativeCookie := (_AutoFireNativeCookie + 1) & 0x7FFFFFFF
    _AutoFireNativeRefresh := {}
    _AutoFireNativeKeys := rules.Clone()
    stride := 132 * 4
    VarSetCapacity(descriptors, rules.Length() * stride, 0)
    for index, rule in rules {
        origin := GetOriginKeyName(rule.key)
        descriptor := GetKeySC(origin) | (GetKeyVK(origin) << 16)
        offset := (index - 1) * stride
        NumPut(descriptor, descriptors, offset, "UInt")
        NumPut(rule.manual, descriptors, offset + 4, "UInt")
        NumPut(rule.delayMs * 1000, descriptors, offset + 8, "UInt")
        NumPut(rule.triggers.Length(), descriptors, offset + 12, "UInt")
        for triggerIndex, trigger in rule.triggers {
            origin := GetOriginKeyName(trigger)
            NumPut(GetKeySC(origin) | (GetKeyVK(origin) << 16), descriptors
                , offset + 16 + (triggerIndex - 1) * 4, "UInt")
        }
    }
    OnMessage(0x8031, "AutoFireNativeOnPhysical")
    ; 默认无忙等待，避免抢占游戏 CPU。实测后可仅在开发基准中比较短尾自旋。
    _AutoFireNativeHandle := DllCall(AutoFireNativeProc("AF_Start"), "Ptr", &descriptors
        , "UInt", rules.Length(), "UInt", timing.downMs * 1000, "UInt", timing.upMs * 1000
        , "UInt", 0, "Ptr", A_ScriptHwnd, "UPtr", _AutoFireNativeCookie, "Cdecl Ptr")
    if (!_AutoFireNativeHandle)
        throw Exception("原生连发启动失败，Windows 错误码：" . A_LastError)
    SetTimer, AutoFireNativeCheck, 500
    } finally {
        Critical, %priorCritical%
    }
}

AutoFireNativeLoad(){
    global _AutoFireNativeModule, _AutoFireNativePath, _AutoFireNativePayload
    if (_AutoFireNativeModule)
        return
    ; Retry a previous cleanup failure before creating another payload. Never
    ; retain a pointer to a module already unloaded by partial cleanup.
    if (IsObject(_AutoFireNativePayload)) {
        NativePayloadClose(_AutoFireNativePayload)
        _AutoFireNativePayload := ""
        _AutoFireNativePath := ""
    }
    ; Only a packaging declaration. The runtime reads PE resource bytes directly,
    ; writes a protected private file, verifies it, and holds its read lock.
    if (false) {
        FileInstall, build\DNFAutoFireNative.dll, unused-native-payload.dll, 0
    }
    _AutoFireNativePayload := NativePayloadOpen()
    _AutoFireNativePath := _AutoFireNativePayload.path
    _AutoFireNativeModule := _AutoFireNativePayload.module
}

AutoFireNativeProc(name){
    global _AutoFireNativeModule
    return DllCall("GetProcAddress", "Ptr", _AutoFireNativeModule, "AStr", name, "Ptr")
}

AutoFireNativeStop(){
    global _AutoFireNativeHandle, _AutoFireNativeCookie, _AutoFireNativeRefresh
    priorCritical := A_IsCritical
    Critical, On
    try {
    SetTimer, AutoFireNativeCheck, Off
    _AutoFireNativeCookie++ ; 先让所有已排队的旧方案消息失效。
    _AutoFireNativeRefresh := {}
    if (_AutoFireNativeHandle) {
        result := DllCall(AutoFireNativeProc("AF_Stop"), "Ptr", _AutoFireNativeHandle, "Cdecl Int")
        if (!result)
            throw Exception("原生连发线程尚未退出，不能卸载引擎。")
        _AutoFireNativeHandle := 0
        if (result < 0) {
            ReleaseManagedKeys()
            MsgBox, 16, 连发输入异常, Windows 拒绝了按键抬起。已停止引擎并尝试补释放，请松开实体键。
        }
    }
    } finally {
        Critical, %priorCritical%
    }
}

AutoFireNativeShutdown(){
    global _AutoFireNativeModule, _AutoFireNativePath, _AutoFireNativePayload
    AutoFireNativeStop()
    try {
        NativePayloadClose(_AutoFireNativePayload)
        _AutoFireNativePayload := ""
    } finally {
        _AutoFireNativeModule := IsObject(_AutoFireNativePayload) ? _AutoFireNativePayload.module : 0
        _AutoFireNativePath := IsObject(_AutoFireNativePayload) ? _AutoFireNativePayload.path : ""
    }
}

; -1 = 未托管；0/1 = 原生 hook 捕获的实体键状态，不受 SendInput 影响。
AutoFireNativePhysical(key){
    global _AutoFireNativeHandle
    if (!_AutoFireNativeHandle)
        return -1
    return DllCall(AutoFireNativeProc("AF_Physical"), "Ptr", _AutoFireNativeHandle
        , "UInt", GetKeySC(key), "UInt", GetKeyVK(key), "Cdecl Int")
}

AutoFireNativeLease(){
    global _AutoFireNativeHandle, _AutoFireNativeCookie
    return {handle: _AutoFireNativeHandle, cookie: _AutoFireNativeCookie}
}

AutoFireNativeLeaseValid(lease){
    global _AutoFireNativeHandle, _AutoFireNativeCookie
    return lease.handle == _AutoFireNativeHandle && lease.cookie == _AutoFireNativeCookie
}

AutoFireNativePauseKey(key, pause, lease := ""){
    global _AutoFireNativeHandle
    if (IsObject(lease) && !AutoFireNativeLeaseValid(lease))
        return false
    if (!_AutoFireNativeHandle)
        return true
    return DllCall(AutoFireNativeProc("AF_PauseKey"), "Ptr", _AutoFireNativeHandle
        , "UInt", GetKeySC(key), "UInt", GetKeyVK(key), "Int", pause, "Cdecl Int")
}

AutoFireNativeOnPhysical(packed, cookie){
    global _AutoFireNativeHandle, _AutoFireNativeCookie, _AutoFireNativeRefresh
    priorCritical := A_IsCritical
    Critical, On
    try {
    if (!_AutoFireNativeHandle || cookie != _AutoFireNativeCookie)
        return
    sc := packed & 0xFFFF
    vk := (packed >> 16) & 0xFF
    key := OneKeyRunGetObservedKey(vk, sc)
    id := OneKeyRunPhysicalInputId(key)
    if (packed & 0x01000000) {
        refreshKey := OneKeyRunGetRefreshCandidate()
        epoch := OneKeyRunNotifyBlockingInput(key)
        OneKeyRunObserveKeyDown("", vk, sc)
        _AutoFireNativeRefresh[id] := {key: refreshKey, epoch: epoch}
    } else {
        OneKeyRunObserveKeyUp("", vk, sc)
        if (_AutoFireNativeRefresh.HasKey(id)) {
            ticket := _AutoFireNativeRefresh[id]
            _AutoFireNativeRefresh.Delete(id)
            if (ticket.key != "") {
                ; OnMessage 默认单回调，不能在这里等待奔跑确认而漏掉后续实体键。
                refreshFn := Func("AutoFireNativeRefreshDirection").Bind(ticket.key, ticket.epoch, cookie)
                SetTimer, %refreshFn%, -1
            }
        }
    }
    } finally {
        Critical, %priorCritical%
    }
}

AutoFireNativeRefreshDirection(key, epoch, cookie){
    global _AutoFireNativeCookie, _AutoFireNativeHandle
    if (_AutoFireNativeHandle && cookie == _AutoFireNativeCookie)
        OneKeyRunRefreshRunningDirection(key, epoch)
}

AutoFireNativeCheck(){
    global _AutoFireNativeHandle
    if (!_AutoFireNativeHandle)
        return
    error := DllCall(AutoFireNativeProc("AF_Error"), "Ptr", _AutoFireNativeHandle, "Cdecl UInt")
    if (error) {
        StopAutoFire()
        MsgBox, 16, 连发已停止, 原生输入或计时失败，Windows 错误码：%error%
    }
}

AutoFireNativeSelfTest(){
    global _AutoFireNativeHandle
    code := 0
    try {
        ; 无手动入口、无触发键，调度线程永远不会发送输入。
        rules := [{key: "F24", manual: false, delayMs: 0, triggers: []}]
        AutoFireNativeStart(rules, {downMs: 10, upMs: 10})
        Sleep, 30
        VarSetCapacity(stats, 32, 0)
        ok := DllCall(AutoFireNativeProc("AF_Stats"), "Ptr", _AutoFireNativeHandle, "Ptr", &stats, "Cdecl Int")
        error := DllCall(AutoFireNativeProc("AF_Error"), "Ptr", _AutoFireNativeHandle, "Cdecl UInt")
        if (!ok || error || NumGet(stats, 0, "UInt64") != 0 || NumGet(stats, 24, "UInt64") != 1)
            throw Exception("Packaged native engine self-test failed: " . error)
        AutoFireNativeShutdown()
        FileAppend, PASS: single EXE extracted/loaded/stopped its native engine; zero input events.`n, *
    } catch error {
        FileAppend, % "FAIL: " . error.Message . " | " . error.What
            . " | line " . error.Line . " | " . error.Extra . "`n", *
        code := 1
    }
    ExitApp, %code%
}
