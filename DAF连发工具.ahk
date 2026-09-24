;@Ahk2Exe-SetMainIcon icon_main.ico
;@Ahk2Exe-AddResource icon_alert.ico, 160
;@Ahk2Exe-AddResource icon_green.ico, 206
;@Ahk2Exe-AddResource icon_red.ico, 207

;@Ahk2Exe-SetDescription DAF连发工具
;@Ahk2Exe-SetCopyright 某亚瑟
;@Ahk2Exe-SetLanguage 0x0804
;@Ahk2Exe-SetProductName DAF连发工具
;@Ahk2Exe-SetProductVersion 0.1.4.2
;@Ahk2Exe-SetVersion 0.1.4.2

#NoEnv

#Persistent
#MenuMaskKey, vkFF
#SingleInstance, Off
#MaxHotkeysPerInterval, 9999
#InstallKeybdHook
#MaxThreadsBuffer, Off
#WinActivateForce
SetWorkingDir, %A_ScriptDir%
SetBatchLines, -1
ListLines, Off
SetStoreCapslockMode, Off

global __Version := "0.1.4.2"
#Include <SingleInstance>
#Include <NativePayload>

; 打包验收入口：不加载用户配置、不显示界面、不产生键盘输入。
if (A_Args[1] == "--self-test") {
    AutoFireNativeSelfTest()
    ExitApp
}

SingleInstanceCheckBeforeElevation()
#Include <RunWithAdministrator>
; 直到所有输入清理完成、进程真正结束才释放锁，不在 OnExit 中提前关闭。
global _ClientInstanceMutex := SingleInstanceAcquireOrExit()
#Include <Keys>
#Include <JSON>
#Include <Time>
#Include <GetPressKey>
#Include <DirectKeyInput>
#Include ./core/AutoFireInput.ahk
#Include ./core/AutoFireNative.ahk
#Include ./core/AutoFireRules.ahk
#Include ./core/CheckDNFWindow.ahk
#Include ./core/KeyConvert.ahk
#Include ./core/KeyValidation.ahk
#Include ./core/AutoFireMode.ahk
#Include ./core/Config.ahk
#Include ./core/OneKeyRun.ahk
#Include ./core/Scripts.ahk
#Include ./core/Combo.ahk
#Include ./gui/Main.ahk
#Include ./gui/QuickSwitch.ahk
#Include ./gui/Setting.ahk
#Include ./gui/ex/Combo.ahk
#Include ./gui/ex/OneKeyRun.ahk
#Include ./gui/ex/AutoFireTiming.ahk
#Include ./gui/ex/LvRen.ahk
#Include ./ex/ExLvRen.ahk
#Include ./gui/ex/ZhanFa.ahk
#Include ./ex/ExZhanFa.ahk
#Include ./gui/ex/JianZong.ahk

;@Ahk2Exe-IgnoreBegin
#Include <Log>
Log()
;@Ahk2Exe-IgnoreEnd

/*@Ahk2Exe-Keep
    Menu, Tray, Icon, %A_ScriptFullPath%, 4
    Menu, Tray, NoStandard
    Menu, Tray, DeleteAll
*/

Menu, Tray, MainWindow
Menu, Tray, Tip, DAF连发工具
Menu, Tray , Add, 连发设置, ShowGuiMain
Menu, Tray , Add, 软件设置, ShowGuiSetting
Menu, Tray , Default, 连发设置
Menu, Tray , Add
Menu, Tray, Add, 退出连发,Exit

Exit(){
    ExitApp
}

global _AutoFireEnableKeys := []
global _NowSelectPreset := LoadLastPreset()
OnExit("CleanupBeforeExit")

ShowGuiMain()
SetDNFWindowClass()
OneKeyRunStartInputObserver()
if(_AutoStart){
    Gui Main:Hide
    StartAutoFire()
}

return

CleanupBeforeExit(exitReason, exitCode){
    StopAutoFire()
    AutoFireNativeShutdown()
    OneKeyRunShutdown()
    OneKeyRunStopInputObserver()
}
