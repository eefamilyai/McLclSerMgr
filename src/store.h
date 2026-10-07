#pragma once
#include <memory>
#include <vector>

#include "server.h"
#include "theme.h"

// Everything the personalization page can change, persisted to %APPDATA%\Voxual\settings.json.
struct Settings {
    // ---- storage & behaviour (pre-existing keys keep working) -----------------
    std::string serversRoot;      // UTF-8
    int defaultRamMB = 2048;
    int accent = 0;
    bool stopOnExit = true;

    // ---- appearance ----------------------------------------------------------
    int theme = 0;                    // palette index (see theme::PaletteCount)
    bool customAccent = false;
    unsigned customAccentRgb = 0x34D399;
    int density = 1;                  // 0 compact, 1 comfortable, 2 spacious
    float radius = 1.f;               // corner rounding multiplier
    float textScale = 1.f;            // body text size multiplier
    float monoScale = 1.f;            // console / editor text multiplier
    float animSpeed = 1.f;            // 0 = no animations
    bool shadows = true;
    bool backdropGlow = true;
    int bgStyle = 1;                  // theme::BgStyle
    float bgStrength = 1.f;
    int fontFamily = 0;               // theme::FontFamilyName index
    int monoFamily = 0;

    // ---- window & navigation -------------------------------------------------
    bool sidebarCollapsed = false;
    int sidebarWidth = 258;
    bool showSidebarStatus = true;    // memory + java card at the bottom of the rail
    int toastCorner = 1;              // 0 TR, 1 BR, 2 BL, 3 TL
    float toastSeconds = 4.2f;
    bool confirmDestructive = true;
    bool showClock = true;
    bool clock24h = true;
    bool restoreWindow = true;
    int windowW = 1380, windowH = 880;
    std::string startPage = "servers";

    // ---- server list ---------------------------------------------------------
    int serverView = 0;               // 0 grid, 1 list
    int cardSize = 1;                 // 0 compact, 1 normal, 2 large
    int sortMode = 0;                 // 0 name, 1 status, 2 players, 3 recent
    bool showStatTiles = true;
    bool showCardActions = true;      // start/stop buttons directly on the cards

    // ---- console & editor ----------------------------------------------------
    int logMaxLines = 4000;
    bool logTimestamps = true;
    bool logWrap = false;
    bool consoleAutoScroll = true;
    int editorTabSize = 2;
    bool editorWrap = false;

    // ---- java ----------------------------------------------------------------
    bool autoInstallJava = true;

    void load();
    void save() const;
    void normalize();                 // clamps values a hand-edited file or theme import could push out of range
    fs::path root() const;            // never empty
    theme::Options themeOptions() const;
};

namespace store {
// Shared ownership: a background backup or restore keeps the server it is working on alive
// even if the user removes it from the list mid-flight.
void loadServers(std::vector<std::shared_ptr<ServerInstance>>& out);
void saveServers(const std::vector<std::shared_ptr<ServerInstance>>& servers);
}  // namespace store
