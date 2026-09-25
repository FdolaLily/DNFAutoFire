#pragma once
// DNF launcher lifecycle (ported from the former LauncherMonitor.cs).
// Only after a DNF.exe was seen in the same session and then exited for one minute
// is a launcher still running terminated, and only when every identity check passes
// (session, full path, start time, directory layout and the DNF update manifest).
// While a launcher runs, its AppData\config.json is kept at
// showMainWindowOnGameExit=false and MainWindowCloseAction="minimize".
#include <map>
#include <string>
#include <vector>
#include "client_config.h"
#include "process_util.h"
#include "service_log.h"

namespace dafclient::svc {

constexpr unsigned long long kLauncherCloseDelayMs = 60 * 1000;

enum class LauncherConfigStatus { Retry, AlreadyConfigured, Updated };
// Pure text transformation of the launcher config.json; out is set only when Updated.
LauncherConfigStatus rewriteLauncherConfig(const std::string& in, std::string& out, std::string& error);
// True when a filelist.json lists a download from dnf.gcloudcdn.qq.com.
bool isDnfUpdateManifest(const std::string& filelistJson);
bool launcherCloseDue(bool gameWasObserved, bool gameRunning, unsigned long long msSinceGameExit);
std::wstring urlHost(const std::wstring& url);

class LauncherMonitor {
public:
    explicit LauncherMonitor(Log& log) : log_(log) {}
    void step(const ServiceOptions& options, Snapshot& snapshot, unsigned long long now);
private:
    struct Key {
        std::wstring path; DWORD session;
        bool operator<(const Key& o) const { return session != o.session ? session < o.session : path < o.path; }
    };
    struct Candidate { Key key; DWORD pid; unsigned long long started; };
    struct State {
        unsigned long long nextConfigAttempt = 0, nextCloseAttempt = 0, gameExitedAt = 0;
        bool gameWasObserved = false, gameExited = false, configHandled = false;
    };
    struct Identity { unsigned long long manifestTime = 0; bool launcher = false; };
    bool isLauncher(const std::wstring& path);
    LauncherConfigStatus configure(const std::wstring& executable, std::wstring& configPath, std::string& error);
    int stopVerified(const std::vector<Candidate>& group, const Key& key);
    Log& log_;
    bool enabled_ = false;
    std::map<Key, State> launchers_;
    std::map<std::wstring, Identity> identities_; // Cached per path until filelist.json changes.
};

} // namespace dafclient::svc
