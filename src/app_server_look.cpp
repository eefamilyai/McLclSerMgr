// Per-server personalization: how the server looks to players and to Voxual itself.
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "app.h"
#include "imgui_internal.h"
#include "motd.h"

using namespace ui;
namespace P = providers;

namespace {

// ---- server icon ------------------------------------------------------------
const char* kIconPatterns[] = {"Blocks", "Gradient", "Split", "Checker", "Rings", "Stripes", "Grass"};
constexpr int kIconPatternCount = (int)(sizeof kIconPatterns / sizeof kIconPatterns[0]);
constexpr int kIconSize = 64;

inline void putPx(std::vector<uint8_t>& px, int x, int y, unsigned rgb, float shade = 1.f) {
    if (x < 0 || y < 0 || x >= kIconSize || y >= kIconSize) return;
    auto ch = [&](int shift) {
        float v = (float)((rgb >> shift) & 255) * shade;
        return (uint8_t)std::clamp(v, 0.f, 255.f);
    };
    size_t o = ((size_t)y * kIconSize + x) * 3;
    px[o] = ch(16);
    px[o + 1] = ch(8);
    px[o + 2] = ch(0);
}

void rasterIcon(std::vector<uint8_t>& px, int pattern, unsigned c1, unsigned c2) {
    px.assign((size_t)kIconSize * kIconSize * 3, 0);
    for (int y = 0; y < kIconSize; ++y) {
        for (int x = 0; x < kIconSize; ++x) {
            float t = (float)y / (kIconSize - 1);
            switch (pattern) {
            case 0: {  // blocks: 2x2 with a lit top-left
                int bx = x / 32, by = y / 32;
                float shade = (bx == 1 && by == 0) ? 1.18f : (bx == 0 && by == 1) ? 0.82f : (bx == 1 && by == 1) ? 0.66f : 1.f;
                putPx(px, x, y, (bx + by) % 2 == 0 ? c1 : c2, shade * (1.f - t * 0.12f));
                break;
            }
            case 1:  // gradient
                putPx(px, x, y, c1, 1.f - t * 0.55f);
                break;
            case 2:  // split
                putPx(px, x, y, (x + y < kIconSize) ? c1 : c2, 1.f - t * 0.25f);
                break;
            case 3:  // checker
                putPx(px, x, y, ((x / 8 + y / 8) % 2) ? c1 : c2, 1.f - t * 0.2f);
                break;
            case 4: {  // rings
                int d = std::max(std::abs(x - 32), std::abs(y - 32));
                putPx(px, x, y, (d / 7) % 2 ? c1 : c2, 1.f - t * 0.2f);
                break;
            }
            case 5:  // stripes
                putPx(px, x, y, ((x / 8) % 2) ? c1 : c2, 1.f - t * 0.2f);
                break;
            default: {  // grass block
                int edge = 20 + ((x * 7919) % 5);
                if (y < edge) putPx(px, x, y, c1, 1.06f - t * 0.1f);
                else if (y < edge + 4) putPx(px, x, y, c1, 0.75f);
                else putPx(px, x, y, c2, 1.f - t * 0.25f);
                break;
            }
            }
        }
    }
}

// The same pattern drawn live for the preview.
void previewIcon(ImDrawList* dl, ImVec2 p, float size, int pattern, unsigned c1, unsigned c2) {
    float u = size / kIconSize;
    auto rect = [&](int x, int y, int w, int h, unsigned rgb, float shade) {
        ImU32 col = RGBA((unsigned)std::clamp(((rgb >> 16) & 255) * shade, 0.f, 255.f) << 16 |
                         (unsigned)std::clamp(((rgb >> 8) & 255) * shade, 0.f, 255.f) << 8 |
                         (unsigned)std::clamp((rgb & 255) * shade, 0.f, 255.f));
        dl->AddRectFilled(ImVec2(p.x + x * u, p.y + y * u), ImVec2(p.x + (x + w) * u, p.y + (y + h) * u), col);
    };
    dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), RGBA(c2), RD(4));
    switch (pattern) {
    case 0:
        rect(0, 0, 32, 32, c1, 1.f);
        rect(32, 0, 32, 32, c2, 1.18f);
        rect(0, 32, 32, 32, c2, 0.82f);
        rect(32, 32, 32, 32, c1, 0.66f);
        break;
    case 1:
        dl->AddRectFilledMultiColor(p, ImVec2(p.x + size, p.y + size), RGBA(c1), RGBA(c1), RGBA(c1, 120), RGBA(c1, 120));
        break;
    case 2:
        dl->AddTriangleFilled(p, ImVec2(p.x + size, p.y), ImVec2(p.x, p.y + size), RGBA(c1));
        dl->AddTriangleFilled(ImVec2(p.x + size, p.y), ImVec2(p.x + size, p.y + size), ImVec2(p.x, p.y + size), RGBA(c2));
        break;
    case 3:
        for (int y = 0; y < 64; y += 8)
            for (int x = 0; x < 64; x += 8) rect(x, y, 8, 8, ((x / 8 + y / 8) % 2) ? c1 : c2, 1.f);
        break;
    case 4:
        for (int d = 4; d >= 0; --d) {
            float inset = d * 7 * u;
            dl->AddRectFilled(ImVec2(p.x + inset, p.y + inset), ImVec2(p.x + size - inset, p.y + size - inset), RGBA(d % 2 ? c1 : c2), RD(2));
        }
        break;
    case 5:
        for (int x = 0; x < 64; x += 8) rect(x, 0, 8, 64, ((x / 8) % 2) ? c1 : c2, 1.f);
        break;
    default:
        rect(0, 0, 64, 21, c1, 1.04f);
        rect(0, 21, 64, 4, c1, 0.75f);
        rect(0, 25, 64, 39, c2, 1.f);
        break;
    }
    dl->AddRect(p, ImVec2(p.x + size, p.y + size), Fade(RGBA(col::borderHi)), RD(4), 0, 1.f);
}

