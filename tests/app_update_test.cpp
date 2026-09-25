// In-place update without the service control manager or a real client: version
// parsing, the version resource reader, target choice, argument round trips and
// update::replace() on copies of this test executable (which carries the client's
// version resource) in a private build\ folder. Never touches an installed copy.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../native/app_update.h"
#include "../native/config_migrate.h"
#include "../native/service_control.h"
#include "../native/win_fs.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks = 0;
void check(bool value, const char* name) { ++checks; if (!value) throw std::runtime_error(name); }
using namespace dafclient;
using namespace dafclient::update;
namespace sfs = std::filesystem;

struct Child {
    PROCESS_INFORMATION info{};
    Child(const std::wstring& exe, const std::wstring& arguments) {
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        std::wstring command = L"\"" + exe + L"\" " + arguments;
        if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info))
            throw std::runtime_error("cannot start child");
    }
    bool alive() const { return WaitForSingleObject(info.hProcess, 0) == WAIT_TIMEOUT; }
    DWORD wait(DWORD ms) const {
        DWORD code = 999;
        if (WaitForSingleObject(info.hProcess, ms) == WAIT_OBJECT_0) GetExitCodeProcess(info.hProcess, &code);
        return code;
    }
    ~Child() {
        if (alive()) { TerminateProcess(info.hProcess, 0); WaitForSingleObject(info.hProcess, 5000); }
        CloseHandle(info.hThread); CloseHandle(info.hProcess);
    }
};
void copy(const std::wstring& from, const std::wstring& to, const char* extra = nullptr) {
    if (!CopyFileW(from.c_str(), to.c_str(), FALSE)) throw std::runtime_error("cannot copy test executable");
    // Trailing bytes change the content but not the PE image or its version resource.
    if (extra) { std::ofstream out(sfs::path(to), std::ios::binary | std::ios::app); out << extra; }
}
// Win32 directly: the C++ stream may narrow non-ASCII file names (服务管理.bat).
void writeText(const std::wstring& path, const char* text) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot write test file");
    DWORD written = 0;
    WriteFile(file, text, DWORD(strlen(text)), &written, nullptr);
    CloseHandle(file);
}
unsigned leftovers(const sfs::path& dir) {
    unsigned n = 0;
    for (const auto& e : sfs::directory_iterator(dir)) {
        const auto name = e.path().filename().wstring();
        if (name.find(L".old-") != std::wstring::npos || name.find(L".update-") != std::wstring::npos) ++n;
    }
    return n;
}
// Version text is ASCII digits and dots.
std::string ascii(const std::wstring& text) { return std::string(text.begin(), text.end()); }
bool waitForFile(const std::wstring& path, DWORD ms) {
    for (DWORD waited = 0; waited < ms; waited += 50) { if (fs::isFile(path)) return true; Sleep(50); }
    return fs::isFile(path);
}
}

