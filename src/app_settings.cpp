// The Personalize page: every look-and-feel option for Voxual itself.
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <thread>

#include "app.h"
#include "nlohmann/json.hpp"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "java.h"
#include "version.h"

using namespace ui;

namespace {
const char* kSectionNames[6] = {"Appearance", "Interface", "Servers page", "Console & editor", "Storage & Java", "About"};
const char* kSectionIcons[6] = {icon::Palette, icon::Window, icon::Grid, icon::Console, icon::Storage, icon::Info};
const char* kSectionBlurb[6] = {"Theme, accent, fonts, background",
                                "Density, sidebar, toasts, clock",
                                "How your server list is laid out",
                                "Log lines, timestamps, wrapping",
                                "Where servers live, Java runtimes",
                                "Version, credits, shortcuts"};

bool vnavItem(int i, int* cur, float width) {
    ImGui::PushID(i);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = HS(48);
    bool clicked = ImGui::InvisibleButton("##vnav", ImVec2(width, h));
    bool hov = ImGui::IsItemHovered();
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    bool sel = *cur == i;
    float a = Anim(ImGui::GetItemID() ^ 0x9911, sel ? 1.f : (hov ? 0.5f : 0.f), 16.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 q(p.x + width, p.y + h);
    if (a > 0.01f) dl->AddRectFilled(p, q, Fade(Mix(Alpha(RGBA(col::rowHover), 0.f), sel ? Accent(0.14f) : RGBA(col::rowHover), a)), RD(11));
    if (sel) dl->AddRectFilled(ImVec2(p.x, p.y + h * 0.26f), ImVec2(p.x + S(3.5f), q.y - h * 0.26f), Fade(Accent()), S(2));
    DrawIcon(dl, ImVec2(p.x + D(22), p.y + h * 0.5f), kSectionIcons[i], 16.f, Mix(RGBA(col::dim), sel ? Accent() : RGBA(col::text), std::min(1.f, a * 1.2f)));
    DrawStr(dl, fBold, 13.5f, ImVec2(p.x + D(42), p.y + S(9)), Mix(RGBA(col::dim), RGBA(col::text), std::min(1.f, a * 1.2f)), kSectionNames[i]);
    DrawStr(dl, fRegular, 11.5f, ImVec2(p.x + D(42), p.y + S(26)), RGBA(col::mute), kSectionBlurb[i]);
    ImGui::PopID();
    if (clicked) *cur = i;
    return clicked;
}

// A labelled block of related rows inside a card.
template <class F>
void group(const char* title, F&& body) {
    Label(title, 13.f, col::mute, true);
    Gap(8);
    body();
    Gap(14);
}
}  // namespace

// ---------------------------------------------------------------------------
// Java runtime download (shared by the Storage tab)
// ---------------------------------------------------------------------------
void App::startJavaInstall(int major) {
    if (javaJob_.active.exchange(true)) return;
    javaJob_.major = major;
    javaJob_.cancel = false;
    javaJob_.progress = 0.f;
    {
        std::lock_guard<std::mutex> lk(javaJob_.mu);
        javaJob_.msg = "Starting...";
        javaJob_.err.clear();
    }
    std::thread([this, major] {
        std::string err;
        bool ok = java::installManaged(major,
                                       [this](float p, const std::string& m) {
                                           javaJob_.progress = p;
                                           std::lock_guard<std::mutex> lk(javaJob_.mu);
                                           javaJob_.msg = m;
                                       },
                                       &javaJob_.cancel, err);
        {
            std::lock_guard<std::mutex> lk(javaJob_.mu);
            javaJob_.err = ok ? "" : err;
        }
        javaJob_.active = false;
        if (ok) java::scan();
    }).detach();
}

// ---------------------------------------------------------------------------
// page
// ---------------------------------------------------------------------------
void App::drawPersonalizePage() {
    drawTopBar("Personalize", "Make Voxual look and behave the way you like. Everything is saved instantly.", [&] {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(4));
        if (Button("Reset", icon::Restart, Btn::Secondary, ImVec2(0, HS(38)))) {
            askConfirm({"Reset personalization?",
                        "Theme, fonts, density and layout options go back to their defaults. Your servers, folders and Java runtimes are untouched.",
                        "Reset", true, [this] { resetPersonalization(); }});
        }
        ImGui::SameLine(0, S(8));
        if (IconButton("exportTheme", icon::Upload, Btn::Secondary, 38, "Export theme to a file")) exportTheme();
        ImGui::SameLine(0, S(6));
        if (IconButton("importTheme", icon::Download, Btn::Secondary, 38, "Load a theme file")) importTheme();
    });

