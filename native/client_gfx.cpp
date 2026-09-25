#include "client_gfx.h"
#include <dwrite_1.h>
#include <dwrite_3.h>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <vector>

namespace dafclient::gfx {
namespace {
template <typename T> void release(T*& p) { if (p) { p->Release(); p = nullptr; } }

// Resource ids in client.rc.
constexpr int kFontResources[] = {201, 202, 203, 204, 205, 206, 207};
constexpr int kGdiSansResource = 201, kGdiMonoResource = 204;

struct FontState {
    IDWriteFactory* factory = nullptr;
    IDWriteFontCollection* collection = nullptr;
    bool embedded = false, loaded = false;
    float shift[2] = {0.43f, 0.36f};
} fonts;

bool resource(HINSTANCE instance, int id, const void*& data, DWORD& size) {
    HRSRC info = FindResourceW(instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10)); // RT_RCDATA
    if (!info) return false;
    HGLOBAL handle = LoadResource(instance, info);
    size = SizeofResource(instance, info);
    data = handle ? LockResource(handle) : nullptr;
    return data && size;
}

float baselineShift(IDWriteFontCollection* collection, const wchar_t* family) {
    UINT32 index = 0; BOOL exists = FALSE;
    if (!collection || FAILED(collection->FindFamilyName(family, &index, &exists)) || !exists) return -1.f;
    IDWriteFontFamily* fam = nullptr; IDWriteFont* font = nullptr; float shift = -1.f;
    if (SUCCEEDED(collection->GetFontFamily(index, &fam)) &&
        SUCCEEDED(fam->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font))) {
        DWRITE_FONT_METRICS m{}; font->GetMetrics(&m);
        if (m.designUnitsPerEm) shift = (float(m.ascent) - float(m.descent)) / 2.f / float(m.designUnitsPerEm);
    }
    release(font); release(fam);
    return shift;
}

// Minimal SVG path parser (M L H V C A Z, absolute and relative) for 24px icons.
struct PathParser {
    const wchar_t* p;
    explicit PathParser(const wchar_t* s) : p(s) {}
    void skip() { while (*p == L' ' || *p == L',') ++p; }
    bool number(float& out) {
        skip();
        const wchar_t* start = p;
        if (*p == L'-' || *p == L'+') ++p;
        bool dot = false, digits = false;
        while (std::iswdigit(*p) || (*p == L'.' && !dot)) { if (*p == L'.') dot = true; else digits = true; ++p; }
        if (!digits) { p = start; return false; }
        out = std::wcstof(std::wstring(start, p).c_str(), nullptr);
        return true;
    }
    bool flag(bool& out) { skip(); if (*p == L'0' || *p == L'1') { out = *p == L'1'; ++p; return true; } return false; }
};

