#include "java.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <mutex>
#include <set>

#include "http.h"
#include "util.h"

namespace java {

namespace {
std::mutex g_mu;
std::vector<Install> g_installs;
std::filesystem::path g_managed;
std::atomic<bool> g_scanning{false};

int parseMajor(const std::string& v) {
    // "1.8.0_392" -> 8 ; "21.0.5" -> 21 ; "25" -> 25
    auto p = util::split(v, '.');
    int a = atoi(p[0].c_str());
    if (a == 1 && p.size() > 1) return atoi(p[1].c_str());
    return a;
}

bool readVersion(const fs::path& exe, std::string& version) {
    // Fast path: the "release" file next to bin/
    fs::path rel = exe.parent_path().parent_path() / "release";
    std::string txt;
    if (util::readFile(rel, txt)) {
        auto pos = txt.find("JAVA_VERSION=\"");
        if (pos != std::string::npos) {
            pos += 14;
            auto end = txt.find('"', pos);
            if (end != std::string::npos) { version = txt.substr(pos, end - pos); return true; }
        }
    }
    std::string captured;
    std::wstring cmd = L"\"" + exe.wstring() + L"\" -version";
    util::runCapture(cmd, {}, [&](const std::string& l) {
        auto q = l.find('"');
        if (q != std::string::npos && captured.empty()) {
            auto e = l.find('"', q + 1);
            if (e != std::string::npos) captured = l.substr(q + 1, e - q - 1);
        }
    });
    if (captured.empty()) return false;
    version = captured;
    return true;
}

void addCandidate(std::set<fs::path>& out, const fs::path& exe) {
    std::error_code ec;
    if (fs::exists(exe, ec)) out.insert(fs::weakly_canonical(exe, ec));
}

void scanDirForJava(std::set<fs::path>& out, const fs::path& dir, int depth) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    addCandidate(out, dir / "bin" / "java.exe");
    if (depth <= 0) return;
    for (auto& e : fs::directory_iterator(dir, ec))
        if (e.is_directory(ec)) scanDirForJava(out, e.path(), depth - 1);
}

fs::path envPath(const wchar_t* name) {
    wchar_t buf[2048];
    DWORD n = GetEnvironmentVariableW(name, buf, (DWORD)std::size(buf));
    return n ? fs::path(std::wstring(buf, n)) : fs::path();
}
}  // namespace

void setManagedRoot(const fs::path& root) { g_managed = root; }
fs::path managedRoot() { return g_managed; }
bool scanning() { return g_scanning; }

void scan() {
    if (g_scanning.exchange(true)) return;
    std::set<fs::path> cands;

    if (auto jh = envPath(L"JAVA_HOME"); !jh.empty()) addCandidate(cands, jh / "bin" / "java.exe");
    if (auto path = util::split(util::narrow(envPath(L"PATH").wstring()), ';'); true)
        for (auto& p : path)
            if (!p.empty()) addCandidate(cands, util::fromUtf8(p) / "java.exe");

    wchar_t pf[MAX_PATH];
    for (int csidl : {CSIDL_PROGRAM_FILES, CSIDL_PROGRAM_FILESX86}) {
        if (!SUCCEEDED(SHGetFolderPathW(nullptr, csidl, nullptr, 0, pf))) continue;
        fs::path base = pf;
        for (const wchar_t* vendor : {L"Java", L"Eclipse Adoptium", L"AdoptOpenJDK", L"Microsoft", L"Zulu", L"Amazon Corretto",
                                      L"BellSoft", L"Semeru", L"Temurin", L"OpenJDK"})
            scanDirForJava(cands, base / vendor, 2);
    }
    fs::path home = envPath(L"USERPROFILE");
    if (!home.empty()) scanDirForJava(cands, home / ".jdks", 1);
    if (auto la = envPath(L"LOCALAPPDATA"); !la.empty()) scanDirForJava(cands, la / "Programs" / "Eclipse Adoptium", 1);
    if (auto ad = envPath(L"APPDATA"); !ad.empty()) scanDirForJava(cands, ad / ".minecraft" / "runtime", 3);
    if (!g_managed.empty()) scanDirForJava(cands, g_managed, 3);

    std::vector<Install> found;
    for (auto& exe : cands) {
        Install in;
        in.exe = exe;
        if (!readVersion(exe, in.version)) continue;
        in.major = parseMajor(in.version);
        if (in.major <= 0) continue;
        in.managed = !g_managed.empty() && exe.wstring().find(g_managed.wstring()) == 0;
        found.push_back(in);
    }
    std::sort(found.begin(), found.end(), [](const Install& a, const Install& b) { return a.major > b.major; });
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_installs = std::move(found);
    }
    g_scanning = false;
}

std::vector<Install> all() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_installs;
}

bool has(int major) {
    for (auto& i : all())
        if (i.major == major) return true;
    return false;
}

std::optional<Install> pick(int major, const std::string& explicitPath) {
    if (!explicitPath.empty()) {
        fs::path p = util::fromUtf8(explicitPath);
        std::error_code ec;
        if (fs::exists(p, ec)) {
            Install in;
            in.exe = p;
            readVersion(p, in.version);
            in.major = parseMajor(in.version);
            return in;
        }
    }
    auto list = all();  // sorted newest first
    if (list.empty()) return std::nullopt;
    if (major <= 0) return list.front();
    for (auto& i : list)
        if (i.major == major) return i;
    const Install* best = nullptr;  // smallest major greater than required
    for (auto& i : list)
        if (i.major > major && (!best || i.major < best->major)) best = &i;
    if (best) return *best;
    return std::nullopt;
}

bool installManaged(int major, const std::function<void(float, const std::string&)>& progress,
                    const std::atomic<bool>* cancel, std::string& err) {
    if (g_managed.empty()) { err = "Runtime folder not configured"; return false; }
    std::string url = "https://api.adoptium.net/v3/binary/latest/" + std::to_string(major) +
                      "/ga/windows/x64/jre/hotspot/normal/eclipse";
    fs::path zip = g_managed / ("java-" + std::to_string(major) + ".zip");
    fs::path dest = g_managed / ("java-" + std::to_string(major));
    if (progress) progress(0.f, "Downloading Java " + std::to_string(major) + " (Temurin)");
    bool ok = http::download(url, zip, [&](uint64_t d, uint64_t t) {
        if (progress) progress(t ? (float)d / (float)t * 0.85f : -1.f,
                               "Downloading Java " + std::to_string(major) + " - " + util::formatBytes(d) + (t ? " / " + util::formatBytes(t) : ""));
    }, cancel, err);
    if (!ok) return false;

    if (progress) progress(0.9f, "Extracting Java " + std::to_string(major));
    std::error_code ec;
    fs::remove_all(dest, ec);
    fs::create_directories(dest, ec);
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring cmd = L"\"" + (fs::path(sys) / L"tar.exe").wstring() + L"\" -xf \"" + zip.wstring() + L"\" -C \"" + dest.wstring() + L"\"";
    int code = util::runCapture(cmd, {}, nullptr, cancel);
    fs::remove(zip, ec);
    if (code != 0) { err = "Failed to extract the Java archive"; return false; }
    if (progress) progress(1.f, "Java " + std::to_string(major) + " ready");
    scan();
    return true;
}

}  // namespace java
