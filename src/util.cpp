#include "util.h"

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>

namespace util {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::string pathStr(const fs::path& p) { return narrow(p.wstring()); }
fs::path fromUtf8(const std::string& s) { return fs::path(widen(s)); }

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) ++a;
    while (b > a && isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

bool startsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
bool endsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }
bool contains(const std::string& s, const std::string& p) { return s.find(p) != std::string::npos; }

std::string sanitizeFileName(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (strchr("<>:\"/\\|?*", c) || c < 32) out += '_';
        else out += (char)c;
    }
    out = trim(out);
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    if (out.empty()) out = "server";
    return out;
}

static fs::path knownFolder(const KNOWNFOLDERID& id) {
    PWSTR p = nullptr;
    fs::path r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) r = p;
    CoTaskMemFree(p);
    return r;
}

static fs::path g_dataOverride, g_dataCached;
void setAppDataDir(const fs::path& p) {
    g_dataOverride = p;
    g_dataCached.clear();
}

// %APPDATA%\Voxual. Builds from before the rename kept their data one folder over; move that
// across once so an existing server list, settings and Java runtimes keep working.
fs::path appDataDir() {
    if (!g_dataCached.empty()) return g_dataCached;
    std::error_code ec;
    fs::path p = g_dataOverride;
    if (p.empty()) {
        fs::path base = knownFolder(FOLDERID_RoamingAppData);
        p = base / L"Voxual";
        if (!fs::exists(p, ec)) {
            fs::path legacy = base / L"CraftDeck";
            if (fs::exists(legacy, ec)) fs::rename(legacy, p, ec);
        }
    }
    ec.clear();
    fs::create_directories(p, ec);
    g_dataCached = p;
    return g_dataCached;
}

fs::path defaultServersRoot() { return knownFolder(FOLDERID_Profile) / L"Voxual" / L"Servers"; }

bool readFile(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool writeFile(const fs::path& p, const std::string& data) {
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), (std::streamsize)data.size());
    return (bool)f;
}

std::string formatBytes(uint64_t b) {
    char buf[32];
    if (b < 1024) snprintf(buf, sizeof buf, "%llu B", (unsigned long long)b);
    else if (b < 1024ull * 1024) snprintf(buf, sizeof buf, "%.1f KB", b / 1024.0);
    else if (b < 1024ull * 1024 * 1024) snprintf(buf, sizeof buf, "%.1f MB", b / 1048576.0);
    else snprintf(buf, sizeof buf, "%.2f GB", b / 1073741824.0);
    return buf;
}

std::string formatDuration(int64_t s) {
    char buf[48];
    if (s < 0) s = 0;
    if (s >= 86400) snprintf(buf, sizeof buf, "%lldd %lldh", (long long)(s / 86400), (long long)(s % 86400 / 3600));
    else if (s >= 3600) snprintf(buf, sizeof buf, "%lldh %lldm", (long long)(s / 3600), (long long)(s % 3600 / 60));
    else if (s >= 60) snprintf(buf, sizeof buf, "%lldm %llds", (long long)(s / 60), (long long)(s % 60));
    else snprintf(buf, sizeof buf, "%llds", (long long)s);
    return buf;
}

int64_t nowUnix() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string formatAgo(int64_t ts) {
    if (ts <= 0) return "never";
    int64_t d = nowUnix() - ts;
    if (d < 60) return "just now";
    if (d < 3600) return std::to_string(d / 60) + " min ago";
    if (d < 86400) return std::to_string(d / 3600) + " h ago";
    return std::to_string(d / 86400) + " d ago";
}

std::string newId() {
    static std::mt19937_64 rng{std::random_device{}()};
    char buf[17];
    snprintf(buf, sizeof buf, "%08x", (unsigned)(rng() & 0xffffffffu));
    return buf;
}

