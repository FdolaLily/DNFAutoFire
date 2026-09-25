#pragma once
// Process helpers for the service: snapshots, identity checks, cross-session launch
// and the resource limits the former .NET service applied.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>

namespace dafclient::svc {

struct ProcessEntry {
    DWORD pid = 0, parent = 0, session = 0;
    std::wstring name; // Image file name as reported by Toolhelp, e.g. "DNF.exe".
};
std::vector<ProcessEntry> snapshotProcesses();

// Lazily captured once per service loop iteration and shared by all monitors.
class Snapshot {
public:
    const std::vector<ProcessEntry>& get();
    void invalidate() { taken_ = false; }
private:
    std::vector<ProcessEntry> entries_;
    bool taken_ = false;
};

// Comparison key of a configured process: lower case, no directory, no ".exe".
// "DNF.exe", "dnf" and "C:\\Game\\DNF.EXE" all become "dnf".
std::wstring processKey(const std::wstring& configured);
bool matches(const ProcessEntry& entry, const std::wstring& key);
bool runningInSession(const std::vector<ProcessEntry>& entries, const std::wstring& key, DWORD session, DWORD pid = 0);

std::wstring imagePath(DWORD pid);                     // Empty when inaccessible.
bool startTime(DWORD pid, unsigned long long& fileTime);
// Expands %VARS% and resolves a relative path against baseDirectory.
std::wstring resolvePath(const std::wstring& configured, const std::wstring& baseDirectory);

struct Launch {
    DWORD pid = 0, error = 0;
    bool elevated = false;
    const wchar_t* step = L"";                         // Failing API, for the log.
};
// Starts an interactive program on the given session's desktop with that user's
// token. With elevate, an administrator's linked (elevated) token is used so the
// client can control the elevated game window without a UAC prompt; other programs
// keep the user's normal token, exactly as the former service started them.
Launch launchInSession(const std::wstring& application, DWORD session, bool elevate);

// Terminates one process (never its tree). Returns true when it exited within waitMs.
bool terminate(DWORD pid, DWORD waitMs, DWORD& error);
bool waitExit(DWORD pid, DWORD waitMs);
bool restrictToLastCpu(DWORD pid, DWORD& error);       // Highest CPU of the current affinity.
bool setVeryLowIoPriority(DWORD pid, long& status);    // NtSetInformationProcess(ProcessIoPriority).

} // namespace dafclient::svc