// SVG elliptical arc (endpoint form) as cubic Beziers of at most 90 degrees each.
// Converting here keeps icon output identical on every Direct2D implementation.
void addArc(ID2D1GeometrySink* sink, float x1, float y1, float rx, float ry, float rotation, bool large, bool sweep, float x2, float y2) {
    if (x1 == x2 && y1 == y2) return;
    if (rx == 0 || ry == 0) { sink->AddLine(D2D1::Point2F(x2, y2)); return; }
    const double pi = 3.14159265358979323846;
    const double phi = rotation * pi / 180.0, cp = std::cos(phi), sp = std::sin(phi);
    const double dx = (x1 - x2) / 2.0, dy = (y1 - y2) / 2.0;
    const double x1p = cp * dx + sp * dy, y1p = -sp * dx + cp * dy;
    double rxd = std::fabs(rx), ryd = std::fabs(ry);
    const double lambda = x1p * x1p / (rxd * rxd) + y1p * y1p / (ryd * ryd);
    if (lambda > 1) { rxd *= std::sqrt(lambda); ryd *= std::sqrt(lambda); }
    double num = rxd * rxd * ryd * ryd - rxd * rxd * y1p * y1p - ryd * ryd * x1p * x1p;
    const double den = rxd * rxd * y1p * y1p + ryd * ryd * x1p * x1p;
    double coef = den == 0 ? 0 : std::sqrt(std::max(0.0, num / den));
    if (large == sweep) coef = -coef;
    const double cxp = coef * rxd * y1p / ryd, cyp = -coef * ryd * x1p / rxd;
    const double cx = cp * cxp - sp * cyp + (x1 + x2) / 2.0, cy = sp * cxp + cp * cyp + (y1 + y2) / 2.0;
    const auto angle = [](double ux, double uy, double vx, double vy) {
        const double a = std::atan2(ux * vy - uy * vx, ux * vx + uy * vy); return a;
    };
    const double theta = angle(1, 0, (x1p - cxp) / rxd, (y1p - cyp) / ryd);
    double delta = angle((x1p - cxp) / rxd, (y1p - cyp) / ryd, (-x1p - cxp) / rxd, (-y1p - cyp) / ryd);
    if (!sweep && delta > 0) delta -= 2 * pi;
    else if (sweep && delta < 0) delta += 2 * pi;
    const int segments = std::max(1, int(std::ceil(std::fabs(delta) / (pi / 2) - 1e-6)));
    const double step = delta / segments, k = 4.0 / 3.0 * std::tan(step / 4);
    double t = theta;
    for (int i = 0; i < segments; ++i) {
        const double t2 = t + step;
        const D2D1_POINT_2F p1 = [&] {
            const double ex = rxd * std::cos(t) - k * rxd * std::sin(t), ey = ryd * std::sin(t) + k * ryd * std::cos(t);
            return D2D1::Point2F(float(cp * ex - sp * ey + cx), float(sp * ex + cp * ey + cy));
        }();
        const D2D1_POINT_2F p2 = [&] {
            const double ex = rxd * std::cos(t2) + k * rxd * std::sin(t2), ey = ryd * std::sin(t2) - k * ryd * std::cos(t2);
            return D2D1::Point2F(float(cp * ex - sp * ey + cx), float(sp * ex + cp * ey + cy));
        }();
        const D2D1_POINT_2F p3 = i == segments - 1 ? D2D1::Point2F(x2, y2) : [&] {
            const double ex = rxd * std::cos(t2), ey = ryd * std::sin(t2);
            return D2D1::Point2F(float(cp * ex - sp * ey + cx), float(sp * ex + cp * ey + cy));
        }();
        sink->AddBezier(D2D1::BezierSegment(p1, p2, p3));
        t = t2;
    }
}

