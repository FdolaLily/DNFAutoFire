#pragma once
// Widgets and services that ClientUi lends to the drawer modules (service,
// toolbox, pickers) so they share one look, hit testing and transition clock.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <functional>
#include <string>
#include "client_gfx.h"

namespace dafclient {

enum class ButtonStyle { Normal, Primary, Danger };

class UiHost {
public:
    virtual ~UiHost() = default;
    virtual gfx::Canvas& uiCanvas() = 0;
    virtual const gfx::Theme& uiTheme() const = 0;
    virtual void uiHit(const D2D1_RECT_F& rect, int id, std::function<void()> click) = 0;
    virtual bool uiHovered(int id) const = 0;
    virtual void uiSwitch(int id, float x, float y, bool on, std::function<void()> click) = 0;
    virtual void uiStepper(int idDec, int idInc, float x, float y, float w, const std::wstring& value,
                           std::function<void()> dec, std::function<void()> inc) = 0;
    virtual void uiSection(float y, const std::wstring& title) = 0;
    virtual float uiLink(int id, float x, float cy, const std::wstring& label, std::function<void()> click) = 0;
    // Single-line text input: drawn as a field; a native edit control is placed over it while active.
    // commit runs when the editor closes (submitted = Enter); change runs on every edit.
    using TextCommit = std::function<void(const std::wstring& text, bool submitted)>;
    using TextChange = std::function<void(const std::wstring& text)>;
    virtual void uiTextField(int id, const D2D1_RECT_F& rect, const std::wstring& value, const std::wstring& placeholder,
                             TextCommit commit, TextChange change = {}) = 0;
    virtual void uiFocusText(int id, TextCommit commit, TextChange change) = 0; // Opens that field's editor.
    virtual void uiCancelText() = 0;
    virtual void uiMessage(const std::wstring& text, bool error) = 0;
    virtual void uiHint(const std::wstring& text) = 0;
    virtual void uiInvalidate() = 0;
    virtual HWND uiWindow() const = 0;
    virtual void uiScheduleServiceSave() = 0;
    virtual void uiQuit() = 0;                     // Stop auto-fire, release keys and exit (after an update).
    // Rounded text button (32 DIP high) in the drawer style; returns its width.
    virtual float uiButton(int id, float x, float y, const std::wstring& label, ButtonStyle style, std::function<void()> click) = 0;
    virtual float uiButtonWidth(const std::wstring& label) = 0;
    // Transitions: eased value for key toward target; see ui_motion.h.
    virtual float uiAnim(int key, float target, float ms = 160.f) = 0;
    // Overlay layer inside the drawer: alpha and vertical offset on top of the drawer slide.
    virtual void uiLayer(float opacity, float dy) = 0;
    virtual void uiHitsEnabled(bool enabled) = 0;  // Fading-out content must not be clickable.
    virtual void uiSpinner(float x, float cy) = 0;  // Three pulsing dots for work in progress.
};


} // namespace dafclient
