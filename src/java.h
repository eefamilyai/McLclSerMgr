#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace java {

struct Install {
    std::filesystem::path exe;
    int major = 0;
    std::string version;
    bool managed = false;  // installed by Voxual
};

void setManagedRoot(const std::filesystem::path& root);
std::filesystem::path managedRoot();

// Blocking: scans common install locations + PATH. Thread-safe.
void scan();
// Runs scan() on a worker thread. Only one background scan exists at a time, so this is safe
// to call from a button and leaves nothing detached behind at exit.
void scanAsync();
void waitForScan();           // joins the background scan; call before the process exits
std::vector<Install> all();   // copies the list: prefer count()/newestMajor() on per-frame paths
size_t count();
int newestMajor();            // -1 when no runtime was found
bool scanning();

// explicitPath wins if non-empty. major == 0 -> newest installed. Otherwise exact match, then next higher.
std::optional<Install> pick(int major, const std::string& explicitPath = "");
bool has(int major);

// Downloads an Eclipse Temurin JRE into the managed folder.
bool installManaged(int major, const std::function<void(float, const std::string&)>& progress,
                    const std::atomic<bool>* cancel, std::string& err);

}  // namespace java