    float leftW = S(210);
    float totalH = ImGui::GetContentRegionAvail().y;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::BeginChild("##persnav", ImVec2(leftW, totalH), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    for (int i = 0; i < 6; ++i) {
        vnavItem(i, &persTab_, leftW - S(10));
        Gap(4);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::SameLine(0, S(22));
    BeginScroll("##persbody", ImVec2(0, totalH));
    float W = std::min(ImGui::GetContentRegionAvail().x - S(6), S(920));
    switch (persTab_) {
    case 0: personalizeAppearance(); break;
    case 1: personalizeInterface(); break;
    case 2: personalizeServersPage(); break;
    case 3: personalizeConsole(); break;
    case 4: personalizeStorage(); break;
    default: personalizeAbout(); break;
    }
    Gap(28);
    EndScroll();
}

// ---------------------------------------------------------------------------
// appearance
// ---------------------------------------------------------------------------
void App::personalizeAppearance() {
    float W = std::min(ImGui::GetContentRegionAvail().x - S(4), S(920));

    BeginCard("##theme", ImVec2(W, 0), 22);
    SectionTitle("Theme", "Colour scheme for the whole app.", icon::Palette);
    Gap(14);
    {
        float gap = S(12);
        int cols = W > S(720) ? 3 : 2;
        float cw = (W - D(44) - gap * (cols - 1)) / cols;
        ImVec2 origin = ImGui::GetCursorScreenPos();
        int n = theme::PaletteCount();
        for (int i = 0; i < n; ++i) {
            ImGui::SetCursorScreenPos(ImVec2(origin.x + (i % cols) * (cw + gap), origin.y + (i / cols) * (HS(88) + gap)));
            ImGui::PushID(i);
            ImVec2 p = ImGui::GetCursorScreenPos();
            bool clicked = ImGui::InvisibleButton("##theme", ImVec2(cw, HS(88)));
            bool hov = ImGui::IsItemHovered();
            if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            bool sel = settings_.theme == i;
            float hv = Anim(ImGui::GetItemID() ^ 0x5511, hov ? 1.f : 0.f, 16.f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 q(p.x + cw, p.y + HS(88));
            dl->AddRectFilled(p, q, Fade(RGBA(col::sunkenSoft)), RD(13));
            dl->AddRect(p, q, Fade(sel ? Accent() : Mix(RGBA(col::border), RGBA(col::borderHi), hv)), RD(13), 0, sel ? 1.8f : 1.f);
            // swatch strip: a miniature window drawn in that palette
            const theme::Palette& prev = theme::PaletteAt(i);
            ImU32 cbg = RGBA(prev.bg), cpanel = RGBA(prev.panel), cside = RGBA(prev.sidebar), ctext = RGBA(prev.text), cdim = RGBA(prev.dim),
                  cbord = RGBA(prev.border);
            float stripH = HS(40);
            ImVec2 sp(p.x + D(12), p.y + D(12));
            dl->AddRectFilled(sp, ImVec2(sp.x + cw - D(24), sp.y + stripH), cbg, RD(8));
            dl->AddRectFilled(sp, ImVec2(sp.x + S(34), sp.y + stripH), cside, RD(8), ImDrawFlags_RoundCornersLeft);
            dl->AddRectFilled(ImVec2(sp.x + S(40), sp.y + S(7)), ImVec2(sp.x + cw - D(32), sp.y + stripH - S(7)), cpanel, RD(6));
            dl->AddRect(ImVec2(sp.x + S(40), sp.y + S(7)), ImVec2(sp.x + cw - D(32), sp.y + stripH - S(7)), cbord, RD(6), 0, 1.f);
            dl->AddRectFilled(ImVec2(sp.x + S(46), sp.y + S(12)), ImVec2(sp.x + S(46) + (cw - D(92)) * 0.5f, sp.y + S(16)), ctext, S(2));
            dl->AddRectFilled(ImVec2(sp.x + S(46), sp.y + S(21)), ImVec2(sp.x + S(46) + (cw - D(92)) * 0.33f, sp.y + S(24)), cdim, S(2));
            dl->AddRectFilled(ImVec2(sp.x + S(46), sp.y + S(28)), ImVec2(sp.x + S(46) + S(28), sp.y + S(33)), RGBA(theme::AccentRgb()), S(2));
            DrawStr(dl, fBold, 13.5f, ImVec2(p.x + D(12), p.y + stripH + D(16)), RGBA(col::text), theme::PaletteName(i));
            if (theme::PaletteIsLight(i))
                DrawStr(dl, fRegular, 11.5f, ImVec2(p.x + D(12), p.y + stripH + D(33)), RGBA(col::mute), "Light");
            else
                DrawStr(dl, fRegular, 11.5f, ImVec2(p.x + D(12), p.y + stripH + D(33)), RGBA(col::mute), "Dark");
            if (sel) DrawIcon(dl, ImVec2(q.x - D(18), p.y + D(18)), icon::Check, 15.f, Accent());
            ImGui::PopID();
            if (clicked && !sel) {
                settings_.theme = i;
                applyPersonalization();
                saveSettings();
            }
        }
        int rows = (n + cols - 1) / cols;
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rows * (HS(88) + gap)));
        ImGui::Dummy(ImVec2(1, 1));
    }
    EndCard();
    Gap(14);

    BeginCard("##accent", ImVec2(W, 0), 22);
    SectionTitle("Accent colour", "Used for highlights, buttons and the tray of every card.", icon::Paint);
    Gap(12);
    {
        for (int i = 0; i < theme::AccentCount(); ++i) {
            ImGui::PushID(i);
            if (i) ImGui::SameLine(0, S(10));
            bool sel = !settings_.customAccent && settings_.accent == i;
            if (ColorSwatch("acc", theme::AccentSwatch(i), sel, 34)) {
                settings_.customAccent = false;
                settings_.accent = i;
                applyPersonalization();
                saveSettings();
            }
            Tooltip(theme::AccentName(i));
            ImGui::PopID();
        }
        Gap(12);
        float rgb[3] = {((settings_.customAccentRgb >> 16) & 255) / 255.f, ((settings_.customAccentRgb >> 8) & 255) / 255.f,
                        (settings_.customAccentRgb & 255) / 255.f};
        ImGui::PushFont(fRegular, FS(13.5f));
        ImGui::SetNextItemWidth(S(210));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(D(10), D(8)));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, RD(10));
        if (ImGui::ColorEdit3("##customacc", rgb, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaOpaque)) {
            settings_.customAccentRgb = ((unsigned)(rgb[0] * 255) << 16) | ((unsigned)(rgb[1] * 255) << 8) | (unsigned)(rgb[2] * 255);
            settings_.customAccent = true;
            applyPersonalization();
            saveSettings();
        }
        ImGui::PopStyleVar(2);
        ImGui::PopFont();
        ImGui::SameLine(0, S(12));
        bool custom = settings_.customAccent;
        if (Toggle("customacc", &custom)) {
            settings_.customAccent = custom;
            applyPersonalization();
            saveSettings();
        }
        ImGui::SameLine(0, S(10));
        Label("Use a custom colour", 14.f, col::text);
        if (settings_.customAccent) {
            ImGui::SameLine(0, S(12));
            char hex[16];
            snprintf(hex, sizeof hex, "#%06X", settings_.customAccentRgb);
            Chip(hex, Accent());
        }
    }
    EndCard();
    Gap(14);

    BeginCard("##font", ImVec2(W, 0), 22);
    SectionTitle("Typography", "Pick the interface font, the code font and how big they are.", icon::Font);
    Gap(14);
    group("Interface font", [&] {
        std::vector<std::string> names;
        for (int i = 0; i < theme::FontFamilyCount(); ++i) {
            std::string n = theme::FontFamilyName(i);
            if (!theme::FontFamilyInstalled(i)) n += "  (not installed)";
            names.push_back(n);
        }
        std::vector<const char*> ptrs;
        for (auto& n : names) ptrs.push_back(n.c_str());
        int cur = settings_.fontFamily;
        if (Combo("##fontfam", &cur, ptrs.data(), (int)ptrs.size(), 320)) {
            settings_.fontFamily = cur;
            applyPersonalization();
            saveSettings();
        }
        ImGui::SameLine(0, S(10));
        Label(theme::FontFamilyInstalled(settings_.fontFamily) ? "Installed" : "Not found - falls back to Segoe UI", 12.5f, col::mute);
    });
    group("Text size", [&] {
        float v = settings_.textScale * 100.f;
        if (SliderFloat("##textsize", &v, 85.f, 140.f, "%.0f%%", 320)) {
            settings_.textScale = v / 100.f;
            applyPersonalization();
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) saveSettings();
        ImGui::SameLine(0, S(10));
        Label("Everything in the app scales with this.", 12.5f, col::mute);
    });
    group("Code font", [&] {
        std::vector<std::string> names;
        for (int i = 0; i < theme::MonoFamilyCount(); ++i) {
            std::string n = theme::MonoFamilyName(i);
            if (!theme::MonoFamilyInstalled(i)) n += "  (not installed)";
            names.push_back(n);
        }
        std::vector<const char*> ptrs;
        for (auto& n : names) ptrs.push_back(n.c_str());
        int cur = settings_.monoFamily;
        if (Combo("##monofam", &cur, ptrs.data(), (int)ptrs.size(), 320)) {
            settings_.monoFamily = cur;
            applyPersonalization();
            saveSettings();
        }
        ImGui::SameLine(0, S(10));
        Label("Used by the console and the config editor.", 12.5f, col::mute);
    });
    group("Console text size", [&] {
        float v = settings_.monoScale * 100.f;
        if (SliderFloat("##monosize", &v, 85.f, 150.f, "%.0f%%", 320)) {
            settings_.monoScale = v / 100.f;
            applyPersonalization();
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) saveSettings();
    });
    EndCard();
    Gap(14);

    BeginCard("##surface", ImVec2(W, 0), 22);
    SectionTitle("Surfaces & motion", "Background pattern, corner rounding and animation.", icon::Grid);
    Gap(14);
    group("Background", [&] {
        const char* styles[5] = {"Solid", "Gradient", "Grid", "Dots", "Voxels"};
        int v = settings_.bgStyle;
        if (Segmented("##bgstyle", styles, 5, &v, 420)) {
            settings_.bgStyle = v;
            applyPersonalization();
            saveSettings();
        }
        Gap(6);
        float strength = settings_.bgStrength * 100.f;
        ImGui::SetNextItemWidth(S(300));
        if (SliderFloat("##bgstrength", &strength, 0.f, 150.f, "Intensity %.0f%%")) {
            settings_.bgStrength = strength / 100.f;
            applyPersonalization();
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) saveSettings();
    });
    group("Corner rounding", [&] {
        float r = settings_.radius;
        ImGui::SetNextItemWidth(S(300));
        if (SliderFloat("##radius", &r, 0.f, 1.8f, "%.2fx")) {
            settings_.radius = r;
            applyPersonalization();
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) saveSettings();
        ImGui::SameLine(0, S(10));
        Label("0 = square corners, 1.8 = very round.", 12.5f, col::mute);
    });
    group("Motion", [&] {
        float a = settings_.animSpeed;
        ImGui::SetNextItemWidth(S(300));
        if (SliderFloat("##anim", &a, 0.f, 1.6f, "%.2fx")) {
            settings_.animSpeed = a;
            applyPersonalization();
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) saveSettings();
        ImGui::SameLine(0, S(10));
        Label(settings_.animSpeed <= 0.001f ? "Animations off" : "Hover, tabs and page fades", 12.5f, col::mute);
    });
    group("Detail", [&] {
        bool sh = settings_.shadows;
        if (Toggle("shadows", &sh)) {
            settings_.shadows = sh;
            applyPersonalization();
            saveSettings();
        }
        ImGui::SameLine(0, S(10));
        Label("Card shadows", 14.f, col::text);
        ImGui::SameLine(0, S(24));
        bool glow = settings_.backdropGlow;
        if (Toggle("glow", &glow)) {
            settings_.backdropGlow = glow;
            applyPersonalization();
            saveSettings();
        }
        ImGui::SameLine(0, S(10));
        Label("Accent glow behind page titles", 14.f, col::text);
    });
    EndCard();
    Gap(14);

    drawThemePreview(W);
}

