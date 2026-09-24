#NoEnv
#NoTrayIcon
#SingleInstance, Off
SetBatchLines, -1
ListLines, Off
#Include %A_ScriptDir%\..\lib\NativePayload.ahk

; These tests never call AF_Start or install an input hook. The only executable
; fixture is the project's native DLL; changed bytes are always rejected before
; LoadLibraryExW. All filesystem fixtures belong to this invocation.
global assertions := 0, payloads := [], extraHandles := []
global fixtureRoot := "", junctionPath := "", metadataPath := ""
OnExit("Cleanup")
try {
    fixturePath := A_ScriptDir . "\..\build\DNFAutoFireNative.dll"
    input := FileOpen(fixturePath, "r")
    Assert(IsObject(input), "native test fixture is readable")
    byteCount := input.Length
    Assert(byteCount > 1024, "native test fixture is not empty")
    VarSetCapacity(bytes, byteCount + 1, 0)
    Assert(input.RawRead(bytes, byteCount) == byteCount, "entire fixture was read")
    input.Close()

    payload := NewPayload()
    NativePayloadWriteAndLock(payload, &bytes, byteCount)
    Assert(payload.file != 0, "private payload retains its read handle")
    NativePayloadVerify(payload.file, &bytes, byteCount)
    assertions++
    originalByte := NumGet(bytes, 0, "UChar")
    NumPut(originalByte ^ 1, bytes, 0, "UChar")
    ExpectReject(Func("NativePayloadVerify").Bind(payload.file, &bytes, byteCount), "one changed byte rejected")
    ExpectReject(Func("NativePayloadLoadVerified").Bind(payload, &bytes, byteCount), "changed payload rejected before loading")
    Assert(!payload.module, "failed verification did not map the DLL")
    NumPut(originalByte, bytes, 0, "UChar")
    ExpectReject(Func("NativePayloadVerify").Bind(payload.file, &bytes, byteCount - 1), "truncated expected payload rejected")
    ExpectReject(Func("NativePayloadVerify").Bind(payload.file, &bytes, byteCount + 1), "extra expected byte rejected")
    ExpectReject(Func("NativePayloadVerify").Bind(payload.file, 0, byteCount), "null expected bytes rejected")
    ExpectReject(Func("NativePayloadVerify").Bind(payload.file, &bytes, 0), "empty expected payload rejected")

    secondWriter := DllCall("CreateFileW", "WStr", payload.path, "UInt", 0x40000000
        , "UInt", 7, "Ptr", 0, "UInt", 3, "UInt", 0x80, "Ptr", 0, "Ptr")
    Assert(secondWriter == -1, "held file forbids a second writer")
    Assert(!DllCall("DeleteFileW", "WStr", payload.path), "held file forbids deletion")
    Assert(!DllCall("MoveFileW", "WStr", payload.path, "WStr", payload.path . ".moved"), "held file forbids rename")
    Assert(!DllCall("MoveFileW", "WStr", payload.directory, "WStr", payload.directory . ".moved"), "held directory forbids rename")
    directoryWriter := DllCall("CreateFileW", "WStr", payload.directory, "UInt", 0x40000000
        , "UInt", 7, "Ptr", 0, "UInt", 3, "UInt", 0x02200000, "Ptr", 0, "Ptr")
    if (directoryWriter != -1)
        DllCall("CloseHandle", "Ptr", directoryWriter)
    Assert(directoryWriter == -1, "held private directory forbids a new GENERIC_WRITE handle")

    originalDirectory := payload.directoryPaths[1]
    payload.directoryPaths[1] := originalDirectory . "-changed"
    ExpectReject(Func("NativePayloadLoadVerified").Bind(payload, &bytes, byteCount), "changed expected ancestor path rejected before load")
    Assert(!payload.module, "changed ancestor expectation did not map the DLL")
    payload.directoryPaths[1] := originalDirectory
    NativePayloadLoadVerified(payload, &bytes, byteCount)
    Assert(payload.module != 0, "verified DLL loads")
    Assert(DllCall("GetProcAddress", "Ptr", payload.module, "AStr", "AF_Start", "Ptr") != 0, "verified DLL exposes the engine API")
    ; A reader that withholds FILE_SHARE_DELETE forces cleanup to fail. Cleanup
    ; must preserve the exact path so a later attempt can finish after releasing it.
    blocker := DllCall("CreateFileW", "WStr", payload.path, "UInt", 0x80000000
        , "UInt", 1, "Ptr", 0, "UInt", 3, "UInt", 0x80, "Ptr", 0, "Ptr")
    Assert(blocker != -1, "cleanup blocker opens the locked DLL for reading")
    extraHandles.Push(blocker)
    savedPath := payload.path, savedDirectory := payload.directory
    ExpectReject(Func("NativePayloadClose").Bind(payload), "blocked cleanup reports a retryable failure")
    Assert(!payload.module, "blocked file cleanup still unloads the module")
    Assert(payload.path == savedPath && FileExist(savedPath), "blocked cleanup retains the exact file path")
    CloseExtra(blocker)
    NativePayloadClose(payload)
    Assert(!FileExist(savedPath) && !FileExist(savedDirectory), "cleanup retry removes file and private directory")
    NativePayloadClose(payload)
    Assert(!payload.module && !payload.file && !payload.directoryHandle, "repeated close is harmless")

    ; Warm up Windows DLL loading before comparing process handle counts.
    payload := NativePayloadOpen(fixturePath)
    payloads.Push(payload)
    Assert(payload.module != 0, "development path loads without copying")
    NativePayloadClose(payload)
    Assert(FileExist(fixturePath), "development fixture survives close")
    beforeHandles := HandleCount()
    Loop, 25 {
        payload := NewPayload()
        NativePayloadWriteAndLock(payload, &bytes, byteCount)
        NativePayloadLoadVerified(payload, &bytes, byteCount)
        savedPath := payload.path, savedDirectory := payload.directory
        NativePayloadClose(payload)
        Assert(!FileExist(savedPath) && !FileExist(savedDirectory), "repeated lifecycle cleans private paths")
    }
    afterHandles := HandleCount()
    Assert(afterHandles <= beforeHandles + 2, "first 25 cycles have bounded system initialization (" . beforeHandles . " -> " . afterHandles . ")")
    Loop, 25 {
        payload := NewPayload()
        NativePayloadWriteAndLock(payload, &bytes, byteCount)
        NativePayloadLoadVerified(payload, &bytes, byteCount)
        savedPath := payload.path, savedDirectory := payload.directory
        NativePayloadClose(payload)
        Assert(!FileExist(savedPath) && !FileExist(savedDirectory), "second lifecycle batch cleans private paths")
    }
    finalHandles := HandleCount()
    Assert(finalHandles <= afterHandles, "second 25 cycles do not grow handles (" . afterHandles . " -> " . finalHandles . ")")

    TestReparsePaths(fixturePath)
    TestPrivateDirectoryReplacement(&bytes, byteCount)
    FileAppend, PASS: %assertions% native payload assertions; exact-byte verification; replacement locks; retryable cleanup; reparse rejection; 50 load/unload cycles; handles %beforeHandles%->%afterHandles%->%finalHandles%; no input hooks or keyboard input.`n, *
    ExitApp, 0
} catch error {
    FileAppend, % "FAIL: " . error.Message . " | " . error.What . " | line " . error.Line . " | " . error.Extra . "`n", *
    ExitApp, 1
}

