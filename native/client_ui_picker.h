#pragma once
// Pickers used by the service drawer: an in-window overlay listing the running
// processes (searchable, with manual entry) and the system file-open dialog for
// programs started with the game. Both follow the drawer's theme through UiHost.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <functional>
#include <string>
#include <vector>

namespace dafclient {

class UiHost;

struct RunningProcess {
    std::wstring name;        // Image name, e.g. "SGuard64.exe".
    std::wstring path;        // Full image path when readable.
    unsigned count = 0;       // Instances with this name.
    bool background = false;  // Only in session 0 (services).
};
// One entry per image name (case-insensitive), sorted by name; kernel pseudo
// processes without an executable image are left out.
std::vector<RunningProcess> runningProcesses();

class ProcessPicker {
public:
    using Choose = std::function<void(const std::wstring& name)>;
    // Non-empty text marks a row as unavailable (e.g. "已添加") and replaces its tag.
    using Status = std::function<std::wstring(const std::wstring& name)>;

    void open(UiHost& host, std::wstring title, std::wstring subtitle, Choose choose, Status status, bool closeOnChoose);
    void close(UiHost& host);
    bool active() const { return active_; }
    void paint(UiHost& host);          // Overlay over the drawer; call after the drawer content.
    bool wheel(float notches);         // True when the overlay consumed the wheel.
    void reload();                     // Re-reads the running processes.
    std::vector<const RunningProcess*> visible() const; // After filtering.
    void setFilter(const std::wstring& filter) { filter_ = filter; scroll_ = 0; }
    bool submit(UiHost& host, const std::wstring& text); // Enter in the search box.

    static constexpr int kIdBase = 7600, kIdEnd = 8000;
private:
    void choose(UiHost& host, const std::wstring& name);
    void focusSearch(UiHost& host);
    bool active_ = false, closeOnChoose_ = false;
    std::wstring title_, subtitle_, filter_;
    Choose choose_;
    Status status_;
    std::vector<RunningProcess> items_;
    float scroll_ = 0, contentHeight_ = 0;
};

// Shows the system "open file" dialog for an .exe, starting in folder.
// Returns the chosen full path, or an empty string when cancelled.
std::wstring pickProgramFile(HWND owner, const std::wstring& folder, const std::wstring& title = L"选择随游戏启动的程序");
// Shows the system folder picker; returns the chosen folder or an empty string.
std::wstring pickFolder(HWND owner, const std::wstring& title, const std::wstring& folder);
// A path inside baseDirectory becomes relative to it ("Tools\\x.exe"); others stay absolute.
std::wstring relativeToDirectory(const std::wstring& path, const std::wstring& baseDirectory);

} // namespace dafclient
