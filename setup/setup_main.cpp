// Voxual installer / uninstaller.
// One exe: embeds Voxual.exe (resource #2), installs per-user (no admin rights), creates shortcuts,
// registers an uninstall entry and copies itself to <install dir>\Uninstall.exe.
//
//   VoxualSetup.exe            interactive install
//   Uninstall.exe                 interactive uninstall (same binary, detected by name or /uninstall)
//   /S                            silent install        /D=<dir>   install location
//   /NODESKTOP /NOSTARTMENU /NOLAUNCH /REMOVEDATA   options
#include <d3d11.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "logo.h"
#include "shot.h"
#include "ui.h"
#include "util.h"
#include "version.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using namespace ui;

namespace {

const wchar_t* kUninstallKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Voxual";

struct Options {
    bool uninstall = false, silent = false;
    std::string dir;
    bool desktop = true, startMenu = true, launch = true, removeData = false;
};

fs::path selfPath() {
    wchar_t b[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, b, (DWORD)std::size(b));
    return fs::path(std::wstring(b, n));
}

fs::path knownFolder(const KNOWNFOLDERID& id) {
    PWSTR p = nullptr;
    fs::path r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) r = p;
    CoTaskMemFree(p);
    return r;
}

fs::path defaultDir() { return knownFolder(FOLDERID_LocalAppData) / L"Programs" / L"Voxual"; }
fs::path startMenuLink() { return knownFolder(FOLDERID_Programs) / L"Voxual.lnk"; }
fs::path desktopLink() { return knownFolder(FOLDERID_Desktop) / L"Voxual.lnk"; }

std::wstring regString(const wchar_t* name) {
    wchar_t buf[1024];
    DWORD sz = sizeof buf;
    if (RegGetValueW(HKEY_CURRENT_USER, kUninstallKey, name, RRF_RT_REG_SZ, nullptr, buf, &sz) == ERROR_SUCCESS) return buf;
    return L"";
}

void regSet(const wchar_t* name, const std::wstring& v) {
    RegSetKeyValueW(HKEY_CURRENT_USER, kUninstallKey, name, REG_SZ, v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
}
void regSetDword(const wchar_t* name, DWORD v) { RegSetKeyValueW(HKEY_CURRENT_USER, kUninstallKey, name, REG_DWORD, &v, sizeof v); }

bool writePayload(const fs::path& target, std::string& err) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(2), RT_RCDATA);
    if (!r) { err = "The installer is damaged (missing payload)."; return false; }
    HGLOBAL g = LoadResource(nullptr, r);
    const void* data = g ? LockResource(g) : nullptr;
    DWORD size = SizeofResource(nullptr, r);
    if (!data || !size) { err = "The installer is damaged (empty payload)."; return false; }
    fs::path tmp = target;
    tmp += L".new";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { err = "Cannot write to " + util::pathStr(tmp.parent_path()); return false; }
    DWORD w = 0;
    bool ok = WriteFile(h, data, size, &w, nullptr) && w == size;
    CloseHandle(h);
    if (!ok) { DeleteFileW(tmp.c_str()); err = "Disk write failed."; return false; }
    if (!MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        err = "Voxual is currently running. Close it and try again.";
        return false;
    }
    return true;
}

bool makeShortcut(const fs::path& lnk, const fs::path& target, const fs::path& workDir) {
    IShellLinkW* sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl)))) return false;
    sl->SetPath(target.c_str());
    sl->SetWorkingDirectory(workDir.c_str());
    sl->SetDescription(L"Voxual - Minecraft server manager");
    sl->SetIconLocation(target.c_str(), 0);
    IPersistFile* pf = nullptr;
    bool ok = false;
    if (SUCCEEDED(sl->QueryInterface(IID_PPV_ARGS(&pf)))) {
        std::error_code ec;
        fs::create_directories(lnk.parent_path(), ec);
        ok = SUCCEEDED(pf->Save(lnk.c_str(), TRUE));
        pf->Release();
    }
    sl->Release();
    return ok;
}

