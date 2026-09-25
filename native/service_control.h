#pragma once
// Service control manager operations used by the client's service page. All
// mutating calls need the elevated client; queries also work unelevated.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>

namespace dafclient::svcctl {

struct Info {
    bool installed = false;
    DWORD state = 0;             // SERVICE_STOPPED, SERVICE_RUNNING, ...
    DWORD pid = 0;
    DWORD startType = 0;         // SERVICE_AUTO_START, ...
    std::wstring command;        // Registered binary path with arguments.
    bool pointsHere = false;     // Registered for this EXE in service mode.
    std::vector<std::wstring> legacy; // Installed earlier stand-alone services.
    std::vector<std::wstring> legacyCommands; // Their registered binary paths, same order.
    DWORD error = 0;             // Query failure (other than "not installed").
    bool operator==(const Info& o) const {
        return installed == o.installed && state == o.state && pid == o.pid && startType == o.startType &&
            command == o.command && pointsHere == o.pointsHere && legacy == o.legacy && legacyCommands == o.legacyCommands && error == o.error;
    }
    bool operator!=(const Info& o) const { return !(*this == o); }
};

struct Result {
    bool ok = false;
    std::wstring message;        // User-facing Chinese sentence.
};

std::wstring serviceCommand(const std::wstring& executable); // "\"<exe>\" --service"
bool commandPointsTo(const std::wstring& command, const std::wstring& executable);
// Executable path of a registered service command (quoted or not); empty when unparsable.
std::wstring commandExecutable(const std::wstring& command);

Info query(const std::wstring& executable);
// Removes earlier services, registers (or re-points) the service to this EXE with
// automatic start as LocalSystem, restarts it on failure, and starts it.
Result install(const std::wstring& executable);
// Rewrites the display name and description of the installed service (no other change), so a
// service registered by an older build shows the current "how to stop / uninstall" wording.
// Called by the service itself at start-up; false when it could not be updated.
bool refreshLabels();
Result start();
Result stop();
Result restart();
Result uninstall();
// Any service by name (the earlier stand-alone services during an update).
Result stopNamed(const wchar_t* name);
Result startNamed(const wchar_t* name);

} // namespace dafclient::svcctl