// ---------------------------------------------------------------------------
// interface
// ---------------------------------------------------------------------------
void App::personalizeInterface() {
    float W = std::min(ImGui::GetContentRegionAvail().x - S(4), S(920));

    BeginCard("##density", ImVec2(W, 0), 22);
    SectionTitle("Density", "How much breathing room the interface gets.", icon::Window);
    Gap(14);
    const char* dens[3] = {"Compact", "Comfortable", "Spacious"};
    int d = settings_.density;
    if (Segmented("##density", dens, 3, &d, 420)) {
        settings_.density = d;
        applyPersonalization();
        saveSettings();
    }
    Gap(10);
    Label("Compact fits more servers on screen, spacious is easier on the eyes.", 13.f, col::mute);
    EndCard();
    Gap(14);

    BeginCard("##sidebar", ImVec2(W, 0), 22);
    SectionTitle("Sidebar", nullptr, icon::List);
    Gap(14);
    group("Navigation", [&] {
        bool c = settings_.sidebarCollapsed;
        if (Toggle("collapse", &c)) {
            settings_.sidebarCollapsed = c;
            saveSettings();
        }
        ImGui::SameLine(0, S(10));
        Label("Start with the icons-only rail", 14.f, col::text);
    });
    group("Width", [&] {
        int w = settings_.sidebarWidth;
        ImGui::SetNextItemWidth(S(320));
        if (SliderInt("##sbw", &w, 210, 340, "%d px")) {
            settings_.sidebarWidth = w;
            saveSettings();
        }
    });
    group("Status card", [&] {
        bool st = settings_.showSidebarStatus;
        if (Toggle("sbstatus", &st)) {
            settings_.showSidebarStatus = st;
            saveSettings();
        }
        ImGui::SameLine(0, S(10));
        Label("Show memory and Java status at the bottom", 14.f, col::text);
    });
    EndCard();
    Gap(14);

    BeginCard("##notify", ImVec2(W, 0), 22);
    SectionTitle("Notifications & clock", nullptr, icon::Bell);
    Gap(14);
    group("Toast position", [&] {
        const char* corners[4] = {"Top right", "Bottom right", "Bottom left", "Top left"};
        int c = settings_.toastCorner;
        if (Segmented("##corner", corners, 4, &c, 520)) {
            settings_.toastCorner = c;
            SetToastPlacement(c, settings_.toastSeconds);
            saveSettings();
            Toast("Notifications appear here", ToastKind::Info);
        }
    });
    group("Toast duration", [&] {
        float s = settings_.toastSeconds;
        ImGui::SetNextItemWidth(S(320));
        if (SliderFloat("##toastlife", &s, 1.5f, 12.f, "%.1f s")) {
            settings_.toastSeconds = s;
            SetToastPlacement(settings_.toastCorner, s);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) saveSettings();
    });
    group("Clock", [&] {
        bool c = settings_.showClock;
        if (Toggle("clock", &c)) {
            settings_.showClock = c;
            saveSettings();
        }
        ImGui::SameLine(0, S(10));
        Label("Show the time in the page header", 14.f, col::text);
        ImGui::SameLine(0, S(24));
        bool h24 = settings_.clock24h;
        if (Toggle("clock24", &h24)) {
            settings_.clock24h = h24;
            saveSettings();
        }
        ImGui::SameLine(0, S(10));
        Label("24-hour time", 14.f, col::text);
    });
    EndCard();
    Gap(14);

    BeginCard("##safety", ImVec2(W, 0), 22);
    SectionTitle("Safety & startup", nullptr, icon::Shield);
    Gap(14);
    group("Confirmations", [&] {
        bool c = settings_.confirmDestructive;
        if (Toggle("confirm", &c)) {
            settings_.confirmDestructive = c;
            saveSettings();
        }
        ImGui::SameLine(0, S(10));
        Label("Ask before removing servers and backups", 14.f, col::text);
    });
    group("Stop servers when quitting", [&] {
        bool c = settings_.stopOnExit;
        if (Toggle("stoponexit", &c)) {
            settings_.stopOnExit = c;
            saveSettings();
        }
        ImGui::SameLine(0, S(10));
        Label("Always shuts them down gracefully", 14.f, col::text);
    });
    group("Page to open on launch", [&] {
        const char* pages[3] = {"Servers", "New server", "Import"};
        int cur = 0;
        if (settings_.startPage == "wizard") cur = 1;
        else if (settings_.startPage == "import") cur = 2;
        if (Combo("##startpage", &cur, pages, 3, 280)) {
            settings_.startPage = cur == 1 ? "wizard" : cur == 2 ? "import" : "servers";
            saveSettings();
        }
    });
    group("Window", [&] {
        bool r = settings_.restoreWindow;
        if (Toggle("restore", &r)) {
            settings_.restoreWindow = r;
            saveSettings();
        }
        ImGui::SameLine(0, S(10));
        Label("Remember the window size", 14.f, col::text);
    });
    EndCard();
    Gap(14);
}