ID2D1Geometry* buildPath(ID2D1Factory* factory, const wchar_t* svg) {
    ID2D1PathGeometry* geometry = nullptr; ID2D1GeometrySink* sink = nullptr;
    if (FAILED(factory->CreatePathGeometry(&geometry))) return nullptr;
    if (FAILED(geometry->Open(&sink))) { release(geometry); return nullptr; }
    PathParser in(svg);
    wchar_t cmd = 0; float x = 0, y = 0, sx = 0, sy = 0; bool open = false;
    auto end = [&](bool closed) { if (open) sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN); open = false; };
    for (;;) {
        in.skip();
        if (!*in.p) break;
        if (std::iswalpha(*in.p)) cmd = *in.p++;
        const bool rel = std::iswlower(cmd) != 0;
        const wchar_t c = static_cast<wchar_t>(std::towupper(cmd));
        float a, b, c1, d, e, f;
        if (c == L'Z') { end(true); x = sx; y = sy; cmd = 0; continue; }
        if (c == L'M') {
            if (!in.number(a) || !in.number(b)) break;
            end(false);
            x = rel ? x + a : a; y = rel ? y + b : b; sx = x; sy = y;
            sink->BeginFigure(D2D1::Point2F(x, y), D2D1_FIGURE_BEGIN_HOLLOW); open = true;
            cmd = rel ? L'l' : L'L';
        } else if (c == L'L') {
            if (!in.number(a) || !in.number(b)) break;
            x = rel ? x + a : a; y = rel ? y + b : b; sink->AddLine(D2D1::Point2F(x, y));
        } else if (c == L'H') {
            if (!in.number(a)) break;
            x = rel ? x + a : a; sink->AddLine(D2D1::Point2F(x, y));
        } else if (c == L'V') {
            if (!in.number(a)) break;
            y = rel ? y + a : a; sink->AddLine(D2D1::Point2F(x, y));
        } else if (c == L'C') {
            if (!in.number(a) || !in.number(b) || !in.number(c1) || !in.number(d) || !in.number(e) || !in.number(f)) break;
            const float ox = rel ? x : 0, oy = rel ? y : 0;
            sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(ox + a, oy + b), D2D1::Point2F(ox + c1, oy + d), D2D1::Point2F(ox + e, oy + f)));
            x = ox + e; y = oy + f;
        } else if (c == L'A') {
            bool large = false, sweep = false;
            if (!in.number(a) || !in.number(b) || !in.number(c1) || !in.flag(large) || !in.flag(sweep) || !in.number(e) || !in.number(f)) break;
            const float tx = rel ? x + e : e, ty = rel ? y + f : f;
            addArc(sink, x, y, a, b, c1, large, sweep, tx, ty);
            x = tx; y = ty;
        } else break;
    }
    end(false);
    sink->Close(); release(sink);
    return geometry;
}

const wchar_t* iconPath(Icon icon) {
    switch (icon) {
    case Icon::Sun: return L"M8 12a4 4 0 1 0 8 0a4 4 0 1 0-8 0M12 2.5v2M12 19.5v2M4.6 4.6l1.4 1.4M18 18l1.4 1.4M2.5 12h2M19.5 12h2M4.6 19.4L6 18M18 6l1.4-1.4";
    case Icon::Moon: return L"M20 14.5A8 8 0 0 1 9.5 4a8 8 0 1 0 10.5 10.5z";
    case Icon::Gear: return L"M9 12a3 3 0 1 0 6 0a3 3 0 1 0-6 0"
        L"M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1"
        L"a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3"
        L"a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3H9"
        L"a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1"
        L"a1.7 1.7 0 0 0-.3 1.8V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z";
    case Icon::Minus: return L"M5 12h14";
    case Icon::Close: return L"M6 6l12 12M18 6L6 18";
    case Icon::More: return L"M4.2 12a.8 .8 0 1 0 1.6 0a.8 .8 0 1 0-1.6 0M11.2 12a.8 .8 0 1 0 1.6 0a.8 .8 0 1 0-1.6 0M18.2 12a.8 .8 0 1 0 1.6 0a.8 .8 0 1 0-1.6 0";
    case Icon::Edit: return L"M4 20h4L19 9l-4-4L4 16v4z";
    case Icon::Copy: return L"M10 8h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2h-8a2 2 0 0 1-2-2v-8a2 2 0 0 1 2-2z"
        L"M16 8V6a2 2 0 0 0-2-2H6a2 2 0 0 0-2 2v8a2 2 0 0 0 2 2h2";
    case Icon::Trash: return L"M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3";
    case Icon::TrashSmall: return L"M4 7h16M6 7l1 13h10l1-13M9 7V4h6v3";
    case Icon::Plus: return L"M12 5v14M5 12h14";
    case Icon::Power: return L"M12 3v8M6.3 7.2a8 8 0 1 0 11.4 0";
    case Icon::ChevronRight: return L"M9 6l6 6-6 6";
    case Icon::ChevronDown: return L"M6 9l6 6 6-6";
    case Icon::ArrowRight: return L"M5 12h14M13 6l6 6-6 6";
    case Icon::Check: return L"M5 12.5l4.5 4.5L19 7.5";
    case Icon::Clock: return L"M4 12a8 8 0 1 0 16 0a8 8 0 1 0-16 0M12 8v4l2.5 2";
    case Icon::Lock: return L"M7 11V8a5 5 0 0 1 10 0v3"; // shackle only; callers fill the body so it stays legible at 12-14px
    case Icon::Wrench: return L"M14.7 6.3a1 1 0 0 0 0 1.4l1.6 1.6a1 1 0 0 0 1.4 0l3.8-3.8a6 6 0 0 1-7.9 7.9l-6.9 6.9a2.1 2.1 0 0 1-3-3l6.9-6.9a6 6 0 0 1 7.9-7.9z";
    case Icon::Folder: return L"M4 7a2 2 0 0 1 2-2h4l2 2h6a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2z";
    case Icon::Refresh: return L"M20 11a8 8 0 0 0-14.8-4M4 5v4h4M4 13a8 8 0 0 0 14.8 4M20 19v-4h-4";
    }
    return L"";
}
} // namespace

