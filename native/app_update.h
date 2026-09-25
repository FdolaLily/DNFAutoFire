#pragma once
// In-place program update while the service is installed.
//
// The service ("<exe>" --service) and the tray client both keep the installed EXE
// open, so it cannot simply be overwritten in Explorer. replace() stops the service
// (through the SCM) and the running client (gracefully via the Global quit event,
// forcibly only after a grace period), swaps the file through a temporary rename so
// that a failure restores the original, starts the service again and reopens the
// client. No backup is kept afterwards. Nothing here knows an installation folder:
// the target always comes from the service registration, a running client or the
// caller.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <functional>
#include <string>
#include <vector>

namespace dafclient::update {

constexpr wchar_t kReplaceArgument[] = L"--replace";   // "<new exe>" --replace "<installed exe>"
constexpr wchar_t kUpdatedArgument[] = L"--updated";   // "<installed exe>" --updated <old version> <pid to wait for>
// Set to "0" by automated tests that launch real client copies on a developer PC with
// an installed service: suppresses the double-click update prompt only.
constexpr wchar_t kPromptEnvironment[] = L"DAF_UPDATE_PROMPT";

struct Version {
    unsigned part[4]{};
    bool valid = false;
};
Version parseVersion(const std::wstring& text);      // "0.3.0.0", "v0.3.1" (missing parts are 0).
int compare(const Version& a, const Version& b);      // <0, 0, >0; invalid sorts first.
std::wstring text(const Version& v);                  // "0.3.0.0"; "?" when invalid.
Version currentVersion();                             // kProductVersion.

// Version resource of an EXE, read as a data file (no code runs, no extra DLL).
struct ExeInfo {
    bool product = false;       // InternalName / OriginalFilename identify DNFAutoFire (v0.1.5 and later).
    bool legacy = false;        // AHK-era client (ProductName / FileDescription "DAF连发工具").
    Version version;            // Fixed file version.
    bool replaceable() const { return product || legacy; }
};
ExeInfo inspect(const std::wstring& path);
bool sameContent(const std::wstring& a, const std::wstring& b);

// What a double-click on `self` should offer for an installed copy.
enum class Offer { None, Upgrade, Reinstall, Downgrade };
Offer classify(const Version& self, const Version& installed, bool identical);

// Where an installed copy was found, in order of preference.
enum class Source { Service, LegacyService, Client };
struct Candidate {
    std::wstring path;
    Source source = Source::Client;
};
// Pure choice used by findInstalled: the first candidate that exists and is not `self`.
bool chooseTarget(const std::vector<Candidate>& candidates, const std::wstring& self, Candidate& chosen);

struct Target {
    std::wstring path;
    ExeInfo info;
    Source source = Source::Client;
    std::wstring legacyService;      // Earlier stand-alone service found (name), if any.
    std::wstring legacyServiceExe;   // Its binary (DNFProcessManager.exe).
};
// Looks for an installed copy other than `self`: the DNFAutoFire service's EXE, the client
// the earlier DNFProcessManager service starts (its appsettings.json AutoStart, by default
// DNFAutoFire.exe beside it), then a running client (native window or AHK-era script).
bool findInstalled(const std::wstring& self, Target& target);

// The DNFAutoFire EXE that an earlier stand-alone service starts: an AutoStart entry of the
// appsettings.json beside `serviceExe` that is this program (any version), else
// "<service folder>\DNFAutoFire.exe". Empty when neither exists.
std::wstring legacyClientFor(const std::wstring& serviceExe);
// A running AHK-era client (its hidden "AutoHotkey" main window), other than `self`.
std::wstring runningLegacyClient(const std::wstring& self);

// Files of the earlier stand-alone package that the unified EXE replaces, removed once
// its service is gone: the service binary (DNFProcessManager.exe / AutoManagerProcess.exe
// and .pdb), 服务管理.bat and DNF专用工具箱8.0.bat (only when their content matches),
// appsettings.Development.json (after appsettings.json was migrated) and the old service
// log logs\auto-manager*.log. config.ini / appsettings.json are removed by the config
// migration only after config.json holds them.
std::vector<std::wstring> removeLegacyFiles(const std::wstring& serviceExe);
// When the earlier service lived in another folder than the client, its appsettings.json
// is moved beside the client (AutoStart made absolute) so the client's migration imports it.
bool adoptLegacySettings(const std::wstring& serviceExe, const std::wstring& client);

struct Options {
    bool manageService = true;  // Stop / restart the DNFAutoFire service registered for the target;
                                // replace earlier stand-alone services with it and remove their files.
    bool askClient = true;      // Signal the running client (main window) to exit gracefully.
    bool launch = true;         // Start the updated client afterwards.
    bool holdsInstance = false; // The caller is the running client: the new one waits for it to exit.
    DWORD graceMs = 5000;       // Before remaining target processes are terminated.
};
struct Outcome {
    bool ok = false;
    bool changed = false;       // The target file now holds the new program.
    std::wstring message;       // User-facing Chinese sentence.
    Version from, to;
    std::vector<std::wstring> removed; // Earlier package files deleted.
};
using Progress = std::function<void(const std::wstring&)>;
// Replaces `target` with the program in `source`. The calling process may itself be
// `target` (the running client updating from a picked file): its own image is renamed
// away, never terminated, and the launched client waits for it to exit.
Outcome replace(const std::wstring& source, const std::wstring& target, const Options& options = {}, const Progress& progress = {});

// Removes "<exe>.old-*" / "<exe>.update-*.tmp" left next to `executable` by an update.
unsigned cleanupLeftovers(const std::wstring& executable);

// Command line for the reopened client; parse returns false for other command lines.
std::wstring updatedArguments(const Version& from, DWORD waitPid);
bool parseUpdated(int argc, wchar_t** argv, Version& from, DWORD& waitPid);

} // namespace dafclient::update