int compareVersions(const std::string& a, const std::string& b) {
    auto pa = split(a, '.'), pb = split(b, '.');
    size_t n = std::max(pa.size(), pb.size());
    for (size_t i = 0; i < n; ++i) {
        long x = i < pa.size() ? atol(pa[i].c_str()) : 0;
        long y = i < pb.size() ? atol(pb[i].c_str()) : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

uint64_t totalRamMB() {
    MEMORYSTATUSEX m{sizeof m};
    GlobalMemoryStatusEx(&m);
    return m.ullTotalPhys / (1024 * 1024);
}

void openPath(const fs::path& p) {
    std::error_code ec;
    fs::create_directories(p, ec);
    ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void openUrl(const std::string& url) { ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

void setClipboard(const std::string& text) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    std::wstring w = widen(text);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
    if (h) {
        memcpy(GlobalLock(h), w.c_str(), (w.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
}

static void ensureWsa() {
    static bool done = [] { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); return true; }();
    (void)done;
}

std::string localIPv4() {
    ensureWsa();
    std::string result = "127.0.0.1";
    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET) return result;
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &to.sin_addr);
    if (connect(s, (sockaddr*)&to, sizeof to) == 0) {
        sockaddr_in me{};
        int len = sizeof me;
        if (getsockname(s, (sockaddr*)&me, &len) == 0) {
            char buf[64];
            inet_ntop(AF_INET, &me.sin_addr, buf, sizeof buf);
            result = buf;
        }
    }
    closesocket(s);
    return result;
}

bool isPortInUse(int port) {
    ensureWsa();
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return false;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    bool used = bind(s, (sockaddr*)&a, sizeof a) != 0;
    closesocket(s);
    return used;
}

void recycle(const fs::path& p) {
    std::wstring from = p.wstring();
    from.push_back(L'\0');
    from.push_back(L'\0');
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    SHFileOperationW(&op);
}

bool pickFolder(void* owner, std::string& out) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dlg)))) return false;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    bool ok = false;
    if (SUCCEEDED(dlg->Show((HWND)owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                out = narrow(path);
                CoTaskMemFree(path);
                ok = true;
            }
            item->Release();
        }
    }
    dlg->Release();
    return ok;
}

bool pickFile(void* owner, const wchar_t* filter, std::string& out) {
    wchar_t buf[MAX_PATH * 2] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = (HWND)owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = (DWORD)std::size(buf);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return false;
    out = narrow(buf);
    return true;
}

bool pickSaveFile(void* owner, const wchar_t* filter, const wchar_t* defExt, const std::string& suggested, std::string& out) {
    wchar_t buf[MAX_PATH * 2] = {};
    std::wstring name = widen(suggested);
    if (name.size() < std::size(buf)) std::copy(name.begin(), name.end(), buf);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = (HWND)owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = (DWORD)std::size(buf);
    ofn.lpstrDefExt = defExt;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetSaveFileNameW(&ofn)) return false;
    out = narrow(buf);
    return true;
}

// ---- minimal PNG writer (stored deflate), enough for generated icons -----------
static uint32_t pngCrc(const uint8_t* d, size_t n) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ d[i]) & 255] ^ (crc >> 8);
    return ~crc;
}

static void pngBe32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x >> 24));
    v.push_back((uint8_t)(x >> 16));
    v.push_back((uint8_t)(x >> 8));
    v.push_back((uint8_t)x);
}

static void pngChunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    pngBe32(out, (uint32_t)data.size());
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    pngBe32(out, pngCrc(td.data(), td.size()));
}

bool writePngRgb(const fs::path& path, const uint8_t* rgb, int w, int h) {
    std::vector<uint8_t> raw;
    raw.reserve((size_t)(w * 3 + 1) * h);
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = rgb + ((size_t)y * w + x) * 3;
            raw.push_back(p[0]);
            raw.push_back(p[1]);
            raw.push_back(p[2]);
        }
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    size_t pos = 0;
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    while (pos < raw.size()) {
        size_t n = std::min<size_t>(65535, raw.size() - pos);
        z.push_back(pos + n >= raw.size() ? 1 : 0);
        z.push_back((uint8_t)(n & 255));
        z.push_back((uint8_t)(n >> 8));
        z.push_back((uint8_t)(~n & 255));
        z.push_back((uint8_t)((~n >> 8) & 255));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    pngBe32(z, (b << 16) | a);
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    pngBe32(ihdr, (uint32_t)w);
    pngBe32(ihdr, (uint32_t)h);
    ihdr.push_back(8);
    ihdr.push_back(2);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    pngChunk(out, "IHDR", ihdr);
    pngChunk(out, "IDAT", z);
    pngChunk(out, "IEND", {});
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write((const char*)out.data(), (std::streamsize)out.size());
    return (bool)f;
}

