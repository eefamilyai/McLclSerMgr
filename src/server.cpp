#include "server.h"

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <fstream>

#include "java.h"

// ===========================================================================
// Properties
// ===========================================================================
bool Properties::parseLine(std::string_view line, std::string_view& k, std::string_view& v) {
    size_t a = 0, b = line.size();
    while (a < b && (unsigned char)line[a] <= ' ') ++a;
    while (b > a && (unsigned char)line[b - 1] <= ' ') --b;
    std::string_view t = line.substr(a, b - a);
    if (t.empty() || t[0] == '#' || t[0] == '!') return false;
    size_t eq = t.find('=');
    if (eq == std::string_view::npos) return false;
    size_t kb = eq;
    while (kb > 0 && (unsigned char)t[kb - 1] <= ' ') --kb;
    k = t.substr(0, kb);
    v = t.substr(eq + 1);
    return true;
}

static std::string unescapeUnicode(const std::string& s) {
    if (s.find("\\u") == std::string::npos) return s;
    std::wstring in = util::widen(s), out;
    for (size_t i = 0; i < in.size();) {
        if (in[i] == L'\\' && i + 6 <= in.size() && in[i + 1] == L'u') {
            out += (wchar_t)wcstol(in.substr(i + 2, 4).c_str(), nullptr, 16);
            i += 6;
        } else {
            out += in[i++];
        }
    }
    return util::narrow(out);
}

static std::string escapeUnicode(const std::string& s) {
    bool ascii = std::all_of(s.begin(), s.end(), [](unsigned char c) { return c < 128; });
    if (ascii) return s;
    std::wstring w = util::widen(s);
    std::string out;
    char buf[8];
    for (wchar_t c : w) {
        if (c < 128) out += (char)c;
        else { snprintf(buf, sizeof buf, "\\u%04X", (unsigned)c); out += buf; }
    }
    return out;
}

bool Properties::load(const fs::path& p) {
    lines_.clear();
    std::string txt;
    if (!util::readFile(p, txt)) return false;
    size_t pos = 0;
    while (pos <= txt.size()) {
        size_t nl = txt.find('\n', pos);
        std::string line = txt.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (nl == std::string::npos) { if (!line.empty()) lines_.push_back(line); break; }
        lines_.push_back(line);
        pos = nl + 1;
    }
    return true;
}

bool Properties::save(const fs::path& p) const {
    std::string out;
    for (auto& l : lines_) { out += l; out += '\n'; }
    return util::writeFile(p, out);
}

bool Properties::has(const std::string& key) const {
    std::string_view k, v;
    for (auto& l : lines_)
        if (parseLine(l, k, v) && k == key) return true;
    return false;
}

std::string Properties::get(const std::string& key, const std::string& def) const {
    std::string_view k, v;
    for (auto& l : lines_)
        if (parseLine(l, k, v) && k == key) return unescapeUnicode(std::string(v));
    return def;
}

int Properties::getInt(const std::string& key, int def) const {
    std::string v = util::trim(get(key));
    if (v.empty()) return def;
    return atoi(v.c_str());
}

bool Properties::getBool(const std::string& key, bool def) const {
    std::string v = util::lower(util::trim(get(key)));
    if (v.empty()) return def;
    return v == "true";
}

void Properties::set(const std::string& key, const std::string& value) {
    std::string_view k, v;
    std::string line = key + "=" + escapeUnicode(value);
    for (auto& l : lines_)
        if (parseLine(l, k, v) && k == key) { l = line; return; }
    lines_.push_back(line);
}

// ===========================================================================
// ServerInstance
// ===========================================================================
const char* stateName(State s) {
    switch (s) {
    case State::Stopped: return "Stopped";
    case State::Starting: return "Starting";
    case State::Running: return "Running";
    case State::Stopping: return "Stopping";
    case State::Crashed: return "Crashed";
    }
    return "";
}

// Upper bound of the console line setting in Settings (500..20000); the UI trims below it.
static constexpr size_t kLogHardMax = 20000;

namespace {
HANDLE jobHandle() {
    static HANDLE job = [] {
        HANDLE h = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(h, JobObjectExtendedLimitInformation, &info, sizeof info);
        return h;
    }();
    return job;
}

std::string stripAnsi(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
            i += 2;
            while (i < s.size() && !(s[i] >= '@' && s[i] <= '~')) ++i;
        } else if (s[i] == '\r') {
            continue;
        } else {
            out += s[i];
        }
    }
    return out;
}
}  // namespace