// step callback gets the index of the step that is starting
bool doInstall(const Options& o, const std::function<void(int)>& step, std::string& err) {
    fs::path dir = util::fromUtf8(o.dir);
    fs::path exe = dir / L"Voxual.exe";
    step(0);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) { err = "Cannot create " + o.dir + ": " + ec.message(); return false; }
    if (!writePayload(exe, err)) return false;
    fs::path unins = dir / L"Uninstall.exe";
    if (_wcsicmp(selfPath().c_str(), unins.c_str()) != 0) CopyFileW(selfPath().c_str(), unins.c_str(), FALSE);

    step(1);
    if (o.startMenu) makeShortcut(startMenuLink(), exe, dir);
    else DeleteFileW(startMenuLink().c_str());
    if (o.desktop) makeShortcut(desktopLink(), exe, dir);

    step(2);
    uint64_t size = 0;
    for (auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file(ec)) size += e.file_size(ec);
    regSet(L"DisplayName", L"Voxual");
    regSet(L"DisplayVersion", util::widen(VX_VERSION_STR));
    regSet(L"Publisher", L"Voxual");
    regSet(L"DisplayIcon", exe.wstring());
    regSet(L"InstallLocation", dir.wstring());
    regSet(L"UninstallString", L"\"" + unins.wstring() + L"\" /uninstall");
    regSet(L"QuietUninstallString", L"\"" + unins.wstring() + L"\" /uninstall /S");
    regSetDword(L"EstimatedSize", (DWORD)(size / 1024));
    regSetDword(L"NoModify", 1);
    regSetDword(L"NoRepair", 1);
    step(3);
    return true;
}

bool doUninstall(const Options& o, const std::function<void(int)>& step, std::string& err) {
    std::wstring loc = regString(L"InstallLocation");
    fs::path dir = loc.empty() ? selfPath().parent_path() : fs::path(loc);
    step(0);
    DeleteFileW(startMenuLink().c_str());
    DeleteFileW(desktopLink().c_str());
    step(1);
    fs::path exe = dir / L"Voxual.exe";
    if (fs::exists(exe) && !DeleteFileW(exe.c_str())) { err = "Voxual is currently running. Close it and try again."; return false; }
    if (o.removeData) {
        std::error_code ec;
        fs::remove_all(util::appDataDir(), ec);
    }
    step(2);
    RegDeleteKeyW(HKEY_CURRENT_USER, kUninstallKey);
    // This exe cannot delete itself while it runs: hand the clean-up to a short-lived cmd.exe.
    fs::path self = selfPath();
    std::wstring cmd = L"cmd.exe /c ping 127.0.0.1 -n 3 >nul & del /f /q \"" + self.wstring() + L"\" & rmdir \"" + dir.wstring() + L"\"";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    step(3);
    return true;
}

// ---- D3D11 host ---------------------------------------------------------------------------------
ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
IDXGISwapChain* g_swap = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;

void createRtv() {
    ID3D11Texture2D* back = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
}

bool createDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate = {60, 1};
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &fl, &g_ctx);
    if (hr == DXGI_ERROR_UNSUPPORTED)
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &fl, &g_ctx);
    if (FAILED(hr)) return false;
    createRtv();
    return true;
}