// ---- gameplay presets --------------------------------------------------------
struct Preset {
    const char* name;
    const char* detail;
    const char* icon;
};
const Preset kPresets[] = {
    {"Vanilla feel", "Balanced defaults close to a fresh server", "Game"},
    {"Just for friends", "Whitelist on, small and tidy", "People"},
    {"Creative sandbox", "Flight, command blocks, no monsters", "Bolt"},
    {"Performance first", "Short view distance, fewer slots", "Cpu"},
    {"Modded heavy", "More memory, flight allowed, wider view", "Package"},
    {"Hardcore survival", "Hard difficulty, one life", "Shield"},
};
constexpr int kPresetCount = (int)(sizeof kPresets / sizeof kPresets[0]);

}  // namespace

void App::applyGameplayPreset(ServerInstance& s, int preset) {
    if (preset < 0 || preset >= kPresetCount) return;   // kPresets[preset] is indexed below
    Properties& pr = ds_.props;
    uint64_t totalMB = util::totalRamMB();
    int cap = (int)std::max<uint64_t>(2048, totalMB * 80 / 100);
    switch (preset) {
    case 0:
        pr.set("difficulty", "normal");
        pr.set("gamemode", "survival");
        pr.setBool("pvp", true);
        pr.setBool("hardcore", false);
        pr.setBool("allow-flight", false);
        pr.setBool("enable-command-block", false);
        pr.setBool("spawn-monsters", true);
        pr.setBool("spawn-animals", true);
        pr.setInt("view-distance", 10);
        pr.setInt("simulation-distance", 10);
        pr.setInt("spawn-protection", 16);
        pr.setInt("max-players", 20);
        pr.setBool("online-mode", true);
        break;
    case 1:
        pr.set("difficulty", "normal");
        pr.set("gamemode", "survival");
        pr.setBool("pvp", true);
        pr.setBool("white-list", true);
        pr.setBool("enforce-whitelist", true);
        pr.setBool("online-mode", true);
        pr.setInt("max-players", 8);
        pr.setInt("view-distance", 8);
        pr.setInt("simulation-distance", 6);
        pr.setInt("spawn-protection", 0);
        break;
    case 2:
        pr.set("gamemode", "creative");
        pr.set("difficulty", "peaceful");
        pr.setBool("pvp", false);
        pr.setBool("allow-flight", true);
        pr.setBool("enable-command-block", true);
        pr.setBool("spawn-monsters", false);
        pr.setInt("spawn-protection", 0);
        pr.setInt("max-players", 20);
        break;
    case 3:
        pr.setInt("view-distance", 6);
        pr.setInt("simulation-distance", 4);
        pr.setInt("spawn-protection", 0);
        pr.setInt("max-players", 10);
        pr.setBool("spawn-monsters", true);
        ds_.draft.maxRamMB = std::min(cap, std::max(2048, (int)(totalMB / 8 / 256 * 256)));
        ds_.draft.minRamMB = std::min(ds_.draft.minRamMB, ds_.draft.maxRamMB);
        break;
    case 4:
        pr.setBool("allow-flight", true);
        pr.setBool("enable-command-block", true);
        pr.setInt("view-distance", 10);
        pr.setInt("simulation-distance", 8);
        pr.setInt("spawn-protection", 0);
        pr.setInt("max-players", 20);
        pr.set("difficulty", "normal");
        ds_.draft.maxRamMB = std::min(cap, std::max(6144, (int)(totalMB / 4 / 256 * 256)));
        ds_.draft.minRamMB = std::min(ds_.draft.maxRamMB, std::max(2048, ds_.draft.maxRamMB / 2));
        ds_.draft.aikarFlags = true;
        break;
    default:
        pr.setBool("hardcore", true);
        pr.set("difficulty", "hard");
        pr.set("gamemode", "survival");
        pr.setBool("pvp", true);
        pr.setInt("view-distance", 10);
        pr.setInt("simulation-distance", 10);
        pr.setInt("spawn-protection", 0);
        break;
    }
    ds_.dirty = true;
    appliedPreset_ = preset;
    Toast(std::string("Preset applied: ") + kPresets[preset].name, ToastKind::Success);
}

