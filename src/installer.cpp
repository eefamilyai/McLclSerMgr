#include "installer.h"

#include "http.h"
#include "java.h"

InstallJob::InstallJob(InstallRequest r) : req(std::move(r)) {
    bool installer = req.sw == providers::Software::Forge || req.sw == providers::Software::NeoForge;
    steps_.push_back({"Find the latest build"});
    steps_.push_back({"Prepare Java"});
    steps_.push_back({"Download server files"});
    if (installer) steps_.push_back({"Run the mod loader installer"});
    steps_.push_back({"Configure server"});
    thread_ = std::thread([this] { run(); });
}

InstallJob::~InstallJob() {
    cancel_ = true;
    if (thread_.joinable()) thread_.join();
}

std::vector<InstallJob::Step> InstallJob::steps() {
    std::lock_guard<std::mutex> lk(mu_);
    return steps_;
}
float InstallJob::progress() {
    std::lock_guard<std::mutex> lk(mu_);
    return progress_;
}
std::vector<std::string> InstallJob::logTail(size_t n) {
    std::lock_guard<std::mutex> lk(mu_);
    if (log_.size() <= n) return log_;
    return std::vector<std::string>(log_.end() - (long)n, log_.end());
}
std::string InstallJob::error() {
    std::lock_guard<std::mutex> lk(mu_);
    return error_;
}

void InstallJob::setStep(size_t i, StepState s, const std::string& detail) {
    std::lock_guard<std::mutex> lk(mu_);
    if (i < steps_.size()) {
        steps_[i].state = s;
        if (!detail.empty()) steps_[i].detail = detail;
    }
}
void InstallJob::setProgress(float p) {
    std::lock_guard<std::mutex> lk(mu_);
    progress_ = p;
}
void InstallJob::addLog(const std::string& s) {
    std::lock_guard<std::mutex> lk(mu_);
    log_.push_back(s);
}

bool InstallJob::fail(const std::string& msg) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        error_ = msg;
        for (auto& s : steps_)
            if (s.state == StepState::Active) { s.state = StepState::Failed; s.detail = msg; }
        log_.push_back("ERROR: " + msg);
    }
    failed_ = true;
    return false;
}

