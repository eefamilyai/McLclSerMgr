#pragma once
// Voxual theme engine: runtime palettes, typography, metrics and backdrop painting.
// Everything the personalization page can change lives here.
#include <string>

#include "imgui.h"

namespace theme {

// ---------------------------------------------------------------------------
// Palette - every colour the UI may draw with, so light themes are possible.
// Field defaults are the "Voxual Dark" look, so a preset only overrides what
// makes it different.
// ---------------------------------------------------------------------------
struct Palette {
    const char* name = "Voxual Dark";
    bool light = false;

    unsigned bg = 0x0B0E14;            // app background
    unsigned bgAlt = 0x111726;         // backdrop gradient second stop
    unsigned sidebar = 0x0E121A;       // navigation rail
    unsigned panel = 0x141924;         // card surface
    unsigned panelHi = 0x1A2130;       // hovered / raised surface
    unsigned panelSoft = 0x171D28;     // menus, subtle rows
    unsigned field = 0x0C1017;         // input background
    unsigned fieldHover = 0x11161F;
    unsigned sunken = 0x070A0F;        // console + code editor
    unsigned sunkenSoft = 0x0A0E15;    // list wells inside cards
    unsigned track = 0x1E2532;         // progress / slider track
    unsigned rowHover = 0x18202C;
    unsigned rowActive = 0x1E2836;

    unsigned border = 0x242C3B;
    unsigned borderHi = 0x39445C;
    unsigned borderSoft = 0x1B2230;

    unsigned text = 0xE8ECF4;
    unsigned textStrong = 0xFFFFFF;
    unsigned dim = 0x98A2B8;
    unsigned mute = 0x6B7488;
    unsigned faint = 0x4A5364;
    unsigned onAccent = 0x06140E;      // text/icons drawn on top of the accent
    unsigned onDanger = 0x1A0708;
    unsigned scrollThumb = 0x2A3242;
    unsigned scrollThumbHover = 0x3A4560;

    unsigned green = 0x3DDC97, amber = 0xF5B942, red = 0xF87171, blue = 0x62A9FF, violet = 0xA78BFA, teal = 0x2DD4BF,
             pink = 0xF472B6, orange = 0xFB923C;

    // config editor syntax
    unsigned codeKey = 0x7AA2F7, codeStr = 0x9ECE6A, codeNum = 0xFF9E64, codeKw = 0xBB9AF7, codeCom = 0x6B7899,
             codePunct = 0x8691AB, codeDef = 0xD3DAEA, codeSec = 0xE0AF68;

    float shadow = 1.f;   // shadow strength multiplier
    float glow = 1.f;     // accent glow strength multiplier
};

// ---------------------------------------------------------------------------
// Metric / typography settings (set from the app's personalization settings)
// ---------------------------------------------------------------------------
enum class Density { Compact = 0, Comfortable = 1, Spacious = 2 };
enum class BgStyle { Solid = 0, Gradient = 1, Grid = 2, Dots = 3, Voxels = 4 };

struct Options {
    int palette = 0;             // index into PaletteCount()
    int accent = 0;              // index into AccentCount()
    bool customAccent = false;
    unsigned customAccentRgb = 0x34D399;
    int density = (int)Density::Comfortable;
    float radius = 1.f;          // corner rounding multiplier
    float textScale = 1.f;       // body text multiplier
    float monoScale = 1.f;       // console / editor text multiplier
    float animSpeed = 1.f;       // 0 disables animations
    bool shadows = true;         // drop shadows on cards and modals
    bool backdropGlow = true;    // accent wash behind page headers
    int bgStyle = (int)BgStyle::Gradient;
    float bgStrength = 1.f;
    int fontFamily = 0;          // index into FontFamilyCount()
    int monoFamily = 0;
};

extern Palette g_pal;            // active palette after runtime overrides
extern ImFont *fRegular, *fBold, *fMono, *fIcon;   // built by LoadFonts()
extern Options g_opt;
extern float g_scale;            // DPI scale - set it through SetScale()
void SetScale(float scale);      // clamps to a range the layout maths can survive

// --- palettes / accents ----------------------------------------------------
int PaletteCount();
const Palette& PaletteAt(int i);   // inspect a preset without switching to it
const char* PaletteName(int i);
bool PaletteIsLight(int i);
void SetPalette(int i);

int AccentCount();
const char* AccentName(int i);
unsigned AccentSwatch(int i);
void SetAccent(int i, bool custom = false, unsigned customRgb = 0);
unsigned AccentRgb();

// --- fonts -----------------------------------------------------------------
int FontFamilyCount();
const char* FontFamilyName(int i);
bool FontFamilyInstalled(int i);
int MonoFamilyCount();
const char* MonoFamilyName(int i);
bool MonoFamilyInstalled(int i);

void LoadFonts(const Options& opt);   // (re)build the font atlas - between frames only
void ApplyStyle(const Options& opt);  // palette + metrics, safe to call inside a frame
bool FontsPending();                  // true when a rebuild was deferred out of a frame
unsigned FontKey(const Options& opt); // what the atlas depends on
void SetupStyle();                    // (re)apply style from the active theme
void Apply(const Options& opt);       // full theme switch: palette + fonts + style

// --- scales ----------------------------------------------------------------
inline float S(float v) { return v * g_scale; }                              // DPI only
inline float FS(float px) {                                                  // font size
    float v = px * 1.2f * g_opt.textScale * g_scale;
    return (float)(int)(v + 0.5f);
}
inline float DensityScale() { return 0.82f + 0.18f * (float)g_opt.density; }
inline float D(float v) { return v * g_scale * DensityScale(); }             // spacing / padding
inline float HS(float v) {                                                   // fixed component heights
    float f = g_opt.textScale > DensityScale() ? g_opt.textScale : DensityScale();
    return v * g_scale * (f < 1.f ? 1.f : f);
}
inline float RD(float v) { return v * g_scale * g_opt.radius; }              // corner rounding
inline float Mono(float px) {
    float v = px * 1.2f * g_opt.monoScale * g_scale;
    return (float)(int)(v + 0.5f);
}

// --- painting ---------------------------------------------------------------
void DrawBackdrop(ImDrawList* dl, ImVec2 pos, ImVec2 size);   // app background for the chosen style
void DrawPageGlow(ImDrawList* dl, ImVec2 a, ImVec2 b);        // accent wash behind a page header
const char* BgStyleName(int i);
unsigned DebugBackdropTop();   // resolved backdrop colour, for --dump-theme

}  // namespace theme