ServerInstance::ServerInstance(ServerConfig c) : cfg(std::move(c)) {
    Properties p;
    if (p.load(propertiesPath())) maxPlayers = p.getInt("max-players", 20);
}

ServerInstance::~ServerInstance() {
    {
        std::lock_guard<std::mutex> lk(procMu_);
        if (hProc_) TerminateProcess((HANDLE)hProc_, 1);
    }
    {
        std::lock_guard<std::mutex> lk(inMu_);
        if (hIn_) { CloseHandle((HANDLE)hIn_); hIn_ = nullptr; }
    }
    // A grandchild (a forked Forge/NeoForge process, say) can hold the stdout pipe open after
    // the server itself is gone, which left the reader blocked in ReadFile and the join below
    // waiting forever. Cancel that read before waiting for the thread.
    if (reader_.joinable()) {
        CancelSynchronousIo(reader_.native_handle());
        reader_.join();
    }
}

void ServerInstance::appendLog(const std::string& text, uint8_t level) {
    std::lock_guard<std::mutex> lk(logMutex);
    log.push_back({text, level});
    // Hard ceiling only: the console setting (up to 20000 lines) trims this from the UI thread,
    // and the old fixed 8000 silently capped that setting.
    if (log.size() > kLogHardMax) log.erase(log.begin(), log.begin() + (kLogHardMax / 8));
    ++logSerial;
}

void ServerInstance::clearLog() {
    std::lock_guard<std::mutex> lk(logMutex);
    log.clear();
    ++logSerial;
}

int64_t ServerInstance::uptimeSeconds() const {
    if (!isActive()) return 0;
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startedAt_).count();
}

std::vector<std::string> ServerInstance::players() {
    std::lock_guard<std::mutex> lk(playersMu_);
    return players_;
}

void ServerInstance::setPlayers(std::vector<std::string> names) {
    std::lock_guard<std::mutex> lk(playersMu_);
    players_ = std::move(names);
    livePlayers = (int)players_.size();
}

int ServerInstance::configuredPort() const {
    auto now = std::chrono::steady_clock::now();
    if (portCache_ >= 0 && now - portCacheAt_ < std::chrono::seconds(5)) return portCache_;
    Properties p;
    portCache_ = p.load(propertiesPath()) ? p.getInt("server-port", 25565) : 25565;
    portCacheAt_ = now;
    return portCache_;
}

std::string ServerInstance::extensionFolder() const { return providers::info(cfg.sw()).extensionKind; }

void ServerInstance::acceptEula() {
    util::writeFile(cfg.path() / "eula.txt",
                    "# Accepted via Voxual (https://aka.ms/MinecraftEULA)\neula=true\n");
    needsEula = false;
    appendLog("[Voxual] EULA accepted. You can start the server now.", LogSystem);
}

void ServerInstance::debugSetRunning(int uptimeSec, std::vector<std::string> names) {
    startedAt_ = std::chrono::steady_clock::now() - std::chrono::seconds(uptimeSec);
    setPlayers(std::move(names));
    state = State::Running;
}

