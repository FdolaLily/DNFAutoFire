#pragma once
// Direct2D / DirectWrite drawing layer for the native client. Only Windows
// system components are used (d2d1, dwrite, dwmapi, gdi32); the two UI
// typefaces are linked into the EXE as RCDATA and registered privately.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <unordered_map>

namespace dafclient::gfx {

struct Color { float r = 0, g = 0, b = 0, a = 0; };
constexpr Color rgb(unsigned hex, float alpha = 1.f) {
    return {((hex >> 16) & 0xff) / 255.f, ((hex >> 8) & 0xff) / 255.f, (hex & 0xff) / 255.f, alpha};
}
Color mix(Color a, Color b, float t);

struct Theme {
    bool dark = true;
    Color bg, bar, surface, surface2, surface3, line, line2, text, text2, text3;
    Color caseBg, caseLine, caseEtch;
    Color capTop, capSide, capTopH, capLegend, modTop, modSide, modTopH, modLegend;
    Color led, ledGlow, ledRing, ledOff, ledBad, ledBadGlow, danger;
    Color accent, onAccent, accentSoft, accentText;
    Color run, runSoft, combo, comboSoft, cls, scrim, shadow, focus;
};
const Theme& darkTheme();
const Theme& lightTheme();

enum class Face { Sans, Mono };
struct Font {
    Face face = Face::Sans;
    int weight = 400;
    float size = 13.f;
    float tracking = 0.f; // Extra spacing between characters, in DIPs.
};
inline Font sans(float size, int weight = 400) { return {Face::Sans, weight, size, 0.f}; }
inline Font mono(float size, int weight = 500) { return {Face::Mono, weight, size, 0.f}; }

// Private, process-wide registration of the embedded fonts. Falls back to
// Microsoft YaHei UI / Consolas when the resources or DirectWrite 3 are absent.
class Fonts {
public:
    static void load(HINSTANCE instance);
    static IDWriteFactory* factory();
    static IDWriteFontCollection* collection(); // nullptr: system collection.
    static const wchar_t* family(Face face);
    static const wchar_t* gdiFamily(Face face);  // Registered with GDI for native edit controls.
    static bool embedded();
};

enum class Icon {
    Sun, Moon, Gear, Minus, Close, More, Edit, Copy, Trash, TrashSmall, Plus, Power,
    ChevronRight, ChevronDown, ArrowRight, Check, Clock
};

enum class Align { Left, Center, Right };

class Canvas {
public:
    Canvas() = default;
    ~Canvas();
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    bool attach(HWND window, float scale);
    void resize(UINT width, UINT height);
    bool begin(Color clear);
    void end(); // Discards device resources when the device is lost.
    float scale() const { return scale_; }

    // Multiplies every color alpha, for disabled or de-emphasised regions.
    void setOpacity(float opacity) { opacity_ = opacity; }
    // While muted every drawing call is skipped; used to measure layout without painting.
    void setMuted(bool muted) { muted_ = muted; }
    bool muted() const { return muted_; }
    float opacity() const { return opacity_; }
    void pushClip(const D2D1_RECT_F& rect);
    void popClip();

    void fill(const D2D1_RECT_F& rect, Color color);
    void fillRound(const D2D1_RECT_F& rect, float radius, Color color);
    void strokeRound(const D2D1_RECT_F& rect, float radius, Color color, float width, bool dashed = false);
    void ring(const D2D1_RECT_F& rect, float radius, Color color, float width); // CSS "0 0 0 w" outside ring
    void insetRing(const D2D1_RECT_F& rect, float radius, Color color, float width);
    void keyShape(const D2D1_RECT_F& rect, float radius, Color top, Color side, float sideHeight);
    void shadow(const D2D1_RECT_F& rect, float radius, Color color, float offsetY, float blur);
    void fillCircle(float cx, float cy, float r, Color color);
    void glow(float cx, float cy, float inner, float outer, Color color);
    void line(float x1, float y1, float x2, float y2, Color color, float width, bool dashed = false);
    void polyline(const D2D1_POINT_2F* points, size_t count, Color color, float width);
    void icon(Icon icon, float x, float y, float size, Color color);

    float textWidth(const std::wstring& text, const Font& font);
    // Distance from a single line's centered CSS line box to its baseline.
    float baselineOffset(const Font& font) const { return baselineShift_[font.face == Face::Mono ? 0 : 1] * font.size; }
    // Draws a single line with its CSS line box centered at centerY.
    void text(const std::wstring& text, const Font& font, float x, float centerY, Color color,
              Align align = Align::Left, float maxWidth = 0.f);

private:
    ID2D1SolidColorBrush* brush(Color color);
    IDWriteTextFormat* format(const Font& font);
    IDWriteTextLayout* layout(const std::wstring& text, const Font& font);
    ID2D1Geometry* iconGeometry(Icon icon);
    void discard();

    HWND window_ = nullptr;
    float scale_ = 1.f, opacity_ = 1.f;
    bool muted_ = false;
    ID2D1Factory* factory_ = nullptr;
    ID2D1HwndRenderTarget* target_ = nullptr;
    ID2D1SolidColorBrush* brush_ = nullptr;
    ID2D1StrokeStyle* roundStroke_ = nullptr;
    ID2D1StrokeStyle* dashStroke_ = nullptr;
    std::unordered_map<unsigned long long, IDWriteTextFormat*> formats_;
    std::unordered_map<std::wstring, IDWriteTextLayout*> layouts_;
    std::unordered_map<int, ID2D1Geometry*> icons_;
    float baselineShift_[2] = {0.36f, 0.43f}; // (ascent - descent) / 2, in em.
};

} // namespace dafclient::gfx