NewPayload() {
    global payloads
    payload := NativePayloadCreateDirectory()
    payloads.Push(payload)
    return payload
}

Assert(condition, message) {
    global assertions
    assertions++
    if (!condition)
        throw Exception(message . " (Win32 error " . A_LastError . ")")
}

ExpectReject(action, message) {
    rejected := false
    try result := action.Call()
    catch error
        rejected := true
    ; If an unexpectedly accepted Open returns a payload, release it before
    ; reporting the failure so this regression never leaves a DLL mapped.
    if (!rejected && IsObject(result)) {
        try NativePayloadClose(result)
    }
    Assert(rejected, message)
}

CloseExtra(handle) {
    global extraHandles
    DllCall("CloseHandle", "Ptr", handle)
    for index, current in extraHandles {
        if (current == handle) {
            extraHandles.RemoveAt(index)
            return
        }
    }
}

HandleCount() {
    Assert(DllCall("GetProcessHandleCount", "Ptr", DllCall("GetCurrentProcess", "Ptr"), "UInt*", count), "handle count is readable")
    return count
}

TestReparsePaths(sourcePath) {
    global fixtureRoot, junctionPath
    fixtureRoot := A_ScriptDir . "\..\build\test-results\native-payload-" . DllCall("GetCurrentProcessId") . "-" . A_TickCount
    fixtureRoot := FullPath(fixtureRoot)
    buildRoot := FullPath(A_ScriptDir . "\..\build")
    Assert(SubStr(fixtureRoot, 1, StrLen(buildRoot) + 1) == buildRoot . "\", "filesystem fixtures remain under build")
    FileCreateDir, % fixtureRoot . "\target\nested"
    Assert(!ErrorLevel, "reparse target directories created")
    FileCopy, %sourcePath%, % fixtureRoot . "\target\fixture.dll"
    Assert(!ErrorLevel, "fixture copied into controlled reparse target")
    FileCopy, %sourcePath%, % fixtureRoot . "\target\nested\fixture.dll"
    Assert(!ErrorLevel, "fixture copied into nested reparse target")
    junctionPath := fixtureRoot . "\junction"
    CreateJunction(junctionPath, fixtureRoot . "\target")
    beforeHandles := HandleCount()
    Loop, 10 {
        ExpectReject(Func("NativePayloadOpen").Bind(junctionPath . "\fixture.dll"), "direct reparse parent rejected")
        ExpectReject(Func("NativePayloadOpen").Bind(junctionPath . "\nested\fixture.dll"), "reparse ancestor rejected")
    }
    afterHandles := HandleCount()
    Assert(afterHandles <= beforeHandles, "20 rejected paths close partially acquired handles (" . beforeHandles . " -> " . afterHandles . ")")
    TestDirectoryMetadata()
}

TestDirectoryMetadata() {
    global fixtureRoot, metadataPath, extraHandles
    metadataPath := fixtureRoot . "\metadata-lock"
    Assert(DllCall("CreateDirectoryW", "WStr", metadataPath, "Ptr", 0), "independent metadata test directory created")
    lock := NativePayloadDirectoryLock(metadataPath)
    extraHandles.Push(lock)
    writer := DllCall("CreateFileW", "WStr", metadataPath, "UInt", 0x40000000
        , "UInt", 7, "Ptr", 0, "UInt", 3, "UInt", 0x02200000, "Ptr", 0, "Ptr")
    writerError := A_LastError
    if (writer != -1)
        DllCall("CloseHandle", "Ptr", writer)
    Assert(writer == -1 && writerError == 32, "locked independent directory denies GENERIC_WRITE with sharing violation")
    ; Attribute-only access is distinct from GENERIC_WRITE. Windows permits an
    ; empty directory's reparse metadata to change through this access, despite
    ; sharing locks. The production path also needs its locked child objects.
    attributes := DllCall("CreateFileW", "WStr", metadataPath, "UInt", 0x100
        , "UInt", 7, "Ptr", 0, "UInt", 3, "UInt", 0x02200000, "Ptr", 0, "Ptr")
    Assert(attributes != -1, "attribute-only directory handle opens independently of data-write access")
    extraHandles.Push(attributes)
    size := JunctionBuffer(data, fixtureRoot . "\target")
    changed := DllCall("DeviceIoControl", "Ptr", attributes, "UInt", 0x900A4
        , "Ptr", &data, "UInt", size, "Ptr", 0, "UInt", 0, "UInt*", returned, "Ptr", 0)
    reparseError := A_LastError
    Assert(changed, "empty-directory probe confirms attribute-only reparse access (error " . reparseError . ")")
    Assert(DllCall("GetFileAttributesW", "WStr", metadataPath, "UInt") & 0x400, "empty-directory probe really became a reparse point")
    CloseExtra(attributes)
    CloseExtra(lock)
    Assert(DllCall("RemoveDirectoryW", "WStr", metadataPath), "empty-directory probe junction is unlinked")
    Assert(DllCall("CreateDirectoryW", "WStr", metadataPath, "Ptr", 0), "ordinary populated-directory fixture created")
    lock := NativePayloadDirectoryLock(metadataPath)
    extraHandles.Push(lock)
    childPath := metadataPath . "\held-child"
    Assert(DllCall("CreateDirectoryW", "WStr", childPath, "Ptr", 0), "locked path next component created")
    childLock := NativePayloadDirectoryLock(childPath)
    extraHandles.Push(childLock)
    Assert(!DllCall("RemoveDirectoryW", "WStr", childPath), "next component cannot be removed while locked")
    Assert(!DllCall("MoveFileW", "WStr", childPath, "WStr", childPath . ".moved"), "next component cannot be renamed while locked")
    attributes := DllCall("CreateFileW", "WStr", metadataPath, "UInt", 0x100
        , "UInt", 7, "Ptr", 0, "UInt", 3, "UInt", 0x02200000, "Ptr", 0, "Ptr")
    Assert(attributes != -1, "populated directory attribute-only handle opens")
    extraHandles.Push(attributes)
    changed := DllCall("DeviceIoControl", "Ptr", attributes, "UInt", 0x900A4
        , "Ptr", &data, "UInt", size, "Ptr", 0, "UInt", 0, "UInt*", returned, "Ptr", 0)
    reparseError := A_LastError
    if (changed) {
        ; Restore only this fixture's own reparse metadata before reporting an
        ; unexpected success, so cleanup never traverses its redirected target.
        VarSetCapacity(clearData, 8, 0)
        NumPut(0xA0000003, clearData, 0, "UInt")
        DllCall("DeviceIoControl", "Ptr", attributes, "UInt", 0x900AC, "Ptr", &clearData
            , "UInt", 8, "Ptr", 0, "UInt", 0, "UInt*", returned, "Ptr", 0)
    }
    Assert(!changed && reparseError == 145, "locked child keeps ancestor nonempty and denies reparse replacement (result " . changed . ", error " . reparseError . ")")
    Assert(!(DllCall("GetFileAttributesW", "WStr", metadataPath, "UInt") & 0x400), "populated directory remained ordinary")
    CloseExtra(attributes)
    CloseExtra(childLock)
    CloseExtra(lock)
    Assert(DllCall("RemoveDirectoryW", "WStr", childPath), "next-component fixture is removed after unlock")
    Assert(DllCall("RemoveDirectoryW", "WStr", metadataPath), "independent metadata fixture is removed")
    metadataPath := ""
}

TestPrivateDirectoryReplacement(expectedBytes, expectedSize) {
    global fixtureRoot, extraHandles
    payload := NewPayload()
    savedDirectory := payload.directory
    attributes := DllCall("CreateFileW", "WStr", payload.directory, "UInt", 0x100
        , "UInt", 7, "Ptr", 0, "UInt", 3, "UInt", 0x02200000, "Ptr", 0, "Ptr")
    Assert(attributes != -1, "same-token fixture can inspect the private empty-directory race")
    extraHandles.Push(attributes)
    size := JunctionBuffer(data, fixtureRoot . "\target")
    changed := DllCall("DeviceIoControl", "Ptr", attributes, "UInt", 0x900A4
        , "Ptr", &data, "UInt", size, "Ptr", 0, "UInt", 0, "UInt*", returned, "Ptr", 0)
    Assert(changed, "private empty-directory replacement fixture established")
    ExpectReject(Func("NativePayloadWriteAndLock").Bind(payload, expectedBytes, expectedSize), "private-directory replacement rejected before writing any bytes")
    Assert(!payload.module && !payload.file && !payload.ownsFile, "replaced directory never produced a file or mapped module")
    Assert(!FileExist(fixtureRoot . "\target\engine.dll"), "rejected replacement did not write into redirected target")
    VarSetCapacity(clearData, 8, 0)
    NumPut(0xA0000003, clearData, 0, "UInt")
    restored := DllCall("DeviceIoControl", "Ptr", attributes, "UInt", 0x900AC
        , "Ptr", &clearData, "UInt", 8, "Ptr", 0, "UInt", 0, "UInt*", returned, "Ptr", 0)
    Assert(restored, "private replacement fixture returned to an ordinary directory")
    CloseExtra(attributes)
    NativePayloadClose(payload)
    Assert(!FileExist(savedDirectory), "rejected private payload cleans its original directory")
}

FullPath(path) {
    VarSetCapacity(buffer, 65536, 0)
    length := DllCall("GetFullPathNameW", "WStr", path, "UInt", 32768, "Ptr", &buffer, "Ptr", 0, "UInt")
    Assert(length > 0 && length < 32768, "fixture path resolves")
    return StrGet(&buffer, length, "UTF-16")
}

CreateJunction(path, target) {
    Assert(DllCall("CreateDirectoryW", "WStr", path, "Ptr", 0), "junction directory created")
    handle := DllCall("CreateFileW", "WStr", path, "UInt", 0x40000000, "UInt", 3
        , "Ptr", 0, "UInt", 3, "UInt", 0x02200000, "Ptr", 0, "Ptr")
    Assert(handle != -1, "junction directory opens for reparse setup")
    total := JunctionBuffer(data, target)
    result := DllCall("DeviceIoControl", "Ptr", handle, "UInt", 0x900A4, "Ptr", &data
        , "UInt", total, "Ptr", 0, "UInt", 0, "UInt*", returned, "Ptr", 0)
    savedError := A_LastError
    DllCall("CloseHandle", "Ptr", handle)
    Assert(result, "junction established (Win32 error " . savedError . ")")
}

JunctionBuffer(ByRef data, target) {
    substitute := "\??\" . target
    substituteBytes := StrLen(substitute) * 2, printBytes := StrLen(target) * 2
    total := 16 + substituteBytes + 2 + printBytes + 2
    VarSetCapacity(data, total, 0)
    NumPut(0xA0000003, data, 0, "UInt")
    NumPut(total - 8, data, 4, "UShort")
    NumPut(substituteBytes, data, 10, "UShort")
    NumPut(substituteBytes + 2, data, 12, "UShort")
    NumPut(printBytes, data, 14, "UShort")
    StrPut(substitute, &data + 16, StrLen(substitute) + 1, "UTF-16")
    StrPut(target, &data + 18 + substituteBytes, StrLen(target) + 1, "UTF-16")
    return total
}

Cleanup(reason, code) {
    global payloads, extraHandles, fixtureRoot, junctionPath, metadataPath
    for _, handle in extraHandles
        DllCall("CloseHandle", "Ptr", handle)
    for _, payload in payloads {
        try NativePayloadClose(payload)
    }
    ; Remove only each named fixture, and unlink the junction itself first.
    ; Never recursively traverse a directory that contains a reparse point.
    if (junctionPath != "")
        DllCall("RemoveDirectoryW", "WStr", junctionPath)
    if (metadataPath != "") {
        DllCall("RemoveDirectoryW", "WStr", metadataPath . "\held-child")
        DllCall("RemoveDirectoryW", "WStr", metadataPath)
    }
    if (fixtureRoot != "") {
        DllCall("DeleteFileW", "WStr", fixtureRoot . "\target\nested\fixture.dll")
        DllCall("DeleteFileW", "WStr", fixtureRoot . "\target\fixture.dll")
        DllCall("RemoveDirectoryW", "WStr", fixtureRoot . "\target\nested")
        DllCall("RemoveDirectoryW", "WStr", fixtureRoot . "\target")
        DllCall("RemoveDirectoryW", "WStr", fixtureRoot)
    }
}