bool ServerInstance::start(std::string& err) {
    if (state != State::Stopped && state != State::Crashed) return false;
    if (reader_.joinable()) reader_.join();

    auto jv = java::pick(cfg.javaMajor, cfg.javaPath);
    if (!jv) {
        err = cfg.javaMajor > 0 ? "Java " + std::to_string(cfg.javaMajor) + " is not installed. Install it from Settings > Java."
                                : "No Java installation found. Install one from Settings > Java.";
        appendLog("[Voxual] " + err, LogError);
        lastError = err;
        return false;
    }
    fs::path dir = cfg.path();
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        err = "Server folder is missing: " + cfg.dir;
        appendLog("[Voxual] " + err, LogError);
        lastError = err;
        return false;
    }
    if (cfg.launchMode == "jar" && !fs::exists(dir / util::fromUtf8(cfg.launchTarget), ec)) {
        err = "Server jar not found: " + cfg.launchTarget;
        appendLog("[Voxual] " + err, LogError);
        lastError = err;
        return false;
    }

    int maxMb = std::max(512, cfg.maxRamMB), minMb = std::min(std::max(256, cfg.minRamMB), maxMb);
    std::wstring cmd = L"\"" + jv->exe.wstring() + L"\" -Xms" + std::to_wstring(minMb) + L"M -Xmx" + std::to_wstring(maxMb) + L"M";
    cmd += L" -Dfile.encoding=UTF-8 -Dstdout.encoding=UTF-8 -Dstderr.encoding=UTF-8";
    if (cfg.aikarFlags) cmd += L" " + util::widen(kAikarFlags);
    if (!cfg.extraArgs.empty()) cmd += L" " + util::widen(cfg.extraArgs);
    if (cfg.launchMode == "args") cmd += L" @" + util::widen(cfg.launchTarget) + L" nogui";
    else cmd += L" -jar \"" + util::widen(cfg.launchTarget) + L"\" nogui";

    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE outR = nullptr, outW = nullptr, inR = nullptr, inW = nullptr;
    if (!CreatePipe(&outR, &outW, &sa, 0)) { err = "Could not create pipes"; return false; }
    if (!CreatePipe(&inR, &inW, &sa, 0)) {
        // The first pipe already exists, so both of its handles have to go back.
        CloseHandle(outR);
        CloseHandle(outW);
        err = "Could not create pipes";
        return false;
    }
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inR;
    si.hStdOutput = outW;
    si.hStdError = outW;
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                             dir.c_str(), &si, &pi);
    CloseHandle(outW);
    CloseHandle(inR);
    if (!ok) {
        CloseHandle(outR);
        CloseHandle(inW);
        err = "Failed to launch Java (" + std::to_string(GetLastError()) + ")";
        appendLog("[Voxual] " + err, LogError);
        lastError = err;
        return false;
    }
    AssignProcessToJobObject(jobHandle(), pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    {
        std::lock_guard<std::mutex> lk(procMu_);
        hProc_ = pi.hProcess;
        hIn_ = inW;
    }
    setPlayers({});
    portCache_ = -1;   // the port may have been edited while the server was stopped
    Properties p;
    if (p.load(propertiesPath())) maxPlayers = p.getInt("max-players", 20);

    userStopped_ = false;
    needsEula = false;
    startedAt_ = std::chrono::steady_clock::now();
    lastSample_ = startedAt_;
    lastCpu100ns_ = 0;
    cfg.lastStarted = util::nowUnix();
    state = State::Starting;
    appendLog("[Voxual] Starting with Java " + std::to_string(jv->major) + " (" + std::to_string(minMb) + "-" +
                  std::to_string(maxMb) + " MB)...",
              LogSystem);
    reader_ = std::thread([this, outR, proc = pi.hProcess] { readerLoop(outR, proc); });
    return true;
}

void ServerInstance::readerLoop(void* readPipe, void* procHandle) {
    HANDLE rd = (HANDLE)readPipe;
    char buf[4096];
    std::string partial;
    for (;;) {
        DWORD n = 0;
        if (!ReadFile(rd, buf, sizeof buf, &n, nullptr) || n == 0) break;
        partial.append(buf, n);
        // Emit the complete lines in one pass with a single erase at the end. Shifting the
        // remainder once per line made a chatty startup log quadratic in its output size.
        size_t start = 0;
        for (;;) {
            size_t pos = partial.find('\n', start);
            if (pos == std::string::npos) break;
            processLine(partial.substr(start, pos - start));
            start = pos + 1;
        }
        partial.erase(0, start);
    }
    if (!partial.empty()) processLine(partial);
    CloseHandle(rd);

    HANDLE proc = (HANDLE)procHandle;
    DWORD code = 0;
    if (proc) {
        WaitForSingleObject(proc, 8000);
        GetExitCodeProcess(proc, &code);
    }
    {
        std::lock_guard<std::mutex> lk(inMu_);
        if (hIn_) { CloseHandle((HANDLE)hIn_); hIn_ = nullptr; }
    }
    {
        std::lock_guard<std::mutex> lk(procMu_);
        // Close only the handle this reader owns: a restart may already have installed a new one.
        if (proc && hProc_ == procHandle) {
            CloseHandle(proc);
            hProc_ = nullptr;
        }
    }
    setPlayers({});
    bool clean = userStopped_ || state == State::Stopping || code == 0;
    if (clean) {
        appendLog("[Voxual] Server stopped.", LogSystem);
        state = State::Stopped;
    } else {
        appendLog("[Voxual] Server exited unexpectedly (exit code " + std::to_string((int)code) + ").", LogError);
        state = State::Crashed;
    }
}

