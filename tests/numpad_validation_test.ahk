#NoEnv
#SingleInstance, Force

#Include %A_ScriptDir%\..\core\KeyValidation.ahk

AssertTrue(value, message) {
    if (!value) {
        FileAppend, Test failed: %message%`n, *
        ExitApp, 1
    }
}

AssertTrue(ZhanFaIsNumpadKey("Numpad6"), "number-pad digit should be accepted")
AssertTrue(ZhanFaIsNumpadKey("NumpadRight"), "number-pad navigation alias should be accepted when Num Lock is off")
AssertTrue(!ZhanFaIsNumpadKey("Space"), "regular keyboard key should be rejected")
AssertTrue(!ZhanFaIsNumpadKey("NumLock"), "Num Lock itself must not be used as the shot key")

ExitApp, 0
