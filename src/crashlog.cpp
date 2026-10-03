#include "crashlog.h"

#include <windows.h>
#include <psapi.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <mutex>
#include <string>
#include <vector>

#include "util.h"
#include "version.h"

namespace crashlog {

namespace {
std::mutex g_mu;
std::vector<std::string> g_breadcrumbs;   // ring buffer of the last steps
std::string g_logDir;
bool g_prevCrashed = false;

std::string stamp(bool forFile) {
    time_t now = time(nullptr);
    tm lt{};
    localtime_s(&lt, &now);
    char buf[64];
    strftime(buf, sizeof buf, forFile ? "%Y%m%d-%H%M%S" : "%Y-%m-%d %H:%M:%S", &lt);
    return buf;
}

std::string moduleOf(void* addr) {
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)addr, &mod) || !mod)
        return "?";
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(mod, path, MAX_PATH);
    std::string name = util::narrow(fs::path(path).filename().wstring());
    char off[32];
    snprintf(off, sizeof off, "+0x%llX", (unsigned long long)((uintptr_t)addr - (uintptr_t)mod));
    return name + off;
}

void writeReport(const char* kind, unsigned code, void* addr, void* const* frames, int frameCount) {
    std::string dir = g_logDir.empty() ? util::pathStr(util::appDataDir()) : g_logDir;
    std::string path = dir + "\\crash-" + stamp(true) + ".log";

    std::string out;
    out += std::string(VX_APP_NAME) + " " + VX_VERSION_STR + " crash report\r\n";
    out += "when      : " + stamp(false) + "\r\n";
    out += "kind      : " + std::string(kind) + "\r\n";
    if (code) {
        char buf[128];
        snprintf(buf, sizeof buf, "exception : 0x%08X at %p (%s)\r\n", code, addr, moduleOf(addr).c_str());
        out += buf;
    }
    out += "process   : pid " + std::to_string((long)GetCurrentProcessId()) + "\r\n";
    {
        std::lock_guard<std::mutex> lk(g_mu);
        out += "doing     :\r\n";
        for (auto& b : g_breadcrumbs) out += "  - " + b + "\r\n";
    }
    out += "stack     :\r\n";
    for (int i = 0; i < frameCount; ++i) {
        char buf[128];
        snprintf(buf, sizeof buf, "  [%02d] %p  %s\r\n", i, frames[i], moduleOf(frames[i]).c_str());
        out += buf;
    }
    out += "modules   :\r\n";
    HMODULE mods[64];
    DWORD needed = 0;
    if (EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &needed)) {
        for (unsigned i = 0; i < needed / sizeof(HMODULE) && i < 64; ++i) {
            wchar_t p[MAX_PATH] = {};
            if (!GetModuleFileNameW(mods[i], p, MAX_PATH)) continue;
            out += "  " + util::narrow(fs::path(p).filename().wstring()) + "\r\n";
        }
    }
    util::writeFile(util::fromUtf8(path), out);
}

LONG WINAPI onException(EXCEPTION_POINTERS* info) {
    void* frames[40] = {};
    USHORT n = RtlCaptureStackBackTrace(0, 40, frames, nullptr);
    writeReport("unhandled exception", info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0,
                info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : nullptr, frames, n);
    std::wstring msg = L"Voxual hit an unexpected error and has to close.\r\n\r\nA report was written to:\r\n" +
                       util::fromUtf8(std::string(g_logDir.empty() ? util::pathStr(util::appDataDir()) : g_logDir)).wstring() +
                       L"\r\n\r\nPlease attach the newest crash-*.log when reporting this.";
    MessageBoxW(nullptr, msg.c_str(), L"Voxual", MB_OK | MB_ICONERROR | MB_TOPMOST);
    return EXCEPTION_EXECUTE_HANDLER;
}

void onTerminate() {
    void* frames[40] = {};
    USHORT n = RtlCaptureStackBackTrace(0, 40, frames, nullptr);
    writeReport("std::terminate", 0, nullptr, frames, n);
    MessageBoxW(nullptr, L"Voxual hit an unexpected error and has to close. A report was written next to your settings.",
                L"Voxual", MB_OK | MB_ICONERROR | MB_TOPMOST);
    abort();
}

void onPureCall() {
    void* frames[40] = {};
    USHORT n = RtlCaptureStackBackTrace(0, 40, frames, nullptr);
    writeReport("pure virtual call", 0, nullptr, frames, n);
    abort();
}
}  // namespace

void breadcrumb(const std::string& text) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_breadcrumbs.push_back(text);
    if (g_breadcrumbs.size() > 24) g_breadcrumbs.erase(g_breadcrumbs.begin());
}

void install() {
    g_logDir = util::pathStr(util::appDataDir());
    SetUnhandledExceptionFilter(onException);
    std::set_terminate(onTerminate);
    _set_purecall_handler(onPureCall);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
}

void beginSession() {
    g_logDir = util::pathStr(util::appDataDir());
    fs::path marker = util::appDataDir() / "session.running";
    std::string prev;
    if (util::readFile(marker, prev)) {
        g_prevCrashed = true;
        util::writeFile(util::appDataDir() / "session.crashed", prev);   // keep the evidence around
    }
    util::writeFile(marker, stamp(false) + "\r\npid " + std::to_string((long)GetCurrentProcessId()) + "\r\n");
    breadcrumb("session started " + stamp(false));
}

void endSession() {
    std::error_code ec;
    fs::remove(util::appDataDir() / "session.running", ec);
}

bool previousSessionCrashed() { return g_prevCrashed; }

std::string lastCrashLogPath() {
    std::error_code ec;
    fs::path dir = util::appDataDir();
    std::string newest;
    fs::file_time_type newestTime{};
    for (auto& e : fs::directory_iterator(dir, ec)) {
        std::string name = util::pathStr(e.path().filename());
        if (name.rfind("crash-", 0) != 0 || e.path().extension() != ".log") continue;
        auto t = fs::last_write_time(e.path(), ec);
        if (newest.empty() || t > newestTime) {
            newest = util::pathStr(e.path());
            newestTime = t;
        }
    }
    return newest;
}

}  // namespace crashlog