void ServerInstance::processLine(const std::string& raw) {
    std::string line = stripAnsi(raw);
    if (line.empty()) return;
    uint8_t level = LogInfo;
    if (line.find("WARN]") != std::string::npos || line.find("/WARN") != std::string::npos) level = LogWarn;
    if (line.find("ERROR]") != std::string::npos || line.find("/ERROR") != std::string::npos || util::startsWith(line, "\tat ") ||
        util::startsWith(line, "Caused by:") || util::startsWith(line, "java.lang.") || util::startsWith(line, "Exception in thread"))
        level = LogError;
    appendLog(line, level);

    if (state == State::Starting && line.find("Done (") != std::string::npos && line.find("For help") != std::string::npos) {
        state = State::Running;
        crashCount_ = 0;
    } else if (line.find("Stopping the server") != std::string::npos || line.find("Stopping server") != std::string::npos) {
        if (state == State::Running || state == State::Starting) {
            state = State::Stopping;
            // A server that shuts itself down must still get the full grace period before
            // tick() force-kills it. Leaving the deadline unset made that happen immediately.
            stopRequested_ = std::chrono::steady_clock::now();
        }
    } else if (line.find("agree to the EULA") != std::string::npos) {
        needsEula = true;
    } else if (line.find(" joined the game") != std::string::npos || line.find(" left the game") != std::string::npos) {
        bool join = line.find(" joined the game") != std::string::npos;
        size_t end = line.find(join ? " joined the game" : " left the game");
        size_t start = line.rfind("]: ", end);
        start = start == std::string::npos ? 0 : start + 3;
        if (end > start && end - start <= 40) {
            std::string name = line.substr(start, end - start);
            if (name.find(' ') == std::string::npos && name.find('<') == std::string::npos) {
                std::lock_guard<std::mutex> lk(playersMu_);
                auto it = std::find(players_.begin(), players_.end(), name);
                if (join && it == players_.end()) players_.push_back(name);
                if (!join && it != players_.end()) players_.erase(it);
                livePlayers = (int)players_.size();
            }
        }
    }
}

void ServerInstance::sendCommand(const std::string& cmd) {
    // The stdin handle has its own lock: a full pipe buffer blocks WriteFile, and holding the
    // process lock across that would also stall the reader thread's handle cleanup.
    std::lock_guard<std::mutex> lk(inMu_);
    if (!hIn_) return;
    std::string data = cmd + "\n";
    DWORD w = 0;
    WriteFile((HANDLE)hIn_, data.data(), (DWORD)data.size(), &w, nullptr);
}

void ServerInstance::stop() {
    State s = state;
    if (s != State::Running && s != State::Starting) return;
    userStopped_ = true;
    autoRestartScheduled_ = false;
    state = State::Stopping;
    stopRequested_ = std::chrono::steady_clock::now();
    appendLog("[Voxual] Stopping server...", LogSystem);
    sendCommand("stop");
    // Closing stdin afterwards lets the server's console thread see EOF; otherwise some
    // servers (Paper & friends) hang on shutdown waiting for the blocked reader.
    std::lock_guard<std::mutex> lk(inMu_);
    if (hIn_) { CloseHandle((HANDLE)hIn_); hIn_ = nullptr; }
}

void ServerInstance::kill() {
    userStopped_ = true;
    autoRestartScheduled_ = false;
    restartPending = false;
    std::lock_guard<std::mutex> lk(procMu_);
    if (hProc_) {
        appendLog("[Voxual] Server process was force-killed.", LogWarn);
        TerminateProcess((HANDLE)hProc_, 1);
    }
}

void ServerInstance::restart() {
    if (!isActive()) { std::string e; start(e); return; }
    restartPending = true;
    stop();
}