void InstallJob::run() {
    namespace P = providers;
    std::error_code ec;
    bool createdDir = !fs::exists(req.dir, ec);
    auto cleanup = [&] {
        if (createdDir) fs::remove_all(req.dir, ec);
    };
    bool ok = [&]() -> bool {
        // 1. resolve ------------------------------------------------------
        size_t si = 0;
        setStep(si, StepState::Active, "Contacting the official download servers");
        setProgress(-1.f);
        P::Download dl;
        std::string err;
        if (!P::resolve(req.sw, req.mcVersion, dl, err)) return fail(err);
        addLog("Resolved " + std::string(P::info(req.sw).name) + " " + req.mcVersion + " (" + dl.build + ")");
        addLog("Source: " + dl.url);
        setStep(si, StepState::Done, dl.build);
        if (cancelled()) return fail("Cancelled");

        // 2. java ---------------------------------------------------------
        ++si;
        setStep(si, StepState::Active, "Checking which Java this version needs");
        int major = P::requiredJava(req.mcVersion);
        if (java::count() == 0) java::scan();
        bool exact = false;
        {
            const std::vector<java::Install> js = java::all();
            for (auto& j : js) exact = exact || j.major == major;
        }
        if (exact) {
            setStep(si, StepState::Done, "Java " + std::to_string(major) + " found");
            addLog("Java " + std::to_string(major) + " is already installed");
        } else {
            addLog("Java " + std::to_string(major) + " not found - downloading Temurin " + std::to_string(major));
            std::string jerr;
            bool jok = java::installManaged(major, [&](float p, const std::string& msg) {
                setProgress(p);
                setStep(si, StepState::Active, msg);
            }, &cancel_, jerr);
            if (!jok) {
                // A newer Java may still be usable
                if (java::pick(major)) {
                    addLog("Could not install Java " + std::to_string(major) + " (" + jerr + "), falling back to a newer installed Java");
                    setStep(si, StepState::Done, "Using installed Java");
                } else {
                    return fail("Java " + std::to_string(major) + " could not be installed: " + jerr);
                }
            } else {
                setStep(si, StepState::Done, "Java " + std::to_string(major) + " installed");
            }
        }
        if (cancelled()) return fail("Cancelled");

        // 3. download -----------------------------------------------------
        ++si;
        fs::create_directories(req.dir, ec);
        if (ec) return fail("Cannot create folder " + util::pathStr(req.dir) + ": " + ec.message());
        setStep(si, StepState::Active, "Starting download");
        setProgress(0.f);
        fs::path jar = req.dir / util::fromUtf8(dl.filename);
        bool dok = http::download(dl.url, jar, [&](uint64_t d, uint64_t t) {
            setProgress(t ? (float)d / (float)t : -1.f);
            setStep(si, StepState::Active, util::formatBytes(d) + (t ? " / " + util::formatBytes(t) : ""));
        }, &cancel_, err);
        if (!dok) return fail(err);
        addLog("Downloaded " + dl.filename);
        setStep(si, StepState::Done, dl.filename);

        ServerConfig c;
        c.id = util::newId();
        c.name = req.name;
        c.software = P::info(req.sw).key;
        c.mcVersion = req.mcVersion;
        c.build = dl.build;
        c.dir = util::pathStr(req.dir);
        c.launchMode = "jar";
        c.launchTarget = dl.filename;
        c.maxRamMB = req.ramMB;
        c.minRamMB = std::min(req.ramMB, 1024);
        c.javaMajor = major;
        c.createdAt = util::nowUnix();

        // 4. installer (Forge / NeoForge) --------------------------------
        if (dl.installer) {
            ++si;
            setStep(si, StepState::Active, "Downloading libraries - this can take a few minutes");
            setProgress(-1.f);
            auto jv = java::pick(major);
            if (!jv) return fail("No Java available to run the installer");
            std::wstring cmd = L"\"" + jv->exe.wstring() + L"\" -jar \"" + jar.wstring() + L"\" --installServer";
            int code = util::runCapture(cmd, req.dir, [&](const std::string& l) { addLog(l); }, &cancel_);
            if (cancelled()) return fail("Cancelled");
            if (code != 0) return fail("The installer failed (exit code " + std::to_string(code) + "). See the log for details.");
            fs::remove(jar, ec);
            // Locate how to launch it
            std::string target, bestVer;
            for (const char* group : {"libraries/net/neoforged/neoforge", "libraries/net/minecraftforge/forge"}) {
                fs::path g = req.dir / group;
                if (!fs::is_directory(g, ec)) continue;
                util::forEachDirEntry(g, [&](const fs::directory_entry& v, std::error_code&) {
                    if (!fs::exists(v.path() / "win_args.txt", ec)) return;
                    // An install can leave more than one version behind: launch the newest.
                    const std::string ver = util::pathStr(v.path().filename());
                    if (!target.empty() && util::compareVersions(ver, bestVer) <= 0) return;
                    bestVer = ver;
                    target = std::string(group) + "/" + ver + "/win_args.txt";
                });
            }
            if (!target.empty()) {
                c.launchMode = "args";
                c.launchTarget = target;
            } else {
                // Older Forge: forge-<ver>.jar / forge-<ver>-universal.jar
                std::string best;
                util::forEachDirEntry(req.dir, [&](const fs::directory_entry& f, std::error_code&) {
                    const std::string n = util::pathStr(f.path().filename());
                    if (util::startsWith(n, "forge-") && util::endsWith(n, ".jar") && n.find("installer") == std::string::npos) best = n;
                });
                if (best.empty()) return fail("Installer finished but no launchable server was produced");
                c.launchTarget = best;
            }
            setStep(si, StepState::Done, "Installed");
        }

        // 5. configure ----------------------------------------------------
        ++si;
        setStep(si, StepState::Active, "Writing eula.txt and server.properties");
        setProgress(-1.f);
        if (req.acceptEula)
            util::writeFile(req.dir / "eula.txt", "# Accepted via Voxual (https://aka.ms/MinecraftEULA)\neula=true\n");
        // Start from whatever is already there: installing into a folder that already had a
        // server.properties used to replace it with these two keys alone.
        Properties p;
        p.load(req.dir / "server.properties");
        p.set("server-port", std::to_string(req.port));
        p.set("motd", req.motd.empty() ? req.name : req.motd);
        if (!p.save(req.dir / "server.properties")) return fail("Could not write server.properties");
        setStep(si, StepState::Done, "Ready");
        setProgress(1.f);
        addLog("Server created in " + util::pathStr(req.dir));
        result = c;
        return true;
    }();
    if (!ok) cleanup();
    finished_ = true;
}