std::atomic<bool> g_busy{false};

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
    switch (msg) {
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_CLOSE:
        if (g_busy) return 0;  // do not interrupt a running install
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---- UI state ---------------------------------------------------------------------------------------
enum class Page { Options, Working, Done, Failed };

struct Setup {
    Options opt;
    Page page = Page::Options;
    std::vector<std::string> steps;
    std::atomic<int> step{-1};
    std::atomic<bool> finished{false}, ok{false};
    std::mutex mu;
    std::string error, existingVersion;
    std::thread worker;
    HWND hwnd = nullptr;
    bool close = false;

    void begin() {
        if (worker.joinable()) worker.join();
        step = -1;
        finished = false;
        ok = false;
        error.clear();
        page = Page::Working;
        g_busy = true;
        worker = std::thread([this] {
            std::string err;
            auto cb = [this](int s) {
                step = s;
                std::this_thread::sleep_for(std::chrono::milliseconds(380));  // let the user see each step
            };
            bool r = opt.uninstall ? doUninstall(opt, cb, err) : doInstall(opt, cb, err);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            { std::lock_guard<std::mutex> lk(mu); error = err; }
            ok = r;
            finished = true;
            g_busy = false;
        });
    }
};

void drawBrandPanel(float w, float h, bool uninstall) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetWindowPos();
    // Accent-tinted panel built from the active palette.
    dl->AddRectFilledMultiColor(p, ImVec2(p.x + w, p.y + h), Fade(Alpha(Mix(RGBA(col::sidebar), Accent(), 0.10f), 1.f)),
                                Fade(RGBA(col::sidebar)), Fade(RGBA(col::bg)), Fade(Alpha(Mix(RGBA(col::bg), Accent(), 0.06f), 1.f)));
    for (int i = 0; i < 5; ++i) dl->AddCircleFilled(ImVec2(p.x + w * 0.5f, p.y + S(146)), S(126.f - i * 22.f), Fade(Accent(0.02f + i * 0.012f)), 64);
    float t = S(100);
    ImVec2 tp(p.x + (w - t) * 0.5f, p.y + S(90));
    dl->AddRectFilled(tp, ImVec2(tp.x + t, tp.y + t), Fade(Accent(0.14f)), RD(24));
    dl->AddRect(tp, ImVec2(tp.x + t, tp.y + t), Fade(Accent(0.42f)), RD(24), 0, 1.5f);
    DrawCube(dl, ImVec2(tp.x + t * 0.5f, tp.y + t * 0.5f + S(1)), t * 0.31f, Accent());
    auto centered = [&](const char* text, float y, float px, ImU32 c, ImFont* font = fRegular) {
        ImVec2 sz = TextSize(font, px, text);
        DrawStr(dl, font, px, ImVec2(p.x + (w - sz.x) * 0.5f, p.y + y), c, text);
    };
    centered(VX_APP_NAME, S(212), 29.f, RGBA(col::text), fBold);
    centered(VX_APP_TAGLINE, S(250), 13.5f, RGBA(col::dim));
    centered("Minecraft Local Server Manager", S(272), 12.5f, RGBA(col::mute));
    std::string ver = std::string("Version ") + VX_VERSION_STR;
    centered(ver.c_str(), S(302), 12.5f, RGBA(col::mute));
    if (uninstall) centered("We're sorry to see you go.", (h - S(104)), 14.f, RGBA(col::dim));
    else {
        centered("Installs for your user account only.", (h - S(72)), 12.f, RGBA(col::mute));
        centered("No administrator rights needed.", (h - S(52)), 12.f, RGBA(col::mute));
    }
}