// ---------------------------------------------------------------------------
// servers page options
// ---------------------------------------------------------------------------
void App::personalizeServersPage() {
    float W = std::min(ImGui::GetContentRegionAvail().x - S(4), S(920));
    BeginCard("##layout", ImVec2(W, 0), 22);
    SectionTitle("Server list", "Choose how your servers are presented.", icon::Grid);
    Gap(14);
    group("Layout", [&] {
        const char* views[2] = {"Grid of cards", "Compact list"};
        int v = settings_.serverView;
        if (Segmented("##srvview", views, 2, &v, 420)) {
            settings_.serverView = v;
            saveSettings();
        }
    });
    group("Card size", [&] {
        const char* sizes[3] = {"Compact", "Normal", "Large"};
        int s = settings_.cardSize;
        if (Segmented("##cardsize", sizes, 3, &s, 420)) {
            settings_.cardSize = s;
            saveSettings();
        }
        ImGui::SameLine(0, S(12));
        Label("Large cards show a little more detail.", 12.5f, col::mute);
    });
    group("Default sort", [&] {
        const char* sorts[4] = {"Name (A-Z)", "Status", "Players online", "Recently started"};
        int s = settings_.sortMode;
        if (Combo("##sortmode", &s, sorts, 4, 280)) {
            settings_.sortMode = s;
            saveSettings();
        }
    });
    group("What to show", [&] {
        bool tiles = settings_.showStatTiles;
        if (Toggle("tiles", &tiles)) {
            settings_.showStatTiles = tiles;
            saveSettings();
        }
        ImGui::SameLine(0, S(10));
        Label("Summary tiles above the list", 14.f, col::text);
        ImGui::SameLine(0, S(24));
        bool acts = settings_.showCardActions;
        if (Toggle("cardacts", &acts)) {
            settings_.showCardActions = acts;
            saveSettings();
        }
        ImGui::SameLine(0, S(10));
        Label("Start / stop buttons on cards", 14.f, col::text);
    });
    EndCard();
    Gap(14);

    BeginCard("##preview", ImVec2(W, 0), 22);
    SectionTitle("Preview", "A sample card using your current settings.", icon::Eye);
    Gap(14);
    {
        float cw = settings_.cardSize == 0 ? S(320) : settings_.cardSize == 2 ? S(440) : S(380);
        float ch = HS(196) * (settings_.cardSize == 0 ? 0.86f : (settings_.cardSize == 2 ? 1.14f : 1.f));
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(cw, ch));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 q(p.x + cw, p.y + ch);
        if (theme::g_opt.shadows) DrawShadow(dl, ImVec2(p.x, p.y + S(4)), q, RD(16), 0.4f);
        dl->AddRectFilled(p, q, Fade(RGBA(col::panel)), RD(16));
        dl->AddRect(p, q, Fade(Alpha(RGBA(col::green), 0.32f)), RD(16), 0, 1.f);
        float pad = D(18), tile = HS(46);
        DrawTile(dl, ImVec2(p.x + pad, p.y + pad), tile, 0x34D399);
        DrawStr(dl, fBold, 17.f, ImVec2(p.x + pad + tile + S(13), p.y + pad + S(1)), RGBA(col::text), "Survival SMP");
        DrawStr(dl, fRegular, 13.f, ImVec2(p.x + pad + tile + S(13), p.y + pad + S(24)), RGBA(col::dim), "Paper  \xC2\xB7  1.21.8  \xC2\xB7  build 142");
        const char* stText = "Online";
        float bw = BadgeWidth(stText, true);
        ImGui::SetCursorScreenPos(ImVec2(q.x - bw - pad, p.y + pad - S(1)));
        Badge(stText, RGBA(col::green), true, 1.f);
        float y = p.y + pad + tile + S(22);
        DrawIcon(dl, ImVec2(p.x + pad + S(7), y + S(9)), icon::People, 14.f, RGBA(col::violet));
        DrawStr(dl, fRegular, 13.f, ImVec2(p.x + pad + S(22), y + S(1)), RGBA(col::dim), "3 / 20");
        DrawIcon(dl, ImVec2(p.x + pad + S(104) + S(7), y + S(9)), icon::Wifi, 14.f, RGBA(col::mute));
        DrawStr(dl, fRegular, 13.f, ImVec2(p.x + pad + S(126), y + S(1)), RGBA(col::dim), ":25565");
        if (settings_.cardSize != 0) {
            DrawIcon(dl, ImVec2(p.x + pad + S(184) + S(7), y + S(9)), icon::Cpu, 14.f, RGBA(col::green));
            DrawStr(dl, fRegular, 13.f, ImVec2(p.x + pad + S(206), y + S(1)), RGBA(col::dim), "2.34 GB  \xC2\xB7  1h 23m");
        }
        float noteY = q.y - HS(56);
        dl->AddLine(ImVec2(p.x + pad, noteY), ImVec2(q.x - pad, noteY), Fade(RGBA(col::border)), 1.f);
        float bh = HS(38);
        ImGui::SetCursorScreenPos(ImVec2(p.x + pad, noteY + S(9)));
        if (settings_.showCardActions) {
            Button("Start", icon::Play, Btn::Primary, ImVec2(S(102), bh));
            ImGui::SameLine(0, S(7));
            IconButton("pvrestart", icon::Restart, Btn::Secondary, 38, "Restart");
            ImGui::SameLine(0, S(7));
            IconButton("pvfolder", icon::FolderOpen, Btn::Secondary, 38, "Open folder");
        }
        float mw = ButtonWidth("Manage", true);
        ImGui::SetCursorScreenPos(ImVec2(q.x - pad - mw, noteY + S(9)));
        Button("Manage", icon::Sliders, Btn::Secondary, ImVec2(0, bh));
    }
    EndCard();
    Gap(14);
}

