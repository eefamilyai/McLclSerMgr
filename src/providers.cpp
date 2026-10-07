#include "providers.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <regex>
#include <string_view>

#include "http.h"
#include "nlohmann/json.hpp"
#include "util.h"

using nlohmann::json;
using nlohmann::ordered_json;

namespace providers {

namespace {

const SoftwareInfo kInfos[kSoftwareCount] = {
    {Software::Vanilla, "vanilla", "Vanilla", "Official Mojang server",
     "Pure Minecraft exactly as Mojang ships it. No plugins, no mods - just the base game.", "", "Official", "", 0x7BC043},
    {Software::Paper, "paper", "Paper", "Fast & plugin-friendly",
     "High-performance Spigot fork with a huge plugin ecosystem. The go-to choice for most servers.", "plugins",
     "Plugins", "Recommended", 0x4F9DFF},
    {Software::Purpur, "purpur", "Purpur", "Paper with extra tweaks",
     "A Paper fork packed with gameplay options and configuration for server owners who love to tinker.", "plugins",
     "Plugins", "Configurable", 0xB66DFF},
    {Software::Folia, "folia", "Folia", "Regionised multithreading",
     "Paper fork that splits the world into regions running in parallel. Built for very large servers.", "plugins",
     "Plugins", "Advanced", 0x2DD4BF},
    {Software::Fabric, "fabric", "Fabric", "Light & modern modding",
     "Lightweight, fast-updating mod loader with a thriving modern mod scene.", "mods", "Mods", "Lightweight", 0xE9C9A0},
    {Software::Forge, "forge", "Forge", "The classic mod loader",
     "The long-standing mod loader with the biggest library of big content mods and modpacks.", "mods", "Mods",
     "Classic", 0xF2683C},
    {Software::NeoForge, "neoforge", "NeoForge", "Modern Forge successor",
     "Community-driven continuation of Forge with a cleaner API and active development.", "mods", "Mods", "Modern",
     0xFFA726},
};

std::mutex g_cacheMu;
std::map<int, std::vector<Version>> g_cache;
std::map<std::string, int> g_javaCache;
std::once_flag g_mojangOnce;

struct MojangVersion {
    std::string id, type, url;
};
std::vector<MojangVersion> g_mojang;
std::string g_mojangErr;
bool g_mojangOk = false;

// The manifest is fetched once. Holding the cache mutex across the request made every other
// thread wait out the whole download, so the fetch has its own once-flag instead.
bool loadMojang(std::string& err) {
    std::call_once(g_mojangOnce, [] {
        auto r = http::get("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json");
        if (!r.ok()) { g_mojangErr = r.error; return; }
        try {
            json j = json::parse(r.body);
            for (auto& v : j.at("versions"))
                g_mojang.push_back({v.at("id").get<std::string>(), v.at("type").get<std::string>(), v.at("url").get<std::string>()});
            g_mojangOk = !g_mojang.empty();
        } catch (const std::exception& e) { g_mojangErr = std::string("Bad Mojang manifest: ") + e.what(); }
    });
    err = g_mojangErr;
    return g_mojangOk;
}

bool isStableName(const std::string& id) {
    static const char* bad[] = {"-rc", "-pre", "snapshot", "-beta", "-alpha"};
    for (auto b : bad)
        if (id.find(b) != std::string::npos) return false;
    return true;
}

// Neoforge "21.1.200" -> "1.21.1", "26.3.0.39-beta" -> "26.3"
std::string neoToMc(const std::string& v) {
    std::string core = v.substr(0, v.find('-'));
    auto p = util::split(core, '.');
    if (p.size() < 2) return {};
    int major = atoi(p[0].c_str());
    if (major >= 26) {
        std::string mc = p[0] + "." + p[1];
        if (p.size() >= 4 && p[2] != "0") mc += "." + p[2];
        return mc;
    }
    std::string mc = "1." + p[0];
    if (p[1] != "0") mc += "." + p[1];
    return mc;
}

std::string stripBeta(const std::string& v) { return v.substr(0, v.find('-')); }

bool listPaperFamily(const char* project, std::vector<Version>& out, std::string& err) {
    auto r = http::get(std::string("https://fill.papermc.io/v3/projects/") + project);
    if (!r.ok()) { err = r.error; return false; }
    try {
        ordered_json j = ordered_json::parse(r.body);
        for (auto& fam : j.at("versions"))
            for (auto& v : fam) {
                std::string id = v.get<std::string>();
                out.push_back({id, isStableName(id)});
            }
    } catch (const std::exception& e) { err = e.what(); return false; }
    return true;
}

bool fetchPaperFamily(const char* project, const std::string& ver, Download& d, std::string& err) {
    auto r = http::get(std::string("https://fill.papermc.io/v3/projects/") + project + "/versions/" + ver + "/builds/latest");
    if (!r.ok()) { err = "No " + std::string(project) + " build found for " + ver + " (" + r.error + ")"; return false; }
    try {
        json j = json::parse(r.body);
        auto& dl = j.at("downloads").at("server:default");
        d.url = dl.at("url").get<std::string>();
        d.filename = dl.at("name").get<std::string>();
        d.build = "build " + std::to_string(j.at("id").get<int>());
    } catch (const std::exception& e) { err = e.what(); return false; }
    return true;
}

}  // namespace

const SoftwareInfo& info(Software s) { return kInfos[(int)s]; }
const SoftwareInfo& infoByIndex(int i) { return kInfos[i]; }

Software fromKey(const std::string& key) {
    for (auto& i : kInfos)
        if (key == i.key) return i.id;
    return Software::Vanilla;
}

bool listVersions(Software s, std::vector<Version>& out, std::string& err) {
    {
        std::lock_guard<std::mutex> lk(g_cacheMu);
        auto it = g_cache.find((int)s);
        if (it != g_cache.end()) { out = it->second; return true; }
    }
    std::vector<Version> res;
    switch (s) {
    case Software::Vanilla: {
        if (!loadMojang(err)) return false;
        for (auto& v : g_mojang) {
            if (v.type == "old_alpha" || v.type == "old_beta") continue;
            res.push_back({v.id, v.type == "release"});
            if (v.id == "1.2.5") break;  // no server jars before this
        }
        break;
    }
    case Software::Paper:
    case Software::Folia:
        if (!listPaperFamily(info(s).key, res, err)) return false;
        break;
    case Software::Purpur: {
        auto r = http::get("https://api.purpurmc.org/v2/purpur");
        if (!r.ok()) { err = r.error; return false; }
        try {
            json j = json::parse(r.body);
            auto vs = j.at("versions").get<std::vector<std::string>>();
            for (auto it = vs.rbegin(); it != vs.rend(); ++it) res.push_back({*it, isStableName(*it)});
        } catch (const std::exception& e) { err = e.what(); return false; }
        break;
    }
    case Software::Fabric: {
        auto r = http::get("https://meta.fabricmc.net/v2/versions/game");
        if (!r.ok()) { err = r.error; return false; }
        try {
            for (auto& v : json::parse(r.body)) res.push_back({v.at("version").get<std::string>(), v.at("stable").get<bool>()});
        } catch (const std::exception& e) { err = e.what(); return false; }
        break;
    }
    case Software::Forge: {
        auto r = http::get("https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json");
        if (!r.ok()) { err = r.error; return false; }
        try {
            json j = json::parse(r.body);
            std::map<std::string, bool> versions;  // mc -> has recommended
            static const std::regex re(R"(^(\d+\.\d+(?:\.\d+)?)-(latest|recommended)$)");
            for (auto& [k, v] : j.at("promos").items()) {
                std::smatch m;
                if (!std::regex_match(k, m, re)) continue;
                std::string mc = m[1];
                if (util::compareVersions(mc, "1.14") < 0) continue;
                versions[mc] = versions[mc] || m[2] == "recommended";
            }
            for (auto& [mc, rec] : versions) res.push_back({mc, rec});
            std::sort(res.begin(), res.end(), [](const Version& a, const Version& b) { return util::compareVersions(a.id, b.id) > 0; });
        } catch (const std::exception& e) { err = e.what(); return false; }
        break;
    }
    case Software::NeoForge: {
        auto r = http::get("https://maven.neoforged.net/api/maven/versions/releases/net/neoforged/neoforge");
        if (!r.ok()) { err = r.error; return false; }
        try {
            json j = json::parse(r.body);
            static const std::regex re(R"(^\d+\.\d+(\.\d+)*(-beta)?$)");
            std::map<std::string, bool> versions;  // mc -> has non-beta
            for (auto& v : j.at("versions")) {
                std::string ver = v.get<std::string>();
                if (!std::regex_match(ver, re)) continue;
                std::string mc = neoToMc(ver);
                if (mc.empty() || util::compareVersions(mc, "1.20.2") < 0) continue;
                versions[mc] = versions[mc] || ver.find("-beta") == std::string::npos;
            }
            for (auto& [mc, stable] : versions) res.push_back({mc, stable});
            std::sort(res.begin(), res.end(), [](const Version& a, const Version& b) { return util::compareVersions(a.id, b.id) > 0; });
        } catch (const std::exception& e) { err = e.what(); return false; }
        break;
    }
    }
    if (res.empty()) { err = "No versions returned by the server"; return false; }
    {
        std::lock_guard<std::mutex> lk(g_cacheMu);
        g_cache[(int)s] = res;
    }
    out = std::move(res);
    return true;
}

bool resolve(Software s, const std::string& ver, Download& d, std::string& err) {
    d = {};
    d.mcVersion = ver;
    switch (s) {
    case Software::Vanilla: {
        if (!loadMojang(err)) return false;
        std::string url;
        for (auto& v : g_mojang)
            if (v.id == ver) url = v.url;
        if (url.empty()) { err = "Unknown Minecraft version " + ver; return false; }
        auto r = http::get(url);
        if (!r.ok()) { err = r.error; return false; }
        try {
            json j = json::parse(r.body);
            if (!j.at("downloads").contains("server")) { err = "Mojang provides no server download for " + ver; return false; }
            d.url = j["downloads"]["server"]["url"].get<std::string>();
            d.filename = "server.jar";
            d.build = "official";
        } catch (const std::exception& e) { err = e.what(); return false; }
        return true;
    }
    case Software::Paper:
    case Software::Folia:
        return fetchPaperFamily(info(s).key, ver, d, err);
    case Software::Purpur: {
        auto r = http::get("https://api.purpurmc.org/v2/purpur/" + ver);
        if (!r.ok()) { err = "No Purpur build for " + ver; return false; }
        try {
            json j = json::parse(r.body);
            std::string build = j.at("builds").at("latest").get<std::string>();
            d.url = "https://api.purpurmc.org/v2/purpur/" + ver + "/" + build + "/download";
            d.filename = "purpur-" + ver + "-" + build + ".jar";
            d.build = "build " + build;
        } catch (const std::exception& e) { err = e.what(); return false; }
        return true;
    }
    case Software::Fabric: {
        auto loaders = http::get("https://meta.fabricmc.net/v2/versions/loader");
        auto installers = http::get("https://meta.fabricmc.net/v2/versions/installer");
        if (!loaders.ok() || !installers.ok()) { err = loaders.ok() ? installers.error : loaders.error; return false; }
        try {
            std::string loader, installer;
            for (auto& l : json::parse(loaders.body))
                if (l.at("stable").get<bool>()) { loader = l.at("version").get<std::string>(); break; }
            for (auto& i : json::parse(installers.body))
                if (i.at("stable").get<bool>()) { installer = i.at("version").get<std::string>(); break; }
            if (loader.empty() || installer.empty()) { err = "Could not determine Fabric loader version"; return false; }
            d.url = "https://meta.fabricmc.net/v2/versions/loader/" + ver + "/" + loader + "/" + installer + "/server/jar";
            d.filename = "fabric-server-" + ver + "-loader-" + loader + ".jar";
            d.build = "loader " + loader;
        } catch (const std::exception& e) { err = e.what(); return false; }
        return true;
    }
    case Software::Forge: {
        auto r = http::get("https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json");
        if (!r.ok()) { err = r.error; return false; }
        try {
            json j = json::parse(r.body)["promos"];
            std::string fv;
            if (j.contains(ver + "-recommended")) fv = j[ver + "-recommended"].get<std::string>();
            else if (j.contains(ver + "-latest")) fv = j[ver + "-latest"].get<std::string>();
            if (fv.empty()) { err = "No Forge build for " + ver; return false; }
            std::string full = ver + "-" + fv;
            d.url = "https://maven.minecraftforge.net/net/minecraftforge/forge/" + full + "/forge-" + full + "-installer.jar";
            d.filename = "forge-" + full + "-installer.jar";
            d.build = "forge " + fv;
            d.installer = true;
        } catch (const std::exception& e) { err = e.what(); return false; }
        return true;
    }
    case Software::NeoForge: {
        auto r = http::get("https://maven.neoforged.net/api/maven/versions/releases/net/neoforged/neoforge");
        if (!r.ok()) { err = r.error; return false; }
        try {
            std::string best;
            bool bestStable = false;
            json all = json::parse(r.body);
            for (auto& v : all.at("versions")) {
                std::string nv = v.get<std::string>();
                if (neoToMc(nv) != ver) continue;
                bool stable = nv.find("-beta") == std::string::npos;
                if (best.empty() || (stable && !bestStable) ||
                    (stable == bestStable && util::compareVersions(stripBeta(nv), stripBeta(best)) >= 0)) {
                    best = nv;
                    bestStable = stable;
                }
            }
            if (best.empty()) { err = "No NeoForge build for " + ver; return false; }
            d.url = "https://maven.neoforged.net/releases/net/neoforged/neoforge/" + best + "/neoforge-" + best + "-installer.jar";
            d.filename = "neoforge-" + best + "-installer.jar";
            d.build = "neoforge " + best;
            d.installer = true;
        } catch (const std::exception& e) { err = e.what(); return false; }
        return true;
    }
    }
    err = "Unsupported software";
    return false;
}

int requiredJava(const std::string& ver) {
    {
        std::lock_guard<std::mutex> lk(g_cacheMu);
        auto it = g_javaCache.find(ver);
        if (it != g_javaCache.end()) return it->second;
    }
    int result = 0;
    std::string err;
    if (loadMojang(err)) {
        std::string url;
        for (auto& v : g_mojang)
            if (v.id == ver) url = v.url;
        if (!url.empty()) {
            auto r = http::get(url);
            if (r.ok()) {
                try {
                    json j = json::parse(r.body);
                    if (j.contains("javaVersion")) result = j["javaVersion"]["majorVersion"].get<int>();
                } catch (...) {}
            }
        }
    }
    if (result <= 0) result = heuristicJava(ver);
    {
        std::lock_guard<std::mutex> lk(g_cacheMu);
        g_javaCache[ver] = result;
    }
    return result;
}

int heuristicJava(const std::string& ver) {
    auto p = util::split(ver, '.');
    int a = atoi(p[0].c_str());
    if (a >= 26) return 25;
    int minor = p.size() > 1 ? atoi(p[1].c_str()) : 0;
    int patch = p.size() > 2 ? atoi(p[2].c_str()) : 0;
    if (minor > 20 || (minor == 20 && patch >= 5)) return 21;
    if (minor >= 17) return 17;
    return 8;
}

}  // namespace providers
