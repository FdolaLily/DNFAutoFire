// Hidden native window and inert callbacks; this test never installs an input
// hook, calls SendInput, or sends any input outside its own window.
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <filesystem>
#include <iostream>
#include "../native/client_ui.h"

using namespace dafclient;
namespace {
int failures=0,checks=0;
void expect(bool condition,const char* message) {
    ++checks;
    if(!condition) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}
}
int main() {
    const auto path=std::filesystem::absolute(L"build/client-ui-test-"+std::to_wstring(GetCurrentProcessId())+L".ini");
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
        expect(ui.currentProfile().downMs==1&&ui.currentProfile().upMs==100,"timing clamped to 1-100ms");

        ui.setRunScope(true);
        expect(ui.runKeys()[0]==L"W","preset run scope restores saved directions");
        ui.flush(); saved=store.loadProfile(L"Existing");
        expect(saved.usePresetRunKeys&&saved.runKeys[0]==L"W","run preset scope saved");
        ui.setRunScope(false);
        expect(ui.runKeys()[0]==L"Up","global run scope restores global directions");

        ui.toggleRun(); expect(store.loadSettings().oneKeyRun.enabled,"run hotkey state saved");

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
        expect(starts==3,"start callbacks match requests");
    }
    std::filesystem::remove(path);
    std::cout << "Native UI: " << checks << " checks, " << failures << " failures\n";
    return failures?1:0;
}
