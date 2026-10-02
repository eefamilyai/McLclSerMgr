#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace providers {

enum class Software { Vanilla, Paper, Purpur, Folia, Fabric, Forge, NeoForge };
constexpr int kSoftwareCount = 7;

struct SoftwareInfo {
    Software id;
    const char* key;          // stable id stored in config
    const char* name;
    const char* tagline;
    const char* description;
    const char* extensionKind;  // "plugins", "mods" or ""
    const char* chip1;
    const char* chip2;
    uint32_t color;             // 0xRRGGBB brand colour
};

const SoftwareInfo& info(Software s);
const SoftwareInfo& infoByIndex(int i);
Software fromKey(const std::string& key);

struct Version {
    std::string id;
    bool stable = true;
};

// Newest first. Blocking network call (cached for the session).
bool listVersions(Software s, std::vector<Version>& out, std::string& err);

struct Download {
    std::string url;
    std::string filename;
    std::string build;       // human readable build label
    std::string mcVersion;
    bool installer = false;  // true: run `java -jar <file> --installServer`
};
bool resolve(Software s, const std::string& mcVersion, Download& out, std::string& err);

// Required Java major version for a Minecraft version (queries Mojang, falls back to heuristics).
int requiredJava(const std::string& mcVersion);
// Offline guess (no network): used for imported servers.
int heuristicJava(const std::string& mcVersion);

}  // namespace providers
