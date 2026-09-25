// Hidden native window and inert callbacks; this test never installs an input
// hook, calls SendInput, or sends any input outside its own window.
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <filesystem>
#include <iostream>
#include "../native/client_ui.h"
#include "../native/client_ui_picker.h"
#include "../native/client_ui_service.h"

using namespace dafclient;
namespace {
int failures=0,checks=0;
void expect(bool condition,const char* message) {
    ++checks;
    if(!condition) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}
}
int main() {
    const auto path=std::filesystem::absolute(L"build/client-ui-test-"+std::to_wstring(GetCurrentProcessId())+L".json");
    Store store(path.wstring());
    unsigned starts=0,stops=0,changes=0;
    bool allowStop=true,allowStart=true;
    ClientUi* uiPointer=nullptr;
    UiCallbacks callbacks;
    callbacks.start=[&](const Profile&,const Settings&) {
        ++starts;
        if(!allowStart&&uiPointer) uiPointer->setStatus(L"测试：启动失败详情");
        return allowStart;
    };
    callbacks.stop=[&] { ++stops; return allowStop; };
    callbacks.settingsChanged=[&](const Profile&,const Settings&) { ++changes; };
    {
        ClientUi empty(GetModuleHandleW(nullptr),store,callbacks);
        expect(empty.create(false),"create hidden defaults");
        expect(IsWindow(empty.window())!=FALSE,"hidden window exists");
        expect(!std::filesystem::exists(path),"hidden creation does not write configuration");
    }
    Profile profile;
    profile.name=L"Existing";
    profile.keys={L"LControl",L"NumpadRight",L"future-key"};
    profile.runKeys={L"W",L"S",L"A",L"D"};
    profile.usePresetRunKeys=true; store.saveProfile(profile);
    profile.usePresetRunKeys=false; store.saveProfile(profile);
    Settings settings; settings.lastPreset=profile.name; store.saveSettings(settings);
    {
        ClientUi ui(GetModuleHandleW(nullptr),store,callbacks); uiPointer=&ui;
        expect(ui.create(false),"create from old configuration");
        expect(ui.currentProfile().name==L"Existing","profile loaded");
        expect(ui.keyEnabled(L"LCtrl")&&ui.keyEnabled(L"Num6"),"aliases light their physical keys");
        const auto before=std::filesystem::last_write_time(path);
        expect(ui.start(false),"startup uses existing settings");
        expect(ui.running(),"running state set");
        expect(std::filesystem::last_write_time(path)==before,"startup does not rewrite configuration");
        expect(ui.stop(),"startup can stop");

        ui.setKeyEnabled(L"X",true);
        expect(ui.keyEnabled(L"X"),"key cap enables key");
        ui.setKeyEnabled(L"X",false);
        expect(!ui.keyEnabled(L"X"),"key cap disables key");
        ui.setTiming(1,1);
        expect(ui.status().find(L"7ms")!=std::wstring::npos,"timing below 7ms shows a friendly hint");
        expect(ui.flush(),"automatic save commits");
        auto saved=store.loadProfile(L"Existing");
        expect(saved.downMs==1&&saved.upMs==1,"1ms timing saved");
        expect(saved.keys.size()==3,"aliases and future keys preserved");
        bool control=false,numpad=false,future=false;
        for(const auto& key:saved.keys) {
            control|=key==L"LControl"; numpad|=key==L"NumpadRight"; future|=key==L"future-key";
        }
        expect(future,"unknown future key retained");
        expect(control&&numpad,"original alias text retained");
        ui.setTiming(0,500);
        expect(ui.currentProfile().downMs==1&&ui.currentProfile().upMs==500,"timing only enforces positive milliseconds");
        ui.flush(); saved=store.loadProfile(L"Existing");
        expect(saved.downMs==1&&saved.upMs==500,"UI timing beyond 100ms survives reload");
        ui.setTiming(kMaxTimingMs,kMaxTimingMs); ui.flush(); saved=store.loadProfile(L"Existing");
        expect(saved.downMs==kMaxTimingMs&&saved.upMs==kMaxTimingMs,"full representable timing range survives UI save");
        ui.setTiming(7,7); ui.flush();

        ui.setRunScope(true);
        expect(ui.runKeys()[0]==L"W","preset run scope restores saved directions");
        ui.flush(); saved=store.loadProfile(L"Existing");
        expect(saved.usePresetRunKeys&&saved.runKeys[0]==L"W","run preset scope saved");
        ui.setRunScope(false);
        expect(ui.runKeys()[0]==L"Up","global run scope restores global directions");

        ui.toggleRun(); expect(store.loadSettings().oneKeyRun.enabled,"run hotkey state saved");
        expect(ui.noticeText()==L"一键奔跑已开启 · 启动连发后生效","run notice explains it waits for auto-fire");

        const unsigned changesBefore=changes;
        ui.setKeyEnabled(L"Q",true);
        expect(ui.flush()&&changes==changesBefore+1,"committed change re-applies the runtime");
        expect(ui.flush()&&changes==changesBefore+1,"flush without pending changes is a no-op");

        expect(ui.start(false),"start callback accepted");
        allowStop=false;
        expect(!ui.stop(),"failed stop is returned to caller");
        expect(ui.running(),"failed stop keeps running state for retry");
        expect(ui.status().find(L"停止未完成")!=std::wstring::npos,"failed stop status does not claim stopped");
        allowStop=true;
        expect(ui.stop(),"stop retry succeeds");
        allowStart=false;
        expect(!ui.start(false),"failed start is returned to caller");
        expect(ui.status()==L"测试：启动失败详情","failed start retains detailed status");
        allowStart=true;

        // In-game switch (hotkey / tray): toggles, then a corner notice that never takes focus.
        {
            const HWND foreground=GetForegroundWindow();
            ui.togglePower(nullptr);
            expect(ui.running(),"power hotkey starts auto-fire");
            expect(ui.noticeText()==L"连发已开启 · 方案 · Existing · Alt + F12 关闭","notice: started, with profile and hotkey");
            const HWND notice=ui.noticeWindow();
            expect(notice&&IsWindowVisible(notice),"notice shown");
            const LONG_PTR ex=notice?GetWindowLongPtrW(notice,GWL_EXSTYLE):0;
            expect((ex&WS_EX_NOACTIVATE)&&(ex&WS_EX_TRANSPARENT)&&(ex&WS_EX_TOPMOST)&&(ex&WS_EX_TOOLWINDOW),"notice is top-most, click-through and never activated");
            expect(GetForegroundWindow()==foreground&&GetActiveWindow()!=notice,"notice does not take focus");
            ui.toggleRun();
            expect(ui.noticeText()==L"一键奔跑已关闭 · 游戏内按 F10 重新开启","run notice while auto-fire runs");
            ui.togglePower(nullptr);
            expect(!ui.running()&&ui.noticeText()==L"连发已关闭 · Alt + F12 重新开启","power hotkey stops; notice says how to resume");
            allowStart=false; ui.togglePower(nullptr); allowStart=true;
            expect(!ui.running()&&ui.noticeText().find(L"连发未能启动")==0,"failed start is reported in the notice");
            ui.toggleRun();
        }

        expect(ui.cloneProfile(),"clone succeeds");
        expect(store.presetNames().size()==2,"clone preserves profiles");
        expect(ui.currentProfile().name==L"Existing 副本","clone becomes current");
        expect(ui.renameProfile(L"Renamed"),"rename succeeds");
        expect(store.presetNames()[1]==L"Renamed"&&store.loadSettings().lastPreset==L"Renamed","rename persisted in place");
        expect(!ui.renameProfile(L"existing"),"rename rejects a duplicate name");
        expect(!ui.renameProfile(L"bad|name"),"rename rejects reserved characters");
        expect(ui.selectProfile(L"Existing")&&ui.currentProfile().name==L"Existing","select profile");
        expect(store.loadSettings().lastPreset==L"Existing","selected profile remembered");
        expect(changes>=5,"settings changes notified");

        // Main-screen service chip: one friendly word per state, opening the service drawer.
        {
            svcctl::Info info;
            auto s=ServicePanel::summarize(info,false);
            expect(s.state==L"未安装"&&s.tone==ServiceTone::Stopped,"chip: not installed is red");
            info.legacy={L"DNFProcessManager"};
            s=ServicePanel::summarize(info,false);
            expect(s.state==L"待替换"&&s.tone==ServiceTone::Update&&s.detail.find(L"DNFProcessManager")!=std::wstring::npos,"chip: legacy service to replace is yellow");
            info.legacy.clear(); info.installed=true; info.pointsHere=true; info.state=SERVICE_RUNNING;
            s=ServicePanel::summarize(info,false);
            expect(s.state==L"运行中"&&s.tone==ServiceTone::Running,"chip: running is green");
            info.state=SERVICE_STOPPED;
            expect(ServicePanel::summarize(info,false).tone==ServiceTone::Stopped,"chip: stopped is red");
            info.state=SERVICE_RUNNING; info.pointsHere=false;
            expect(ServicePanel::summarize(info,false).tone==ServiceTone::Update,"chip: service registered for another EXE is yellow");
            expect(ServicePanel::summarize(info,true).tone==ServiceTone::Busy,"chip: busy while an operation runs");
            info.error=5;
            s=ServicePanel::summarize(info,false);
            expect(s.tone==ServiceTone::Stopped&&s.detail.find(L"5")!=std::wstring::npos,"chip: query error is red and shows its code");
        }
        // Service drawer: validated list edits saved to the same config.json.
        ui.openServicePage();
        expect(ui.addServiceItem(0,L"Foo.exe"),"kill list accepts a process name");
        expect(!ui.addServiceItem(0,L"foo"),"duplicate process rejected");
        expect(!ui.addServiceItem(0,L"DNF.exe"),"game process cannot be killed");
        expect(!ui.addServiceItem(0,L"C:\\x\\a.exe"),"kill list takes names, not paths");
        expect(ui.addServiceItem(2,L"Tools\\Other.exe"),"auto-start accepts a relative path");
        expect(ui.status().find(L"不存在")!=std::wstring::npos,"missing auto-start file is hinted");
        {
            wchar_t self[MAX_PATH]{}; GetModuleFileNameW(nullptr,self,MAX_PATH);
            const std::wstring name=std::filesystem::path(self).filename().wstring();
            expect(ui.addServiceItem(2,name),"the client itself can be listed for auto-start");
            const size_t at=ui.serviceOptions().autoStart.size()-1;
            expect(!ui.removeServiceItem(2,at),"the client's own auto-start entry cannot be removed");
            expect(ui.serviceOptions().autoStart.size()==at+1,"locked entry kept");
            expect(ui.removeServiceItem(2,1),"other auto-start entries can be removed");
            expect(ui.addServiceItem(2,L"Tools\\Other.exe"),"re-add the relative program");
        }
        expect(ui.flushService(),"service options saved");
        const auto service=store.loadService();
        expect(service.kill.size()==3&&service.kill.back()==L"Foo.exe","kill list persisted");
        expect(service.autoStart.size()==3,"auto-start persisted");
        ui.setKeyEnabled(L"W",true); ui.flush();
        expect(store.loadService().kill.size()==3,"profile save keeps service options");

        // Pickers: running-process catalogue and path normalisation for chosen programs.
        const auto processes=runningProcesses();
        bool sorted=true,unique=true;
        for(size_t i=1;i<processes.size();++i) {
            if(_wcsicmp(processes[i-1].name.c_str(),processes[i].name.c_str())>0) sorted=false;
            if(_wcsicmp(processes[i-1].name.c_str(),processes[i].name.c_str())==0) unique=false;
        }
        expect(!processes.empty()&&sorted&&unique,"running processes listed once each, sorted");
        expect(relativeToDirectory(L"C:\\DAF\\Tools\\x.exe",L"C:\\DAF")==L"Tools\\x.exe","program inside the EXE folder becomes relative");
        expect(relativeToDirectory(L"c:\\daf\\y.exe",L"C:\\DAF\\")==L"y.exe","relative conversion ignores case and trailing slash");
        expect(relativeToDirectory(L"D:\\Other\\z.exe",L"C:\\DAF")==L"D:\\Other\\z.exe","program elsewhere keeps its full path");
        expect(relativeToDirectory(L"C:\\DAFX\\z.exe",L"C:\\DAF")==L"C:\\DAFX\\z.exe","sibling folder with the same prefix stays absolute");
        expect(starts==5,"start callbacks match requests (including the two power-hotkey starts)");

        // Program update from a picked file (this test EXE carries the client's version resource).
        {
            wchar_t selfPath[MAX_PATH]{}; GetModuleFileNameW(nullptr,selfPath,MAX_PATH);
            ServicePanel panel(store,selfPath);
            const auto dir=std::filesystem::absolute(L"build/client-ui-update-"+std::to_wstring(GetCurrentProcessId()));
            std::filesystem::create_directories(dir);
            const auto picked=(dir/L"DNFAutoFire.exe").wstring(), same=(dir/L"Same.exe").wstring(), notes=(dir/L"notes.exe").wstring();
            CopyFileW(selfPath,picked.c_str(),FALSE); CopyFileW(selfPath,same.c_str(),FALSE);
            { HANDLE f=CreateFileW(picked.c_str(),FILE_APPEND_DATA,0,nullptr,OPEN_EXISTING,0,nullptr); DWORD n=0; WriteFile(f,"new",3,&n,nullptr); CloseHandle(f); }
            { HANDLE f=CreateFileW(notes.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr); DWORD n=0; WriteFile(f,"text",4,&n,nullptr); CloseHandle(f); }
            std::wstring error;
            expect(!panel.chooseUpdate(selfPath,error)&&error.find(L"当前正在运行")!=std::wstring::npos,"update: the running program itself is refused");
            error.clear();
            expect(!panel.chooseUpdate(notes,error)&&error.find(L"不是")!=std::wstring::npos,"update: a file that is not the program is refused");
            error.clear();
            expect(!panel.chooseUpdate(same,error)&&error.find(L"完全相同")!=std::wstring::npos,"update: an identical copy needs no update");
            error.clear();
            expect(panel.chooseUpdate(L" "+picked+L" ",error)&&error.empty(),"update: a different build is accepted");
            expect(!panel.pendingUpdate().empty()&&std::filesystem::equivalent(panel.pendingUpdate(),picked),"update: picked file awaits confirmation");
            panel.cancelUpdate();
            expect(panel.pendingUpdate().empty(),"update: cancel clears the pending file");
            std::error_code ignored; std::filesystem::remove_all(dir,ignored);
        }
    }
    std::filesystem::remove(path);
    std::cout << "Native UI: " << checks << " checks, " << failures << " failures\n";
    return failures?1:0;
}
