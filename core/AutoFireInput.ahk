; 与捆绑 AHK v1.1.37.02 的 SendLevel 0 一致，供 InputHook I1 / hook hotkeys 排除自身输入。
; 来源：AutoHotkey/v1.1.37.02/source/keyboard_mouse.h，KEY_IGNORE_LEVEL(0)。
; 这是 AHK 内部标记，升级运行时需同时验证 tests/autofire_input_test.ahk。
AutoFireInputMarker(){
    return 0xFFC3D44D
}

; native SendInput 避免 AHK 在检测到其他 AHK hooks 后自动退回 SendEvent。
; 每个对象只负责一个键，Down/Up INPUT buffers 在初始化时构建，发送时不解析字符串。
class AutoFireInput {
    __New(key){
        this.Key := key
        this.ScanCode := GetKeySC(key)
        this.VirtualKey := GetKeyVK(key)
        if (!this.ScanCode && !this.VirtualKey) {
            throw Exception("Invalid autofire input key", -1, key)
        }
        this.Size := A_PtrSize == 8 ? 40 : 28
        this.KeyboardOffset := A_PtrSize == 8 ? 8 : 4
        this.ExtraInfoOffset := A_PtrSize == 8 ? 24 : 16
        this.LastError := 0
        this.LastSent := 0
        this.SetCapacity("Down", this.Size)
        this.SetCapacity("Up", this.Size)
        this.Build(this.GetAddress("Down"), false)
        this.Build(this.GetAddress("Up"), true)
    }

    Build(ptr, isUp){
        DllCall("RtlZeroMemory", "Ptr", ptr, "UPtr", this.Size)
        NumPut(1, ptr + 0, 0, "UInt") ; INPUT_KEYBOARD
        offset := this.KeyboardOffset
        ; Pause 使用特殊 E1 序列，不能把其 SC045 当普通扫描码（会变成 NumLock）。
        ; 没有扫描码的功能键同样交给 Windows 按 VK 映射。
        useVirtualKey := !this.ScanCode || this.VirtualKey == 0x13
        flags := isUp ? 0x2 : 0 ; KEYEVENTF_KEYUP
        if (useVirtualKey) {
            NumPut(this.VirtualKey, ptr + offset, 0, "UShort")
        } else {
            flags |= 0x8 ; KEYEVENTF_SCANCODE；此时 wVk 被 Windows 忽略。
            if (this.ScanCode & 0x100) {
                flags |= 0x1 ; KEYEVENTF_EXTENDEDKEY
            }
            NumPut(this.ScanCode & 0xFF, ptr + offset + 2, 0, "UShort")
        }
        NumPut(flags, ptr + offset + 4, 0, "UInt")
        NumPut(AutoFireInputMarker(), ptr + this.ExtraInfoOffset, 0, "UPtr")
    }

    Send(isDown){
        this.LastSent := DllCall("SendInput", "UInt", 1
            , "Ptr", this.GetAddress(isDown ? "Down" : "Up"), "Int", this.Size, "UInt")
        this.LastError := this.LastSent == 1 ? 0 : A_LastError
        return this.LastSent == 1
    }
}