Color mix(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

const Theme& darkTheme() {
    static const Theme t = [] {
        Theme x; x.dark = true;
        x.bg = rgb(0x0B0D10); x.bar = rgb(0x0F1215); x.surface = rgb(0x14171B); x.surface2 = rgb(0x1B1F24);
        x.surface3 = rgb(0x252A31); x.line = rgb(0x242930); x.line2 = rgb(0x323841);
        x.text = rgb(0xE8EBEE); x.text2 = rgb(0xA1A9B3); x.text3 = rgb(0x7D8691);
        x.caseBg = rgb(0x111418); x.caseLine = rgb(0x22272D); x.caseEtch = rgb(0x5E6771);
        x.capTop = rgb(0x282D34); x.capSide = rgb(0x1B1F24); x.capTopH = rgb(0x30363E); x.capLegend = rgb(0xDDE2E7);
        x.modTop = rgb(0x1D2126); x.modSide = rgb(0x15181C); x.modTopH = rgb(0x242A31); x.modLegend = rgb(0xA1A9B3);
        x.led = rgb(0x3BE38B); x.ledGlow = rgb(0x3BE38B, .65f); x.ledRing = rgb(0x3BE38B, .28f); x.ledOff = rgb(0x353B43);
        x.ledBad = rgb(0xFF5D5D); x.ledBadGlow = rgb(0xFF5D5D, .6f); x.danger = rgb(0xFF6B6B);
        x.ledWarn = rgb(0xF5C542); x.ledWarnGlow = rgb(0xF5C542, .6f);
        x.accent = rgb(0x3BE38B); x.onAccent = rgb(0x05140B); x.accentSoft = rgb(0x3BE38B, .13f); x.accentText = rgb(0x5BEA9F);
        x.run = rgb(0x4D9CFF); x.runSoft = rgb(0x4D9CFF, .16f); x.combo = rgb(0xFF9E45); x.comboSoft = rgb(0xFF9E45, .15f);
        x.cls = rgb(0xB08CFF); x.scrim = rgb(0x030507, .6f); x.shadow = rgb(0x000000, .55f); x.focus = rgb(0x7CB8FF);
        return x;
    }();
    return t;
}
const Theme& lightTheme() {
    static const Theme t = [] {
        Theme x; x.dark = false;
        x.bg = rgb(0xE6E9EC); x.bar = rgb(0xF1F3F5); x.surface = rgb(0xF9FAFB); x.surface2 = rgb(0xEFF1F3);
        x.surface3 = rgb(0xE2E6EA); x.line = rgb(0xD7DCE1); x.line2 = rgb(0xC4CBD2);
        x.text = rgb(0x12161A); x.text2 = rgb(0x4F5862); x.text3 = rgb(0x687480);
        x.caseBg = rgb(0xCBD1D7); x.caseLine = rgb(0xB8BFC7); x.caseEtch = rgb(0x6E7781);
        x.capTop = rgb(0xFDFDFE); x.capSide = rgb(0xD6DBE0); x.capTopH = rgb(0xF1F4F7); x.capLegend = rgb(0x1B2026);
        x.modTop = rgb(0xE3E7EB); x.modSide = rgb(0xC2C9D0); x.modTopH = rgb(0xD9DEE3); x.modLegend = rgb(0x454D56);
        x.led = rgb(0x16C466); x.ledGlow = rgb(0x16C466, .6f); x.ledRing = rgb(0x16C466, .3f); x.ledOff = rgb(0xBCC3CA);
        x.ledBad = rgb(0xE5484D); x.ledBadGlow = rgb(0xE5484D, .5f); x.danger = rgb(0xC62F34);
        x.ledWarn = rgb(0xE0A800); x.ledWarnGlow = rgb(0xE0A800, .5f);
        x.accent = rgb(0x0E8743); x.onAccent = rgb(0xFFFFFF); x.accentSoft = rgb(0x0E8743, .11f); x.accentText = rgb(0x0B7039);
        x.run = rgb(0x1D66D6); x.runSoft = rgb(0x1D66D6, .11f); x.combo = rgb(0xC0650A); x.comboSoft = rgb(0xC0650A, .12f);
        x.cls = rgb(0x6E43D8); x.scrim = rgb(0x12161A, .26f); x.shadow = rgb(0x18222C, .2f); x.focus = rgb(0x1D66D6);
        return x;
    }();
    return t;
}

void Fonts::load(HINSTANCE instance) {
    if (fonts.loaded) return;
    fonts.loaded = true;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&fonts.factory)))) return;
    // GDI copies serve the few native edit controls (rename, delay fields).
    for (const int id : {kGdiSansResource, kGdiMonoResource}) {
        const void* data = nullptr; DWORD size = 0; DWORD count = 0;
        if (resource(instance, id, data, size)) AddFontMemResourceEx(const_cast<void*>(data), size, nullptr, &count);
    }
    IDWriteFactory5* factory5 = nullptr;
    IDWriteInMemoryFontFileLoader* loader = nullptr;
    IDWriteFontSetBuilder1* builder = nullptr;
    IDWriteFontSet* set = nullptr;
    IDWriteFontCollection1* collection = nullptr;
    bool ok = SUCCEEDED(fonts.factory->QueryInterface(__uuidof(IDWriteFactory5), reinterpret_cast<void**>(&factory5)))
        && SUCCEEDED(factory5->CreateInMemoryFontFileLoader(&loader))
        && SUCCEEDED(factory5->RegisterFontFileLoader(loader))
        && SUCCEEDED(factory5->CreateFontSetBuilder(&builder));
    unsigned added = 0;
    if (ok) for (const int id : kFontResources) {
        const void* data = nullptr; DWORD size = 0;
        if (!resource(instance, id, data, size)) continue;
        IDWriteFontFile* file = nullptr;
        // The resource section stays mapped for the process lifetime; the loader copies it anyway.
        if (SUCCEEDED(loader->CreateInMemoryFontFileReference(factory5, data, size, nullptr, &file))
            && SUCCEEDED(builder->AddFontFile(file))) ++added;
        release(file);
    }
    ok = ok && added == sizeof(kFontResources) / sizeof(kFontResources[0])
        && SUCCEEDED(builder->CreateFontSet(&set))
        && SUCCEEDED(factory5->CreateFontCollectionFromFontSet(set, &collection));
    if (ok) {
        const float sansShift = baselineShift(collection, L"Noto Sans SC");
        const float monoShift = baselineShift(collection, L"JetBrains Mono");
        if (sansShift > 0 && monoShift > 0) {
            fonts.collection = collection; collection = nullptr;
            fonts.embedded = true; fonts.shift[0] = sansShift; fonts.shift[1] = monoShift;
        }
    }
    if (!fonts.embedded) {
        IDWriteFontCollection* system = nullptr;
        if (SUCCEEDED(fonts.factory->GetSystemFontCollection(&system))) {
            const float s = baselineShift(system, L"Microsoft YaHei UI"), m = baselineShift(system, L"Consolas");
            if (s > 0) fonts.shift[0] = s;
            if (m > 0) fonts.shift[1] = m;
        }
        release(system);
    }
    release(collection); release(set); release(builder); release(loader); release(factory5);
}
IDWriteFactory* Fonts::factory() { return fonts.factory; }
IDWriteFontCollection* Fonts::collection() { return fonts.collection; }
bool Fonts::embedded() { return fonts.embedded; }
const wchar_t* Fonts::family(Face face) {
    if (fonts.embedded) return face == Face::Mono ? L"JetBrains Mono" : L"Noto Sans SC";
    return face == Face::Mono ? L"Consolas" : L"Microsoft YaHei UI";
}
const wchar_t* Fonts::gdiFamily(Face face) {
    if (fonts.embedded) return face == Face::Mono ? L"JetBrains Mono" : L"Noto Sans SC";
    return face == Face::Mono ? L"Consolas" : L"Microsoft YaHei UI";
}

