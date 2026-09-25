#pragma once
// Game lifecycle (ported from the former Worker.cs):
//  - waits for the configured game process in an interactive session (low-frequency polling);
//  - on start: launches autoStart programs into that session, then after actionDelaySeconds
//    terminates the kill list and limits the limit list (last CPU core, very low I/O priority);
//  - while running: keeps the game at Normal / AboveNormal priority;
//  - on exit (process handle signalled, polling fallback): closes autoStop programs in that
//    session. The client itself is asked to exit gracefully so held keys are released.
#include <string>
#include "client_config.h"
#include "process_util.h"
#include "service_log.h"

namespace dafclient::svc {

class GameMonitor {
public:
    GameMonitor(Log& log, std::wstring baseDirectory, std::wstring selfPath, HANDLE stopEvent);
    ~GameMonitor();
    GameMonitor(const GameMonitor&) = delete;
    GameMonitor& operator=(const GameMonitor&) = delete;
    void step(const ServiceOptions& options, Snapshot& snapshot, unsigned long long now);
    HANDLE exitHandle() const { return game_.handle; } // Wait on it to react to the game's exit at once.
    unsigned long long nextWake(unsigned long long now) const;
    bool gameRunning() const { return game_.pid != 0; }
    void shutdown();
private:
    struct Game {
        DWORD pid = 0, session = 0;
        HANDLE handle = nullptr;        // SYNCHRONIZE (+ query/set information when granted)
        bool canSetPriority = false;
        std::wstring key;
        ServiceOptions options;         // Options in effect when the game started.
        unsigned long long actionsDue = 0, nextPriority = 0, nextExitPoll = 0;
        bool actionsDone = false, priorityLogged = false, priorityFailureLogged = false;
    };
    void detected(const ProcessEntry& entry, const ServiceOptions& options, unsigned long long now);
    void exited(const ServiceOptions& options);
    void startApplications(const std::vector<std::wstring>& paths, DWORD session);
    void killProcesses(const std::vector<std::wstring>& names);
    void limitProcesses(const std::vector<std::wstring>& names);
    void stopApplications(const std::vector<std::wstring>& names, DWORD session);
    void maintainPriority(const ServiceOptions& options);
    bool pause(DWORD ms) const;          // False when the service is stopping.
    Log& log_;
    std::wstring base_, self_;
    HANDLE stop_;
    Game game_;
    unsigned long long nextPoll_ = 0;
};

} // namespace dafclient::svc