void App::writeServerIcon(ServerInstance& s) {
    std::vector<uint8_t> px;
    rasterIcon(px, ds_.iconPattern, ds_.iconColor1, ds_.iconColor2);
    fs::path out = s.cfg.path() / "server-icon.png";
    bool ok = demo_ ? true : util::writePngRgb(out, px.data(), kIconSize, kIconSize);
    if (ok) {
        Toast("server-icon.png written - restart the server to see it", ToastKind::Success);
        s.appendLog("[" VX_APP_NAME "] Wrote server-icon.png (64x64, pattern: " + std::string(kIconPatterns[ds_.iconPattern]) + ")", LogSystem);
    } else {
        Toast("Could not write server-icon.png", ToastKind::Error);
    }
}

// ---------------------------------------------------------------------------
// the tab
// ---------------------------------------------------------------------------
void App::drawPersonalizeTab(ServerInstance& s) {
    if (!ds_.loaded) loadDraft(s);
    if (!ds_.lookLoaded) {
        ds_.lookLoaded = true;
        ds_.noteDraft = s.cfg.note;
        ds_.colorDraft = s.cfg.color;
        ds_.motdDraft = motd::fromSectionCodes(ds_.props.get("motd", "A Minecraft Server"));
    }
    float barH = ds_.dirty ? HS(76) : 0.f;
    BeginScroll("##look", ImVec2(0, ImGui::GetContentRegionAvail().y - barH));
    float W = std::min(ImGui::GetContentRegionAvail().x - S(4), S(960));
    Properties& pr = ds_.props;

    // ================= identity =================
    BeginCard("##identity", ImVec2(W, 0), 22);
    SectionTitle("This server in " VX_APP_NAME, "How the card looks in your own list.", icon::Star);
    Gap(14);
    settingRow("Display name", "Shown in the server list and the sidebar.", [&] {
        if (InputText("##lookname", &ds_.draft.name)) ds_.dirty = true;
    });
    {
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::BeginGroup();
        Label("Card colour", 14.5f, col::text, true);
        Label("Overrides the software colour on the server card.", 12.5f, col::mute);
        ImGui::EndGroup();
        Gap(8);
        static const unsigned swatches[] = {0x34D399, 0x2DD4BF, 0x38BDF8, 0x818CF8, 0xA78BFA, 0xE879F9, 0xFB7185, 0xFB923C, 0xFBBF24, 0xA3E635};
        unsigned cur = ds_.colorDraft.size() == 6 ? (unsigned)strtoul(ds_.colorDraft.c_str(), nullptr, 16) : 0xFFFFFFFFu;
        for (int i = 0; i < (int)(sizeof swatches / sizeof swatches[0]); ++i) {
            ImGui::PushID(i);
            if (i) ImGui::SameLine(0, S(9));
            if (ColorSwatch("sw", swatches[i], cur == swatches[i], 30)) {
                char b[8];
                snprintf(b, sizeof b, "%06X", swatches[i]);
                ds_.colorDraft = (cur == swatches[i]) ? "" : std::string(b);
                s.cfg.color = ds_.colorDraft;
                saveAll();
            }
            ImGui::PopID();
        }
        ImGui::SameLine(0, S(12));
        if (Button("Software colour", nullptr, Btn::Ghost, ImVec2(0, HS(30)))) {
            ds_.colorDraft.clear();
            s.cfg.color.clear();
            saveAll();
        }
        Gap(4);
        ImDrawList* dl2 = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float fullW = w;
        dl2->AddLine(ImVec2(p.x, std::floor(p.y) + 0.5f), ImVec2(p.x + fullW, std::floor(p.y) + 0.5f), Fade(Alpha(RGBA(col::border), 0.75f)), 1.f);
        Gap(12);
    }
    settingRow("Pin to top", "Pinned servers always sort first.", [&] {
        bool p = s.cfg.pinned;
        if (Toggle("pin", &p)) {
            s.cfg.pinned = p;
            saveAll();
        }
    });
    settingRow("Note", "A reminder shown on the card.", [&] {
        if (InputText("##note", &ds_.noteDraft, "e.g. Back up before updating plugins")) {
            s.cfg.note = util::trim(ds_.noteDraft);
            saveAll();
        }
    });
    EndCard();
    Gap(14);

    // ================= MOTD =================
    BeginCard("##motd", ImVec2(W, 0), 22);
    SectionTitle("Message of the day", "The line players read in their multiplayer list.", icon::Edit);
    Gap(12);
    {
        float previewW = std::min(W - D(44), S(560));
        float previewH = HS(96);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(previewW, previewH + S(10)));
        std::string vline = std::string(P::info(s.cfg.sw()).name) + (s.cfg.mcVersion.empty() ? "" : " " + s.cfg.mcVersion);
        motd::drawListEntry(ImGui::GetWindowDrawList(), p, previewW, previewH, serverColor(s), s.cfg.name, vline, ds_.motdDraft);
    }
    Gap(6);
    FieldLabel("MOTD", "(use & codes: &a green  &c red  &l bold  &r reset)");
    if (InputText("##motdinput", &ds_.motdDraft, "A Minecraft Server")) {
        pr.set("motd", motd::toSectionCodes(util::trim(ds_.motdDraft)));
        ds_.dirty = true;
    }
    Gap(8);
    {
        for (int i = 0; i < motd::colorCount(); ++i) {
            ImGui::PushID(i);
            if (i) ImGui::SameLine(0, S(5));
            if (ColorSwatch("mc", motd::colors()[i].rgb, false, 24)) {
                char code[4] = {'&', motd::colors()[i].code, 0, 0};
                ds_.motdDraft += code;
                pr.set("motd", motd::toSectionCodes(util::trim(ds_.motdDraft)));
                ds_.dirty = true;
            }
            Tooltip(motd::colors()[i].name);
            ImGui::PopID();
        }
        ImGui::SameLine(0, S(8));
        if (Button("Bold", nullptr, Btn::Ghost, ImVec2(0, HS(24)))) {
            ds_.motdDraft += "&l";
            pr.set("motd", motd::toSectionCodes(util::trim(ds_.motdDraft)));
            ds_.dirty = true;
        }
        ImGui::SameLine(0, S(4));
        if (Button("Reset", nullptr, Btn::Ghost, ImVec2(0, HS(24)))) ds_.motdDraft += "&r";
    }
    EndCard();
    Gap(14);

    // ================= icon =================
    BeginCard("##icon", ImVec2(W, 0), 22);
    SectionTitle("Server icon", "The 64x64 picture next to your server in the multiplayer list.", icon::Image);
    Gap(14);
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float size = HS(96);
        ImGui::Dummy(ImVec2(size, size));
        previewIcon(ImGui::GetWindowDrawList(), p, size, ds_.iconPattern, ds_.iconColor1, ds_.iconColor2);
        ImGui::SameLine(0, S(18));
        ImGui::BeginGroup();
        FieldLabel("Pattern");
        int pat = ds_.iconPattern;
        if (Segmented("##pat", kIconPatterns, kIconPatternCount, &pat, std::min(S(520), W - D(160)))) ds_.iconPattern = pat;
        Gap(6);
        FieldLabel("Colours");
        ImGui::BeginGroup();
        if (ColorSwatch("c1", ds_.iconColor1, false, 28)) {
            // cycle through a friendly palette
            static const unsigned pal[] = {0x34D399, 0x2DD4BF, 0x38BDF8, 0x818CF8, 0xA78BFA, 0xE879F9, 0xFB7185, 0xFB923C, 0xFBBF24, 0xA3E635};
            int idx = 0;
            for (int i = 0; i < 10; ++i)
                if (pal[i] == ds_.iconColor1) idx = i;
            ds_.iconColor1 = pal[(idx + 1) % 10];
        }
        Tooltip("Main colour - click to cycle");
        ImGui::SameLine(0, S(8));
        if (ColorSwatch("c2", ds_.iconColor2, false, 28)) {
            static const unsigned pal[] = {0x1F2937, 0x111827, 0x7F1D1D, 0x14532D, 0x1E3A8A, 0x4C1D95, 0x78350F, 0x0F172A};
            int idx = 0;
            for (int i = 0; i < 8; ++i)
                if (pal[i] == ds_.iconColor2) idx = i;
            ds_.iconColor2 = pal[(idx + 1) % 8];
        }
        Tooltip("Background colour - click to cycle");
        ImGui::EndGroup();
        Gap(10);
        if (Button("Write server-icon.png", icon::Save, Btn::Primary, ImVec2(0, HS(40)))) writeServerIcon(s);
        ImGui::SameLine(0, S(8));
        if (Button("Use my own image", icon::Image, Btn::Secondary, ImVec2(0, HS(40)))) {
            std::string f;
            if (util::pickFile(hwnd_, L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0All files\0*.*\0", f)) {
                fs::path dst = s.cfg.path() / "server-icon.png";
                bool ok = demo_ ? true : util::makeServerIcon(util::fromUtf8(f), dst);
                Toast(ok ? "Icon replaced with your image (resized to 64x64)" : "That image could not be read",
                      ok ? ToastKind::Success : ToastKind::Error);
            }
        }
        ImGui::EndGroup();
    }
    EndCard();
    Gap(14);

    // ================= presets =================
    BeginCard("##presets", ImVec2(W, 0), 22);
    SectionTitle("One-click setups", "Fills in the gameplay settings below. Nothing is written until you save.", icon::Bolt);
    Gap(14);
    {
        float gap = S(12);
        int cols = W > S(700) ? 3 : 2;
        float cw = (W - D(44) - gap * (cols - 1)) / cols;
        ImVec2 origin = ImGui::GetCursorScreenPos();
        for (int i = 0; i < kPresetCount; ++i) {
            ImGui::SetCursorScreenPos(ImVec2(origin.x + (i % cols) * (cw + gap), origin.y + (i / cols) * (HS(76) + gap)));
            ImGui::PushID(i);
            ImVec2 p = ImGui::GetCursorScreenPos();
            bool clicked = ImGui::InvisibleButton("##preset", ImVec2(cw, HS(76)));
            bool hov = ImGui::IsItemHovered();
            if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 q(p.x + cw, p.y + HS(76));
            float hv = Anim(ImGui::GetItemID() ^ 0x1234, hov ? 1.f : 0.f, 16.f);
            dl->AddRectFilled(p, q, Fade(Mix(RGBA(col::sunkenSoft), RGBA(col::panelHi), hv)), RD(13));
            dl->AddRect(p, q, Fade(Mix(RGBA(col::border), Accent(0.5f), hv)), RD(13), 0, 1.f);
            DrawStr(dl, fBold, 14.f, ImVec2(p.x + D(14), p.y + S(13)), RGBA(col::text), kPresets[i].name);
            dl->AddText(fRegular, FS(12.f), ImVec2(std::floor(p.x + D(14)), std::floor(p.y + S(34))), Fade(RGBA(col::mute)), kPresets[i].detail, nullptr,
                        cw - D(28));
            ImGui::PopID();
            if (clicked) applyGameplayPreset(s, i);
        }
        int rows = (kPresetCount + cols - 1) / cols;
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rows * (HS(76) + gap)));
        ImGui::Dummy(ImVec2(1, 1));
    }
    EndCard();
    Gap(14);

    // ================= gameplay =================
    settingSection("Gameplay", "Rules players will notice straight away.", [&] {
        settingRow("Difficulty", "Peaceful disables hostile mobs.", [&] {
            std::vector<const char*> items = {"peaceful", "easy", "normal", "hard"};
            std::string cur = pr.get("difficulty", "easy");
            int idx = 0;
            for (int i = 0; i < 4; ++i)
                if (cur == items[i]) idx = i;
            if (Combo("##diff", &idx, items.data(), 4)) { pr.set("difficulty", items[idx]); ds_.dirty = true; }
        });
        settingRow("Game mode", "Mode new players start in.", [&] {
            std::vector<const char*> items = {"survival", "creative", "adventure", "spectator"};
            std::string cur = pr.get("gamemode", "survival");
            int idx = 0;
            for (int i = 0; i < 4; ++i)
                if (cur == items[i]) idx = i;
            if (Combo("##gm", &idx, items.data(), 4)) { pr.set("gamemode", items[idx]); ds_.dirty = true; }
        });
        settingRow("Hardcore", "Players are banned when they die.", [&] {
            bool v = pr.getBool("hardcore", false);
            if (Toggle("hc", &v)) { pr.setBool("hardcore", v); ds_.dirty = true; }
        });
        settingRow("PvP", "Let players damage each other.", [&] {
            bool v = pr.getBool("pvp", true);
            if (Toggle("pvp", &v)) { pr.setBool("pvp", v); ds_.dirty = true; }
        });
        settingRow("Allow flight", "Stops anti-fly kicks. Needed by some mods.", [&] {
            bool v = pr.getBool("allow-flight", false);
            if (Toggle("fly", &v)) { pr.setBool("allow-flight", v); ds_.dirty = true; }
        });
        settingRow("Command blocks", "Allow command blocks to run.", [&] {
            bool v = pr.getBool("enable-command-block", false);
            if (Toggle("cb", &v)) { pr.setBool("enable-command-block", v); ds_.dirty = true; }
        });
        settingRow("Monsters", "Spawn hostile mobs.", [&] {
            bool v = pr.getBool("spawn-monsters", true);
            if (Toggle("mon", &v)) { pr.setBool("spawn-monsters", v); ds_.dirty = true; }
        });
        settingRow("Animals", "Spawn passive animals.", [&] {
            bool v = pr.getBool("spawn-animals", true);
            if (Toggle("ani", &v)) { pr.setBool("spawn-animals", v); ds_.dirty = true; }
        });
        settingRow("Spawn protection", "Radius around spawn where only operators can build.", [&] {
            int v = pr.getInt("spawn-protection", 16);
            if (SliderInt("##sp", &v, 0, 64)) { pr.setInt("spawn-protection", v); ds_.dirty = true; }
        });
        settingRow("Max players", "Player slots.", [&] {
            int v = pr.getInt("max-players", 20);
            if (SliderInt("##mp", &v, 1, 200)) { pr.setInt("max-players", v); ds_.dirty = true; }
        });
    });

    settingSection("World", "Where the world lives and how it is generated.", [&] {
        settingRow("World folder", "Name of the folder inside the server directory.", [&] {
            std::string v = pr.get("level-name", "world");
            if (InputText("##lvl", &v)) { pr.set("level-name", v); ds_.dirty = true; }
        });
        settingRow("Seed", "Leave empty for a random world.", [&] {
            std::string v = pr.get("level-seed", "");
            if (InputText("##seed", &v, "Random")) { pr.set("level-seed", v); ds_.dirty = true; }
        });
        settingRow("World type", "Changes terrain generation for new worlds.", [&] {
            std::vector<const char*> items = {"minecraft:normal", "minecraft:flat", "minecraft:large_biomes", "minecraft:amplified", "minecraft:single_biome_surface"};
            std::string cur = pr.get("level-type", "minecraft:normal");
            int idx = 0;
            for (int i = 0; i < 5; ++i)
                if (cur == items[i]) idx = i;
            if (Combo("##lt", &idx, items.data(), 5)) { pr.set("level-type", items[idx]); ds_.dirty = true; }
        });
        settingRow("View distance", "Chunks sent to players. Lower is faster.", [&] {
            int v = pr.getInt("view-distance", 10);
            if (SliderInt("##vd", &v, 2, 32)) { pr.setInt("view-distance", v); ds_.dirty = true; }
        });
        settingRow("Simulation distance", "Chunks that are actively ticked.", [&] {
            int v = pr.getInt("simulation-distance", 10);
            if (SliderInt("##sd", &v, 3, 32)) { pr.setInt("simulation-distance", v); ds_.dirty = true; }
        });
        settingRow("Nether", "Enable travel to the Nether.", [&] {
            bool v = pr.getBool("allow-nether", true);
            if (Toggle("nether", &v)) { pr.setBool("allow-nether", v); ds_.dirty = true; }
        });
    });

    settingSection("Access & sharing", "Who can join and how they reach you.", [&] {
        settingRow("Online mode", "Verify players with Mojang. Turn off only for offline play.", [&] {
            bool v = pr.getBool("online-mode", true);
            if (Toggle("om", &v)) { pr.setBool("online-mode", v); ds_.dirty = true; }
        });
        settingRow("Whitelist", "Only whitelisted players may join.", [&] {
            bool v = pr.getBool("white-list", false);
            if (Toggle("wl", &v)) { pr.setBool("white-list", v); ds_.dirty = true; }
        });
        settingRow("Enforce whitelist", "Kick players removed from the list.", [&] {
            bool v = pr.getBool("enforce-whitelist", false);
            if (Toggle("ewl", &v)) { pr.setBool("enforce-whitelist", v); ds_.dirty = true; }
        });
        settingRow("Port", "The TCP port players connect on.", [&] {
            int v = pr.getInt("server-port", 25565);
            if (InputInt("##port2", &v, 1, 65535, 140)) { pr.setInt("server-port", v); ds_.dirty = true; }
        });
        settingRow("Address to share", "Give this to friends on your local network.", [&] {
            std::string addr = util::localIPv4() + ":" + std::to_string(pr.getInt("server-port", 25565));
            Label(addr.c_str(), 14.f, col::text, true);
            ImGui::SameLine(0, S(8));
            if (Button("Copy", icon::Copy, Btn::Ghost, ImVec2(0, HS(30)))) {
                util::setClipboard(addr);
                Toast("Copied " + addr, ToastKind::Success);
            }
        });
    });

    settingSection("Resource pack", "Optional pack every player downloads when they join.", [&] {
        settingRow("Pack URL", "A direct link to the .zip file.", [&] {
            std::string v = pr.get("resource-pack", "");
            if (InputText("##rp", &v, "https://example.com/pack.zip")) { pr.set("resource-pack", v); ds_.dirty = true; }
        });
        settingRow("Prompt", "Message shown before the download.", [&] {
            std::string v = pr.get("resource-pack-prompt", "");
            if (InputText("##rpp", &v, "Optional")) { pr.set("resource-pack-prompt", v); ds_.dirty = true; }
        });
        settingRow("Required", "Kick players who decline the pack.", [&] {
            bool v = pr.getBool("require-resource-pack", false);
            if (Toggle("rpr", &v)) { pr.setBool("require-resource-pack", v); ds_.dirty = true; }
        });
    });

    Gap(24);
    EndScroll();
    if (ds_.dirty) drawSaveBar(s, W);
}