Canvas::~Canvas() {
    discard();
    for (auto& f : formats_) release(f.second);
    for (auto& l : layouts_) release(l.second);
    for (auto& g : icons_) release(g.second);
    release(roundStroke_); release(dashStroke_); release(factory_);
}

bool Canvas::attach(HWND window, float scale) {
    window_ = window; scale_ = scale;
    baselineShift_[0] = fonts.shift[1]; baselineShift_[1] = fonts.shift[0];
    if (!factory_ && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory_))) return false;
    if (!roundStroke_) {
        const auto props = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
        factory_->CreateStrokeStyle(props, nullptr, 0, &roundStroke_);
        const float dashes[] = {2.f, 3.f};
        const auto dashed = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
            D2D1_LINE_JOIN_MITER, 10.f, D2D1_DASH_STYLE_CUSTOM, 0.f);
        factory_->CreateStrokeStyle(dashed, dashes, 2, &dashStroke_);
    }
    return true;
}

void Canvas::discard() { release(brush_); release(target_); }

void Canvas::resize(UINT width, UINT height) {
    if (target_) target_->Resize(D2D1::SizeU(width, height));
}

bool Canvas::begin(Color clear) {
    if (!factory_ || !window_) return false;
    if (!target_) {
        RECT rc{}; GetClientRect(window_, &rc);
        const float dpi = 96.f * scale_;
        const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpi, dpi);
        if (FAILED(factory_->CreateHwndRenderTarget(props,
            D2D1::HwndRenderTargetProperties(window_, D2D1::SizeU(UINT(rc.right), UINT(rc.bottom))), &target_))) return false;
        target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        if (FAILED(target_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &brush_))) { discard(); return false; }
    }
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    target_->Clear(D2D1::ColorF(clear.r, clear.g, clear.b, 1.f));
    opacity_ = 1.f; layer_ = 1.f; dx_ = dy_ = 0.f;
    return true;
}
void Canvas::setLayer(float opacity, float dx, float dy) {
    layer_ = std::clamp(opacity, 0.f, 1.f); dx_ = dx; dy_ = dy;
    if (target_) target_->SetTransform(D2D1::Matrix3x2F::Translation(dx_, dy_));
}

