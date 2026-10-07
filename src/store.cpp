#include "store.h"

#include <algorithm>

#include "nlohmann/json.hpp"

using nlohmann::json;

namespace {
// Every field is fetched defensively: a single key of the wrong type used to throw out of the
// whole load(), silently discarding all the settings that followed it.
template <class T>
T jvalue(const json& j, const char* key, T def) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    try {
        return it->get<T>();
    } catch (...) {
        return def;
    }
}
}  // namespace

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
        if (!j.is_object()) return;
        const bool legacy = !j.contains("settingsVersion");
        serversRoot = jvalue(j, "serversRoot", serversRoot);
        defaultRamMB = jvalue(j, "defaultRamMB", defaultRamMB);
        accent = legacy ? migrateAccent(jvalue(j, "accent", 0)) : jvalue(j, "accent", accent);
        stopOnExit = jvalue(j, "stopOnExit", stopOnExit);

        theme = jvalue(j, "theme", theme);
        customAccent = jvalue(j, "customAccent", customAccent);
        customAccentRgb = jvalue(j, "customAccentRgb", customAccentRgb) & 0xFFFFFFu;
        density = jvalue(j, "density", density);
        radius = jvalue(j, "radius", radius);
        textScale = jvalue(j, "textScale", textScale);
        monoScale = jvalue(j, "monoScale", monoScale);
        animSpeed = jvalue(j, "animSpeed", animSpeed);
        shadows = jvalue(j, "shadows", shadows);
        backdropGlow = jvalue(j, "backdropGlow", backdropGlow);
        bgStyle = jvalue(j, "bgStyle", bgStyle);
        bgStrength = jvalue(j, "bgStrength", bgStrength);
        fontFamily = jvalue(j, "fontFamily", fontFamily);
        monoFamily = jvalue(j, "monoFamily", monoFamily);

        sidebarCollapsed = jvalue(j, "sidebarCollapsed", sidebarCollapsed);
        sidebarWidth = jvalue(j, "sidebarWidth", sidebarWidth);
        showSidebarStatus = jvalue(j, "showSidebarStatus", showSidebarStatus);
        toastCorner = jvalue(j, "toastCorner", toastCorner);
        toastSeconds = jvalue(j, "toastSeconds", toastSeconds);
        confirmDestructive = jvalue(j, "confirmDestructive", confirmDestructive);
        showClock = jvalue(j, "showClock", showClock);
        clock24h = jvalue(j, "clock24h", clock24h);
        restoreWindow = jvalue(j, "restoreWindow", restoreWindow);
        windowW = jvalue(j, "windowW", windowW);
        windowH = jvalue(j, "windowH", windowH);
        startPage = jvalue(j, "startPage", startPage);

        serverView = jvalue(j, "serverView", serverView);
        cardSize = jvalue(j, "cardSize", cardSize);
        sortMode = jvalue(j, "sortMode", sortMode);
        showStatTiles = jvalue(j, "showStatTiles", showStatTiles);
        showCardActions = jvalue(j, "showCardActions", showCardActions);

        logMaxLines = jvalue(j, "logMaxLines", logMaxLines);
        logTimestamps = jvalue(j, "logTimestamps", logTimestamps);
        logWrap = jvalue(j, "logWrap", logWrap);
        consoleAutoScroll = jvalue(j, "consoleAutoScroll", consoleAutoScroll);
        editorTabSize = jvalue(j, "editorTabSize", editorTabSize);
        editorWrap = jvalue(j, "editorWrap", editorWrap);

        autoInstallJava = jvalue(j, "autoInstallJava", autoInstallJava);
    } catch (...) {}
    normalize();
}

// settings.json is user-editable (and theme files are imported from anywhere), so nothing that
// reaches the layout maths or an allocation may be trusted: a tiny radius or a huge line count
// would otherwise divide by zero or reserve gigabytes.
void Settings::normalize() {
    theme = std::clamp(theme, 0, theme::PaletteCount() - 1);
    accent = std::clamp(accent, 0, theme::AccentCount() - 1);
    customAccentRgb &= 0xFFFFFFu;
    density = std::clamp(density, 0, 2);
    radius = std::clamp(radius, 0.f, 1.8f);
    textScale = std::clamp(textScale, 0.85f, 1.4f);
    monoScale = std::clamp(monoScale, 0.85f, 1.5f);
    animSpeed = std::clamp(animSpeed, 0.f, 1.6f);
    bgStyle = std::clamp(bgStyle, 0, 4);
    bgStrength = std::clamp(bgStrength, 0.f, 1.5f);
    fontFamily = std::clamp(fontFamily, 0, theme::FontFamilyCount() - 1);
    monoFamily = std::clamp(monoFamily, 0, theme::MonoFamilyCount() - 1);
    sidebarWidth = std::clamp(sidebarWidth, 210, 340);
    toastCorner = std::clamp(toastCorner, 0, 3);
    toastSeconds = std::clamp(toastSeconds, 1.5f, 12.f);
    windowW = std::clamp(windowW, 900, 8000);
    windowH = std::clamp(windowH, 600, 8000);
    serverView = std::clamp(serverView, 0, 1);
    cardSize = std::clamp(cardSize, 0, 2);
    sortMode = std::clamp(sortMode, 0, 3);
    logMaxLines = std::clamp(logMaxLines, 500, 20000);
    editorTabSize = editorTabSize == 4 ? 4 : 2;
    if (defaultRamMB < 512) defaultRamMB = 512;
    if (startPage != "servers" && startPage != "wizard" && startPage != "import") startPage = "servers";
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
    // Fetching the default eagerly called newId() for every server on every load, and a
    // mistyped id key would have thrown out of the whole parse.
    c.id = jvalue(j, "id", std::string());
    if (c.id.empty()) c.id = util::newId();
    c.name = jvalue(j, "name", std::string("Server"));
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
    c.note = jvalue(j, "note", std::string());
    if (c.color.size() != 6) c.color.clear();
    // Only reject nonsense: a hand-edited entry must not ask for a negative heap, but the
    // ceiling has to stay far above anything a real machine would use for one server.
    c.maxRamMB = std::clamp(c.maxRamMB, 512, 1 << 20);
    c.minRamMB = std::clamp(c.minRamMB, 256, c.maxRamMB);
    c.javaMajor = std::clamp(c.javaMajor, 0, 64);
    if (c.launchMode != "jar" && c.launchMode != "args") c.launchMode = "jar";
    return c;
}

void loadServers(std::vector<std::shared_ptr<ServerInstance>>& out) {
    std::string txt;
    if (!util::readFile(util::appDataDir() / "servers.json", txt)) return;
    try {
        json j = json::parse(txt);
        if (!j.is_array()) return;
        for (auto& e : j) out.push_back(std::make_shared<ServerInstance>(fromJson(e)));
    } catch (...) {}
}

void saveServers(const std::vector<std::shared_ptr<ServerInstance>>& servers) {
    json arr = json::array();
    for (auto& s : servers) arr.push_back(toJson(s->cfg));
    util::writeFile(util::appDataDir() / "servers.json", arr.dump(2));
}

}  // namespace store
