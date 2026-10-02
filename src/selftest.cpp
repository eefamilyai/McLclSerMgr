// Developer-only end-to-end check: `Voxual.exe --selftest <software>:<mcVersion> <logfile>`
// Installs a real server, starts it, runs a command, stops it and takes a backup.
#include <chrono>
#include <fstream>
#include <thread>

#include "installer.h"
#include "java.h"
#include "providers.h"
#include "server.h"

namespace {
std::ofstream g_log;
void say(const std::string& s) {
    g_log << s << "\n";
    g_log.flush();
}
}  // namespace

int runSelfTest(const std::string& spec, const std::string& logPath) {
    g_log.open(logPath, std::ios::trunc);
    namespace P = providers;
    java::setManagedRoot(util::appDataDir() / "runtimes");
    java::scan();
    for (auto& j : java::all()) say("java: " + std::to_string(j.major) + "  " + j.version + "  " + util::pathStr(j.exe));

    auto colon = spec.find(':');
    P::Software sw = P::fromKey(spec.substr(0, colon));
    std::string ver = spec.substr(colon + 1);

    if (ver == "resolve-all") {
        for (int i = 0; i < P::kSoftwareCount; ++i) {
            auto s = (P::Software)i;
            std::vector<P::Version> list;
            std::string err;
            if (!P::listVersions(s, list, err)) { say(std::string(P::info(s).name) + ": LIST FAILED " + err); continue; }
            std::string v;
            for (auto& x : list)
                if (x.stable) { v = x.id; break; }
            P::Download d;
            bool ok = P::resolve(s, v, d, err);
            say(std::string(P::info(s).name) + ": " + std::to_string(list.size()) + " versions, latest stable " + v + " -> " +
                (ok ? d.url + "  [" + d.build + "]  java " + std::to_string(P::requiredJava(v)) : "RESOLVE FAILED " + err));
        }
        return 0;
    }

    fs::path root = fs::temp_directory_path() / "voxual-selftest";
    std::error_code ec;
    fs::remove_all(root, ec);
    InstallRequest rq;
    rq.sw = sw;
    rq.mcVersion = ver;
    rq.name = "Selftest";
    rq.port = 25599;
    rq.ramMB = 2048;
    rq.acceptEula = true;
    rq.dir = root / "server";
    say("installing " + std::string(P::info(sw).name) + " " + ver + " into " + util::pathStr(rq.dir));
    {
        InstallJob job(rq);
        size_t shown = 0;
        while (!job.finished()) {
            auto logs = job.logTail(1000);
            for (; shown < logs.size(); ++shown) say("  [install] " + logs[shown]);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
        auto logs = job.logTail(1000);
        for (; shown < logs.size(); ++shown) say("  [install] " + logs[shown]);
        for (auto& s : job.steps()) say("  step: " + s.label + " -> " + std::to_string((int)s.state) + " " + s.detail);
        if (job.failed()) { say("INSTALL FAILED: " + job.error()); return 2; }
        say("install ok. launch=" + job.result.launchMode + " target=" + job.result.launchTarget + " javaMajor=" + std::to_string(job.result.javaMajor));

        ServerInstance srv(job.result);
        std::string err;
        if (!srv.start(err)) { say("START FAILED: " + err); return 3; }
        auto t0 = std::chrono::steady_clock::now();
        size_t logShown = 0;
        auto pump = [&] {
            std::lock_guard<std::mutex> lk(srv.logMutex);
            for (; logShown < srv.log.size(); ++logShown) say("  [server] " + srv.log[logShown].text);
        };
        while (srv.state != State::Running && srv.state != State::Stopped && srv.state != State::Crashed) {
            srv.tick();
            pump();
            if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(300)) { say("TIMEOUT waiting for Running"); srv.kill(); return 4; }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        pump();
        say(std::string("state after start: ") + stateName(srv.state));
        if (srv.state != State::Running) return 5;
        say("startup took " + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t0).count()) + "s");

        srv.sendCommand("list");
        std::this_thread::sleep_for(std::chrono::seconds(2));
        for (int i = 0; i < 4; ++i) { srv.tick(); std::this_thread::sleep_for(std::chrono::milliseconds(300)); }
        say("cpu=" + std::to_string(srv.cpuPercent) + " mem=" + util::formatBytes(srv.memBytes) + " uptime=" + std::to_string(srv.uptimeSeconds()));
        pump();

        bool bok = false;
        srv.createBackup(root, &bok);
        say(std::string("backup ok=") + (bok ? "yes" : "no"));
        for (auto& e : fs::directory_iterator(srv.backupDir(root), ec)) say("  backup file: " + util::pathStr(e.path().filename()) + " " + util::formatBytes(e.file_size(ec)));
        pump();

        srv.stop();
        auto t1 = std::chrono::steady_clock::now();
        while (srv.state != State::Stopped && srv.state != State::Crashed) {
            srv.tick();
            pump();
            if (std::chrono::steady_clock::now() - t1 > std::chrono::seconds(90)) { say("TIMEOUT waiting for stop"); srv.kill(); return 6; }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        pump();
        say(std::string("final state: ") + stateName(srv.state));
    }
    say("SELFTEST PASSED");
    return 0;
}