void Canvas::end() {
    if (!target_) return;
    if (target_->EndDraw() == D2DERR_RECREATE_TARGET) discard();
    if (layouts_.size() > 1500) { for (auto& l : layouts_) release(l.second); layouts_.clear(); }
}

ID2D1SolidColorBrush* Canvas::brush(Color c) {
    brush_->SetColor(D2D1::ColorF(c.r, c.g, c.b, c.a * opacity_ * layer_));
    return brush_;
}

void Canvas::pushClip(const D2D1_RECT_F& rect) { target_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED); }
void Canvas::popClip() { target_->PopAxisAlignedClip(); }

void Canvas::fill(const D2D1_RECT_F& r, Color c) { if (muted_) return; if (c.a > 0) target_->FillRectangle(r, brush(c)); }
void Canvas::fillRound(const D2D1_RECT_F& r, float radius, Color c) { if (muted_) return;
    if (c.a <= 0 || r.right <= r.left || r.bottom <= r.top) return;
    target_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush(c));
}
void Canvas::strokeRound(const D2D1_RECT_F& r, float radius, Color c, float w, bool dashed) { if (muted_) return;
    if (c.a <= 0) return;
    target_->DrawRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush(c), w, dashed ? dashStroke_ : nullptr);
}
void Canvas::ring(const D2D1_RECT_F& r, float radius, Color c, float w) {
    const float h = w / 2;
    strokeRound(D2D1::RectF(r.left - h, r.top - h, r.right + h, r.bottom + h), radius + h, c, w);
}
void Canvas::insetRing(const D2D1_RECT_F& r, float radius, Color c, float w) {
    const float h = w / 2;
    strokeRound(D2D1::RectF(r.left + h, r.top + h, r.right - h, r.bottom - h), std::max(0.f, radius - h), c, w);
}
void Canvas::keyShape(const D2D1_RECT_F& r, float radius, Color top, Color side, float sideHeight) {
    fillRound(r, radius, side);
    fillRound(D2D1::RectF(r.left, r.top, r.right, r.bottom - sideHeight), radius, top);
}
void Canvas::shadow(const D2D1_RECT_F& r, float radius, Color c, float offsetY, float blur) {
    constexpr int steps = 10;
    for (int i = steps; i >= 1; --i) {
        const float e = blur * 0.5f * float(i) / steps;
        Color layer = c; layer.a = c.a * 0.16f * (1.f - float(i - 1) / steps);
        fillRound(D2D1::RectF(r.left - e, r.top - e + offsetY * 0.6f, r.right + e, r.bottom + e + offsetY),
            radius + e, layer);
    }
}
void Canvas::fillCircle(float cx, float cy, float rad, Color c) { if (muted_) return;
    if (c.a <= 0) return;
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rad, rad), brush(c));
}
void Canvas::glow(float cx, float cy, float inner, float outer, Color c) {
    if (c.a <= 0 || outer <= inner) return;
    // Stacked translucent discs approximate a Gaussian box-shadow and render the
    // same on every Direct2D implementation (no gradient brush involved).
    constexpr int steps = 8;
    Color layer = c; layer.a = c.a * 0.075f;
    for (int i = 0; i < steps; ++i) {
        const float t = float(i) / steps;
        fillCircle(cx, cy, outer - (outer - inner) * t, layer);
    }
}
void Canvas::line(float x1, float y1, float x2, float y2, Color c, float w, bool dashed) { if (muted_) return;
    if (c.a <= 0) return;
    target_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush(c), w, dashed ? dashStroke_ : nullptr);
}
void Canvas::polyline(const D2D1_POINT_2F* points, size_t count, Color c, float w) { if (muted_) return;
    if (count < 2) return;
    ID2D1PathGeometry* geometry = nullptr; ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(factory_->CreatePathGeometry(&geometry)) && SUCCEEDED(geometry->Open(&sink))) {
        sink->BeginFigure(points[0], D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLines(points + 1, UINT32(count - 1));
        sink->EndFigure(D2D1_FIGURE_END_OPEN); sink->Close();
        target_->DrawGeometry(geometry, brush(c), w, roundStroke_);
    }
    release(sink); release(geometry);
}
ID2D1Geometry* Canvas::iconGeometry(Icon icon) {
    auto& slot = icons_[int(icon)];
    if (!slot) slot = buildPath(factory_, iconPath(icon));
    return slot;
}
void Canvas::icon(Icon icon, float x, float y, float size, Color c) { if (muted_) return;
    ID2D1Geometry* geometry = iconGeometry(icon);
    if (!geometry || c.a <= 0) return;
    const float s = size / 24.f;
    target_->SetTransform(D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(x + dx_, y + dy_));
    target_->DrawGeometry(geometry, brush(c), 1.7f, roundStroke_);
    target_->SetTransform(D2D1::Matrix3x2F::Translation(dx_, dy_));
}

