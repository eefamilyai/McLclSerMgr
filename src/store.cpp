#include "store.h"

#include "nlohmann/json.hpp"

using nlohmann::json;

// The accent list was reordered when the palette grew; map the old indices across.
static int migrateAccent(int old) {
    static const int map[6] = {0, 3, 6, 10, 8, 9};   // Emerald, Sky, Violet, Amber, Rose, Orange
    return (old >= 0 && old < 6) ? map[old] : 0;
}

void Settings::load() {
    serversRoot = util::pathStr(util::defaultServersRoot());
    std::string txt;
    if (!util::readFile(util::appDataDir() / "settings.json", txt)) return;
    try {
        json j = json::parse(txt);
        const bool legacy = !j.contains("settingsVersion");
        serversRoot = j.value("serversRoot", serversRoot);
        defaultRamMB = j.value("defaultRamMB", defaultRamMB);
        accent = legacy ? migrateAccent(j.value("accent", 0)) : j.value("accent", accent);
        stopOnExit = j.value("stopOnExit", stopOnExit);

        theme = j.value("theme", theme);
        customAccent = j.value("customAccent", customAccent);
        customAccentRgb = j.value("customAccentRgb", customAccentRgb) & 0xFFFFFFu;
        density = j.value("density", density);
        radius = j.value("radius", radius);
        textScale = j.value("textScale", textScale);
        monoScale = j.value("monoScale", monoScale);
        animSpeed = j.value("animSpeed", animSpeed);
        shadows = j.value("shadows", shadows);
        backdropGlow = j.value("backdropGlow", backdropGlow);
        bgStyle = j.value("bgStyle", bgStyle);
        bgStrength = j.value("bgStrength", bgStrength);
        fontFamily = j.value("fontFamily", fontFamily);
        monoFamily = j.value("monoFamily", monoFamily);

        sidebarCollapsed = j.value("sidebarCollapsed", sidebarCollapsed);
        sidebarWidth = j.value("sidebarWidth", sidebarWidth);
        showSidebarStatus = j.value("showSidebarStatus", showSidebarStatus);
        toastCorner = j.value("toastCorner", toastCorner);
        toastSeconds = j.value("toastSeconds", toastSeconds);
        confirmDestructive = j.value("confirmDestructive", confirmDestructive);
        showClock = j.value("showClock", showClock);
        clock24h = j.value("clock24h", clock24h);
        restoreWindow = j.value("restoreWindow", restoreWindow);
        windowW = j.value("windowW", windowW);
        windowH = j.value("windowH", windowH);
        startPage = j.value("startPage", startPage);

        serverView = j.value("serverView", serverView);
        cardSize = j.value("cardSize", cardSize);
        sortMode = j.value("sortMode", sortMode);
        showStatTiles = j.value("showStatTiles", showStatTiles);
        showCardActions = j.value("showCardActions", showCardActions);

        logMaxLines = j.value("logMaxLines", logMaxLines);
        logTimestamps = j.value("logTimestamps", logTimestamps);
        logWrap = j.value("logWrap", logWrap);
        consoleAutoScroll = j.value("consoleAutoScroll", consoleAutoScroll);
        editorTabSize = j.value("editorTabSize", editorTabSize);
        editorWrap = j.value("editorWrap", editorWrap);

        autoInstallJava = j.value("autoInstallJava", autoInstallJava);
    } catch (...) {}
}

void Settings::save() const {
    json j;
    j["settingsVersion"] = 2;
    j["serversRoot"] = serversRoot;
    j["defaultRamMB"] = defaultRamMB;
    j["accent"] = accent;
    j["stopOnExit"] = stopOnExit;

    j["theme"] = theme;
    j["customAccent"] = customAccent;
    j["customAccentRgb"] = customAccentRgb;
    j["density"] = density;
    j["radius"] = radius;
    j["textScale"] = textScale;
    j["monoScale"] = monoScale;
    j["animSpeed"] = animSpeed;
    j["shadows"] = shadows;
    j["backdropGlow"] = backdropGlow;
    j["bgStyle"] = bgStyle;
    j["bgStrength"] = bgStrength;
    j["fontFamily"] = fontFamily;
    j["monoFamily"] = monoFamily;

    j["sidebarCollapsed"] = sidebarCollapsed;
    j["sidebarWidth"] = sidebarWidth;
    j["showSidebarStatus"] = showSidebarStatus;
    j["toastCorner"] = toastCorner;
    j["toastSeconds"] = toastSeconds;
    j["confirmDestructive"] = confirmDestructive;
    j["showClock"] = showClock;
    j["clock24h"] = clock24h;
    j["restoreWindow"] = restoreWindow;
    j["windowW"] = windowW;
    j["windowH"] = windowH;
    j["startPage"] = startPage;

    j["serverView"] = serverView;
    j["cardSize"] = cardSize;
    j["sortMode"] = sortMode;
    j["showStatTiles"] = showStatTiles;
    j["showCardActions"] = showCardActions;

    j["logMaxLines"] = logMaxLines;
    j["logTimestamps"] = logTimestamps;
    j["logWrap"] = logWrap;
    j["consoleAutoScroll"] = consoleAutoScroll;
    j["editorTabSize"] = editorTabSize;
    j["editorWrap"] = editorWrap;

    j["autoInstallJava"] = autoInstallJava;
    util::writeFile(util::appDataDir() / "settings.json", j.dump(2));
}