// ---------------------------------------------------------------------------
// console & editor
// ---------------------------------------------------------------------------
void App::personalizeConsole() {
    float W = std::min(ImGui::GetContentRegionAvail().x - S(4), S(920));
    BeginCard("##console", ImVec2(W, 0), 22);
    SectionTitle("Console", "How server output is displayed.", icon::Console);
    Gap(14);
    group("Lines kept in memory", [&] {
        int n = settings_.logMaxLines;
        ImGui::SetNextItemWidth(S(320));
        if (SliderInt("##logmax", &n, 500, 20000, "%d lines")) settings_.logMaxLines = n;
        if (ImGui::IsItemDeactivatedAfterEdit()) saveSettings();
    });
    group("Display", [&] {
        bool ts = settings_.logTimestamps;
        if (Toggle("timestamps", &ts)) {
            settings_.logTimestamps = ts;
            saveSettings();
        }
        ImGui::SameLine(0, S(10));
        Label("Show the [time LEVEL] prefix", 14.f, col::text);
        ImGui::SameLine(0, S(24));
        bool wrap = settings_.logWrap;
        if (Toggle("logwrap", &wrap)) {
            settings_.logWrap = wrap;
            saveSettings();
        }
        ImGui::SameLine(0, S(10));
        Label("Wrap long lines", 14.f, col::text);
    });
    group("Behaviour", [&] {
        bool follow = settings_.consoleAutoScroll;
        if (Toggle("follow", &follow)) {
            settings_.consoleAutoScroll = follow;
            saveSettings();
        }
        ImGui::SameLine(0, S(10));
        Label("Follow new output by default", 14.f, col::text);
    });
    EndCard();
    Gap(14);

    BeginCard("##editor", ImVec2(W, 0), 22);
    SectionTitle("Config editor", "The built-in editor on each server's Files tab.", icon::Edit);
    Gap(14);
    group("Tab size", [&] {
        const char* sizes[2] = {"2 spaces", "4 spaces"};
        int s = settings_.editorTabSize == 4 ? 1 : 0;
        if (Segmented("##tabsize", sizes, 2, &s, 300)) {
            settings_.editorTabSize = s == 1 ? 4 : 2;
            saveSettings();
        }
        ImGui::SameLine(0, S(12));
        Label("YAML files prefer two spaces.", 12.5f, col::mute);
    });
    group("Wrapping", [&] {
        bool w = settings_.editorWrap;
        if (Toggle("editwrap", &w)) {
            settings_.editorWrap = w;
            saveSettings();
        }
        ImGui::SameLine(0, S(10));
        Label("Wrap long config lines instead of scrolling", 14.f, col::text);
    });
    EndCard();
    Gap(14);
}