IDWriteTextFormat* Canvas::format(const Font& font) {
    const unsigned long long key = (unsigned long long)(font.face == Face::Mono) << 40 |
        (unsigned long long)font.weight << 24 | (unsigned long long)(font.size * 100.f);
    auto& slot = formats_[key];
    if (!slot && fonts.factory) {
        fonts.factory->CreateTextFormat(Fonts::family(font.face), fonts.collection, DWRITE_FONT_WEIGHT(font.weight),
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, font.size, L"zh-cn", &slot);
        if (slot) { slot->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP); slot->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING); }
    }
    return slot;
}

IDWriteTextLayout* Canvas::layout(const std::wstring& text, const Font& font) {
    std::wstring key = text;
    key.push_back(L'\x1f'); key += std::to_wstring(int(font.face)) + L":" + std::to_wstring(font.weight) + L":" +
        std::to_wstring(int(font.size * 100)) + L":" + std::to_wstring(int(font.tracking * 100));
    auto found = layouts_.find(key);
    if (found != layouts_.end()) return found->second;
    IDWriteTextFormat* f = format(font);
    IDWriteTextLayout* result = nullptr;
    if (!f || FAILED(fonts.factory->CreateTextLayout(text.c_str(), UINT32(text.size()), f, 100000.f, 1000.f, &result))) return nullptr;
    if (font.tracking != 0.f) {
        IDWriteTextLayout1* l1 = nullptr;
        if (SUCCEEDED(result->QueryInterface(__uuidof(IDWriteTextLayout1), reinterpret_cast<void**>(&l1)))) {
            l1->SetCharacterSpacing(0.f, font.tracking, 0.f, DWRITE_TEXT_RANGE{0, UINT32(text.size())});
            l1->Release();
        }
    }
    layouts_[key] = result;
    return result;
}

