; Native DLL extraction/loading for this process only. No game-process injection.
; Ancestors deny deletion. Directory metadata is rechecked after file locking.
NativePayloadOpen(sourcePath := ""){
    payload := ""
    try {
        if (A_IsCompiled && sourcePath == "") {
            host := DllCall("GetModuleHandleW", "Ptr", 0, "Ptr")
            resource := DllCall("FindResourceW", "Ptr", host
                , "WStr", "BUILD\DNFAUTOFIRENATIVE.DLL", "Ptr", 10, "Ptr")
            if (!resource)
                throw Exception("Native engine resource is missing.",, A_LastError)
            size := DllCall("SizeofResource", "Ptr", host, "Ptr", resource, "UInt")
            loaded := DllCall("LoadResource", "Ptr", host, "Ptr", resource, "Ptr")
            bytes := DllCall("LockResource", "Ptr", loaded, "Ptr")
            if (!bytes || !size || size > 67108864)
                throw Exception("Native engine resource is invalid.")
            payload := NativePayloadCreateDirectory()
            NativePayloadWriteAndLock(payload, bytes, size)
            NativePayloadLoadVerified(payload, bytes, size)
        } else {
            if (sourcePath == "")
                sourcePath := A_ScriptDir . "\build\DNFAutoFireNative.dll"
            path := NativePayloadAbsolutePath(sourcePath)
            SplitPath, path, filename, parent
            payload := {path: path, directory: "", file: 0, module: 0
                , directoryHandle: 0, ownsFile: false, ownsDirectory: false}
            payload.directoryHandles := NativePayloadLockDirectories(parent)
            payload.directoryPaths := NativePayloadDirectoryPaths(payload.directoryHandles)
            payload.file := NativePayloadReadLock(path)
            payload.path := NativePayloadCanonicalPath(payload.file)
            if (payload.path != payload.directoryPaths[payload.directoryPaths.Length()] . "\" . filename)
                throw Exception("Native engine path changed while acquiring its locks.")
            NativePayloadCheckDirectoryChain(payload)
            payload.module := DllCall("LoadLibraryExW", "WStr", payload.path
                , "Ptr", 0, "UInt", 0x800, "Ptr") ; LOAD_LIBRARY_SEARCH_SYSTEM32
            if (!payload.module)
                throw Exception("Cannot load the development native engine.",, A_LastError)
        }
        return payload
    } catch error {
        NativePayloadAbandon(payload)
        throw error
    }
}

NativePayloadCreateDirectory(){
    base := NativePayloadAbsolutePath(A_Temp)
    payload := {path: "", directory: "", file: 0, module: 0
        , directoryHandle: 0, ownsFile: false, ownsDirectory: false}
    descriptor := 0
    try {
        payload.directoryHandles := NativePayloadLockDirectories(base)
        payload.directoryPaths := NativePayloadDirectoryPaths(payload.directoryHandles)
        base := NativePayloadCanonicalPath(payload.directoryHandles[payload.directoryHandles.Length()])
        descriptor := NativePayloadSecurity(attributes)
        Loop, 8 {
            VarSetCapacity(random, 16, 0)
            status := DllCall("bcrypt\BCryptGenRandom", "Ptr", 0, "Ptr", &random
                , "UInt", 16, "UInt", 2, "UInt") ; system preferred RNG
            if (status)
                throw Exception("Cannot create a random native engine directory.",, status)
            suffix := ""
            Loop, 16
                suffix .= Format("{:02X}", NumGet(random, A_Index - 1, "UChar"))
            directory := RTrim(base, "\") . "\DNFAutoFire-" . suffix
            if (DllCall("CreateDirectoryW", "WStr", directory, "Ptr", &attributes)) {
                payload.directory := directory
                payload.ownsDirectory := true
                break
            }
            if (A_LastError != 183)
                throw Exception("Cannot create the protected native engine directory.",, A_LastError)
        }
        if (!payload.ownsDirectory)
            throw Exception("Cannot allocate a unique native engine directory.")
        payload.directoryHandle := NativePayloadDirectoryLock(payload.directory)
        payload.directory := NativePayloadCanonicalPath(payload.directoryHandle)
        if (payload.directory != directory)
            throw Exception("Private native engine directory changed while acquiring its lock.")
        payload.path := payload.directory . "\engine.dll"
        NativePayloadCheckDirectoryChain(payload)
        return payload
    } catch error {
        NativePayloadAbandon(payload)
        throw error
    } finally {
        if (descriptor)
            DllCall("LocalFree", "Ptr", descriptor, "Ptr")
    }
}

NativePayloadWriteAndLock(payload, expectedBytes, expectedSize){
    if (!IsObject(payload) || !payload.ownsDirectory || !payload.directoryHandle
        || payload.file || payload.ownsFile || !expectedBytes || expectedSize < 1)
        throw Exception("Invalid native engine extraction request.")
    writer := 0
    descriptor := NativePayloadSecurity(attributes)
    try {
        NativePayloadCheckDirectoryChain(payload)
        writer := DllCall("CreateFileW", "WStr", payload.path, "UInt", 0xC0000000
            , "UInt", 1, "Ptr", &attributes, "UInt", 1, "UInt", 0x200080, "Ptr", 0, "Ptr")
        if (writer == -1)
            throw Exception("Cannot create the native engine file.",, A_LastError)
        payload.ownsFile := true
        NativePayloadCheckAttributes(writer, false)
        if (!DllCall("WriteFile", "Ptr", writer, "Ptr", expectedBytes
            , "UInt", expectedSize, "UIntP", written, "Ptr", 0) || written != expectedSize)
            throw Exception("Cannot write the complete native engine resource.",, A_LastError)
        if (!DllCall("FlushFileBuffers", "Ptr", writer))
            throw Exception("Cannot flush the native engine resource.",, A_LastError)
        DllCall("CloseHandle", "Ptr", writer)
        writer := 0
        ; The directory's protected DACL excludes lower-trust tokens. The read
        ; lock then excludes all writers/deleters, including the current user.
        payload.file := NativePayloadReadLock(payload.path)
        if (payload.path != NativePayloadCanonicalPath(payload.file))
            throw Exception("Native engine file path changed while acquiring its lock.")
        NativePayloadCheckDirectoryChain(payload)
        NativePayloadVerify(payload.file, expectedBytes, expectedSize)
    } finally {
        if (writer && writer != -1)
            DllCall("CloseHandle", "Ptr", writer)
        DllCall("LocalFree", "Ptr", descriptor, "Ptr")
    }
}

NativePayloadLoadVerified(payload, expectedBytes, expectedSize){
    if (!IsObject(payload) || !payload.file || payload.module)
        throw Exception("Invalid native engine load request.")
    NativePayloadCheckDirectoryChain(payload)
    if (payload.ownsDirectory && payload.path != payload.directory . "\engine.dll")
        throw Exception("Native engine file is outside its locked private directory.")
    if (payload.path != NativePayloadCanonicalPath(payload.file))
        throw Exception("Native engine file path changed before loading.")
    NativePayloadVerify(payload.file, expectedBytes, expectedSize)
    payload.module := DllCall("LoadLibraryExW", "WStr", payload.path
        , "Ptr", 0, "UInt", 0x800, "Ptr") ; dependency lookup restricted to System32
    if (!payload.module)
        throw Exception("Cannot load the verified native engine.",, A_LastError)
    return payload.module
}

NativePayloadVerify(handle, expectedBytes, expectedSize){
    if (!handle || handle == -1 || !expectedBytes || expectedSize < 1)
        throw Exception("Invalid native engine verification request.")
    NativePayloadCheckAttributes(handle, false)
    if (!DllCall("GetFileSizeEx", "Ptr", handle, "Int64P", actualSize)
        || actualSize != expectedSize)
        throw Exception("Native engine size does not match the embedded resource.")
    if (!DllCall("SetFilePointerEx", "Ptr", handle, "Int64", 0, "Ptr", 0, "UInt", 0))
        throw Exception("Cannot seek the native engine file.",, A_LastError)
    VarSetCapacity(buffer, 65536, 0)
    offset := 0
    while (offset < expectedSize) {
        count := Min(65536, expectedSize - offset)
        if (!DllCall("ReadFile", "Ptr", handle, "Ptr", &buffer, "UInt", count
            , "UIntP", received, "Ptr", 0) || received != count)
            throw Exception("Cannot read the complete native engine file.",, A_LastError)
        if (DllCall("ntdll\RtlCompareMemory", "Ptr", &buffer, "Ptr", expectedBytes + offset
            , "UPtr", count, "UPtr") != count)
            throw Exception("Native engine content does not match the embedded resource.")
        offset += count
    }
    return true
}

NativePayloadAbsolutePath(path){
    VarSetCapacity(buffer, 65536, 0)
    count := DllCall("GetFullPathNameW", "WStr", path, "UInt", 32768, "Ptr", &buffer, "Ptr", 0)
    if (!count || count >= 32768)
        throw Exception("Cannot resolve the native engine path.",, A_LastError)
    result := StrGet(&buffer, count, "UTF-16")
    if (!RegExMatch(result, "i)^[a-z]:\\") || InStr(SubStr(result, 3), ":")
        || DllCall("GetDriveTypeW", "WStr", SubStr(result, 1, 3), "UInt") != 3)
        throw Exception("The native engine requires an absolute path on a local fixed disk.")
    return StrLen(result) > 3 ? RTrim(result, "\") : result
}

NativePayloadLockDirectories(path){
    path := NativePayloadAbsolutePath(path)
    handles := []
    try {
        component := SubStr(path, 1, 3)
        handles.Push(NativePayloadDirectoryLock(component, true))
        previous := NativePayloadCanonicalPath(handles[1])
        for _, segment in StrSplit(SubStr(path, 4), "\") {
            if (segment == "")
                continue
            component := RTrim(component, "\") . "\" . segment
            handles.Push(NativePayloadDirectoryLock(component, true))
            current := NativePayloadCanonicalPath(handles[handles.Length()])
            if (current != previous . "\" . segment)
                throw Exception("Native engine ancestor changed while acquiring its locks.")
            previous := current
        }
        return handles
    } catch error {
        for _, handle in handles
            DllCall("CloseHandle", "Ptr", handle)
        throw error
    }
}

NativePayloadDirectoryLock(path, allowWrite := false){
    ; FILE_LIST_DIRECTORY establishes data-access sharing rules; attributes-only
    ; handles do not. Metadata writes still require explicit reparse rechecks.
    ; Shared ancestors allow ordinary sibling file operations. Each held child
    ; prevents its parent becoming empty; NTFS forbids reparsing nonempty dirs.
    handle := DllCall("CreateFileW", "WStr", path, "UInt", 0x81, "UInt", allowWrite ? 3 : 1, "Ptr", 0
        , "UInt", 3, "UInt", 0x2200000, "Ptr", 0, "Ptr")
    if (handle == -1)
        throw Exception("Cannot lock the native engine directory: " . path,, A_LastError)
    try NativePayloadCheckAttributes(handle, true)
    catch error {
        DllCall("CloseHandle", "Ptr", handle)
        throw error
    }
    return handle
}

NativePayloadDirectoryPaths(handles){
    paths := []
    for _, handle in handles
        paths.Push(NativePayloadCanonicalPath(handle))
    return paths
}

NativePayloadCheckDirectoryChain(payload){
    if (!IsObject(payload.directoryHandles) || !IsObject(payload.directoryPaths)
        || !payload.directoryHandles.Length() || payload.directoryHandles.Length() != payload.directoryPaths.Length())
        throw Exception("Native engine directory lock chain is incomplete.")
    for index, handle in payload.directoryHandles {
        NativePayloadCheckAttributes(handle, true)
        if (NativePayloadCanonicalPath(handle) != payload.directoryPaths[index])
            throw Exception("Native engine ancestor path changed before loading.")
    }
    if (payload.directoryHandle) {
        NativePayloadCheckAttributes(payload.directoryHandle, true)
        if (NativePayloadCanonicalPath(payload.directoryHandle) != payload.directory)
            throw Exception("Private native engine directory path changed before loading.")
    }
}

NativePayloadReadLock(path){
    handle := DllCall("CreateFileW", "WStr", path, "UInt", 0x80000000, "UInt", 1
        , "Ptr", 0, "UInt", 3, "UInt", 0x200080, "Ptr", 0, "Ptr")
    if (handle == -1)
        throw Exception("Cannot lock the native engine file.",, A_LastError)
    try NativePayloadCheckAttributes(handle, false)
    catch error {
        DllCall("CloseHandle", "Ptr", handle)
        throw error
    }
    return handle
}

NativePayloadCheckAttributes(handle, directory){
    VarSetCapacity(info, 52, 0)
    if (!DllCall("GetFileInformationByHandle", "Ptr", handle, "Ptr", &info))
        throw Exception("Cannot inspect the native engine path.",, A_LastError)
    attributes := NumGet(info, 0, "UInt")
    if ((attributes & 0x400) || (!!(attributes & 0x10) != !!directory))
        throw Exception("Reparse points or unexpected native engine path types are not allowed.")
}

NativePayloadCanonicalPath(handle){
    VarSetCapacity(buffer, 65536, 0)
    count := DllCall("GetFinalPathNameByHandleW", "Ptr", handle, "Ptr", &buffer
        , "UInt", 32768, "UInt", 1, "UInt") ; VOLUME_NAME_GUID avoids drive remapping
    if (!count || count >= 32768)
        throw Exception("Cannot resolve the locked native engine path.",, A_LastError)
    return RTrim(StrGet(&buffer, count, "UTF-16"), "\")
}

NativePayloadSecurity(ByRef attributes){
    if (A_IsAdmin) {
        sddl := "D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)S:(ML;OICI;NW;;;HI)"
    } else {
        token := 0
        sidText := 0
        try {
            if (!DllCall("advapi32\OpenProcessToken", "Ptr", DllCall("GetCurrentProcess", "Ptr")
                , "UInt", 8, "PtrP", token))
                throw Exception("Cannot inspect the client token.",, A_LastError)
            DllCall("advapi32\GetTokenInformation", "Ptr", token, "Int", 1
                , "Ptr", 0, "UInt", 0, "UIntP", length)
            if (!length)
                throw Exception("Cannot inspect the client SID.",, A_LastError)
            VarSetCapacity(user, length, 0)
            if (!DllCall("advapi32\GetTokenInformation", "Ptr", token, "Int", 1
                , "Ptr", &user, "UInt", length, "UIntP", length)
                || !DllCall("advapi32\ConvertSidToStringSidW", "Ptr", NumGet(user, 0, "Ptr"), "PtrP", sidText))
                throw Exception("Cannot resolve the client SID.",, A_LastError)
            sddl := "D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;" . StrGet(sidText, "UTF-16") . ")S:(ML;OICI;NW;;;ME)"
        } finally {
            if (sidText)
                DllCall("LocalFree", "Ptr", sidText, "Ptr")
            if (token)
                DllCall("CloseHandle", "Ptr", token)
        }
    }
    if (!DllCall("advapi32\ConvertStringSecurityDescriptorToSecurityDescriptorW", "WStr", sddl
        , "UInt", 1, "PtrP", descriptor, "Ptr", 0))
        throw Exception("Cannot create native engine security attributes.",, A_LastError)
    VarSetCapacity(attributes, A_PtrSize == 8 ? 24 : 12, 0)
    NumPut(A_PtrSize == 8 ? 24 : 12, attributes, 0, "UInt")
    NumPut(descriptor, attributes, A_PtrSize, "Ptr")
    return descriptor
}

NativePayloadClose(payload){
    if (!IsObject(payload))
        return
    if (payload.module) {
        if (!DllCall("FreeLibrary", "Ptr", payload.module))
            throw Exception("Cannot unload the native engine.",, A_LastError)
        payload.module := 0
    }
    if (payload.file) {
        DllCall("CloseHandle", "Ptr", payload.file)
        payload.file := 0
    }
    if (payload.ownsFile) {
        if (!DllCall("DeleteFileW", "WStr", payload.path) && A_LastError != 2)
            throw Exception("Cannot remove the private native engine file.",, A_LastError)
        payload.ownsFile := false
        payload.path := ""
    }
    if (payload.directoryHandle) {
        DllCall("CloseHandle", "Ptr", payload.directoryHandle)
        payload.directoryHandle := 0
    }
    if (payload.ownsDirectory) {
        if (!DllCall("RemoveDirectoryW", "WStr", payload.directory) && A_LastError != 2)
            throw Exception("Cannot remove the private native engine directory.",, A_LastError)
        payload.ownsDirectory := false
        payload.directory := ""
    }
    if (IsObject(payload.directoryHandles)) {
        for _, handle in payload.directoryHandles
            DllCall("CloseHandle", "Ptr", handle)
        payload.directoryHandles := []
    }
}

; Construction failures have no caller to retry cleanup. Release every acquired
; handle even when a scanner temporarily prevents removing our private file.
; Normal NativePayloadClose keeps its retryable ownership/path behavior.
NativePayloadAbandon(payload){
    if (!IsObject(payload))
        return
    try NativePayloadClose(payload)
    catch cleanupError {
        if (payload.module) {
            DllCall("FreeLibrary", "Ptr", payload.module)
            payload.module := 0
        }
        if (payload.file) {
            DllCall("CloseHandle", "Ptr", payload.file)
            payload.file := 0
        }
        if (payload.directoryHandle) {
            DllCall("CloseHandle", "Ptr", payload.directoryHandle)
            payload.directoryHandle := 0
        }
        if (IsObject(payload.directoryHandles)) {
            for _, handle in payload.directoryHandles
                DllCall("CloseHandle", "Ptr", handle)
            payload.directoryHandles := []
        }
    }
}
