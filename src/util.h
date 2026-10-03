#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace util {

// --- strings / paths -------------------------------------------------------
std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& s);
std::string pathStr(const fs::path& p);       // UTF-8
fs::path fromUtf8(const std::string& s);
std::string trim(const std::string& s);
std::string lower(std::string s);
std::vector<std::string> split(const std::string& s, char sep);
bool startsWith(const std::string& s, const std::string& p);
bool endsWith(const std::string& s, const std::string& p);
bool contains(const std::string& s, const std::string& p);
std::string sanitizeFileName(const std::string& s);

void setAppDataDir(const fs::path& p);   // dev/testing: redirect app data
fs::path appDataDir();            // %APPDATA%\Voxual (migrates the pre-rename folder once)
fs::path defaultServersRoot();    // %USERPROFILE%\Voxual\Servers
bool readFile(const fs::path& p, std::string& out);
bool writeFile(const fs::path& p, const std::string& data);

// --- formatting ------------------------------------------------------------
std::string formatBytes(uint64_t b);
std::string formatDuration(int64_t seconds);
std::string formatAgo(int64_t unixTs);
int64_t nowUnix();
std::string newId();
// numeric dotted compare: "1.21.4" vs "1.21" ; -1, 0, 1
int compareVersions(const std::string& a, const std::string& b);
uint64_t totalRamMB();

// --- system helpers ----------------------------------------------------------
void openPath(const fs::path& p);                 // explorer
void openUrl(const std::string& url);
void setClipboard(const std::string& text);
std::string localIPv4();
bool isPortInUse(int port);
void recycle(const fs::path& p);                  // move to Recycle Bin
bool pickFolder(void* ownerHwnd, std::string& outUtf8);
bool pickFile(void* ownerHwnd, const wchar_t* filter, std::string& outUtf8);
bool pickSaveFile(void* ownerHwnd, const wchar_t* filter, const wchar_t* defExt, const std::string& suggested, std::string& outUtf8);

// --- images -------------------------------------------------------------------
// Minimal 24-bit PNG writer (used for generated server icons).
bool writePngRgb(const fs::path& path, const uint8_t* rgb, int w, int h);
bool appendTextFile(const fs::path& path, const std::string& text);
// Decode any Windows-supported image, scale it to 64x64 and save it as a PNG (server-icon.png).
bool makeServerIcon(const fs::path& src, const fs::path& dst);

// Runs a process, streaming merged stdout/stderr lines. Returns exit code (-1 on spawn failure).
int runCapture(const std::wstring& cmdline, const fs::path& cwd,
               const std::function<void(const std::string&)>& onLine,
               const std::atomic<bool>* cancel = nullptr);

}  // namespace util