fs::path Settings::root() const {
    return serversRoot.empty() ? util::defaultServersRoot() : util::fromUtf8(serversRoot);
}

theme::Options Settings::themeOptions() const {
    theme::Options o;
    o.palette = theme;
    o.accent = accent;
    o.customAccent = customAccent;
    o.customAccentRgb = customAccentRgb;
    o.density = density;
    o.radius = radius;
    o.textScale = textScale;
    o.monoScale = monoScale;
    o.animSpeed = animSpeed;
    o.shadows = shadows;
    o.backdropGlow = backdropGlow;
    o.bgStyle = bgStyle;
    o.bgStrength = bgStrength;
    o.fontFamily = fontFamily;
    o.monoFamily = monoFamily;
    return o;
}

namespace store {

static json toJson(const ServerConfig& c) {
    return {{"id", c.id},           {"name", c.name},           {"software", c.software},       {"mcVersion", c.mcVersion},
            {"build", c.build},     {"dir", c.dir},             {"launchMode", c.launchMode},   {"launchTarget", c.launchTarget},
            {"minRamMB", c.minRamMB}, {"maxRamMB", c.maxRamMB}, {"javaMajor", c.javaMajor},     {"javaPath", c.javaPath},
            {"extraArgs", c.extraArgs}, {"aikarFlags", c.aikarFlags}, {"autoRestart", c.autoRestart}, {"autoStart", c.autoStart},
            {"createdAt", c.createdAt}, {"lastStarted", c.lastStarted},
            {"color", c.color},     {"pinned", c.pinned},       {"note", c.note}};
}

static ServerConfig fromJson(const json& j) {
    ServerConfig c;
    c.id = j.value("id", util::newId());
    c.name = j.value("name", "Server");
    c.software = j.value("software", "vanilla");
    c.mcVersion = j.value("mcVersion", "");
    c.build = j.value("build", "");
    c.dir = j.value("dir", "");
    c.launchMode = j.value("launchMode", "jar");
    c.launchTarget = j.value("launchTarget", "server.jar");
    c.minRamMB = j.value("minRamMB", 1024);
    c.maxRamMB = j.value("maxRamMB", 2048);
    c.javaMajor = j.value("javaMajor", 0);
    c.javaPath = j.value("javaPath", "");
    c.extraArgs = j.value("extraArgs", "");
    c.aikarFlags = j.value("aikarFlags", false);
    c.autoRestart = j.value("autoRestart", false);
    c.autoStart = j.value("autoStart", false);
    c.createdAt = j.value("createdAt", (int64_t)0);
    c.lastStarted = j.value("lastStarted", (int64_t)0);
    c.color = j.value("color", "");
    c.pinned = j.value("pinned", false);
    c.note = j.value("note", "");
    return c;
}

void loadServers(std::vector<std::unique_ptr<ServerInstance>>& out) {
    std::string txt;
    if (!util::readFile(util::appDataDir() / "servers.json", txt)) return;
    try {
        for (auto& j : json::parse(txt)) out.push_back(std::make_unique<ServerInstance>(fromJson(j)));
    } catch (...) {}
}

void saveServers(const std::vector<std::unique_ptr<ServerInstance>>& servers) {
    json arr = json::array();
    for (auto& s : servers) arr.push_back(toJson(s->cfg));
    util::writeFile(util::appDataDir() / "servers.json", arr.dump(2));
}

}  // namespace store