int wmain(int argc, wchar_t** argv) {
    // Child roles. A copy started by replace() as "<exe> --updated <version> <pid>" records its arguments.
    if (argc > 1 && std::wstring(argv[1]) == L"--child-sleep") { Sleep(60000); return 0; }
    if (argc == 4 && std::wstring(argv[1]) == kUpdatedArgument) {
        std::wofstream out(sfs::path(fs::join(fs::directoryOf(fs::modulePath()), L"launched.txt")));
        out << argv[2] << L" " << argv[3];
        return 0;
    }
    if (argc == 3 && std::wstring(argv[1]) == L"--self-replace") {
        // Models the running client updating itself from the service drawer.
        Options options; options.manageService = false; options.askClient = false; options.launch = false; options.holdsInstance = true;
        const auto outcome = replace(argv[2], fs::modulePath(), options);
        return outcome.ok && outcome.changed ? 0 : 3;
    }

    const auto dir = sfs::path(fs::fullPath(L"build\\app-update-test-" + std::to_wstring(GetCurrentProcessId())));
    sfs::create_directories(dir);
    int result = 0;
    try {
        // Versions.
        const auto v = parseVersion(L"0.3.0.0");
        check(v.valid && v.part[0] == 0 && v.part[1] == 3 && v.part[2] == 0 && v.part[3] == 0, "parse full version");
        check(parseVersion(L" v1.2 ").valid && parseVersion(L"v1.2").part[1] == 2 && parseVersion(L"1.2").part[3] == 0, "parse short version with v");
        check(!parseVersion(L"").valid && !parseVersion(L"1..2").valid && !parseVersion(L"1.2.3.4.5").valid && !parseVersion(L"1.x").valid &&
              !parseVersion(L"70000").valid && !parseVersion(L"1.").valid, "reject malformed versions");
        check(compare(parseVersion(L"0.3.1.0"), parseVersion(L"0.3.0.9")) > 0 && compare(parseVersion(L"0.10"), parseVersion(L"0.9.9.9")) > 0, "numeric ordering");
        check(compare(parseVersion(L"1.0"), parseVersion(L"1.0.0.0")) == 0 && compare(Version{}, parseVersion(L"0.0.0.0")) < 0, "equal and invalid ordering");
        check(text(parseVersion(L"v2.10.0.3")) == L"2.10.0.3" && text(Version{}) == L"?", "version text");
        check(currentVersion().valid, "product version constant parses");
        check(classify(parseVersion(L"0.4"), parseVersion(L"0.3"), false) == Offer::Upgrade &&
              classify(parseVersion(L"0.3"), parseVersion(L"0.3"), false) == Offer::Reinstall &&
              classify(parseVersion(L"0.2"), parseVersion(L"0.3"), false) == Offer::Downgrade &&
              classify(parseVersion(L"0.4"), parseVersion(L"0.3"), true) == Offer::None, "offer classification");

        // Version resource of this test executable (linked with the client's resources).
        const auto self = fs::modulePath();
        const auto info = inspect(self);
        check(info.product, "version resource identifies DNFAutoFire");
        check(info.version.valid && compare(info.version, currentVersion()) == 0, "file version matches kProductVersion");
        const auto text1 = (dir / L"notes.exe").wstring();
        writeText(text1, "not a program");
        check(!inspect(text1).product && !inspect((dir / L"missing.exe").wstring()).product, "non-programs are not the product");

        // Service command parsing and target choice.
        check(svcctl::commandExecutable(L"\"C:\\A B\\DNFAutoFire.exe\" --service") == L"C:\\A B\\DNFAutoFire.exe", "quoted service executable");
        check(svcctl::commandExecutable(L"C:\\Tools\\DNFAutoFire.exe --service") == L"C:\\Tools\\DNFAutoFire.exe", "unquoted service executable");
        check(svcctl::commandExecutable(L"\"broken --service").empty() && svcctl::commandExecutable(L"").empty(), "unparsable service command");
        check(svcctl::commandPointsTo(L"\"C:\\A B\\x.exe\" --service", L"C:\\A B\\x.exe") && !svcctl::commandPointsTo(L"\"C:\\A B\\x.exe\"", L"C:\\A B\\x.exe"),
              "service argument still required");
        const auto installedDir = dir / L"installed", downloadDir = dir / L"download";
        sfs::create_directories(installedDir); sfs::create_directories(downloadDir);
        const auto target = (installedDir / L"DNFAutoFire.exe").wstring();
        const auto source = (downloadDir / L"DNFAutoFire.exe").wstring();
        copy(self, target);
        copy(self, source, "new build");
        Candidate chosen;
        check(chooseTarget({{target, Source::Service}, {source, Source::Client}}, source, chosen) && fs::samePath(chosen.path, target) &&
              chosen.source == Source::Service, "service EXE is the target");
        check(chooseTarget({{source, Source::Service}, {L"", Source::LegacyService}, {target, Source::Client}}, source, chosen) &&
              fs::samePath(chosen.path, target) && chosen.source == Source::Client, "self and empty candidates are skipped");
        check(!chooseTarget({{(dir / L"gone.exe").wstring(), Source::Service}}, source, chosen), "missing EXE skipped");
        check(!chooseTarget({{source, Source::Client}}, source, chosen) && !chooseTarget({}, source, chosen), "self and nothing are no target");

        // Arguments for the reopened client.
        Version from; DWORD pid = 0;
        std::wstring command = updatedArguments(parseVersion(L"0.3.0.0"), 1234);
        wchar_t* parts[4] = {const_cast<wchar_t*>(L"x.exe"), nullptr, nullptr, nullptr};
        std::wstring a1 = command.substr(0, command.find(L' ')), rest = command.substr(command.find(L' ') + 1);
        std::wstring a2 = rest.substr(0, rest.find(L' ')), a3 = rest.substr(rest.find(L' ') + 1);
        parts[1] = a1.data(); parts[2] = a2.data(); parts[3] = a3.data();
        check(parseUpdated(4, parts, from, pid) && pid == 1234 && text(from) == L"0.3.0.0", "updated arguments round trip");
        check(!parseUpdated(3, parts, from, pid), "updated needs all arguments");
        std::wstring badPid = L"12x"; parts[3] = badPid.data();
        check(!parseUpdated(4, parts, from, pid), "updated rejects a malformed pid");

        // Content comparison.
        check(sameContent(target, target) && !sameContent(target, source) && !sameContent(target, text1), "content comparison");

        // Refusals change nothing.
        Options quiet; quiet.manageService = false; quiet.askClient = false; quiet.launch = false; quiet.graceMs = 300;
        auto outcome = replace(text1, target, quiet);
        check(!outcome.ok && !outcome.changed && sameContent(target, target) && !sameContent(target, source), "non-program source refused");
        outcome = replace(target, target, quiet);
        check(!outcome.ok && !outcome.changed, "same path refused");
        const auto notProduct = (installedDir / L"Other.exe").wstring();
        writeText(notProduct, "user file");
        outcome = replace(source, notProduct, quiet);
        check(!outcome.ok && !outcome.changed && fs::read(notProduct, 64) == "user file", "non-program target never overwritten");
        fs::remove(notProduct);

        // A process running the installed file (a stand-in for the service / client) is ended and the file swapped.
        {
            Child running(target, L"--child-sleep");
            Sleep(300);
            check(running.alive(), "installed copy running before update");
            outcome = replace(source, target, quiet);
            check(outcome.ok && outcome.changed, "update succeeds while the installed copy runs");
            check(!running.alive(), "running installed copy was ended");
        }
        check(sameContent(source, target), "installed file now holds the new program");
        check(leftovers(installedDir) == 0, "no backup or temporary file kept");
        check(compare(outcome.from, currentVersion()) == 0 && compare(outcome.to, currentVersion()) == 0, "outcome reports both versions");
        outcome = replace(source, target, quiet);
        check(outcome.ok && !outcome.changed, "identical files need no update");

        // The updated client is reopened with the previous version (no pid: this process holds no lock).
        copy(self, source, "third build");
        Options reopen = quiet; reopen.launch = true;
        outcome = replace(source, target, reopen);
        const auto marker = (installedDir / L"launched.txt").wstring();
        check(outcome.ok && outcome.changed && waitForFile(marker, 10000), "updated client launched");
        Sleep(200);
        check(fs::read(marker, 256) == ascii(text(currentVersion())) + " 0", "launched with --updated <old version> 0");
        fs::remove(marker);

        // The running client updating itself: its own image is renamed away, cleanup removes it later.
        {
            copy(self, source, "fourth build");
            Child updater(target, L"--self-replace \"" + source + L"\"");
            check(updater.wait(20000) == 0, "running copy replaced its own file");
        }
        check(sameContent(source, target), "self update wrote the new program");
        cleanupLeftovers(target);
        check(leftovers(installedDir) == 0, "leftover of the running copy is removed by cleanup");

        // ---- The earlier package: AHK-era client + DNFProcessManager service + INI / appsettings.json.
        const auto legacyExe = fs::join(fs::directoryOf(self), L"legacy_client.exe");
        const auto old = inspect(legacyExe);
        check(old.legacy && !old.product && old.replaceable() && text(old.version) == L"0.1.3.3", "AHK-era client recognised by its version names");
        check(!info.legacy && info.replaceable(), "current client is not legacy");
        const auto package = dir / L"package";
        sfs::create_directories(package / L"Tools");
        const auto packageClient = (package / L"DNFAutoFire.exe").wstring();
        const auto serviceExe = (package / L"DNFProcessManager.exe").wstring();
        copy(legacyExe, packageClient);
        writeText(serviceExe, "old service");
        check(fs::samePath(legacyClientFor(serviceExe), packageClient), "old service starts DNFAutoFire.exe beside it by default");
        copy(legacyExe, (package / L"Tools" / L"DAF.exe").wstring());
        writeText((package / L"appsettings.json").wstring(),
            "{ // comment\n \"Manager\": { \"ProcessName\": \"DNF.exe\", \"AutoStart\": [ \"Notepad.exe\", \"Tools\\\\DAF.exe\" ], \"AutoStop\": [ \"DAF.exe\" ] } }");
        check(fs::samePath(legacyClientFor(serviceExe), (package / L"Tools" / L"DAF.exe").wstring()), "AutoStart entry that is this program wins");
        fs::remove((package / L"Tools" / L"DAF.exe").wstring());
        check(fs::samePath(legacyClientFor(serviceExe), packageClient), "falls back when the AutoStart entry is gone");
        check(legacyClientFor((dir / L"nowhere" / L"DNFProcessManager.exe").wstring()).empty(), "no client for an empty folder");

        {
            // A running AHK-era client is found, asked to close (WM_CLOSE) and replaced.
            Child running(packageClient, L"");
            Sleep(500);
            check(fs::samePath(runningLegacyClient(self), packageClient), "running AHK-era client found by its main window");
            outcome = replace(source, packageClient, quiet);
            check(outcome.ok && outcome.changed && compare(outcome.from, parseVersion(L"0.1.3.3")) == 0, "AHK-era client replaced");
            check(running.wait(3000) == 7, "AHK-era client closed itself on WM_CLOSE");
        }
        check(sameContent(source, packageClient) && leftovers(package) == 0 && runningLegacyClient(self).empty(), "new program in place of the AHK client");
        {
            copy(legacyExe, packageClient);
            Child hung(packageClient, L"--ignore-close");
            Sleep(500);
            outcome = replace(source, packageClient, quiet);
            check(outcome.ok && outcome.changed && hung.wait(3000) == 1, "unresponsive AHK-era client ended after the grace period");
        }

        // Package files: only the known ones, and the bat scripts only when their content matches.
        writeText((package / L"DNFProcessManager.pdb").wstring(), "pdb");
        writeText((package / L"服务管理.bat").wstring(), "set \"SERVICE_NAME=DNFProcessManager\"");
        writeText((package / L"DNF专用工具箱8.0.bat").wstring(), "set SUFFIX=.dnf-toolbox-disabled");
        writeText((package / L"其他.bat").wstring(), "DNFProcessManager");
        writeText((package / L"appsettings.Development.json").wstring(), "{}");
        writeText((package / L"README.md").wstring(), "old readme");
        sfs::create_directories(package / L"logs");
        writeText((package / L"logs" / L"auto-manager.log").wstring(), "x");
        writeText((package / L"logs" / L"auto-manager_001.log").wstring(), "x");
        writeText((package / L"logs" / L"service.log").wstring(), "x");
        auto removed = removeLegacyFiles(serviceExe);
        check(removed.size() == 6 && !fs::isFile(serviceExe) && !fs::isFile((package / L"服务管理.bat").wstring()) &&
              !fs::isFile((package / L"DNF专用工具箱8.0.bat").wstring()) && !fs::isFile((package / L"logs" / L"auto-manager.log").wstring()),
              "service program, pdb, both scripts and old service logs removed");
        check(fs::isFile((package / L"appsettings.Development.json").wstring()) && fs::isFile((package / L"appsettings.json").wstring()),
              "settings not yet migrated are kept");
        check(fs::isFile((package / L"其他.bat").wstring()) && fs::isFile((package / L"README.md").wstring()) &&
              fs::isFile((package / L"logs" / L"service.log").wstring()) && fs::isFile(packageClient), "other files kept");
        fs::remove((package / L"appsettings.json").wstring());
        removed = removeLegacyFiles(serviceExe);
        check(removed.size() == 1 && !fs::isFile((package / L"appsettings.Development.json").wstring()), "development settings go once appsettings.json was migrated");
        writeText((package / L"服务管理.bat").wstring(), "@echo my own script");
        writeText((package / L"MyService.exe").wstring(), "user program");
        removed = removeLegacyFiles((package / L"MyService.exe").wstring());
        check(removed.empty() && fs::isFile((package / L"MyService.exe").wstring()) && fs::isFile((package / L"服务管理.bat").wstring()),
              "unknown binaries and unrelated scripts are never removed");

        // Old service in another folder than the client: its settings move beside the client.
        const auto serviceDir = dir / L"svc", clientDir = dir / L"client";
        sfs::create_directories(serviceDir); sfs::create_directories(clientDir);
        const auto otherService = (serviceDir / L"DNFProcessManager.exe").wstring(), otherClient = (clientDir / L"DNFAutoFire.exe").wstring();
        writeText(otherService, "old service"); copy(legacyExe, otherClient);
        writeText((serviceDir / L"appsettings.json").wstring(),
            "{ \"Manager\": { \"ProcessName\": \"DNF.exe\", \"ActionDelaySeconds\": 30, \"KillList\": [\"GameLoader.exe\"], \"AutoStart\": [ \"Tools\\\\x.exe\" ] } }");
        check(!adoptLegacySettings(otherService, (serviceDir / L"DNFAutoFire.exe").wstring()), "same folder is migrated in place");
        check(adoptLegacySettings(otherService, otherClient), "settings adopted by the client folder");
        check(!fs::isFile((serviceDir / L"appsettings.json").wstring()), "old settings moved, not copied");
        const auto adopted = importAppSettings(fs::read((clientDir / L"appsettings.json").wstring(), 65536));
        check(adopted.actionDelaySeconds == 30 && adopted.kill.size() == 1 && adopted.autoStart.size() == 1 &&
              fs::samePath(adopted.autoStart[0], (serviceDir / L"Tools" / L"x.exe").wstring()), "adopted settings keep values, AutoStart made absolute");
        writeText((serviceDir / L"appsettings.json").wstring(), "{ \"Manager\": { } }");
        check(!adoptLegacySettings(otherService, otherClient), "existing settings beside the client are not overwritten");

        // Cleanup only touches this program's leftovers.
        writeText(target + L".old-1-2", "x");
        writeText(target + L".update-3-4.tmp", "x");
        writeText((installedDir / L"config.json").wstring(), "{}");
        writeText((installedDir / L"DNFAutoFire.exe.bak").wstring(), "x");
        check(cleanupLeftovers(target) == 2, "cleanup removes old and temporary files");
        check(fs::isFile((installedDir / L"config.json").wstring()) && fs::isFile((installedDir / L"DNFAutoFire.exe.bak").wstring()), "cleanup keeps other files");

        std::cout << "PASS: " << checks << " update checks.\n";
    } catch (const std::exception& error) {
        std::cout << "FAIL " << error.what() << " (after " << checks << " checks)\n";
        result = 1;
    }
    std::error_code ignored;
    sfs::remove_all(dir, ignored);
    return result;
}