// ---------------------------------------------------------------------------
// storage & java
// ---------------------------------------------------------------------------
void App::personalizeStorage() {
    float W = std::min(ImGui::GetContentRegionAvail().x - S(4), S(920));
    BeginCard("##storage", ImVec2(W, 0), 22);
    SectionTitle("Server folder", "New servers and backups are created here.", icon::Storage);
    Gap(14);
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float bw = ButtonWidth("Change", true) + ButtonWidth("Open", true) + S(18);
        float fw = ImGui::GetContentRegionAvail().x - bw - S(10);
        ImGui::Dummy(ImVec2(fw, HS(42)));
        dl->AddRectFilled(p, ImVec2(p.x + fw, p.y + HS(42)), Fade(RGBA(col::field)), RD(10));
        dl->AddRect(p, ImVec2(p.x + fw, p.y + HS(42)), Fade(RGBA(col::borderHi)), RD(10), 0, 1.f);
        std::string shown = FitText(util::pathStr(settings_.root()), fRegular, 14.f, fw - D(28), true);
        DrawStr(dl, fRegular, 14.f, ImVec2(p.x + D(14), p.y + (HS(42) - S(18)) * 0.5f), RGBA(col::text), shown.c_str());
        ImGui::SameLine(0, S(10));
        if (Button("Change", icon::FolderOpen, Btn::Secondary, ImVec2(0, HS(42)))) {
            std::string d;
            if (util::pickFolder(hwnd_, d)) {
                settings_.serversRoot = d;
                saveSettings();
                Toast("New servers will be created in " + d, ToastKind::Success);
            }
        }
        ImGui::SameLine(0, S(10));
        if (Button("Open", icon::Link, Btn::Secondary, ImVec2(0, HS(42)))) util::openPath(settings_.root());
    }
    Gap(14);
    group("Default memory for new servers", [&] {
        uint64_t totalMB = util::totalRamMB();
        int cap = (int)std::max<uint64_t>(2048, totalMB * 80 / 100);
        if (SliderInt("##defram", &settings_.defaultRamMB, 1024, cap, "%d MB")) settings_.defaultRamMB = std::max(1024, (settings_.defaultRamMB / 256) * 256);
        if (ImGui::IsItemDeactivatedAfterEdit()) saveSettings();
    });
    group("Voxual's own data", [&] {
        Label(util::pathStr(util::appDataDir()).c_str(), 13.f, col::dim);
        Gap(6);
        if (Button("Open data folder", icon::FolderOpen, Btn::Secondary)) util::openPath(util::appDataDir());
        ImGui::SameLine(0, S(8));
        Label("settings.json, servers.json and downloaded Java runtimes live here.", 12.5f, col::mute);
    });
    EndCard();
    Gap(14);

    BeginCard("##java", ImVec2(W, 0), 22);
    SectionTitle("Java runtimes", "Minecraft needs a specific Java version - Voxual can fetch the right one.", icon::Java);
    Gap(14);
    auto js = java::all();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (java::scanning() && js.empty()) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(1, HS(32)));
        DrawSpinner(dl, ImVec2(p.x + S(12), p.y + S(14)), S(9), S(2.5f), Accent());
        DrawStr(dl, fRegular, 13.5f, ImVec2(p.x + D(32), p.y + S(5)), RGBA(col::dim), "Looking for installed Java versions...");
    } else if (js.empty()) {
        Label("No Java found on this PC.", 14.f, col::amber, true);
    }
    for (auto& j : js) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x, h = HS(56);
        ImGui::Dummy(ImVec2(w, h));
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Fade(RGBA(col::sunkenSoft)), RD(12));
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), Fade(RGBA(col::borderSoft)), RD(12), 0, 1.f);
        dl->AddRectFilled(ImVec2(p.x + D(10), p.y + (h - HS(36)) * 0.5f), ImVec2(p.x + D(46), p.y + (h + HS(36)) * 0.5f),
                          Fade(Alpha(RGBA(col::amber), 0.14f)), RD(10));
        std::string mj = std::to_string(j.major);
        ImVec2 ms = TextSize(fBold, 16.f, mj.c_str());
        DrawStr(dl, fBold, 16.f, ImVec2(p.x + D(28) - ms.x * 0.5f, p.y + h * 0.5f - ms.y * 0.5f), RGBA(col::amber), mj.c_str());
        DrawStr(dl, fBold, 14.f, ImVec2(p.x + D(58), p.y + S(9)), RGBA(col::text), ("Java " + j.version).c_str());
        std::string path = FitText(util::pathStr(j.exe), fRegular, 12.f, w - D(200), true);
        DrawStr(dl, fRegular, 12.f, ImVec2(p.x + D(58), p.y + S(30)), RGBA(col::mute), path.c_str());
        if (j.managed) {
            ImGui::SetCursorScreenPos(ImVec2(p.x + w - D(150), p.y + (h - HS(24)) * 0.5f));
            Chip("Managed by " VX_APP_NAME, RGBA(col::green));
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + S(8)));
        ImGui::Dummy(ImVec2(1, 1));
    }
    Gap(8);
    if (javaJob_.active) {
        std::string msg;
        {
            std::lock_guard<std::mutex> lk(javaJob_.mu);
            msg = javaJob_.msg;
        }
        Label(msg.c_str(), 13.5f, col::dim);
        ProgressBar(javaJob_.progress, ImVec2(-1, HS(8)));
        Gap(8);
        if (Button("Cancel", nullptr, Btn::Danger)) javaJob_.cancel = true;
    } else {
        std::string err;
        {
            std::lock_guard<std::mutex> lk(javaJob_.mu);
            err = javaJob_.err;
        }
        if (!err.empty()) {
            Label(err.c_str(), 13.f, col::red);
            Gap(6);
        }
        Label("Install another runtime", 13.f, col::dim, true);
        Gap(6);
        for (int mj : {8, 17, 21, 25}) {
            ImGui::PushID(mj);
            std::string l = (java::has(mj) ? "Java " : "Get Java ") + std::to_string(mj);
            ImGui::BeginDisabled(java::has(mj));
            if (Button(l.c_str(), java::has(mj) ? icon::Check : icon::Download, Btn::Secondary)) startJavaInstall(mj);
            ImGui::EndDisabled();
            ImGui::PopID();
            ImGui::SameLine(0, S(8));
        }
        if (Button("Rescan", icon::Restart, Btn::Ghost)) std::thread([] { java::scan(); }).detach();
    }
    EndCard();
    Gap(14);
}

