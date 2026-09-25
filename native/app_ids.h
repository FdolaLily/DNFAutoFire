#pragma once
// Identities shared by the client (GUI) and the Windows service mode of the same EXE.
#include "version.h"

namespace dafclient {

constexpr wchar_t kProductVersion[] = L"" DAF_VERSION_STRING; // native/version.h

// Windows service. The EXE is registered as "<path>\DNFAutoFire.exe" --service.
constexpr wchar_t kServiceName[] = L"DNFAutoFire";
// While the service runs it keeps DNFAutoFire.exe open, so deleting or moving the EXE fails.
// Explorer's "file in use" dialog names the service by its display name, which therefore says
// how to release the file; services.msc shows the full steps in the description.
constexpr wchar_t kServiceDisplayName[] = L"DNF 连发后台服务（删除或移动程序前：打开连发 → 后台服务 → 卸载）";
constexpr wchar_t kServiceDescription[] =
    L"DAF 连发工具服务：检测 DNF 启动与退出，自动启动 / 关闭连发程序，保持游戏优先级并管理指定后台进程。"
    L"删除或移动 DNFAutoFire.exe 前请先卸载本服务：双击打开 DNFAutoFire.exe → 点“启动连发”左侧的“后台服务” → “卸载”"
    L"（再点一次确认），然后在托盘图标右键菜单中点“退出”。也可以在管理员命令提示符中执行 sc stop DNFAutoFire 和 sc delete DNFAutoFire。";
static_assert(sizeof(kServiceDisplayName) / sizeof(wchar_t) <= 256, "SCM display names are limited to 256 characters");
static_assert(sizeof(kServiceDescription) / sizeof(wchar_t) <= 512, "service_control.cpp copies the description into 512 characters");
constexpr wchar_t kServiceArgument[] = L"--service";
// Earlier stand-alone .NET service (DNFProcessManager) and its original name; removed on install.
constexpr const wchar_t* kLegacyServiceNames[] = {L"DNFProcessManager", L"AutoManagerProcess"};

// Signalled by the service to let the client in the game session exit gracefully
// (stop auto-fire, release keys, remove the tray icon) before any forced termination.
constexpr wchar_t kClientQuitEvent[] = L"Global\\DNFAutoFire.Client.Quit.{B797BFB2-305A-44DC-9E06-76D6CC424419}";

constexpr wchar_t kLogDirectory[] = L"logs";
constexpr wchar_t kServiceLogName[] = L"service.log";

} // namespace dafclient