void ServerInstance::tick() {
    State s = state;
    auto now = std::chrono::steady_clock::now();
    if (s == State::Stopping && now - stopRequested_ > std::chrono::seconds(60)) {
        appendLog("[Voxual] Server did not stop in time, killing it.", LogWarn);
        kill();
    }
    if ((s == State::Stopped || s == State::Crashed) && restartPending) {
        restartPending = false;
        std::string e;
        start(e);
        return;
    }
    if (s == State::Crashed && cfg.autoRestart && !userStopped_) {
        if (!autoRestartScheduled_) {
            ++crashCount_;
            if (crashCount_ > 3) {
                appendLog("[Voxual] Crashed " + std::to_string(crashCount_) + " times in a row, auto-restart disabled.", LogError);
                userStopped_ = true;  // give up
            } else {
                autoRestartScheduled_ = true;
                nextAutoRestart_ = now + std::chrono::seconds(5);
                appendLog("[Voxual] Restarting in 5 seconds...", LogSystem);
            }
        } else if (now >= nextAutoRestart_) {
            autoRestartScheduled_ = false;
            std::string e;
            start(e);
            return;
        }
    }
    if (isActive()) sampleStats();
    else { cpuPercent = 0; memBytes = 0; }
}

void ServerInstance::sampleStats() {
    auto now = std::chrono::steady_clock::now();
    if (now - lastSample_ < std::chrono::milliseconds(1000)) return;
    std::lock_guard<std::mutex> lk(procMu_);
    if (!hProc_) return;
    FILETIME c, e, k, u;
    if (GetProcessTimes((HANDLE)hProc_, &c, &e, &k, &u)) {
        auto toU64 = [](FILETIME f) { return ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime; };
        uint64_t total = toU64(k) + toU64(u);
        double wall100ns = std::chrono::duration<double>(now - lastSample_).count() * 1e7;
        static const int cores = [] { SYSTEM_INFO si; GetSystemInfo(&si); return std::max<int>(1, (int)si.dwNumberOfProcessors); }();
        if (lastCpu100ns_ && wall100ns > 0) cpuPercent = std::min(100.0, (double)(total - lastCpu100ns_) / wall100ns / cores * 100.0);
        lastCpu100ns_ = total;
    }
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo((HANDLE)hProc_, &pmc, sizeof pmc)) memBytes = pmc.WorkingSetSize;
    lastSample_ = now;
}

// ---- backups ---------------------------------------------------------------
fs::path ServerInstance::backupDir(const fs::path& root) const { return root / "Backups" / cfg.id; }

void ServerInstance::createBackup(const fs::path& serversRoot, bool* okOut) {
    // Runs synchronously (call from a worker thread).
    backupRunning = true;
    bool ok = false;
    do {
        Properties p;
        p.load(propertiesPath());
        std::string level = p.get("level-name", "world");
        fs::path dir = cfg.path();
        std::vector<std::wstring> items;
        std::error_code ec;
        for (const std::string& n : {level, level + "_nether", level + "_the_end"})
            if (fs::exists(dir / util::fromUtf8(n), ec)) items.push_back(util::widen(n));
        if (items.empty()) { appendLog("[Voxual] Backup skipped: no world folder found yet.", LogWarn); break; }

        bool live = state == State::Running;
        if (live) {
            sendCommand("save-off");
            sendCommand("save-all flush");
            std::this_thread::sleep_for(std::chrono::seconds(4));
        }
        time_t t = time(nullptr);
        tm lt{};
        localtime_s(&lt, &t);
        char stamp[32];
        strftime(stamp, sizeof stamp, "%Y-%m-%d_%H-%M-%S", &lt);
        fs::path out = backupDir(serversRoot) / (std::string(stamp) + ".zip");
        fs::create_directories(out.parent_path(), ec);

        wchar_t sys[MAX_PATH];
        GetSystemDirectoryW(sys, MAX_PATH);
        std::wstring cmd = L"\"" + (fs::path(sys) / L"tar.exe").wstring() + L"\" -a -c -f \"" + out.wstring() + L"\" --exclude=session.lock -C \"" + dir.wstring() + L"\"";
        for (auto& i : items) cmd += L" \"" + i + L"\"";
        int code = util::runCapture(cmd, {}, nullptr);
        if (live) sendCommand("save-on");
        ok = code == 0 && fs::exists(out, ec);
        appendLog(ok ? "[Voxual] Backup created: " + util::pathStr(out.filename()) : "[Voxual] Backup failed (tar exit " + std::to_string(code) + ")",
                  ok ? LogSystem : LogError);
    } while (false);
    if (okOut) *okOut = ok;
    backupRunning = false;
}