float Canvas::textWidth(const std::wstring& text, const Font& font) {
    if (text.empty()) return 0.f;
    IDWriteTextLayout* l = layout(text, font);
    DWRITE_TEXT_METRICS m{};
    if (!l || FAILED(l->GetMetrics(&m))) return 0.f;
    // Tracking is applied after the last glyph too; CSS letter-spacing is likewise trailing.
    return m.widthIncludingTrailingWhitespace;
}

void Canvas::text(const std::wstring& value, const Font& font, float x, float centerY, Color c, Align align, float maxWidth) {
    if (muted_ || value.empty() || c.a <= 0 || !target_) return;
    std::wstring shown = value;
    float width = textWidth(shown, font);
    if (maxWidth > 0 && width > maxWidth) {
        while (!shown.empty() && textWidth(shown + L"…", font) > maxWidth) shown.pop_back();
        shown += L"…"; width = textWidth(shown, font);
    }
    IDWriteTextLayout* l = layout(shown, font);
    if (!l) return;
    DWRITE_LINE_METRICS lm{}; UINT32 lines = 0;
    l->GetLineMetrics(&lm, 1, &lines);
    const float shift = baselineShift_[font.face == Face::Mono ? 0 : 1];
    const float baseline = centerY + shift * font.size;
    float left = x;
    if (align == Align::Center) left = x - width / 2.f;
    else if (align == Align::Right) left = x - width;
    target_->DrawTextLayout(D2D1::Point2F(left, baseline - lm.baseline), l, brush(c), D2D1_DRAW_TEXT_OPTIONS_NONE);
}

} // namespace dafclient::gfx