// ---------------------------------------------------------------------------
// about
// ---------------------------------------------------------------------------
void App::personalizeAbout() {
    float W = std::min(ImGui::GetContentRegionAvail().x - S(4), S(920));
    BeginCard("##about", ImVec2(W, 0), 24);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float t = HS(64);
        dl->AddRectFilled(p, ImVec2(p.x + t, p.y + t), Fade(Accent(0.16f)), RD(16));
        dl->AddRect(p, ImVec2(p.x + t, p.y + t), Fade(Accent(0.35f)), RD(16), 0, 1.f);
        DrawCube(dl, ImVec2(p.x + t * 0.5f, p.y + t * 0.5f), t * 0.31f, Accent());
        ImGui::Dummy(ImVec2(t, t));
        ImGui::SameLine(0, S(16));
        ImGui::BeginGroup();
        Label(VX_APP_NAME " " VX_VERSION_STR, 20.f, col::text, true);
        Label("A local Minecraft server manager for Windows.", 13.5f, col::dim);
        Gap(3);
        Label("Create, run, configure and personalise Minecraft servers - all from one app.", 12.5f, col::mute);
        ImGui::EndGroup();
    }
    EndCard();
    Gap(14);

    BeginCard("##shortcuts", ImVec2(W, 0), 22);
    SectionTitle("Keyboard shortcuts", nullptr, icon::Keyboard);
    Gap(12);
    auto shortcut = [&](const char* keys, const char* what) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float h = HS(30);
        ImGui::Dummy(ImVec2(1, h));
        ImVec2 ks = TextSize(fBold, 12.5f, keys);
        float kw = ks.x + D(16);
        dl->AddRectFilled(p, ImVec2(p.x + kw, p.y + HS(24)), Fade(RGBA(col::sunkenSoft)), RD(7));
        dl->AddRect(p, ImVec2(p.x + kw, p.y + HS(24)), Fade(RGBA(col::border)), RD(7), 0, 1.f);
        DrawStr(dl, fBold, 12.5f, ImVec2(p.x + D(8), p.y + (HS(24) - ks.y) * 0.5f), RGBA(col::text), keys);
        DrawStr(dl, fRegular, 13.f, ImVec2(p.x + kw + S(14), p.y + (HS(24) - TextSize(fRegular, 13.f, "A").y) * 0.5f), RGBA(col::dim), what);
    };
    shortcut("Ctrl+K", "Open the command palette");
    shortcut("Ctrl+S", "Save the file open in the config editor");
    shortcut("Up / Down", "Previous or next command in the console");
    shortcut("Ctrl+Scroll", "Scroll the console output");
    EndCard();
    Gap(14);

    BeginCard("##credits", ImVec2(W, 0), 22);
    SectionTitle("Built with", nullptr, icon::Shield);
    Gap(10);
    LabelWrapped("Dear ImGui (MIT) for the interface toolkit, nlohmann/json (MIT) for settings and storage, Direct3D 11 for rendering, "
                 "and WinHTTP for downloading server software and Java runtimes. "
                 "Minecraft server files come straight from Mojang, PaperMC, Purpur, FabricMC, Forge and NeoForge.",
                 13.f, col::dim);
    Gap(12);
    if (Link("Minecraft EULA")) util::openUrl("https://aka.ms/MinecraftEULA");
    ImGui::SameLine(0, S(16));
    if (Link("PaperMC")) util::openUrl("https://papermc.io");
    ImGui::SameLine(0, S(16));
    if (Link("Modrinth")) util::openUrl("https://modrinth.com");
    ImGui::SameLine(0, S(16));
    if (Link("Hangar")) util::openUrl("https://hangar.papermc.io");
    EndCard();
    Gap(14);
}