// Decode any image Windows can read, scale it to 64x64 and save it as server-icon.png.
bool makeServerIcon(const fs::path& src, const fs::path& dst) {
    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return false;
    bool ok = false;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICFormatConverter* conv = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* encFrame = nullptr;
    IPropertyBag2* props = nullptr;
    do {
        if (FAILED(factory->CreateDecoderFromFilename(src.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec))) break;
        if (FAILED(dec->GetFrame(0, &frame))) break;
        if (FAILED(factory->CreateBitmapScaler(&scaler))) break;
        if (FAILED(scaler->Initialize(frame, 64, 64, WICBitmapInterpolationModeFant))) break;
        if (FAILED(factory->CreateFormatConverter(&conv))) break;
        if (FAILED(conv->Initialize(scaler, GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) break;
        if (FAILED(factory->CreateStream(&stream))) break;
        if (FAILED(stream->InitializeFromFilename(dst.c_str(), GENERIC_WRITE))) break;
        if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc))) break;
        if (FAILED(enc->Initialize(stream, WICBitmapEncoderNoCache))) break;
        if (FAILED(enc->CreateNewFrame(&encFrame, &props))) break;
        if (FAILED(encFrame->Initialize(props))) break;
        if (FAILED(encFrame->SetSize(64, 64))) break;
        WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
        if (FAILED(encFrame->SetPixelFormat(&fmt))) break;
        if (FAILED(encFrame->WriteSource(conv, nullptr))) break;
        if (FAILED(encFrame->Commit())) break;
        if (FAILED(enc->Commit())) break;
        ok = true;
    } while (false);
    if (props) props->Release();
    if (encFrame) encFrame->Release();
    if (enc) enc->Release();
    if (stream) stream->Release();
    if (conv) conv->Release();
    if (scaler) scaler->Release();
    if (frame) frame->Release();
    if (dec) dec->Release();
    factory->Release();
    return ok;
}

int runCapture(const std::wstring& cmdline, const fs::path& cwd,
               const std::function<void(const std::string&)>& onLine, const std::atomic<bool>* cancel) {
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::wstring cl = cmdline;
    BOOL ok = CreateProcessW(nullptr, cl.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                             cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); return -1; }

    std::string partial;
    char buf[4096];
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr)) break;  // pipe closed
        if (avail > 0) {
            DWORD n = 0;
            if (!ReadFile(rd, buf, std::min<DWORD>(avail, sizeof buf), &n, nullptr) || n == 0) break;
            partial.append(buf, n);
            size_t pos;
            while ((pos = partial.find('\n')) != std::string::npos) {
                std::string line = partial.substr(0, pos);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (onLine) onLine(line);
                partial.erase(0, pos + 1);
            }
            continue;
        }
        if (cancel && cancel->load()) { TerminateProcess(pi.hProcess, 1); break; }
        if (WaitForSingleObject(pi.hProcess, 40) == WAIT_OBJECT_0) {
            // drain whatever is left
            DWORD a2 = 0;
            while (PeekNamedPipe(rd, nullptr, 0, nullptr, &a2, nullptr) && a2 > 0) {
                DWORD n = 0;
                if (!ReadFile(rd, buf, std::min<DWORD>(a2, sizeof buf), &n, nullptr) || n == 0) break;
                partial.append(buf, n);
            }
            break;
        }
    }
    if (!partial.empty()) {
        size_t pos;
        while ((pos = partial.find('\n')) != std::string::npos) {
            std::string line = partial.substr(0, pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (onLine) onLine(line);
            partial.erase(0, pos + 1);
        }
        if (!partial.empty() && onLine) onLine(partial);
    }
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD code = (DWORD)-1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(rd);
    return (int)code;
}

}  // namespace util
