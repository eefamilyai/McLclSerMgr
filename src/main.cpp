// Voxual - entry point: Win32 window + Direct3D 11 + Dear ImGui host.
#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "app.h"
#include "crashlog.h"
#include "imgui.h"
#include "java.h"
#include "logo.h"
#include "shot.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "ui.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#define DWMWA_TEXT_COLOR 36
#define DWMWA_BORDER_COLOR 34
#endif

static ID3D11Device* g_dev = nullptr;
static ID3D11DeviceContext* g_ctx = nullptr;
static IDXGISwapChain* g_swap = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static App* g_app = nullptr;
static bool g_swapOccluded = false;
static UINT g_resizeW = 0, g_resizeH = 0;
static HICON g_icon = nullptr, g_iconSmall = nullptr;

static void createRtv() {
    if (!g_swap || !g_dev) return;
    ID3D11Texture2D* back = nullptr;
    if (FAILED(g_swap->GetBuffer(0, IID_PPV_ARGS(&back))) || !back) return;
    g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
}
static void releaseRtv() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

static bool createDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate = {60, 1};
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
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

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void applyDarkTitleBar(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
    COLORREF cap = RGB(0x0D, 0x10, 0x16), txt = RGB(0xE7, 0xEB, 0xF3), bor = RGB(0x1E, 0x24, 0x30);
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &cap, sizeof cap);
    DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &txt, sizeof txt);
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &bor, sizeof bor);
}