void drawContent(Setup& s, float w, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    const bool un = s.opt.uninstall;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(10), S(10)));

    if (s.page == Page::Options) {
        Heading(un ? "Uninstall Voxual" : (s.existingVersion.empty() ? "Install Voxual" : "Update Voxual"), 28);
        Gap(2);
        if (un) LabelWrapped("This removes Voxual from your PC.", 14.f, col::dim);
        else if (!s.existingVersion.empty()) {
            std::string t = "Version " + s.existingVersion + " is installed. Your servers and settings are kept.";
            LabelWrapped(t.c_str(), 14.f, col::dim);
        } else LabelWrapped("Choose where to install and which shortcuts to create.", 14.f, col::dim);
        Gap(18);
        if (!un) {
            FieldLabel("Install location");
            float bw = ButtonWidth("Browse", true);
            InputText("##dir", &s.opt.dir, nullptr, (ImGui::GetContentRegionAvail().x - bw - S(10)) / g_scale);
            ImGui::SameLine(0, S(10));
            if (Button("Browse", icon::FolderOpen, Btn::Secondary, ImVec2(0, HS(42)))) {
                std::string d;
                if (util::pickFolder(s.hwnd, d)) {
                    fs::path p = util::fromUtf8(d);
                    if (p.filename() != L"Voxual") p /= L"Voxual";
                    s.opt.dir = util::pathStr(p);
                }
            }
            Gap(10);
            auto row = [&](const char* id, bool* v, const char* title, const char* sub) {
                Toggle(id, v);
                ImGui::SameLine(0, S(12));
                ImGui::BeginGroup();
                Label(title, 14.5f, col::text, true);
                Label(sub, 12.5f, col::mute);
                ImGui::EndGroup();
                Gap(2);
            };
            row("sm", &s.opt.startMenu, "Start menu shortcut", "Find Voxual by searching in Windows");
            row("dt", &s.opt.desktop, "Desktop shortcut", "Open Voxual with one click");
            row("ln", &s.opt.launch, "Launch Voxual when finished", nullptr);
        } else {
            Toggle("rd", &s.opt.removeData);
            ImGui::SameLine(0, S(12));
            ImGui::BeginGroup();
            Label("Also delete Voxual's own data", 14.5f, col::text, true);
            Label("Settings, the server list and downloaded Java runtimes.", 12.5f, col::mute);
            ImGui::EndGroup();
            Gap(10);
            ImVec2 p = ImGui::GetCursorScreenPos();
            float bw = ImGui::GetContentRegionAvail().x, bh = S(62);
            ImGui::Dummy(ImVec2(bw, bh));
            dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + bh), Fade(Alpha(RGBA(col::green), 0.08f)), S(12));
            dl->AddRect(p, ImVec2(p.x + bw, p.y + bh), Fade(Alpha(RGBA(col::green), 0.35f)), S(12), 0, 1.f);
            DrawIcon(dl, ImVec2(p.x + S(26), p.y + bh * 0.5f), icon::Shield, 18.f, RGBA(col::green));
            dl->AddText(fRegular, FS(13.f), ImVec2(std::floor(p.x + S(50)), std::floor(p.y + S(11))), Fade(RGBA(0xC9D0E0)),
                        "Your Minecraft servers and worlds are never touched.\nThey stay exactly where they are.");
        }
        // buttons pinned to the bottom
        float bh = HS(46);
        ImGui::SetCursorPosY(h - bh - S(36));
        if (Button("Cancel", nullptr, Btn::Ghost, ImVec2(0, bh))) s.close = true;
        const char* go = un ? "Uninstall" : (s.existingVersion.empty() ? "Install" : "Update");
        float gw = ButtonWidth(go, true) + S(20);
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - gw));
        ImGui::BeginDisabled(!un && util::trim(s.opt.dir).empty());
        if (Button(go, un ? icon::Trash : icon::Download, un ? Btn::Danger : Btn::Primary, ImVec2(gw, bh))) s.begin();
        ImGui::EndDisabled();
    } else if (s.page == Page::Working) {
        Heading(un ? "Uninstalling..." : "Installing...", 28);
        Gap(2);
        LabelWrapped(un ? "Removing Voxual from your PC." : "Setting up Voxual. This only takes a moment.", 14.f, col::dim);
        Gap(22);
        int cur = s.step;
        for (int i = 0; i < (int)s.steps.size(); ++i) {
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, S(40)));
            ImVec2 c(p.x + S(14), p.y + S(20));
            if (i < cur || (s.finished && s.ok)) {
                DrawCheckCircle(dl, c, S(10), Accent(), OnAccent());
            } else if (i == cur) DrawSpinner(dl, c, S(9), S(2.5f), Accent());
            else dl->AddCircle(c, S(9), Fade(RGBA(col::borderHi)), 24, 1.5f);
            DrawStr(dl, fBold, 14.5f, ImVec2(p.x + S(38), p.y + S(9)), i <= cur ? RGBA(col::text) : RGBA(col::mute), s.steps[i].c_str());
        }
        Gap(14);
        float frac = s.steps.empty() ? 0.f : std::clamp((float)(cur + (s.finished ? 1 : 0)) / (float)s.steps.size(), 0.f, 1.f);
        ProgressBar(frac, ImVec2(-1, S(8)));
        if (s.finished) s.page = s.ok ? Page::Done : Page::Failed;
    } else if (s.page == Page::Done) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 c(p.x + S(34), p.y + S(34));
        ImGui::Dummy(ImVec2(S(68), S(68)));
        for (int i = 0; i < 3; ++i) dl->AddCircleFilled(c, S(34.f + i * 8.f), Fade(Accent(0.07f - i * 0.02f)), 40);
        dl->AddCircleFilled(c, S(30), Fade(Accent()), 40);
        DrawIcon(dl, c, icon::Check, 30.f, OnAccent());
        Gap(14);
        Heading(un ? "Voxual was removed" : "Voxual is installed", 28);
        Gap(2);
        if (un) LabelWrapped(s.opt.removeData ? "The app and its data are gone. Your servers were left untouched." : "Your servers, worlds and settings were left untouched.", 14.f, col::dim);
        else {
            LabelWrapped("You're all set. Create your first server in under a minute.", 14.f, col::dim);
            Gap(6);
            Label(s.opt.dir.c_str(), 12.5f, col::mute);
        }
        float bh = HS(46);
        ImGui::SetCursorPosY(h - bh - S(36));
        float fw = ButtonWidth(!un && s.opt.launch ? "Launch Voxual" : "Close", true) + S(20);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - fw));
        if (!un && s.opt.launch) {
            if (Button("Launch Voxual", icon::Play, Btn::Primary, ImVec2(fw, bh))) {
                fs::path exe = util::fromUtf8(s.opt.dir) / L"Voxual.exe";
                ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, exe.parent_path().c_str(), SW_SHOWNORMAL);
                s.close = true;
            }
        } else if (Button("Close", icon::Check, Btn::Primary, ImVec2(fw, bh))) s.close = true;
    } else {
        Heading(un ? "Uninstall failed" : "Installation failed", 28);
        Gap(8);
        std::string e;
        { std::lock_guard<std::mutex> lk(s.mu); e = s.error; }
        ImVec2 p = ImGui::GetCursorScreenPos();
        float bw = ImGui::GetContentRegionAvail().x;
        ImGui::Dummy(ImVec2(bw, S(70)));
        dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + S(70)), Fade(Alpha(RGBA(col::red), 0.09f)), S(12));
        dl->AddRect(p, ImVec2(p.x + bw, p.y + S(70)), Fade(Alpha(RGBA(col::red), 0.4f)), S(12), 0, 1.f);
        DrawIcon(dl, ImVec2(p.x + S(26), p.y + S(35)), icon::Warning, 18.f, RGBA(col::red));
        dl->AddText(fRegular, FS(13.5f), ImVec2(std::floor(p.x + S(50)), std::floor(p.y + S(14))), Fade(RGBA(0xE7EBF3)), e.c_str(), nullptr, bw - S(66));
        float bh = HS(46);
        ImGui::SetCursorPosY(h - bh - S(36));
        if (Button("Close", nullptr, Btn::Ghost, ImVec2(0, bh))) s.close = true;
        float rw = ButtonWidth("Try again", true) + S(20);
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - rw));
        if (Button("Try again", icon::Restart, Btn::Primary, ImVec2(rw, bh))) s.begin();
    }
    ImGui::PopStyleVar();
    (void)wp;
    (void)w;
}

