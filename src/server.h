#pragma once
#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "providers.h"
#include "util.h"

// ---------------------------------------------------------------------------
// server.properties (order and comments preserved)
// ---------------------------------------------------------------------------
class Properties {
public:
    bool load(const fs::path& p);
    bool save(const fs::path& p) const;
    bool has(const std::string& key) const;
    std::string get(const std::string& key, const std::string& def = "") const;
    int getInt(const std::string& key, int def) const;
    bool getBool(const std::string& key, bool def) const;
    void set(const std::string& key, const std::string& value);
    void setInt(const std::string& key, int v) { set(key, std::to_string(v)); }
    void setBool(const std::string& key, bool v) { set(key, v ? "true" : "false"); }

private:
    std::vector<std::string> lines_;
    static bool parseLine(const std::string& line, std::string& k, std::string& v);
};

// ---------------------------------------------------------------------------
// Persistent per-server configuration
// ---------------------------------------------------------------------------
struct ServerConfig {
    std::string id;
    std::string name;
    std::string software = "vanilla";
    std::string mcVersion;
    std::string build;
    std::string dir;                     // absolute server folder (UTF-8)
    std::string launchMode = "jar";      // "jar" | "args" (Forge/NeoForge 1.17+)
    std::string launchTarget;            // jar file name or args-file path relative to dir
    int minRamMB = 1024;
    int maxRamMB = 2048;
    int javaMajor = 0;                   // 0 = newest installed
    std::string javaPath;                // explicit override
    std::string extraArgs;
    bool aikarFlags = false;
    bool autoRestart = false;
    bool autoStart = false;
    int64_t createdAt = 0;
    int64_t lastStarted = 0;

    // per-server personalization
    std::string color;      // "RRGGBB" card accent, empty = follow the software colour
    bool pinned = false;    // floats to the top of the list
    std::string note;       // free-form reminder shown on the card / overview

    providers::Software sw() const { return providers::fromKey(software); }
    fs::path path() const { return util::fromUtf8(dir); }
};

enum class State { Stopped, Starting, Running, Stopping, Crashed };
const char* stateName(State s);

enum LogLevel : uint8_t { LogInfo = 0, LogWarn = 1, LogError = 2, LogSystem = 3, LogCommand = 4 };
struct LogLine {
    std::string text;
    uint8_t level = LogInfo;
};

// ---------------------------------------------------------------------------
// A managed server and (optionally) its running process
// ---------------------------------------------------------------------------
class ServerInstance {
public:
    explicit ServerInstance(ServerConfig c);
    ~ServerInstance();
    ServerInstance(const ServerInstance&) = delete;
    ServerInstance& operator=(const ServerInstance&) = delete;

    ServerConfig cfg;
    std::atomic<State> state{State::Stopped};

    bool start(std::string& err);
    void stop();      // graceful ("stop" command), force-kills after a timeout
    void kill();
    void restart();   // stop, then start again once stopped
    void sendCommand(const std::string& cmd);
    void tick();      // call from UI thread every frame

    bool isActive() const { State s = state; return s == State::Starting || s == State::Running || s == State::Stopping; }
    int64_t uptimeSeconds() const;
    std::vector<std::string> players();
    int playerCount();

    // Console log (guarded by logMutex)
    std::mutex logMutex;
    std::deque<LogLine> log;
    std::atomic<uint64_t> logSerial{0};
    void appendLog(const std::string& text, uint8_t level);
    void clearLog();

    // live stats (written by tick on UI thread)
    double cpuPercent = 0;
    uint64_t memBytes = 0;
    int maxPlayers = 20;
    std::atomic<bool> needsEula{false};
    std::atomic<bool> restartPending{false};
    std::string lastError;

    // backups
    std::atomic<bool> backupRunning{false};
    void createBackup(const fs::path& serversRoot, bool* ok = nullptr);  // blocking; run on a worker thread
    fs::path backupDir(const fs::path& root) const;

    // helpers
    fs::path propertiesPath() const { return cfg.path() / "server.properties"; }
    int configuredPort() const;
    std::string extensionFolder() const;   // "plugins", "mods" or ""
    void acceptEula();
    // demo/screenshot support: fake a running server without a process
    void debugSetRunning(int uptimeSec, std::vector<std::string> names);

private:
    void readerLoop(void* readPipe);
    void processLine(const std::string& line);
    void sampleStats();

    std::mutex procMu_;
    void* hProc_ = nullptr;   // HANDLE
    void* hIn_ = nullptr;
    std::thread reader_;
    std::chrono::steady_clock::time_point startedAt_{};
    std::chrono::steady_clock::time_point stopRequested_{};
    std::chrono::steady_clock::time_point nextAutoRestart_{};
    std::chrono::steady_clock::time_point lastSample_{};
    uint64_t lastCpu100ns_ = 0;
    bool userStopped_ = false;
    int crashCount_ = 0;
    bool autoRestartScheduled_ = false;
    std::mutex playersMu_;
    std::vector<std::string> players_;
};

const char* const kAikarFlags =
    "-XX:+UseG1GC -XX:+ParallelRefProcEnabled -XX:MaxGCPauseMillis=200 -XX:+UnlockExperimentalVMOptions "
    "-XX:+DisableExplicitGC -XX:+AlwaysPreTouch -XX:G1NewSizePercent=30 -XX:G1MaxNewSizePercent=40 "
    "-XX:G1HeapRegionSize=8M -XX:G1ReservePercent=20 -XX:G1HeapWastePercent=5 -XX:G1MixedGCCountTarget=4 "
    "-XX:InitiatingHeapOccupancyPercent=15 -XX:G1MixedGCLiveThresholdPercent=90 -XX:G1RSetUpdatingPauseTimePercent=5 "
    "-XX:SurvivorRatio=32 -XX:+PerfDisableSharedMem -XX:MaxTenuringThreshold=1";
