#include "http.h"

#include <windows.h>
#include <winhttp.h>

#include <fstream>
#include <mutex>

#include "util.h"

namespace http {

namespace {

struct Conn {
    HINTERNET session = nullptr, connect = nullptr, request = nullptr;
    ~Conn() {
        if (request) WinHttpCloseHandle(request);
        if (connect) WinHttpCloseHandle(connect);
        // session is process-wide and shared, so it is deliberately not closed here
    }
};

// Opening a session per request re-resolved the proxy configuration every time, which cost
// more than the small version-list requests themselves.
HINTERNET sharedSession() {
    static std::once_flag once;
    static HINTERNET session = nullptr;
    std::call_once(once, [] {
        session = WinHttpOpen(L"Voxual/1.0 (Minecraft server manager)", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (session) {
            DWORD decomp = WINHTTP_DECOMPRESSION_FLAG_ALL;
            WinHttpSetOption(session, WINHTTP_OPTION_DECOMPRESSION, &decomp, sizeof decomp);
        }
    });
    return session;
}

std::string winErr(const char* what) {
    return std::string(what) + " (error " + std::to_string(GetLastError()) + ")";
}

// Opens the request and reads headers. Returns status code or 0 on failure.
int openRequest(Conn& c, const std::string& url, int timeoutMs, std::string& err, uint64_t* contentLength) {
    std::wstring wurl = util::widen(url);
    URL_COMPONENTSW uc{};
    uc.dwStructSize = sizeof uc;
    uc.dwSchemeLength = uc.dwHostNameLength = uc.dwUrlPathLength = uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) { err = "Invalid URL: " + url; return 0; }
    std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.dwExtraInfoLength) path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);

    c.session = sharedSession();
    if (!c.session) { err = winErr("WinHttpOpen failed"); return 0; }

    c.connect = WinHttpConnect(c.session, host.c_str(), uc.nPort, 0);
    if (!c.connect) { err = winErr("Connect failed"); return 0; }
    DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    c.request = WinHttpOpenRequest(c.connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                   WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!c.request) { err = winErr("OpenRequest failed"); return 0; }
    WinHttpSetTimeouts(c.request, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    if (!WinHttpSendRequest(c.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(c.request, nullptr)) {
        err = winErr("Request failed — check your internet connection");
        return 0;
    }
    DWORD status = 0, sz = sizeof status;
    WinHttpQueryHeaders(c.request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    if (contentLength) {
        wchar_t buf[64] = {};
        DWORD bsz = sizeof buf;
        if (WinHttpQueryHeaders(c.request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, buf, &bsz,
                                WINHTTP_NO_HEADER_INDEX))
            *contentLength = _wcstoui64(buf, nullptr, 10);
    }
    return (int)status;
}

}  // namespace

Response get(const std::string& url, int timeoutMs) {
    Response r;
    Conn c;
    r.status = openRequest(c, url, timeoutMs, r.error, nullptr);
    if (!r.status) return r;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(c.request, &avail) || avail == 0) break;
        size_t old = r.body.size();
        r.body.resize(old + avail);
        DWORD read = 0;
        if (!WinHttpReadData(c.request, r.body.data() + old, avail, &read)) { r.error = winErr("Read failed"); break; }
        r.body.resize(old + read);
    }
    if (!r.ok() && r.error.empty()) r.error = "HTTP " + std::to_string(r.status) + " from " + url;
    return r;
}

bool download(const std::string& url, const std::filesystem::path& dest, const Progress& progress,
              const std::atomic<bool>* cancel, std::string& err) {
    Conn c;
    uint64_t total = 0;
    int status = openRequest(c, url, 30000, err, &total);
    if (!status) return false;
    if (status < 200 || status >= 300) { err = "HTTP " + std::to_string(status) + " while downloading " + url; return false; }

    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);
    std::filesystem::path part = dest;
    part += L".part";
    std::ofstream out(part, std::ios::binary | std::ios::trunc);
    if (!out) { err = "Cannot write to " + util::pathStr(part); return false; }

    uint64_t done = 0;
    std::string buf;
    bool ok = true;
    for (;;) {
        if (cancel && cancel->load()) { err = "Cancelled"; ok = false; break; }
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(c.request, &avail)) { err = winErr("Download interrupted"); ok = false; break; }
        if (avail == 0) break;
        buf.resize(avail);
        DWORD read = 0;
        if (!WinHttpReadData(c.request, buf.data(), avail, &read)) { err = winErr("Download interrupted"); ok = false; break; }
        out.write(buf.data(), read);
        if (!out) { err = "Could not write to " + util::pathStr(part) + " (disk full?)"; ok = false; break; }
        done += read;
        if (progress) progress(done, total);
    }
    out.close();
    if (!ok) { std::filesystem::remove(part, ec); return false; }
    std::filesystem::remove(dest, ec);
    std::filesystem::rename(part, dest, ec);
    if (ec) { err = "Could not finalise download: " + ec.message(); return false; }
    return true;
}

}  // namespace http