void applyDarkTitleBar(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof dark);
    COLORREF cap = RGB(0x0D, 0x10, 0x16), txt = RGB(0xE7, 0xEB, 0xF3), bor = RGB(0x1E, 0x24, 0x30);
    DwmSetWindowAttribute(hwnd, 35, &cap, sizeof cap);
    DwmSetWindowAttribute(hwnd, 36, &txt, sizeof txt);
    DwmSetWindowAttribute(hwnd, 34, &bor, sizeof bor);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Setup s;
    s.opt.dir = util::pathStr(defaultDir());
    std::string shotPath, startPage;
    float scaleOverride = 0.f;
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; i < argc; ++i) {
            std::wstring w = argv[i];
            std::wstring l = w;
            for (auto& c : l) c = (wchar_t)towlower(c);
            if (l == L"/s") s.opt.silent = true;
            else if (l == L"/uninstall") s.opt.uninstall = true;
            else if (l == L"/nodesktop") s.opt.desktop = false;
            else if (l == L"/nostartmenu") s.opt.startMenu = false;
            else if (l == L"/nolaunch") s.opt.launch = false;
            else if (l == L"/removedata") s.opt.removeData = true;
            else if (l.rfind(L"/d=", 0) == 0) s.opt.dir = util::narrow(w.substr(3));
            else if (l == L"--shot" && i + 1 < argc) shotPath = util::narrow(argv[++i]);
            else if (l == L"--page" && i + 1 < argc) startPage = util::narrow(argv[++i]);
            else if (l == L"--scale" && i + 1 < argc) scaleOverride = (float)_wtof(argv[++i]);
            else if (l == L"--data" && i + 1 < argc) util::setAppDataDir(fs::path(argv[++i]));
        }
        LocalFree(argv);
    }
    if (_wcsicmp(selfPath().filename().c_str(), L"Uninstall.exe") == 0) s.opt.uninstall = true;
    s.steps = s.opt.uninstall ? std::vector<std::string>{"Removing shortcuts", "Removing program files", "Unregistering from Windows", "Finishing up"}
                              : std::vector<std::string>{"Copying Voxual", "Creating shortcuts", "Registering with Windows", "Finishing up"};

    // existing installation?
    std::wstring prevDir = regString(L"InstallLocation");
    if (!prevDir.empty()) {
        s.existingVersion = util::narrow(regString(L"DisplayVersion"));
        if (!s.opt.uninstall && s.opt.dir == util::pathStr(defaultDir())) s.opt.dir = util::narrow(prevDir);
    }

    if (s.opt.silent) {  // no UI
        std::string err;
        bool ok = s.opt.uninstall ? doUninstall(s.opt, [](int) {}, err) : doInstall(s.opt, [](int) {}, err);
        if (ok && !s.opt.uninstall && s.opt.launch) {
            fs::path exe = util::fromUtf8(s.opt.dir) / L"Voxual.exe";
            ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, exe.parent_path().c_str(), SW_SHOWNORMAL);
        }
        return ok ? 0 : 1;
    }

    ImGui_ImplWin32_EnableDpiAwareness();
    float dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    if (!shotPath.empty()) dpi = scaleOverride > 0 ? scaleOverride : 1.f;
    ui::g_scale = dpi;
    const int W = 780, H = 520;

    HICON bigIcon = logo::load(inst, GetSystemMetrics(SM_CXICON)), smallIcon = logo::load(inst, GetSystemMetrics(SM_CXSMICON));
    WNDCLASSEXW wc{sizeof wc, CS_CLASSDC, WndProc, 0, 0, inst, bigIcon, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, L"VoxualSetupWnd", smallIcon};
    RegisterClassExW(&wc);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc{0, 0, (LONG)(W * dpi), (LONG)(H * dpi)};
    AdjustWindowRect(&rc, style, FALSE);
    int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    int sx = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2, sy = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;
    HWND hwnd = CreateWindowW(wc.lpszClassName, s.opt.uninstall ? L"Voxual Uninstall" : L"Voxual Setup", style, sx, sy, ww, wh, nullptr, nullptr, inst, nullptr);
    s.hwnd = hwnd;
    applyDarkTitleBar(hwnd);
    if (!createDevice(hwnd)) {
        MessageBoxW(nullptr, L"Could not initialise Direct3D 11.", L"Voxual Setup", MB_ICONERROR);
        return 1;
    }
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);
    ui::LoadFonts();
    ui::SetupStyle();

    if (startPage == "working") { s.page = Page::Working; s.step = 1; }
    else if (startPage == "done") { s.page = Page::Done; s.finished = true; s.ok = true; }
    else if (startPage == "failed") { s.page = Page::Failed; s.error = "Voxual is currently running. Close it and try again."; }

    bool done = false;
    int frame = 0;
    const float clear[4] = {0.039f, 0.047f, 0.063f, 1.f};
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        {
            ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->Pos);
            ImGui::SetNextWindowSize(vp->Size);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, RGBA(col::bg));
            ImGui::PushFont(fRegular, FS(15));
            ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor();
            float total = ImGui::GetContentRegionAvail().x, h = ImGui::GetContentRegionAvail().y;
            float left = S(280);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
            ImGui::BeginChild("##brand", ImVec2(left, h), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            drawBrandPanel(left, h, s.opt.uninstall);
            ImGui::EndChild();
            ImGui::SameLine(0, 0);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(44), S(40)));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
            ImGui::BeginChild("##content", ImVec2(total - left, h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            DrawGlow(ImGui::GetWindowDrawList(), ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + total - left, ImGui::GetWindowPos().y + S(240)));
            drawContent(s, total - left, h);
            ImGui::EndChild();
            ImGui::End();
            ImGui::PopFont();
        }
        ImGui::Render();
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        ++frame;
        if (!shotPath.empty() && frame == 40) {
            saveBackbufferPng(g_dev, g_ctx, g_swap, shotPath.c_str());
            break;
        }
        g_swap->Present(1, 0);
        if (s.close) DestroyWindow(hwnd);
        if (!shotPath.empty()) Sleep(16);
    }
    if (s.worker.joinable()) s.worker.join();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    if (g_rtv) g_rtv->Release();
    if (g_swap) g_swap->Release();
    if (g_ctx) g_ctx->Release();
    if (g_dev) g_dev->Release();
    return s.page == Page::Done && s.ok ? 0 : 0;
}