static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) { g_swapOccluded = true; return 0; }
        g_swapOccluded = false;
        g_resizeW = LOWORD(lp);
        g_resizeH = HIWORD(lp);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mm = (MINMAXINFO*)lp;
        float s = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
        mm->ptMinTrackSize.x = (LONG)(1040 * s);
        mm->ptMinTrackSize.y = (LONG)(660 * s);
        return 0;
    }
    case WM_DPICHANGED: {
        theme::SetScale(LOWORD(wp) / 96.f);
        if (g_app) g_app->applyPersonalization();
        ui::SetupStyle();
        RECT* r = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_CLOSE:
        if (g_app && !g_app->requestClose()) return 0;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int runSelfTest(const std::string& spec, const std::string& logPath);

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    // command line (developer options)
    std::string shotPath, startPage, dumpTheme;
    int stressFrames = 0;
    bool crashTest = false;
    bool demo = false;
    int winW = 1360, winH = 860, shotFrames = 50;
    float scaleOverride = 0.f;
    std::string scriptPath;
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; i < argc; ++i) {
            std::string a = util::narrow(argv[i]);
            if (a == "--shot" && i + 1 < argc) shotPath = util::narrow(argv[++i]);
            else if (a == "--page" && i + 1 < argc) startPage = util::narrow(argv[++i]);
            else if (a == "--dump-theme" && i + 1 < argc) dumpTheme = util::narrow(argv[++i]);
            else if (a == "--stress-ui" && i + 1 < argc) stressFrames = _wtoi(argv[++i]);
            else if (a == "--crash-test") crashTest = true;   // developer aid: verify crash reporting
            else if (a == "--demo") demo = true;
            else if (a == "--dump-icon" && i + 1 < argc) {  // build tooling: raw RGBA renders of the logo
                std::string dir = util::narrow(argv[i + 1]);
                for (int sz : {16, 24, 32, 48, 64, 128, 256, 512}) {
                    auto px = logo::render(sz);
                    std::vector<uint8_t> out;
                    out.reserve(px.size() * 4);
                    for (uint32_t p : px) { out.push_back((p >> 16) & 255); out.push_back((p >> 8) & 255); out.push_back(p & 255); out.push_back(p >> 24); }
                    std::ofstream f(dir + "/icon_" + std::to_string(sz) + ".rgba", std::ios::binary);
                    f.write((const char*)out.data(), (std::streamsize)out.size());
                }
                LocalFree(argv);
                return 0;
            }
            else if (a == "--script" && i + 1 < argc) scriptPath = util::narrow(argv[++i]);
            else if (a == "--data" && i + 1 < argc) util::setAppDataDir(fs::path(argv[++i]));
            else if (a == "--scale" && i + 1 < argc) scaleOverride = (float)_wtof(argv[++i]);
            else if (a == "--selftest" && i + 2 < argc) {
                CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                int rc = runSelfTest(util::narrow(argv[i + 1]), util::narrow(argv[i + 2]));
                LocalFree(argv);
                return rc;
            }
            else if (a == "--size" && i + 2 < argc) { winW = _wtoi(argv[++i]); winH = _wtoi(argv[++i]); }
            else if (a == "--frames" && i + 1 < argc) shotFrames = _wtoi(argv[++i]);
        }
        LocalFree(argv);
    }

    ImGui_ImplWin32_EnableDpiAwareness();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    float dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    if (!shotPath.empty()) dpi = scaleOverride > 0 ? scaleOverride : 1.f;
    theme::SetScale(dpi);
    dpi = theme::g_scale;   // read back the clamped value, the window maths below uses it

    g_icon = logo::load(inst, GetSystemMetrics(SM_CXICON));
    g_iconSmall = logo::load(inst, GetSystemMetrics(SM_CXSMICON));
    WNDCLASSEXW wc{sizeof wc, CS_CLASSDC, WndProc, 0, 0, inst, g_icon, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, L"VoxualWnd", g_iconSmall};
    RegisterClassExW(&wc);
    RECT rc{0, 0, (LONG)(winW * dpi), (LONG)(winH * dpi)};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Voxual", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                              nullptr, nullptr, inst, nullptr);
    applyDarkTitleBar(hwnd);

    if (!createDevice(hwnd)) {
        MessageBoxW(nullptr, L"Could not initialise Direct3D 11.", L"Voxual", MB_ICONERROR);
        return 1;
    }
    ShowWindow(hwnd, scriptPath.empty() ? SW_SHOWDEFAULT : SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);
    ui::LoadFonts();

    crashlog::install();
    crashlog::beginSession();
    if (crashTest) {   // deliberately fault, to prove the report and the recovery path work
        crashlog::breadcrumb("crash test requested");
        int* p = nullptr;
        *p = 42;
    }
    App app;
    g_app = &app;
    app.init(hwnd);
    ui::SetupStyle();
    if (demo) app.loadDemo();
    if (app.settings().restoreWindow) {   // reopen at the size the user left it
        int w = app.settings().windowW, h = app.settings().windowH;
        RECT r{0, 0, (LONG)(w * dpi), (LONG)(h * dpi)};
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        SetWindowPos(hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (!dumpTheme.empty()) {   // developer aid: write the resolved theme as JSON and exit
        char buf[4096];
        snprintf(buf, sizeof buf,
                 "{\n  \"palette\": \"%s\",\n  \"light\": %s,\n  \"accent\": \"#%06X\",\n  \"bg\": \"#%06X\",\n  \"bgAlt\": \"#%06X\",\n"
                 "  \"panel\": \"#%06X\",\n  \"text\": \"#%06X\",\n  \"onAccent\": \"#%06X\",\n  \"density\": %d,\n  \"radius\": %.2f,\n"
                 "  \"textScale\": %.2f,\n  \"font\": \"%s\",\n  \"mono\": \"%s\",\n  \"bgStyle\": %d,\n  \"bgStrength\": %.2f,\n"
                 "  \"backdropTop\": \"#%06X\"\n}\n",
                 theme::g_pal.name, theme::g_pal.light ? "true" : "false", theme::AccentRgb(), theme::g_pal.bg, theme::g_pal.bgAlt, theme::g_pal.panel,
                 theme::g_pal.text, theme::g_pal.onAccent, theme::g_opt.density, theme::g_opt.radius, theme::g_opt.textScale,
                 theme::FontFamilyName(theme::g_opt.fontFamily), theme::MonoFamilyName(theme::g_opt.monoFamily), theme::g_opt.bgStyle,
                 theme::g_opt.bgStrength, theme::DebugBackdropTop());
        util::writeFile(util::fromUtf8(dumpTheme), buf);
        return 0;
    }
    if (!startPage.empty()) app.gotoPage(startPage);
    else if (!app.settings().startPage.empty() && app.settings().startPage != "servers") app.gotoPage(app.settings().startPage);

    // Developer test script: one command per line
    //   click X Y | press X Y | dragto X Y | release | move X Y | char TEXT | key NAME [ctrl] | wheel DY | wait N | shot NAME
    struct Cmd { std::string op, arg; float x = 0, y = 0; };
    std::vector<Cmd> script;
    size_t scriptIdx = 0;
    int scriptWait = 20;
    std::string pendingShot;
    if (!scriptPath.empty()) {
        std::ifstream sf(scriptPath);
        std::string line;
        while (std::getline(sf, line)) {
            if (line.empty() || line[0] == '#') continue;
            Cmd c;
            auto sp = line.find(' ');
            c.op = line.substr(0, sp);
            c.arg = sp == std::string::npos ? "" : line.substr(sp + 1);
            if (c.op == "click" || c.op == "move" || c.op == "press") sscanf(c.arg.c_str(), "%f %f", &c.x, &c.y);
            script.push_back(c);
        }
    }
    auto keyFromName = [](const std::string& n) -> ImGuiKey {
        if (n == "tab") return ImGuiKey_Tab;
        if (n == "enter") return ImGuiKey_Enter;
        if (n == "down") return ImGuiKey_DownArrow;
        if (n == "up") return ImGuiKey_UpArrow;
        if (n == "left") return ImGuiKey_LeftArrow;
        if (n == "right") return ImGuiKey_RightArrow;
        if (n == "end") return ImGuiKey_End;
        if (n == "home") return ImGuiKey_Home;
        if (n == "backspace") return ImGuiKey_Backspace;
        if (n == "pagedown") return ImGuiKey_PageDown;
        if (n == "s") return ImGuiKey_S;
        if (n == "a") return ImGuiKey_A;
        if (n == "k") return ImGuiKey_K;
        if (n == "f") return ImGuiKey_F;
        if (n == "escape") return ImGuiKey_Escape;
        if (n == "space") return ImGuiKey_Space;
        return ImGuiKey_None;
    };
    // --stress-ui N: mutate one personalization option every other frame, through the same
    // path the UI uses. Used to shake out theme/font atlas bugs.
    int stressLeft = stressFrames;
    auto stressStep = [&](int step) {
        Settings& s = g_app->settings();
        const int round = step / 14;
        switch (step % 14) {
        case 0: s.theme = (s.theme + 1) % theme::PaletteCount(); break;
        case 1: s.accent = (s.accent + 1) % theme::AccentCount(); break;
        case 2: s.density = (s.density + 1) % 3; break;
        case 3: s.textScale = 0.85f + 0.05f * (round % 11); break;
        case 4: s.monoScale = 0.85f + 0.05f * (round % 13); break;
        case 5: s.fontFamily = (s.fontFamily + 1) % theme::FontFamilyCount(); break;
        case 6: s.monoFamily = (s.monoFamily + 1) % theme::MonoFamilyCount(); break;
        case 7: s.radius = 0.4f + 0.1f * (round % 12); break;
        case 8: s.bgStyle = (s.bgStyle + 1) % 5; break;
        case 9: s.bgStrength = 0.2f + 0.1f * (round % 10); break;
        case 10: s.animSpeed = (round % 2) ? 0.f : 1.f; break;
        case 11: s.shadows = !s.shadows; break;
        case 12: s.sidebarWidth = 210 + 10 * (round % 13); break;
        default: s.cardSize = (s.cardSize + 1) % 3; break;
        }
        g_app->applyPersonalization();
    };
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
        // Automated runs (--shot / --script) must finish even when the window is hidden behind
        // another one: waiting for visibility here used to stall them indefinitely.
        const bool automated = !shotPath.empty() || !script.empty();
        if (!automated && g_swapOccluded && g_swap->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) { Sleep(40); continue; }
        g_swapOccluded = false;
        if (g_resizeW && g_resizeH) {
            // ResizeBuffers fails while the pipeline still references a back buffer, and the
            // failed resize left the window showing a stretched image at the old size.
            g_ctx->OMSetRenderTargets(0, nullptr, nullptr);
            releaseRtv();
            g_ctx->Flush();
            g_swap->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeW = g_resizeH = 0;
            createRtv();
        }
        if (stressLeft > 0 && (frame % 2) == 0) {
            stressStep(stressFrames - stressLeft);
            --stressLeft;
        }
        app.prepareFrame();   // deferred theme/font changes, applied between frames
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        if (!script.empty()) {
            ImGuiIO& sio = ImGui::GetIO();
            if (scriptWait > 0) --scriptWait;
            else if (scriptIdx < script.size()) {
                Cmd& c = script[scriptIdx++];
                scriptWait = 6;
                if (c.op == "move" || c.op == "dragto") sio.AddMousePosEvent(c.x, c.y);
                else if (c.op == "press") {   // hold the left button (pair with dragto / release)
                    sio.AddMousePosEvent(c.x, c.y);
                    sio.AddMouseButtonEvent(0, true);
                } else if (c.op == "release") sio.AddMouseButtonEvent(0, false);
                else if (c.op == "click") {
                    sio.AddMousePosEvent(c.x, c.y);
                    sio.AddMouseButtonEvent(0, true);
                    scriptWait = 3;
                    script.insert(script.begin() + scriptIdx, Cmd{"mouseup", "", 0, 0});
                } else if (c.op == "mouseup") sio.AddMouseButtonEvent(0, false);
                else if (c.op == "char") {
                    for (char ch : c.arg) {
                        if (ch == '|') { sio.AddKeyEvent(ImGuiKey_Enter, true); sio.AddKeyEvent(ImGuiKey_Enter, false); }
                        else if (ch == '~') { sio.AddKeyEvent(ImGuiKey_Tab, true); sio.AddKeyEvent(ImGuiKey_Tab, false); }
                        else sio.AddInputCharacter((unsigned)ch);
                    }
                } else if (c.op == "key") {
                    bool ctrl = c.arg.find("ctrl+") == 0;
                    std::string name = ctrl ? c.arg.substr(5) : c.arg;
                    ImGuiKey k = keyFromName(name);
                    if (ctrl) sio.AddKeyEvent(ImGuiMod_Ctrl, true);
                    sio.AddKeyEvent(k, true);
                    sio.AddKeyEvent(k, false);
                    if (ctrl) sio.AddKeyEvent(ImGuiMod_Ctrl, false);
                } else if (c.op == "wheel") sio.AddMouseWheelEvent(0, (float)atof(c.arg.c_str()));
                else if (c.op == "wait") scriptWait = atoi(c.arg.c_str());
                else if (c.op == "shot") { pendingShot = c.arg; scriptWait = 4; }
            } else if (pendingShot.empty() && scriptWait == 0) {
                done = true;
            }
        }
        ImGui::NewFrame();
        app.frame();
        ImGui::Render();
        if (g_rtv) {
            g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
            g_ctx->ClearRenderTargetView(g_rtv, clear);
        }
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        ++frame;
        if (!pendingShot.empty() && scriptWait <= 1) {
            const char* tmp = getenv("TEMP");
            fs::path dir = fs::path(tmp && *tmp ? tmp : ".") / "voxual-shots";
            std::error_code ec;
            fs::create_directories(dir, ec);
            std::string out = util::pathStr(dir / (pendingShot + ".png"));
            saveBackbufferPng(g_dev, g_ctx, g_swap, out.c_str());
            pendingShot.clear();
        }
        if (!shotPath.empty() && script.empty() && frame >= shotFrames) {
            bool ok = saveBackbufferPng(g_dev, g_ctx, g_swap, shotPath.c_str());
            if (!ok) util::appendTextFile(util::appDataDir() / "voxual.log", "screenshot failed: " + shotPath + "\r\n");
            break;
        }
        HRESULT hr = g_swap->Present(1, 0);
        g_swapOccluded = hr == DXGI_STATUS_OCCLUDED;
        if (app.shouldQuit()) DestroyWindow(hwnd);
        if (!shotPath.empty()) Sleep(16);
    }

    g_app->rememberWindowSize();
    crashlog::endSession();
    app.joinBackgroundWork();   // scanners, version fetches, downloads and backups
    java::waitForScan();
    g_app = nullptr;
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    releaseRtv();
    if (g_swap) g_swap->Release();
    if (g_ctx) g_ctx->Release();
    if (g_dev) g_dev->Release();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, inst);
    return 0;
}