void App::drawSaveBar(ServerInstance& s, float width) {
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(10));
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = std::min(ImGui::GetContentRegionAvail().x - S(4), width), h = HS(62);
    ImGui::Dummy(ImVec2(w, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (theme::g_opt.shadows) DrawShadow(dl, p, ImVec2(p.x + w, p.y + h), RD(16), 0.8f);
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Fade(RGBA(col::panelSoft)), RD(16));
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), Fade(Accent(0.5f)), RD(16), 0, 1.f);
    dl->AddCircleFilled(ImVec2(p.x + D(26), p.y + h * 0.5f), S(5), Fade(RGBA(col::amber)), 16);
    DrawStr(dl, fBold, 14.5f, ImVec2(p.x + D(44), p.y + (h - S(20)) * 0.5f), RGBA(col::text), "You have unsaved changes");
    float sw = ButtonWidth("Save changes", true), dw = ButtonWidth("Discard", false);
    ImGui::SetCursorScreenPos(ImVec2(p.x + w - D(12) - sw, p.y + (h - HS(40)) * 0.5f));
    if (Button("Save changes", icon::Save, Btn::Primary, ImVec2(0, HS(40)))) saveDraft(s);
    ImGui::SetCursorScreenPos(ImVec2(p.x + w - D(12) - sw - S(8) - dw, p.y + (h - HS(40)) * 0.5f));
    if (Button("Discard", nullptr, Btn::Ghost, ImVec2(0, HS(40)))) {
        loadDraft(s);
        ds_.lookLoaded = false;
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
    ImGui::Dummy(ImVec2(1, 1));
}
