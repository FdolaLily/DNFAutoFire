; 固定产品标识：不得加入文件路径、文件名、版本或 PID。
SingleInstanceName(){
    return "Global\DNFAutoFire.Client.{B797BFB2-305A-44DC-9E06-76D6CC424419}"
}

; 提权前只读探测；最终竞争由 Acquire 原子裁决。
SingleInstanceAlreadyRunning(name := ""){
    if (name == "")
        name := SingleInstanceName()
    handle := DllCall("OpenMutexW", "UInt", 0x100000, "Int", false, "WStr", name, "Ptr")
    error := A_LastError
    if (handle) {
        DllCall("CloseHandle", "Ptr", handle)
        return true
    }
    if (error == 5)
        return true
    if (error != 2)
        throw Exception("无法检查客户端唯一性，Windows 错误码：" . error)
    return false
}

; 保留对象而不等待/抢占旧实例。句柄直到进程退出才由 Windows 回收。
SingleInstanceAcquire(name := ""){
    if (name == "")
        name := SingleInstanceName()
    handle := DllCall("CreateMutexExW", "Ptr", 0, "WStr", name
        , "UInt", 0, "UInt", 0x100000, "Ptr")
    error := A_LastError
    if (!handle) {
        if (error == 5)
            return 0
        throw Exception("无法建立客户端唯一性，Windows 错误码：" . error)
    }
    if (error == 183) {
        DllCall("CloseHandle", "Ptr", handle)
        return 0
    }
    return handle
}

SingleInstanceCheckBeforeElevation(){
    try {
        if (SingleInstanceAlreadyRunning())
            ExitApp
    } catch error {
        MsgBox, 16, 连发未启动, % error.Message
        ExitApp, 1
    }
}

SingleInstanceAcquireOrExit(){
    try {
        handle := SingleInstanceAcquire()
        if (!handle)
            ExitApp
        return handle
    } catch error {
        MsgBox, 16, 连发未启动, % error.Message
        ExitApp, 1
    }
}
