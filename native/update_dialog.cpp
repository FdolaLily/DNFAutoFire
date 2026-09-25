#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include "update_dialog.h"
#include "win_fs.h"
#include <atomic>
#include <mutex>
#include <thread>

namespace dafclient::update {
namespace {
constexpr wchar_t kTitle[] = L"DAF 连发工具";
constexpr int kReplaceButton = 101, kSkipButton = 102;

struct Work {
    std::wstring source, target;
    std::mutex mutex;
    std::wstring step, shown;
    Outcome outcome;
    std::atomic<bool> done{false};
    std::thread thread;
};

HRESULT CALLBACK progressProc(HWND window, UINT notification, WPARAM wParam, LPARAM, LONG_PTR data) {
    auto* work = reinterpret_cast<Work*>(data);
    switch (notification) {
    case TDN_CREATED:
        SendMessageW(window, TDM_ENABLE_BUTTON, IDCANCEL, FALSE);
        SendMessageW(window, TDM_SET_PROGRESS_BAR_MARQUEE, TRUE, 30);
        work->thread = std::thread([work] {
            Outcome outcome;
            try {
                Options options; // Service, client and relaunch are all managed; this process holds no lock.
                outcome = replace(work->source, work->target, options, [work](const std::wstring& step) {
                    std::lock_guard<std::mutex> lock(work->mutex);
                    work->step = step;
                });
            } catch (...) { outcome.ok = false; outcome.message = L"更新时发生异常"; }
            { std::lock_guard<std::mutex> lock(work->mutex); work->outcome = outcome; }
            work->done = true;
        });
        return S_OK;
    case TDN_TIMER: {
        std::wstring step;
        { std::lock_guard<std::mutex> lock(work->mutex); step = work->step; }
        if (!step.empty() && step != work->shown) {
            work->shown = step;
            SendMessageW(window, TDM_SET_ELEMENT_TEXT, TDE_CONTENT, reinterpret_cast<LPARAM>(work->shown.c_str()));
        }
        if (work->done) {
            SendMessageW(window, TDM_ENABLE_BUTTON, IDCANCEL, TRUE);
            PostMessageW(window, TDM_CLICK_BUTTON, IDCANCEL, 0);
        }
        return S_OK;
    }
    case TDN_BUTTON_CLICKED:
        (void)wParam;
        return work->done ? S_OK : S_FALSE; // The swap cannot be interrupted halfway.
    default:
        return S_OK;
    }
}

} // namespace
void notice(PCWSTR icon, const std::wstring& instruction, const std::wstring& content) {
    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
    config.dwCommonButtons = TDCBF_OK_BUTTON;
    config.pszWindowTitle = kTitle;
    config.pszMainIcon = icon;
    config.pszMainInstruction = instruction.c_str();
    config.pszContent = content.c_str();
    TaskDialogIndirect(&config, nullptr, nullptr, nullptr);
}

Choice confirm(const Target& target, const Version& self, Offer offer) {
    const std::wstring installed = text(target.info.version), mine = text(self);
    // The earlier package: AHK-era client and / or the stand-alone DNFProcessManager service.
    const bool legacy = target.info.legacy || !target.legacyService.empty();
    std::wstring instruction, content, replaceText, skipText;
    if (legacy) {
        const std::wstring service = target.legacyService.empty() ? L"" : L"和 " + target.legacyService + L" 服务";
        instruction = L"将旧版 DAF 连发工具" + service + L"升级到 v" + mine + L"？";
        content = L"旧版：v" + installed + (target.info.legacy ? L"（AHK 版）" : L"") +
            (target.legacyService.empty() ? L"" : L" + 服务 " + target.legacyService) + L"\n" + target.path +
            L"\n此文件：v" + mine + L"\n\n升级时会：";
        if (!target.legacyService.empty())
            content += L"\n· 停止并删除旧服务 " + target.legacyService + L"，改装为新的后台服务 DNFAutoFire";
        content += L"\n· 关闭旧版连发（先释放按键），用此文件替换 " + fs::fileName(target.path) +
            L"\n· config.ini、appsettings.json 合并为 config.json，校验成功后删除";
        if (!target.legacyService.empty())
            content += L"\n· 删除 " + fs::fileName(target.legacyServiceExe) + L"、服务管理.bat、DNF专用工具箱8.0.bat（工具箱已内置）";
        content += L"\n\n方案、按键和服务设置全部保留，完成后自动打开新版本。";
        replaceText = L"升级旧版\n替换 " + target.path + L"，完成后自动打开新版本";
        skipText = L"暂不升级\n旧版保持不变";
    } else {
        switch (offer) {
        case Offer::Downgrade: instruction = L"用较旧的 v" + mine + L" 替换已安装的 v" + installed + L"？"; break;
        case Offer::Reinstall: instruction = L"用此文件重新覆盖已安装的 v" + installed + L"？"; break;
        default: instruction = L"将已安装的 DAF 连发工具更新到 v" + mine + L"？"; break;
        }
        content =
            L"已安装：v" + installed + (target.source == Source::Service ? L"（后台服务正在使用）" : L"（正在运行）") + L"\n" + target.path +
            L"\n此文件：v" + mine +
            (offer == Offer::Reinstall ? L"（版本号相同，文件内容不同）" : L"") +
            L"\n\n更新时会先停止后台服务和连发（连发会先释放按键），用此文件覆盖已安装的程序，"
            L"然后重新启动服务并打开新版本。config.json、日志和服务设置在原目录中保持不变，不保留旧版本文件。";
        replaceText = std::wstring(offer == Offer::Downgrade ? L"降级已安装的程序" : L"更新已安装的程序") +
            L"\n覆盖 " + target.path + L"，完成后自动重新打开";
        skipText = L"暂不更新\n保持已安装的程序不变，只打开 DAF 连发工具";
    }
    const TASKDIALOG_BUTTON buttons[] = {{kReplaceButton, replaceText.c_str()}, {kSkipButton, skipText.c_str()}};
    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS | TDF_SIZE_TO_CONTENT;
    config.pszWindowTitle = kTitle;
    config.pszMainIcon = offer == Offer::Downgrade ? TD_WARNING_ICON : TD_INFORMATION_ICON;
    config.pszMainInstruction = instruction.c_str();
    config.pszContent = content.c_str();
    config.cButtons = 2;
    config.pButtons = buttons;
    // An accidental double-click on an older copy must not downgrade by pressing Enter.
    config.nDefaultButton = offer == Offer::Downgrade ? kSkipButton : kReplaceButton;
    int pressed = 0;
    if (FAILED(TaskDialogIndirect(&config, &pressed, nullptr, nullptr))) return Choice::Skip;
    if (pressed == kReplaceButton) return Choice::Replace;
    if (pressed == kSkipButton) return Choice::Skip;
    return Choice::Cancel;
}

int runWithProgress(const std::wstring& source, const std::wstring& target) {
    Work work;
    work.source = source; work.target = target;
    const std::wstring initial = L"正在准备…"; // The dialog keeps this pointer; later steps are sent by message.
    work.shown = initial;
    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.dwFlags = TDF_SHOW_MARQUEE_PROGRESS_BAR | TDF_CALLBACK_TIMER | TDF_SIZE_TO_CONTENT;
    config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    config.pszWindowTitle = kTitle;
    config.pszMainIcon = TD_INFORMATION_ICON;
    const std::wstring instruction = L"正在更新 DAF 连发工具";
    config.pszMainInstruction = instruction.c_str();
    config.pszContent = initial.c_str();
    config.pfCallback = progressProc;
    config.lpCallbackData = reinterpret_cast<LONG_PTR>(&work);
    const HRESULT shown = TaskDialogIndirect(&config, nullptr, nullptr, nullptr);
    if (work.thread.joinable()) work.thread.join();
    if (FAILED(shown) && !work.done) {
        // No dialog could be shown: still perform the confirmed update, silently.
        work.outcome = replace(source, target);
    }
    const Outcome& outcome = work.outcome;
    if (outcome.ok && outcome.changed) {
        // The reopened client says "已更新"; the one-time package clean-up is listed here.
        if (!outcome.removed.empty()) {
            std::wstring names;
            for (const auto& name : outcome.removed) names += (names.empty() ? L"" : L"、") + name;
            notice(TD_INFORMATION_ICON, L"旧版已升级到 v" + text(outcome.to), L"旧版服务已替换为新的后台服务 DNFAutoFire，已删除：" + names + L"。");
        }
        return 0;
    }
    if (outcome.ok) { notice(TD_INFORMATION_ICON, L"无需更新", outcome.message); return 0; }
    notice(outcome.changed ? TD_WARNING_ICON : TD_ERROR_ICON, outcome.changed ? L"程序已更新，但有步骤未完成" : L"更新未完成",
           outcome.message);
    return outcome.changed ? 0 : 1;
}

} // namespace dafclient::update