// ---------------------------------------------------------------------------
// live preview + theme import/export
// ---------------------------------------------------------------------------
void App::drawThemePreview(float width) {
    BeginCard("##preview", ImVec2(width, 0), 22);
    SectionTitle("Live preview", "Exactly how these controls look with your current theme.", icon::Eye);
    Gap(14);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    static std::string previewText;
    float previewW = std::min(width - D(44), S(560));
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = HS(250);
    ImGui::Dummy(ImVec2(previewW, h));
    ImVec2 q(p.x + previewW, p.y + h);
    dl->AddRectFilled(p, q, Fade(RGBA(col::bg)), RD(14));
    dl->AddRect(p, q, Fade(RGBA(col::border)), RD(14), 0, 1.f);
    dl->PushClipRect(p, q, true);
    theme::DrawBackdrop(dl, p, q);
    dl->AddRectFilled(p, ImVec2(p.x + S(58), q.y), Fade(RGBA(col::sidebar)), RD(14), ImDrawFlags_RoundCornersLeft);
    dl->AddRectFilled(ImVec2(p.x + D(10), p.y + D(12)), ImVec2(p.x + S(48), p.y + D(12) + HS(28)), Fade(Accent(0.14f)), RD(8));
    DrawIcon(dl, ImVec2(p.x + S(29), p.y + D(12) + HS(14)), icon::Console, 14.f, Accent());
    for (int i = 0; i < 3; ++i)
        dl->AddCircleFilled(ImVec2(p.x + S(29), p.y + D(58) + i * S(22)), S(2.5f), Fade(i == 1 ? Accent() : RGBA(col::faint)), 12);
    float x0 = p.x + S(74);
    DrawStr(dl, fBold, 17.f, ImVec2(x0, p.y + D(14)), RGBA(col::text), "Servers");
    DrawStr(dl, fRegular, 12.5f, ImVec2(x0, p.y + D(38)), RGBA(col::dim), "3 servers  \xC2\xB7  1 running");
    float bw = BadgeWidth("Online", true);
    ImGui::SetCursorScreenPos(ImVec2(q.x - D(18) - bw, p.y + D(16)));
    Badge("Online", RGBA(col::green), true, 1.f);
    ImVec2 cp(x0, p.y + D(62));
    dl->AddRectFilled(cp, ImVec2(q.x - D(18), cp.y + HS(104)), Fade(RGBA(col::panel)), RD(13));
    dl->AddRect(cp, ImVec2(q.x - D(18), cp.y + HS(104)), Fade(RGBA(col::border)), RD(13), 0, 1.f);
    DrawStr(dl, fBold, 14.f, ImVec2(cp.x + D(14), cp.y + S(12)), RGBA(col::text), "Survival SMP");
    DrawStr(dl, fRegular, 12.f, ImVec2(cp.x + D(14), cp.y + S(32)), RGBA(col::mute), "Paper  \xC2\xB7  1.21.8");
    ImGui::SetCursorScreenPos(ImVec2(cp.x + D(14), cp.y + S(56)));
    Button("Start", icon::Play, Btn::Primary, ImVec2(S(96), HS(34)));
    ImGui::SameLine(0, S(8));
    Button("Folder", icon::FolderOpen, Btn::Secondary, ImVec2(S(104), HS(34)));
    ImGui::SameLine(0, S(8));
    Chip("build 142", RGBA(col::blue));
    ImGui::SetCursorScreenPos(ImVec2(cp.x + D(14), cp.y + HS(104) + S(10)));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(D(10), D(6)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, RD(9));
    ImGui::PushStyleColor(ImGuiCol_Text, RGBA(col::mute));
    ImGui::SetNextItemWidth(std::max(S(120), previewW - S(110)));
    ImGui::InputTextWithHint("##pvinput", "Search servers...", &previewText, ImGuiInputTextFlags_None);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::SetCursorScreenPos(ImVec2(cp.x + D(14) + std::max(S(120), previewW - S(110)) + S(8), cp.y + HS(104) + S(10)));
    static bool previewToggle = true;
    Toggle("pvtoggle", &previewToggle);
    ImGui::SetCursorScreenPos(ImVec2(cp.x + D(14), cp.y + HS(104) + S(46)));
    ProgressBar(0.62f, ImVec2(std::max(S(120), previewW - S(110)), HS(8)));
    dl->PopClipRect();
    Gap(10);
    Label("Themes can be exported as a small JSON file and shared.", 12.5f, col::mute);
    EndCard();
    Gap(14);
}

void App::exportTheme() {
    std::string path;
    if (!util::pickSaveFile(hwnd_, L"Voxual theme (*.json)\0*.json\0All files\0*.*\0", L"json", "voxual-theme.json", path)) return;
    nlohmann::json j;
    j["voxualTheme"] = 1;
    j["theme"] = settings_.theme;
    j["accent"] = settings_.accent;
    j["customAccent"] = settings_.customAccent;
    j["customAccentRgb"] = settings_.customAccentRgb;
    j["density"] = settings_.density;
    j["radius"] = settings_.radius;
    j["textScale"] = settings_.textScale;
    j["monoScale"] = settings_.monoScale;
    j["animSpeed"] = settings_.animSpeed;
    j["shadows"] = settings_.shadows;
    j["backdropGlow"] = settings_.backdropGlow;
    j["bgStyle"] = settings_.bgStyle;
    j["bgStrength"] = settings_.bgStrength;
    j["fontFamily"] = settings_.fontFamily;
    j["monoFamily"] = settings_.monoFamily;
    bool ok = util::writeFile(util::fromUtf8(path), j.dump(2));
    Toast(ok ? "Theme exported" : "Could not write that file", ok ? ToastKind::Success : ToastKind::Error);
}

void App::importTheme() {
    std::string path;
    if (!util::pickFile(hwnd_, L"Voxual theme (*.json)\0*.json\0All files\0*.*\0", path)) return;
    std::string txt;
    if (!util::readFile(util::fromUtf8(path), txt)) {
        Toast("Could not read that file", ToastKind::Error);
        return;
    }
    try {
        auto j = nlohmann::json::parse(txt);
        settings_.theme = j.value("theme", settings_.theme);
        settings_.accent = j.value("accent", settings_.accent);
        settings_.customAccent = j.value("customAccent", settings_.customAccent);
        settings_.customAccentRgb = j.value("customAccentRgb", settings_.customAccentRgb);
        settings_.density = j.value("density", settings_.density);
        settings_.radius = j.value("radius", settings_.radius);
        settings_.textScale = j.value("textScale", settings_.textScale);
        settings_.monoScale = j.value("monoScale", settings_.monoScale);
        settings_.animSpeed = j.value("animSpeed", settings_.animSpeed);
        settings_.shadows = j.value("shadows", settings_.shadows);
        settings_.backdropGlow = j.value("backdropGlow", settings_.backdropGlow);
        settings_.bgStyle = j.value("bgStyle", settings_.bgStyle);
        settings_.bgStrength = j.value("bgStrength", settings_.bgStrength);
        settings_.fontFamily = j.value("fontFamily", settings_.fontFamily);
        settings_.monoFamily = j.value("monoFamily", settings_.monoFamily);
        applyPersonalization();
        saveSettings();
        Toast("Theme loaded", ToastKind::Success);
    } catch (...) {
        Toast("That is not a Voxual theme file", ToastKind::Error);
    }
}

void App::resetPersonalization() {
    Settings def;
    def.serversRoot = settings_.serversRoot;
    def.defaultRamMB = settings_.defaultRamMB;
    def.stopOnExit = settings_.stopOnExit;
    def.startPage = settings_.startPage;
    settings_ = def;
    applyPersonalization();
    saveSettings();
    Toast("Personalization reset to defaults", ToastKind::Success);
}

// Kept for the developer flags page name.
void App::drawSettingsPage() { drawPersonalizePage(); }
