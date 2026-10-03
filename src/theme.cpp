// Voxual theme engine implementation.
#include "theme.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "imgui_internal.h"

namespace theme {

Palette g_pal;
Options g_opt;
float g_scale = 1.f;
ImFont *fRegular = nullptr, *fBold = nullptr, *fMono = nullptr, *fIcon = nullptr;

// ---------------------------------------------------------------------------
// palettes
// ---------------------------------------------------------------------------
namespace {

// Voxual Dark is the struct default; the others override what makes them different.
const Palette kDark{};

const Palette kMidnight{
    .name = "Midnight", .bg = 0x080A12, .bgAlt = 0x121735, .sidebar = 0x0A0D18, .panel = 0x111527, .panelHi = 0x181D36,
    .panelSoft = 0x141930, .field = 0x090C16, .fieldHover = 0x0E1220, .sunken = 0x05070D, .sunkenSoft = 0x080A12,
    .track = 0x1B2140, .rowHover = 0x141A31, .rowActive = 0x1A2140, .border = 0x222A4A, .borderHi = 0x364070,
    .borderSoft = 0x181E37, .text = 0xE7EBFA, .dim = 0x98A2C6, .mute = 0x6E779B, .faint = 0x4C5474,
    .onAccent = 0x08091A, .scrollThumb = 0x2A3358, .scrollThumbHover = 0x3C4878, .green = 0x9ECE6A, .blue = 0x7AA2F7,
    .violet = 0xBB9AF7, .teal = 0x7DCFFF, .codeKey = 0x7AA2F7, .codeStr = 0x9ECE6A, .codeNum = 0xFF9E64,
    .codeKw = 0xBB9AF7, .codeCom = 0x565F89, .codePunct = 0x89DDFF, .codeDef = 0xC0CAF5, .codeSec = 0xE0AF68,
};

const Palette kGraphite{
    .name = "Graphite", .bg = 0x101112, .bgAlt = 0x1B1D1F, .sidebar = 0x141517, .panel = 0x1A1B1D, .panelHi = 0x242629,
    .panelSoft = 0x1F2123, .field = 0x121314, .fieldHover = 0x17181A, .sunken = 0x0B0C0C, .sunkenSoft = 0x0E0F10,
    .track = 0x2A2C2F, .rowHover = 0x212326, .rowActive = 0x2A2C30, .border = 0x2E3033, .borderHi = 0x45484D,
    .borderSoft = 0x242629, .text = 0xEDEEF0, .dim = 0xA0A3A9, .mute = 0x74777D, .faint = 0x54575C,
    .onAccent = 0x111111, .scrollThumb = 0x33363A, .scrollThumbHover = 0x474B50, .green = 0x6FCF97, .amber = 0xE8C468,
    .blue = 0x8AB4F8, .violet = 0xB9A3F0, .teal = 0x76C7C0, .shadow = 0.9f,
};

const Palette kDaylight{
    .name = "Daylight", .light = true, .bg = 0xF3F5F9, .bgAlt = 0xE3E9F4, .sidebar = 0xEBEFF6, .panel = 0xFFFFFF,
    .panelHi = 0xF5F7FC, .panelSoft = 0xF2F5FA, .field = 0xF7F9FC, .fieldHover = 0xEDF1F8, .sunken = 0xFAFBFD,
    .sunkenSoft = 0xF4F6FA, .track = 0xE2E7F0, .rowHover = 0xEFF3FA, .rowActive = 0xE3EAF9, .border = 0xD7DDE9,
    .borderHi = 0xB4BECF, .borderSoft = 0xE5E9F2, .text = 0x1B2231, .textStrong = 0x0A1020, .dim = 0x59637A,
    .mute = 0x838CA1, .faint = 0xA7AFC0, .onAccent = 0xFFFFFF, .onDanger = 0xFFFFFF, .scrollThumb = 0xC7CEDC,
    .scrollThumbHover = 0xADB6C8, .green = 0x0E9F6E, .amber = 0xB45309, .red = 0xDC2626, .blue = 0x2563EB,
    .violet = 0x7C3AED, .teal = 0x0D9488, .pink = 0xDB2777, .orange = 0xEA580C, .codeKey = 0x1D4ED8,
    .codeStr = 0x15803D, .codeNum = 0xB45309, .codeKw = 0x7C3AED, .codeCom = 0x94A0B4, .codePunct = 0x64748B,
    .codeDef = 0x1F2937, .codeSec = 0xA16207, .shadow = 0.5f, .glow = 0.45f,
};

const Palette kPaper{
    .name = "Paper", .light = true, .bg = 0xF7F4EE, .bgAlt = 0xF0E7D8, .sidebar = 0xF1EDE4, .panel = 0xFFFDF8,
    .panelHi = 0xFAF6EC, .panelSoft = 0xF8F4EA, .field = 0xFFFDF9, .fieldHover = 0xF6F1E5, .sunken = 0xFCFAF4,
    .sunkenSoft = 0xF7F3E9, .track = 0xE8E0CF, .rowHover = 0xF6F1E5, .rowActive = 0xEFE6D3, .border = 0xE0D7C4,
    .borderHi = 0xC3B79E, .borderSoft = 0xEBE4D5, .text = 0x2A2620, .textStrong = 0x14110C, .dim = 0x6B6355,
    .mute = 0x8E8676, .faint = 0xB3AA97, .onAccent = 0xFFFFFF, .onDanger = 0xFFFFFF, .scrollThumb = 0xD8CEB8,
    .scrollThumbHover = 0xC0B49A, .green = 0x3F7D3F, .amber = 0xA96A12, .red = 0xC0392B, .blue = 0x2C5F8A,
    .violet = 0x7A4F9E, .teal = 0x2A7B76, .pink = 0xB03A6B, .orange = 0xC96A18, .codeKey = 0x2C5F8A,
    .codeStr = 0x3F7D3F, .codeNum = 0xA96A12, .codeKw = 0x7A4F9E, .codeCom = 0xA79C88, .codePunct = 0x7C7361,
    .codeDef = 0x2A2620, .codeSec = 0x8A6A1F, .shadow = 0.45f, .glow = 0.4f,
};

const Palette kContrast{
    .name = "High contrast", .bg = 0x000000, .bgAlt = 0x101010, .sidebar = 0x050505, .panel = 0x0C0C0C, .panelHi = 0x1A1A1A,
    .panelSoft = 0x141414, .field = 0x000000, .fieldHover = 0x141414, .sunken = 0x000000, .sunkenSoft = 0x080808,
    .track = 0x2E2E2E, .rowHover = 0x1C1C1C, .rowActive = 0x2A2A2A, .border = 0x5A5A5A, .borderHi = 0x9A9A9A,
    .borderSoft = 0x3A3A3A, .text = 0xFFFFFF, .textStrong = 0xFFFFFF, .dim = 0xD6D6D6, .mute = 0xAFAFAF,
    .faint = 0x8A8A8A, .onAccent = 0x000000, .onDanger = 0x000000, .scrollThumb = 0x6A6A6A, .scrollThumbHover = 0x9A9A9A,
    .shadow = 0.f, .glow = 0.f,
};

const Palette* const kPalettes[] = {&kDark, &kMidnight, &kGraphite, &kDaylight, &kPaper, &kContrast};
constexpr int kPaletteCount = (int)(sizeof kPalettes / sizeof kPalettes[0]);

struct AccentDef {
    const char* name;
    unsigned rgb;
};
const AccentDef kAccents[] = {{"Emerald", 0x34D399}, {"Teal", 0x2DD4BF},  {"Cyan", 0x22D3EE},  {"Sky", 0x38BDF8},
                              {"Azure", 0x60A5FA},   {"Indigo", 0x818CF8}, {"Violet", 0xA78BFA}, {"Magenta", 0xE879F9},
                              {"Rose", 0xFB7185},    {"Coral", 0xFB923C},  {"Amber", 0xFBBF24},  {"Lime", 0xA3E635}};
constexpr int kAccentCount = (int)(sizeof kAccents / sizeof kAccents[0]);

unsigned g_accentRgb = 0x34D399;

// Relative luminance, used to pick a foreground that stays readable on the accent colour.
float luminance(unsigned x) {
    auto lin = [](float v) { return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    float r = lin(((x >> 16) & 255) / 255.f), g = lin(((x >> 8) & 255) / 255.f), b = lin((x & 255) / 255.f);
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

// Text/icons drawn on top of an accent fill: near-black on bright accents, white on dark ones.
unsigned readable(unsigned c, unsigned) { return luminance(c) > 0.35f ? 0x08130Eu : 0xFFFFFFu; }

}  // namespace

int PaletteCount() { return kPaletteCount; }
const Palette& PaletteAt(int i) { return *kPalettes[std::clamp(i, 0, kPaletteCount - 1)]; }
const char* PaletteName(int i) { return kPalettes[std::clamp(i, 0, kPaletteCount - 1)]->name; }
bool PaletteIsLight(int i) { return kPalettes[std::clamp(i, 0, kPaletteCount - 1)]->light; }
void SetPalette(int i) { g_pal = *kPalettes[std::clamp(i, 0, kPaletteCount - 1)]; }

int AccentCount() { return kAccentCount; }
const char* AccentName(int i) { return kAccents[std::clamp(i, 0, kAccentCount - 1)].name; }
unsigned AccentSwatch(int i) { return kAccents[std::clamp(i, 0, kAccentCount - 1)].rgb; }
unsigned AccentRgb() { return g_accentRgb; }

void SetAccent(int i, bool custom, unsigned customRgb) {
    g_opt.accent = std::clamp(i, 0, kAccentCount - 1);
    g_opt.customAccent = custom;
    if (custom) g_opt.customAccentRgb = customRgb & 0xFFFFFF;
    g_accentRgb = custom ? (customRgb & 0xFFFFFF) : AccentSwatch(g_opt.accent);
}

// ---------------------------------------------------------------------------
// fonts
// ---------------------------------------------------------------------------
namespace {
std::string userFontDir() {
    char buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", buf, MAX_PATH);
    return n ? std::string(buf, n) + "\\Microsoft\\Windows\\Fonts\\" : std::string();
}

bool fileExists(const std::string& p) {
    if (p.empty()) return false;
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

struct Family {
    const char* label;
    const char* regular[3];
    const char* bold[3];
};

const Family kFamilies[] = {
    {"Segoe UI Variable", {"C:/Windows/Fonts/SegUIVar.ttf", nullptr, nullptr}, {"C:/Windows/Fonts/SegUIVar.ttf", nullptr, nullptr}},
    {"Segoe UI", {"C:/Windows/Fonts/segoeui.ttf", nullptr, nullptr}, {"C:/Windows/Fonts/segoeuib.ttf", nullptr, nullptr}},
    {"Inter", {"C:/Windows/Fonts/Inter-Regular.ttf", nullptr, nullptr}, {"C:/Windows/Fonts/Inter-SemiBold.ttf", nullptr, nullptr}},
    {"Bahnschrift", {"C:/Windows/Fonts/bahnschrift.ttf", nullptr, nullptr}, {"C:/Windows/Fonts/bahnschrift.ttf", nullptr, nullptr}},
    {"Calibri", {"C:/Windows/Fonts/calibri.ttf", nullptr, nullptr}, {"C:/Windows/Fonts/calibrib.ttf", nullptr, nullptr}},
    {"Trebuchet MS", {"C:/Windows/Fonts/trebuc.ttf", nullptr, nullptr}, {"C:/Windows/Fonts/trebucbd.ttf", nullptr, nullptr}},
    {"Verdana", {"C:/Windows/Fonts/verdana.ttf", nullptr, nullptr}, {"C:/Windows/Fonts/verdanab.ttf", nullptr, nullptr}},
    {"Tahoma", {"C:/Windows/Fonts/tahoma.ttf", nullptr, nullptr}, {"C:/Windows/Fonts/tahomabd.ttf", nullptr, nullptr}},
    {"Arial", {"C:/Windows/Fonts/arial.ttf", nullptr, nullptr}, {"C:/Windows/Fonts/arialbd.ttf", nullptr, nullptr}},
};
constexpr int kFamilyCount = (int)(sizeof kFamilies / sizeof kFamilies[0]);

struct MonoFamily {
    const char* label;
    const char* regular[3];
    const char* bold[3];
};
const MonoFamily kMonoFamilies[] = {
    {"Cascadia Mono", {"C:/Windows/Fonts/CascadiaMono.ttf", nullptr, nullptr}, {"C:/Windows/Fonts/CascadiaMono.ttf", nullptr, nullptr}},
    {"Consolas", {"C:/Windows/Fonts/consola.ttf", nullptr, nullptr}, {"C:/Windows/Fonts/consolab.ttf", nullptr, nullptr}},
    {"Cascadia Code", {"C:/Windows/Fonts/CascadiaCode.ttf", nullptr, nullptr}, {"C:/Windows/Fonts/CascadiaCode.ttf", nullptr, nullptr}},
    {"JetBrains Mono", {"C:/Windows/Fonts/JetBrainsMono-Regular.ttf", nullptr, nullptr}, {"C:/Windows/Fonts/JetBrainsMono-Bold.ttf", nullptr, nullptr}},
    {"Fira Code", {"C:/Windows/Fonts/FiraCode-Regular.ttf", nullptr, nullptr}, {"C:/Windows/Fonts/FiraCode-Bold.ttf", nullptr, nullptr}},
    {"Lucida Console", {"C:/Windows/Fonts/lucon.ttf", nullptr, nullptr}, {"C:/Windows/Fonts/lucon.ttf", nullptr, nullptr}},
    {"Courier New", {"C:/Windows/Fonts/cour.ttf", nullptr, nullptr}, {"C:/Windows/Fonts/courbd.ttf", nullptr, nullptr}},
};
constexpr int kMonoCount = (int)(sizeof kMonoFamilies / sizeof kMonoFamilies[0]);

std::string resolve(const char* const paths[3]) {
    std::string user = userFontDir();
    for (int i = 0; i < 3; ++i) {
        if (!paths[i]) break;
        if (fileExists(paths[i])) return paths[i];
        // fonts installed for the current user only
        std::string name = paths[i];
        size_t slash = name.find_last_of('/');
        if (!user.empty() && slash != std::string::npos) {
            std::string alt = user + name.substr(slash + 1);
            if (fileExists(alt)) return alt;
        }
    }
    return {};
}

ImFont* addFont(const std::string& path, float px, ImFont* fallback) {
    if (path.empty()) return fallback;
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH = true;
    ImFont* f = ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), px, &cfg);
    return f ? f : fallback;
}
}  // namespace

int FontFamilyCount() { return kFamilyCount; }
const char* FontFamilyName(int i) { return kFamilies[std::clamp(i, 0, kFamilyCount - 1)].label; }
bool FontFamilyInstalled(int i) {
    int k = std::clamp(i, 0, kFamilyCount - 1);
    return !resolve(kFamilies[k].regular).empty();
}
int MonoFamilyCount() { return kMonoCount; }
const char* MonoFamilyName(int i) { return kMonoFamilies[std::clamp(i, 0, kMonoCount - 1)].label; }
bool MonoFamilyInstalled(int i) {
    int k = std::clamp(i, 0, kMonoCount - 1);
    return !resolve(kMonoFamilies[k].regular).empty();
}

// Cheap key of everything that affects the atlas, so unrelated settings changes
// (colours, density, radius, background...) never touch the fonts.
unsigned FontKey(const Options& opt) {
    auto q = [](float v) { return (unsigned)(v * 1000.f + 0.5f); };
    return (unsigned)opt.fontFamily * 2654435761u ^ (unsigned)opt.monoFamily * 40503u ^ q(opt.textScale) * 2246822519u ^
           q(opt.monoScale) * 3266489917u ^ q(g_scale) * 668265263u;
}

static bool g_fontsPending = false;   // set when a rebuild had to be deferred out of a frame

bool FontsPending() { return g_fontsPending; }

void LoadFonts(const Options& opt) {
    static unsigned builtKey = 0;
    static bool built = false;
    // ImFontAtlas::Clear() is documented as "Don't call mid-frame!" - refuse to touch the
    // atlas inside a frame and let the caller retry from between frames instead.
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (ctx && ctx->WithinFrameScope) {
        g_fontsPending = true;
        return;
    }
    const unsigned key = FontKey(opt);
    if (built && key == builtKey) {
        g_fontsPending = false;
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFont* def = io.Fonts->AddFontDefault();
    const float base = 16.f * g_scale;
    int fam = std::clamp(opt.fontFamily, 0, kFamilyCount - 1);
    std::string reg = resolve(kFamilies[fam].regular), bold = resolve(kFamilies[fam].bold);
    fRegular = addFont(reg, base, def);
    fBold = addFont(bold.empty() ? reg : bold, base, fRegular);
    int mono = std::clamp(opt.monoFamily, 0, kMonoCount - 1);
    std::string mon = resolve(kMonoFamilies[mono].regular);
    fMono = addFont(mon.empty() ? "C:/Windows/Fonts/consola.ttf" : mon, base, fRegular);
    // Segoe Fluent Icons ships with Windows 11; Segoe MDL2 Assets covers Windows 10.
    std::string icons = fileExists("C:/Windows/Fonts/SegoeIcons.ttf") ? "C:/Windows/Fonts/SegoeIcons.ttf"
                                                                     : "C:/Windows/Fonts/segmdl2.ttf";
    fIcon = addFont(icons, base, fRegular);
    io.Fonts->Build();
    builtKey = key;
    built = true;
    g_fontsPending = false;
}

// ---------------------------------------------------------------------------
// style
// ---------------------------------------------------------------------------
void SetupStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    const float dens = DensityScale();
    s = ImGuiStyle();
    s.WindowPadding = ImVec2(S(16) * dens, S(16) * dens);
    s.FramePadding = ImVec2(S(12) * dens, S(9) * dens);
    s.ItemSpacing = ImVec2(S(10) * dens, S(10) * dens);
    s.ItemInnerSpacing = ImVec2(S(8) * dens, S(6) * dens);
    s.ScrollbarSize = S(10);
    s.ScrollbarRounding = S(9);
    s.GrabMinSize = S(16);
    s.GrabRounding = S(8);
    s.FrameRounding = RD(10);
    s.WindowRounding = RD(16);
    s.ChildRounding = RD(16);
    s.PopupRounding = RD(14);
    s.TabRounding = RD(9);
    s.FrameBorderSize = 1.f;
    s.WindowBorderSize = 0.f;
    s.PopupBorderSize = 1.f;
    s.ChildBorderSize = 1.f;
    s.SeparatorTextBorderSize = 1.f;
    s.DisabledAlpha = 0.42f;
    s.AntiAliasedLines = true;
    s.AntiAliasedFill = true;
    s.CircleTessellationMaxError = 0.2f;
    s.CurveTessellationTol = 0.5f;

    ImVec4* c = s.Colors;
    auto V = [](unsigned rgb, float a = 1.f) {
        return ImVec4(((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, a);
    };
    const Palette& p = g_pal;
    c[ImGuiCol_Text] = V(p.text);
    c[ImGuiCol_TextDisabled] = V(p.mute);
    c[ImGuiCol_WindowBg] = V(p.bg);
    c[ImGuiCol_ChildBg] = V(p.panel);
    c[ImGuiCol_PopupBg] = V(p.panelSoft);
    c[ImGuiCol_Border] = V(p.border);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = V(p.field);
    c[ImGuiCol_FrameBgHovered] = V(p.fieldHover);
    c[ImGuiCol_FrameBgActive] = V(p.fieldHover);
    c[ImGuiCol_TitleBg] = V(p.sidebar);
    c[ImGuiCol_TitleBgActive] = V(p.sidebar);
    c[ImGuiCol_MenuBarBg] = V(p.panel);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = V(p.scrollThumb);
    c[ImGuiCol_ScrollbarGrabHovered] = V(p.scrollThumbHover);
    c[ImGuiCol_ScrollbarGrabActive] = V(p.borderHi);
    c[ImGuiCol_CheckMark] = V(AccentRgb());
    c[ImGuiCol_SliderGrab] = V(AccentRgb());
    c[ImGuiCol_SliderGrabActive] = V(AccentRgb());
    c[ImGuiCol_Button] = V(p.panelHi);
    c[ImGuiCol_ButtonHovered] = V(p.rowActive);
    c[ImGuiCol_ButtonActive] = V(p.border);
    c[ImGuiCol_Header] = V(p.rowActive);
    c[ImGuiCol_HeaderHovered] = V(p.rowHover);
    c[ImGuiCol_HeaderActive] = V(p.rowActive);
    c[ImGuiCol_Separator] = V(p.border);
    c[ImGuiCol_SeparatorHovered] = V(p.borderHi);
    c[ImGuiCol_SeparatorActive] = V(AccentRgb());
    c[ImGuiCol_ResizeGrip] = V(p.border);
    c[ImGuiCol_ResizeGripHovered] = V(p.borderHi);
    c[ImGuiCol_ResizeGripActive] = V(AccentRgb());
    c[ImGuiCol_TextSelectedBg] = V(AccentRgb(), 0.32f);
    c[ImGuiCol_DragDropTarget] = V(AccentRgb());
    c[ImGuiCol_NavCursor] = V(AccentRgb());
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(p.light ? 0.35f : 0.02f, p.light ? 0.38f : 0.03f, p.light ? 0.45f : 0.05f, p.light ? 0.45f : 0.72f);
}

// Palette, accents and metrics. Touches no atlas state, so it is safe from anywhere.
void ApplyStyle(const Options& opt) {
    g_opt = opt;
    SetPalette(opt.palette);
    SetAccent(opt.accent, opt.customAccent, opt.customAccentRgb);
    // Colour-coded surfaces (accents on dark/light) keep enough contrast to stay readable.
    g_pal.onAccent = readable(AccentRgb(), g_pal.panel);
    SetupStyle();
}

void Apply(const Options& opt) {
    ApplyStyle(opt);
    LoadFonts(g_opt);   // no-op unless the font family or size actually changed
}

// ---------------------------------------------------------------------------
// backdrops
// ---------------------------------------------------------------------------
const char* BgStyleName(int i) {
    switch ((BgStyle)std::clamp(i, 0, 4)) {
    case BgStyle::Solid: return "Solid";
    case BgStyle::Grid: return "Grid";
    case BgStyle::Dots: return "Dots";
    case BgStyle::Voxels: return "Voxels";
    default: return "Gradient";
    }
}

// Palette entries are plain 0xRRGGBB literals; ImGui wants IM_COL32 (0xAABBGGRR).
// Convert exactly once, at the point of drawing, and keep the maths in 0xRRGGBB.
static ImU32 packRgb(unsigned rgb, float alpha = 1.f) {
    return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, (int)(255.f * std::clamp(alpha, 0.f, 1.f) + 0.5f));
}

static unsigned mixRgb(unsigned a, unsigned b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    auto ch = [&](int sh) {
        float x = (float)((a >> sh) & 255), y = (float)((b >> sh) & 255);
        return (unsigned)(x + (y - x) * t + 0.5f);
    };
    return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

// Resolved top colour of the backdrop - exposed so --dump-theme can report it.
unsigned DebugBackdropTop() {
    const Palette& p = g_pal;
    const float strength = std::clamp(g_opt.bgStrength, 0.f, 1.5f);
    if ((BgStyle)g_opt.bgStyle == BgStyle::Gradient && strength > 0.001f)
        return mixRgb(p.bgAlt, AccentRgb(), (p.light ? 0.08f : 0.12f) * strength);
    return p.bg;
}

void DrawBackdrop(ImDrawList* dl, ImVec2 pos, ImVec2 size) {
    const Palette& p = g_pal;
    const float strength = std::clamp(g_opt.bgStrength, 0.f, 1.5f);
    const BgStyle style = (BgStyle)g_opt.bgStyle;
    const ImVec2 q(pos.x + size.x, pos.y + size.y);
    dl->AddRectFilled(pos, q, packRgb(p.bg));

    if (style == BgStyle::Solid || strength <= 0.001f) return;

    if (style == BgStyle::Gradient) {
        unsigned top = mixRgb(p.bgAlt, AccentRgb(), (p.light ? 0.08f : 0.12f) * strength);
        dl->AddRectFilledMultiColor(pos, q, packRgb(top), packRgb(top), packRgb(p.bg), packRgb(p.bg));
        return;
    }

    dl->PushClipRect(pos, q, true);
    if (style == BgStyle::Grid) {
        const float step = S(34);
        const ImU32 line = packRgb(AccentRgb(), (p.light ? 0.10f : 0.13f) * strength);
        for (float x = pos.x; x < q.x; x += step) dl->AddLine(ImVec2(std::floor(x), pos.y), ImVec2(std::floor(x), q.y), line, 1.f);
        for (float y = pos.y; y < q.y; y += step) dl->AddLine(ImVec2(pos.x, std::floor(y)), ImVec2(q.x, std::floor(y)), line, 1.f);
    } else if (style == BgStyle::Dots) {
        const float step = S(30);
        const ImU32 dot = packRgb(AccentRgb(), (p.light ? 0.16f : 0.20f) * strength);
        for (float y = pos.y + step * 0.5f; y < q.y; y += step)
            for (float x = pos.x + step * 0.5f; x < q.x; x += step) dl->AddCircleFilled(ImVec2(x, y), S(1.4f), dot, 8);
    } else if (style == BgStyle::Voxels) {
        // faint isometric cubes, echoing the logo
        const float step = S(64);
        const ImU32 face = packRgb(AccentRgb(), (p.light ? 0.05f : 0.06f) * strength);
        const ImU32 edge = packRgb(AccentRgb(), (p.light ? 0.07f : 0.09f) * strength);
        int row = 0;
        for (float y = pos.y - step; y < q.y + step; y += step * 0.86f, ++row) {
            float off = (row % 2) ? step * 0.5f : 0.f;
            for (float x = pos.x - step + off; x < q.x + step; x += step) {
                ImVec2 c(x + step * 0.5f, y + step * 0.5f);
                ImVec2 mid(c.x, c.y + step * 0.5f);
                dl->AddTriangleFilled(ImVec2(c.x, c.y), ImVec2(mid.x - step * 0.433f, mid.y - step * 0.25f), ImVec2(mid.x, mid.y), face);
                dl->AddTriangleFilled(ImVec2(c.x, c.y), ImVec2(mid.x + step * 0.433f, mid.y - step * 0.25f), ImVec2(mid.x, mid.y), face);
                dl->AddLine(ImVec2(c.x, c.y), mid, edge, 1.f);
                dl->AddLine(ImVec2(mid.x - step * 0.433f, mid.y - step * 0.25f), mid, edge, 1.f);
                dl->AddLine(ImVec2(mid.x + step * 0.433f, mid.y - step * 0.25f), mid, edge, 1.f);
            }
        }
    }
    dl->PopClipRect();
}

void DrawPageGlow(ImDrawList* dl, ImVec2 a, ImVec2 b) {
    if (!g_opt.backdropGlow || g_pal.glow <= 0.001f) return;
    const float s = g_pal.glow;
    ImU32 top = packRgb(AccentRgb(), (g_pal.light ? 0.05f : 0.09f) * s), clear = packRgb(AccentRgb(), 0.f);
    dl->AddRectFilledMultiColor(a, b, top, top, clear, clear);
    dl->AddRectFilledMultiColor(a, ImVec2(a.x + (b.x - a.x) * 0.6f, a.y + (b.y - a.y) * 0.7f), top, clear, clear, clear);
}

}  // namespace theme
