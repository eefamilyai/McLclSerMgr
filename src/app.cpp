#include "app.h"

#include <windows.h>
#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <thread>

#include "imgui_internal.h"
#include "java.h"
#include "crashlog.h"
#include "version.h"

using namespace ui;
namespace P = providers;

ImU32 stateColor(State s) {
    switch (s) {
    case State::Running: return RGBA(col::green);
    case State::Starting:
    case State::Stopping: return RGBA(col::amber);
    case State::Crashed: return RGBA(col::red);
    default: return RGBA(col::mute);
    }
}

const char* stateTip(State s) {
    switch (s) {
    case State::Running: return "Online";
    case State::Starting: return "Starting up";
    case State::Stopping: return "Shutting down";
    case State::Crashed: return "Crashed";
    default: return "Stopped";
    }
}

App::App() = default;

App::~App() {
    // This runs before any member is destroyed and on every way out of the program, early returns
    // included. The workers touch this app's state and the process-wide caches (Java runtimes,
    // provider lists, the data folder), so one still running during static teardown terminates the
    // process - which is exactly what the --dump-theme path used to do.
    joinBackgroundWork();
    java::waitForScan();
}

// Win32 caption/border colours follow the active theme (dark title bar in dark themes).
static void applyWindowChrome(HWND hwnd, const theme::Palette& pal) {
    if (!hwnd) return;
    BOOL dark = pal.light ? FALSE : TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof dark);   // DWMWA_USE_IMMERSIVE_DARK_MODE
    auto rgb = [](unsigned c) { return RGB((c >> 16) & 255, (c >> 8) & 255, c & 255); };
    COLORREF cap = rgb(pal.sidebar), txt = rgb(pal.text), bor = rgb(pal.border);
    DwmSetWindowAttribute(hwnd, 35, &cap, sizeof cap);     // DWMWA_CAPTION_COLOR
    DwmSetWindowAttribute(hwnd, 36, &txt, sizeof txt);     // DWMWA_TEXT_COLOR
    DwmSetWindowAttribute(hwnd, 34, &bor, sizeof bor);     // DWMWA_BORDER_COLOR
}

void App::init(void* hwnd) {
    hwnd_ = hwnd;
    settings_.load();
    applyPersonalization(true);
    SetToastPlacement(settings_.toastCorner, settings_.toastSeconds);
    java::setManagedRoot(util::appDataDir() / "runtimes");
    java::scanAsync();
    store::loadServers(servers_);
    for (auto& s : servers_) lastState_[s->cfg.id] = s->state;
    rootDraft_ = settings_.serversRoot;
    resetWizard();
    buildCommands();
    crashlog::breadcrumb("app ready: theme=" + std::to_string(settings_.theme) + " accent=" + std::to_string(settings_.accent) +
                         (settings_.customAccent ? " (custom)" : "") + " font=" + std::to_string(settings_.fontFamily) +
                         " density=" + std::to_string(settings_.density) + " text=" + std::to_string(settings_.textScale));
    if (crashlog::previousSessionCrashed()) {
        recoveryOpen_ = true;
        crashlog::breadcrumb("previous session did not exit cleanly");
    }
}

void App::applyPersonalization(bool immediate) {
    crashlog::breadcrumb(std::string("apply: ") + (immediate ? "startup" : "deferred") + " theme=" + std::to_string(settings_.theme) +
                         " accent=" + std::to_string(settings_.accent) + (settings_.customAccent ? " custom" : "") +
                         " font=" + std::to_string(settings_.fontFamily) + "/" + std::to_string(settings_.monoFamily) +
                         " text=" + std::to_string(settings_.textScale) + " mono=" + std::to_string(settings_.monoScale) +
                         " density=" + std::to_string(settings_.density) + " radius=" + std::to_string(settings_.radius));
    if (!immediate) {
        // Deferred: ImFontAtlas may only be modified between frames, so the whole
        // apply runs from prepareFrame() instead of from inside the frame.
        pendingTheme_ = true;
        return;
    }
    ui::ApplyTheme(settings_.themeOptions());
    SetToastPlacement(settings_.toastCorner, settings_.toastSeconds);
    applyWindowChrome((HWND)hwnd_, theme::g_pal);
}

void App::rememberWindowSize() {
    if (demo_ || !hwnd_ || !settings_.restoreWindow) return;
    RECT r{};
    if (!GetClientRect((HWND)hwnd_, &r)) return;
    float dpi = (float)GetDpiForWindow((HWND)hwnd_) / 96.f;
    if (dpi <= 0.f) dpi = 1.f;
    settings_.windowW = std::max(900, (int)((r.right - r.left) / dpi));
    settings_.windowH = std::max(600, (int)((r.bottom - r.top) / dpi));
    settings_.save();
}

void App::saveSettings() { if (!demo_) settings_.save(); }

std::shared_ptr<ServerInstance> App::sharedOf(ServerInstance* s) {
    for (auto& p : servers_)
        if (p.get() == s) return p;
    return nullptr;
}

void App::spawnBackground(std::function<void()> fn) {
    std::lock_guard<std::mutex> lk(bgMu_);
    bg_.emplace_back(std::move(fn));
}

// Called once on the way out: a detached worker used to keep running while the CRT tore down
// the statics it touches (the Java cache, the provider caches, util's data folder).
void App::joinBackgroundWork() {
    std::vector<std::thread> pending;
    {
        std::lock_guard<std::mutex> lk(bgMu_);
        pending.swap(bg_);
    }
    for (auto& t : pending)
        if (t.joinable()) t.join();
}

ServerInstance* App::find(const std::string& id) {
    for (auto& s : servers_)
        if (s->cfg.id == id) return s.get();
    return nullptr;
}

void App::saveAll() {
    if (demo_) return;
    store::saveServers(servers_);
}

int App::activeCount() {
    int n = 0;
    for (auto& s : servers_) n += s->isActive() ? 1 : 0;
    return n;
}

void App::loadDemo() {
    demo_ = true;
    struct D { const char* name; const char* sw; const char* ver; State st; int up; std::vector<std::string> pl; int ram; };
    std::vector<D> demo = {
        {"Survival SMP", "paper", "1.21.8", State::Running, 5025, {"Steve", "Alex", "Notch"}, 4096},
        {"Creative Plots", "purpur", "1.21.8", State::Stopped, 0, {}, 2048},
        {"Modded Adventure", "neoforge", "1.21.1", State::Starting, 12, {}, 6144},
        {"Vanilla Realm", "vanilla", "1.21.8", State::Stopped, 0, {}, 2048},
        {"Fabric Tech", "fabric", "1.21.4", State::Crashed, 0, {}, 3072},
    };
    for (auto& d : demo) {
        ServerConfig c;
        c.id = util::newId();
        c.name = d.name;
        c.software = d.sw;
        c.mcVersion = d.ver;
        c.build = "build 60";
        c.dir = util::pathStr(settings_.root() / d.name);
        c.maxRamMB = d.ram;
        c.javaMajor = 21;
        c.createdAt = util::nowUnix() - 86400 * 3;
        c.lastStarted = util::nowUnix() - 3600;
        if (std::string(d.name) == "Survival SMP") { c.pinned = true; c.note = "Main world - keep backups weekly."; }
        auto s = std::make_shared<ServerInstance>(c);
        s->maxPlayers = 20;
        if (d.st == State::Running) {
            s->debugSetRunning(d.up, d.pl);
            s->cpuPercent = 14.5;
            s->memBytes = 2'400ull * 1024 * 1024;
            const char* lines[] = {"[12:01:02 INFO]: Starting minecraft server version 1.21.8", "[12:01:02 INFO]: Loading properties",
                                   "[12:01:05 INFO]: Preparing level \"world\"", "[12:01:09 WARN]: Can't keep up! Is the server overloaded? Running 2034ms behind",
                                   "[12:01:11 INFO]: Done (6.214s)! For help, type \"help\"", "[12:03:44 INFO]: Steve joined the game",
                                   "[12:03:50 INFO]: Alex joined the game", "[12:04:10 INFO]: <Steve> anyone up for mining?",
                                   "[12:05:02 ERROR]: Could not pass event PlayerMoveEvent to ExamplePlugin v1.0"};
            for (auto l : lines) s->appendLog(l, std::string(l).find("WARN") != std::string::npos ? LogWarn : std::string(l).find("ERROR") != std::string::npos ? LogError : LogInfo);
            s->appendLog("[" VX_APP_NAME "] Server started.", LogSystem);
        } else if (d.st == State::Starting) {
            s->debugSetRunning(d.up, {});
            s->state = State::Starting;
            s->cpuPercent = 62.0;
            s->memBytes = 1'100ull * 1024 * 1024;
        } else if (d.st == State::Crashed) {
            s->state = State::Crashed;
        }
        lastState_[c.id] = s->state;
        servers_.push_back(std::move(s));
    }
    buildCommands();
}

void App::gotoPage(const std::string& name) {
    if (name == "servers") navigate(Page::Servers);
    else if (name == "wizard") { resetWizard(); navigate(Page::Wizard); }
    else if (name == "wizard2") { resetWizard(); enterConfigStep(); navigate(Page::Wizard); }
    else if (name == "install") {  // dev: show the in-progress install screen (starts a real download into %TEMP%)
        resetWizard();
        wSoftware_ = 1;
        InstallRequest rq;
        rq.sw = P::Software::Paper;
        rq.mcVersion = "1.20.1";
        rq.name = "Preview Server";
        rq.port = 25588;
        rq.ramMB = 2048;
        rq.acceptEula = true;
        rq.dir = fs::temp_directory_path() / "voxual-preview";
        job_ = std::make_unique<InstallJob>(rq);
        wStep_ = 2;
        navigate(Page::Wizard);
    }
    else if (name == "quit") { quitAsk_ = true; }
    else if (name == "editor") {  // dev: Files tab on a demo server (no real folder)
        for (auto& s : servers_) {
            std::error_code ec;
            if (!fs::exists(s->cfg.path(), ec)) { openServer(s.get()); ds_.tab = 4; break; }
        }
    }
    else if (name == "settings" || name == "personalize") navigate(Page::Personalize);
    else if (name == "about") { persTab_ = 5; navigate(Page::Personalize); }
    else if (name == "appsettings") navigate(Page::Settings);
    else if (name == "import") navigate(Page::Import);
    else if (name == "palette") { paletteOpen_ = true; paletteQuery_.clear(); paletteSel_ = 0; paletteOpened_ = ImGui::GetTime(); }
    else if (name.rfind("detail", 0) == 0 && !servers_.empty()) {
        openServer(servers_[0].get());
        if (name.size() > 6) ds_.tab = atoi(name.c_str() + 6);
    }
    pageT_ = 1.f;
    scrollGuard_ = 8;
    buildCommands();
}

void App::navigate(Page p) {
    if (p == page_) return;
    page_ = p;
    pageT_ = 0.f;
    scrollGuard_ = 8;
}

void App::askConfirm(ConfirmReq r) {
    confirm_ = std::move(r);
    confirmOpen_ = true;
}

bool App::requestClose() {
    if (activeCount() == 0) return true;
    quitAsk_ = true;
    return false;
}

// ---------------------------------------------------------------------------
// per-frame
// ---------------------------------------------------------------------------
void App::tickServers() {
    int active = 0;
    for (auto& s : servers_) {
        s->tick();
        State now = s->state, prev = lastState_[s->cfg.id];
        if (now != prev) {
            if (now == State::Running) Toast(s->cfg.name + " is online", ToastKind::Success);
            else if (now == State::Crashed) Toast(s->cfg.name + " crashed", ToastKind::Error);
            else if (now == State::Stopped && prev == State::Stopping) Toast(s->cfg.name + " stopped", ToastKind::Info);
            lastState_[s->cfg.id] = now;
            if (now == State::Running || now == State::Stopped || now == State::Crashed) saveAll();
        }
        if (s->isActive()) ++active;
        if (settings_.logMaxLines > 200) {
            std::lock_guard<std::mutex> lk(s->logMutex);
            while ((int)s->log.size() > settings_.logMaxLines) s->log.pop_front();
        }
    }
    static int lastActive = -1;
    if (active != lastActive) {
        lastActive = active;
        if (hwnd_) {
            std::wstring title = util::widen(std::string(VX_APP_NAME));
            if (active > 0) title += util::widen(" - " + std::to_string(active) + (active == 1 ? " server running" : " servers running"));
            SetWindowTextW((HWND)hwnd_, title.c_str());
        }
    }
    pollJob();
    if (!autoStartDone_ && !java::scanning() && java::count() != 0) {
        autoStartDone_ = true;
        if (!demo_)
            for (auto& s : servers_)
                if (s->cfg.autoStart) { std::string e; s->start(e); }
    }
    if (quitting_) {
        bool allDone = true;
        for (auto& s : servers_) allDone = allDone && !s->isActive();
        if (allDone || ImGui::GetTime() - quitStart_ > 50.0) quitReady_ = true;
    }
}

// Called by the host between ImGui frames (after Present, before NewFrame), which is the only
// safe window for ImFontAtlas changes. Colours and metrics are applied immediately; atlas work
// waits until the value stops changing, so dragging a size slider rebuilds fonts once.
void App::prepareFrame() {
    if (pendingTheme_) {
        pendingTheme_ = false;
        ui::ApplyThemeStyle(settings_.themeOptions());
        SetToastPlacement(settings_.toastCorner, settings_.toastSeconds);
        applyWindowChrome((HWND)hwnd_, theme::g_pal);
        fontSettle_ = 12;   // ~0.2 s of quiet before touching the font atlas
    } else if (fontSettle_ > 0 && --fontSettle_ == 0) {
        ui::ApplyThemeFonts(settings_.themeOptions());
    } else if (ui::ThemeFontsPending()) {
        ui::ApplyThemeFonts(settings_.themeOptions());   // something asked mid-frame: retry here
    }
}

void App::frame() {
    tickServers();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_K)) {
        paletteOpen_ = true;
        paletteQuery_.clear();
        paletteSel_ = 0;
        paletteOpened_ = ImGui::GetTime();
    }
    float dt = ImGui::GetIO().DeltaTime;
    pageT_ = std::min(1.f, pageT_ + dt * (theme::g_opt.animSpeed > 0.f ? 4.5f : 100.f));
    ImGui::PushFont(fRegular, FS(15));
    drawShell();
    drawModals();
    drawCommandPalette();
    RenderToasts();
    ImGui::PopFont();
}

// ---------------------------------------------------------------------------
// shell
// ---------------------------------------------------------------------------
void App::drawShell() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor();

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    theme::DrawBackdrop(ImGui::GetWindowDrawList(), origin, size);

    float sbW = settings_.sidebarCollapsed ? S(78.f) : S((float)settings_.sidebarWidth);
    if (settings_.sidebarCollapsed) drawRail(sbW);
    else drawSidebar(sbW);
    ImGui::SameLine(0, 0);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + sbW, origin.y));

    bool fixedHeight = page_ == Page::Detail;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(D(38), D(30)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(D(10), D(10)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGuiWindowFlags wf = fixedHeight ? (ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse) : ImGuiWindowFlags_None;
    ImGui::BeginChild("##content", ImVec2(size.x - sbW, size.y), ImGuiChildFlags_AlwaysUseWindowPadding, wf);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    {
        if (scrollGuard_ > 0) {   // ImGui's nav init can scroll a fresh page on its own
            ImGui::SetScrollY(0.f);
            --scrollGuard_;
        }
        float ease = 1.f - std::pow(1.f - pageT_, 3.f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::max(0.001f, ease));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(10) * (1.f - ease));
        switch (page_) {
        case Page::Servers: drawServersPage(); break;
        case Page::Wizard: drawWizard(); break;
        case Page::Detail: drawDetailPage(); break;
        case Page::Personalize: drawPersonalizePage(); break;
        case Page::Settings: drawSettingsPage(); break;
        case Page::Import: drawImport(); break;
        case Page::About: drawPersonalizePage(); break;
        }
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
    ImGui::End();
}

// --- nav items ---------------------------------------------------------------
bool App::navItem(const char* label, const char* ic, bool active, int badge, const char* tip) {
    float w = ImGui::GetContentRegionAvail().x, h = HS(42);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    bool clicked = ImGui::InvisibleButton("##nav", ImVec2(w, h));
    bool hov = ImGui::IsItemHovered();
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    float a = Anim(ImGui::GetItemID() ^ 0xabc, active ? 1.f : (hov ? 0.5f : 0.f), 16.f);
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 q(p.x + w, p.y + h);
    if (a > 0.01f)
        dl->AddRectFilled(p, q, Fade(Mix(Alpha(RGBA(col::rowHover), 0.f), active ? Accent(0.14f) : RGBA(col::rowHover), a)), RD(11));
    if (active) dl->AddRectFilled(ImVec2(p.x, p.y + h * 0.26f), ImVec2(p.x + S(3.5f), q.y - h * 0.26f), Fade(Accent()), S(2));
    ImU32 fg = Mix(RGBA(col::dim), active ? Accent() : RGBA(col::text), std::min(1.f, a * 1.2f));
    DrawIcon(dl, ImVec2(p.x + D(24), p.y + h * 0.5f), ic, 16.5f, fg);
    ImVec2 ts = TextSize(fBold, 14.5f, label);
    DrawStr(dl, fBold, 14.5f, ImVec2(p.x + D(46), p.y + (h - ts.y) * 0.5f), Mix(RGBA(col::dim), RGBA(col::text), std::min(1.f, a * 1.2f)), label);
    if (badge > 0) {
        char b[16];
        snprintf(b, sizeof b, "%d", badge);
        ImVec2 bs = TextSize(fBold, 12.f, b);
        float bw = std::max(S(24), bs.x + D(14)), bh = HS(22);
        ImVec2 bp(q.x - bw - S(10), p.y + (h - bh) * 0.5f);
        dl->AddRectFilled(bp, ImVec2(bp.x + bw, bp.y + bh), Fade(Accent(0.18f)), bh * 0.5f);
        DrawStr(dl, fBold, 12.f, ImVec2(bp.x + (bw - bs.x) * 0.5f, bp.y + (bh - bs.y) * 0.5f), Accent(), b);
    }
    if (tip) Tooltip(tip);
    return clicked;
}

// --- sidebar -----------------------------------------------------------------
void App::drawSidebar(float w) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(D(16), D(20)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, RGBA(col::sidebar));
    ImGui::BeginChild("##sidebar", ImVec2(w, ImGui::GetContentRegionAvail().y), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    dl->AddRectFilledMultiColor(wp, ImVec2(wp.x + ws.x, wp.y + S(200)), Fade(Accent(0.05f)), Fade(Accent(0.0f)), Fade(Accent(0.0f)), Fade(Accent(0.0f)));
    dl->AddLine(ImVec2(wp.x + ws.x - 0.5f, wp.y), ImVec2(wp.x + ws.x - 0.5f, wp.y + ws.y), Fade(RGBA(col::border)), 1.f);

    // brand
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float tile = S(40);
        dl->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + tile, p.y + tile), Fade(Accent(0.15f)), RD(12));
        dl->AddRect(ImVec2(p.x, p.y), ImVec2(p.x + tile, p.y + tile), Fade(Accent(0.34f)), RD(12), 0, 1.f);
        DrawCube(dl, ImVec2(p.x + tile * 0.5f, p.y + tile * 0.5f), tile * 0.31f, Accent());
        DrawStr(dl, fBold, 19.f, ImVec2(p.x + tile + S(12), p.y + S(1)), RGBA(col::text), VX_APP_NAME);
        DrawStr(dl, fRegular, 12.f, ImVec2(p.x + tile + S(13), p.y + tile - S(15)), RGBA(col::mute), "v" VX_VERSION_STR);
        ImGui::SetCursorScreenPos(ImVec2(p.x + w - D(32) - S(16), p.y + S(4)));
        if (IconButton("collapse", icon::ChevronLeft, Btn::Ghost, 32, "Collapse sidebar (more room for servers)")) {
            settings_.sidebarCollapsed = true;
            saveSettings();
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + tile + D(18)));
    }

    Label("MANAGE", 11.f, col::mute, true);
    Gap(7);
    int running = activeCount();
    if (navItem("Servers", icon::Console, page_ == Page::Servers || page_ == Page::Detail, running)) navigate(Page::Servers);
    if (navItem("New server", icon::Add, page_ == Page::Wizard)) { resetWizard(); navigate(Page::Wizard); }
    if (navItem("Import existing", icon::FolderOpen, page_ == Page::Import)) navigate(Page::Import);

    // servers quick list: running + pinned
    std::vector<ServerInstance*> quick;
    for (auto& s : servers_)
        if (s->isActive() || s->cfg.pinned) quick.push_back(s.get());
    if (!quick.empty()) {
        Gap(14);
        Label("SERVERS", 11.f, col::mute, true);
        Gap(6);
        int shown = 0;
        for (auto* s : quick) {
            if (shown++ >= 6) break;
            bool active = sel_ == s && page_ == Page::Detail;
            ImGui::PushID(s->cfg.id.c_str());
            ImVec2 p = ImGui::GetCursorScreenPos();
            float h = HS(34), rw = ImGui::GetContentRegionAvail().x;
            bool clicked = ImGui::InvisibleButton("##qs", ImVec2(rw, h));
            bool hov = ImGui::IsItemHovered();
            if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (hov || active) dl->AddRectFilled(p, ImVec2(p.x + rw, p.y + h), Fade(active ? Accent(0.12f) : RGBA(col::rowHover)), RD(9));
            ImU32 sc = stateColor(s->state);
            ImVec2 dot(p.x + D(12), p.y + h * 0.5f);
            if (s->isActive()) {
                float t = (float)std::fmod(ImGui::GetTime() * 1.1, 1.0);
                dl->AddCircleFilled(dot, S(3.5f) + t * S(4), Fade(Alpha(sc, 0.30f * (1.f - t))), 16);
            }
            dl->AddCircleFilled(dot, S(3.5f), Fade(sc), 16);
            std::string nm = FitText(s->cfg.name, fRegular, 13.5f, rw - D(34));
            DrawStr(dl, fRegular, 13.5f, ImVec2(p.x + D(24), p.y + (h - TextSize(fRegular, 13.5f, "A").y) * 0.5f),
                    active ? RGBA(col::text) : RGBA(col::dim), nm.c_str());
            if (clicked) openServer(s);
            if (ImGui::BeginPopupContextItem("##qsctx")) {
                if (ImGui::MenuItem("Open")) openServer(s);
                if (s->isActive()) { if (ImGui::MenuItem("Stop")) s->stop(); }
                else if (ImGui::MenuItem("Start")) startServer(*s);
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if ((int)quick.size() > shown) {
            char more[32];
            snprintf(more, sizeof more, "+%d more", (int)quick.size() - shown);
            Gap(2);
            Label(more, 12.f, col::mute);
        }
    }

    Gap(14);
    Label("APP", 11.f, col::mute, true);
    Gap(7);
    if (navItem("Personalize", icon::Palette, page_ == Page::Personalize)) { persTab_ = 0; navigate(Page::Personalize); }
    if (navItem("About", icon::Info, false, 0)) { persTab_ = 5; navigate(Page::Personalize); }

    // footer: memory + java
    if (settings_.showSidebarStatus) {
        float footerH = HS(96);
        ImGui::SetCursorScreenPos(ImVec2(wp.x + D(16), wp.y + ws.y - footerH - D(18)));
        ImVec2 p = ImGui::GetCursorScreenPos();
        float cw = w - D(32);
        dl->AddRectFilled(p, ImVec2(p.x + cw, p.y + footerH), Fade(RGBA(col::panel)), RD(14));
        dl->AddRect(p, ImVec2(p.x + cw, p.y + footerH), Fade(RGBA(col::border)), RD(14), 0, 1.f);
        uint64_t allocMB = 0;
        for (auto& s : servers_)
            if (s->isActive()) allocMB += (uint64_t)s->cfg.maxRamMB;
        uint64_t totalMB = std::max<uint64_t>(1, util::totalRamMB());
        DrawStr(dl, fBold, 11.5f, ImVec2(p.x + D(14), p.y + S(13)), RGBA(col::mute), "MEMORY RESERVED");
        char buf[64];
        snprintf(buf, sizeof buf, "%.1f / %.0f GB", allocMB / 1024.0, totalMB / 1024.0);
        ImVec2 vs = TextSize(fBold, 15.f, buf);
        DrawStr(dl, fBold, 15.f, ImVec2(p.x + cw - D(14) - vs.x, p.y + S(10)), RGBA(col::text), buf);
        float frac = std::min(1.f, (float)allocMB / (float)totalMB);
        ImVec2 bp(p.x + D(14), p.y + S(38));
        dl->AddRectFilled(bp, ImVec2(bp.x + cw - D(28), bp.y + S(6)), Fade(RGBA(col::track)), S(3));
        if (frac > 0)
            dl->AddRectFilled(bp, ImVec2(bp.x + (cw - D(28)) * frac, bp.y + S(6)), Fade(frac > 0.85f ? RGBA(col::red) : Accent()), S(3));
        // Counted instead of copied: this runs on every frame of the sidebar.
        const bool anyJava = java::count() != 0;
        std::string jt = !anyJava ? (java::scanning() ? "Detecting Java..." : "No Java found")
                                  : "Java " + std::to_string(java::newestMajor()) + " ready";
        ImU32 jc = anyJava ? RGBA(col::green) : (java::scanning() ? RGBA(col::dim) : RGBA(col::amber));
        dl->AddCircleFilled(ImVec2(p.x + D(20), p.y + S(66)), S(3.5f), Fade(jc), 14);
        DrawStr(dl, fRegular, 12.5f, ImVec2(p.x + D(32), p.y + S(58)), RGBA(col::dim), jt.c_str());
        ImGui::Dummy(ImVec2(cw, footerH));
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
}

// --- collapsed rail ------------------------------------------------------------
void App::drawRail(float w) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, D(20)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, RGBA(col::sidebar));
    ImGui::BeginChild("##rail", ImVec2(w, ImGui::GetContentRegionAvail().y), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    dl->AddLine(ImVec2(wp.x + ws.x - 0.5f, wp.y), ImVec2(wp.x + ws.x - 0.5f, wp.y + ws.y), Fade(RGBA(col::border)), 1.f);

    auto railItem = [&](const char* id, const char* ic, bool active, const char* tip, int badge) {
        float sz = HS(42);
        ImGui::SetCursorScreenPos(ImVec2(wp.x + (w - sz) * 0.5f, ImGui::GetCursorScreenPos().y));
        ImGui::PushID(id);
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool clicked = ImGui::InvisibleButton("##ri", ImVec2(sz, sz));
        bool hov = ImGui::IsItemHovered();
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        float a = Anim(ImGui::GetItemID() ^ 0x5a11, active ? 1.f : (hov ? 0.5f : 0.f), 16.f);
        ImGui::PopID();
        if (a > 0.01f) dl->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz), Fade(Mix(Alpha(RGBA(col::rowHover), 0.f), active ? Accent(0.14f) : RGBA(col::rowHover), a)), RD(11));
        if (active) dl->AddRectFilled(ImVec2(p.x - S(9), p.y + sz * 0.26f), ImVec2(p.x - S(6), p.y + sz * 0.74f), Fade(Accent()), S(2));
        DrawIcon(dl, ImVec2(p.x + sz * 0.5f, p.y + sz * 0.5f), ic, 17.f, Mix(RGBA(col::dim), active ? Accent() : RGBA(col::text), std::min(1.f, a * 1.2f)));
        if (badge > 0) {
            dl->AddCircleFilled(ImVec2(p.x + sz - S(6), p.y + S(8)), S(7), Fade(RGBA(col::sidebar)), 16);
            dl->AddCircleFilled(ImVec2(p.x + sz - S(6), p.y + S(8)), S(5.5f), Fade(Accent()), 16);
            char b[8];
            snprintf(b, sizeof b, "%d", std::min(badge, 9));
            ImVec2 bs = TextSize(fBold, 9.f, b);
            DrawStr(dl, fBold, 9.f, ImVec2(p.x + sz - S(6) - bs.x * 0.5f, p.y + S(8) - bs.y * 0.5f), RGBA(col::onAccent), b);
        }
        Tooltip(tip);
        Gap(6);
        return clicked;
    };

    // logo doubles as the expand button
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float tile = S(42);
        p.x = wp.x + (w - tile) * 0.5f;
        ImGui::SetCursorScreenPos(p);
        ImGui::PushID("brand");
        bool clicked = ImGui::InvisibleButton("##brand", ImVec2(tile, tile));
        ImGui::PopID();
        dl->AddRectFilled(p, ImVec2(p.x + tile, p.y + tile), Fade(Accent(0.15f)), RD(12));
        dl->AddRect(p, ImVec2(p.x + tile, p.y + tile), Fade(Accent(0.34f)), RD(12), 0, 1.f);
        DrawCube(dl, ImVec2(p.x + tile * 0.5f, p.y + tile * 0.5f), tile * 0.31f, Accent());
        if (clicked) { settings_.sidebarCollapsed = false; saveSettings(); }
        Tooltip("Expand sidebar");
        Gap(16);
    }

    if (railItem("servers", icon::Console, page_ == Page::Servers || page_ == Page::Detail, "Servers", activeCount())) navigate(Page::Servers);
    if (railItem("new", icon::Add, page_ == Page::Wizard, "New server", 0)) { resetWizard(); navigate(Page::Wizard); }
    if (railItem("import", icon::FolderOpen, page_ == Page::Import, "Import existing", 0)) navigate(Page::Import);
    Gap(10);
    if (railItem("pers", icon::Palette, page_ == Page::Personalize, "Personalize", 0)) { persTab_ = 0; navigate(Page::Personalize); }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// page header
// ---------------------------------------------------------------------------
void App::drawClock(ImVec2) {
    if (!settings_.showClock) return;
    time_t now = time(nullptr);
    tm lt{};
    localtime_s(&lt, &now);
    char buf[32];
    if (settings_.clock24h) strftime(buf, sizeof buf, "%H:%M", &lt);
    else strftime(buf, sizeof buf, "%I:%M %p", &lt);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(20));
    Label(buf, 13.f, col::mute, true);
}

void App::drawTopBar(const char* title, const char* subtitle, const std::function<void()>& actions, const char* backTip, unsigned brandRgb) {
    float rowY = ImGui::GetCursorPosY();
    if (backTip) {
        ImGui::SetCursorPosY(rowY + S(6));
        if (IconButton("##back", icon::Back, Btn::Secondary, 40, backTip)) navigate(Page::Servers);
        ImGui::SameLine(0, S(14));
        ImGui::SetCursorPosY(rowY);
    }
    if (brandRgb) {
        float tile = HS(46);
        ImGui::SetCursorPosY(rowY + S(4));
        ImVec2 tp = ImGui::GetCursorScreenPos();   // taken after the offset so the tile lands in its slot
        DrawTile(ImGui::GetWindowDrawList(), tp, tile, brandRgb);
        ImGui::Dummy(ImVec2(tile, tile + S(4)));
        ImGui::SameLine(0, S(14));
        ImGui::SetCursorPosY(rowY);
    }
    ImGui::BeginGroup();
    Heading(title, 27);
    if (subtitle && *subtitle) Label(subtitle, 14.f, col::dim);
    ImGui::EndGroup();

    ImGui::SameLine();
    float avail = ImGui::GetContentRegionAvail().x;
    ImGuiID awid = ImGui::GetID("##actw");
    float aw = ImGui::GetStateStorage()->GetFloat(awid, 0.f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, avail - aw - S(6)));
    ImGui::SetCursorPosY(rowY + S(2));
    ImGui::BeginGroup();
    if (actions) actions();
    if (settings_.showClock) {
        ImGui::SameLine(0, S(16));
        drawClock(ImVec2(0, 0));
    }
    ImGui::SameLine(0, S(12));
    ImGui::SetCursorPosY(rowY + S(3));
    if (IconButton("##palette", icon::Search, Btn::Secondary, 38, "Command palette  (Ctrl+K)")) {
        paletteOpen_ = true;
        paletteQuery_.clear();
        paletteSel_ = 0;
        paletteOpened_ = ImGui::GetTime();
    }
    ImGui::EndGroup();
    ImGui::GetStateStorage()->SetFloat(awid, ImGui::GetItemRectSize().x);
    Gap(14);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, std::floor(p.y) + 0.5f), ImVec2(p.x + w, std::floor(p.y) + 0.5f), Fade(RGBA(col::border)), 1.f);
    Gap(16);
}

// ---------------------------------------------------------------------------
// command palette
// ---------------------------------------------------------------------------
void App::buildCommands() {
    commands_.clear();
    auto nav = [this](const char* label, const char* hint, const char* ic, Page p, int tab) {
        Command c;
        c.label = label;
        c.hint = hint;
        c.icon = ic;
        c.run = [this, p, tab] {
            if (p == Page::Personalize) persTab_ = tab;
            if (p == Page::Wizard) resetWizard();
            navigate(p);
        };
        commands_.push_back(c);
    };
    nav("Go to Servers", "Your server list", icon::Console, Page::Servers, 0);
    nav("Create a new server", "Pick software, version and go", icon::Add, Page::Wizard, 0);
    nav("Import an existing server", "Add a folder you already have", icon::FolderOpen, Page::Import, 0);
    nav("Personalize Voxual", "Theme, fonts, layout, console", icon::Palette, Page::Personalize, 0);
    nav("Appearance options", "Colours, fonts, background", icon::Paint, Page::Personalize, 0);
    nav("Interface options", "Layout, toasts, clock", icon::Window, Page::Personalize, 1);
    nav("Console options", "Log lines, timestamps, wrapping", icon::Console, Page::Personalize, 3);
    nav("Storage options", "Servers folder and Java", icon::Storage, Page::Personalize, 4);
    nav("About Voxual", "Version and credits", icon::Info, Page::Personalize, 5);

    const char* sw = "Switch to the next theme";
    Command theme;
    theme.label = "Cycle theme";
    theme.hint = sw;
    theme.icon = icon::Palette;
    theme.run = [this] {
        settings_.theme = (settings_.theme + 1) % theme::PaletteCount();
        applyPersonalization();
        saveSettings();
        Toast(std::string("Theme: ") + theme::PaletteName(settings_.theme), ToastKind::Success);
    };
    commands_.push_back(theme);

    for (auto& s : servers_) {
        ServerInstance* sp = s.get();
        std::string name = s->cfg.name;
        Command open;
        open.label = "Open " + name;
        open.hint = "Server console and settings";
        open.icon = icon::ChevronRight;
        open.run = [this, sp] { openServer(sp); };
        commands_.push_back(open);

        if (s->isActive()) {
            Command stop;
            stop.label = "Stop " + name;
            stop.hint = "Graceful shutdown";
            stop.icon = icon::Stop;
            stop.run = [sp] { sp->stop(); };
            commands_.push_back(stop);
        } else {
            Command start;
            start.label = "Start " + name;
            start.hint = "Launch this server";
            start.icon = icon::Play;
            start.run = [this, sp] { startServer(*sp); };
            commands_.push_back(start);
        }
        Command folder;
        folder.label = "Open folder: " + name;
        folder.hint = util::pathStr(s->cfg.path());
        folder.icon = icon::FolderOpen;
        folder.run = [sp] { util::openPath(sp->cfg.path()); };
        commands_.push_back(folder);
    }

    if (!servers_.empty()) {
        Command all;
        all.label = "Start every server";
        all.hint = "Launches all stopped servers";
        all.icon = icon::Play;
        all.run = [this] {
            for (auto& s : servers_)
                if (!s->isActive()) startServer(*s);
        };
        commands_.push_back(all);
        Command stopAll;
        stopAll.label = "Stop every server";
        stopAll.hint = "Graceful shutdown for all running servers";
        stopAll.icon = icon::Stop;
        stopAll.run = [this] {
            for (auto& s : servers_)
                if (s->isActive()) s->stop();
        };
        commands_.push_back(stopAll);
    }
    Command data;
    data.label = "Open Voxual data folder";
    data.hint = util::pathStr(util::appDataDir());
    data.icon = icon::Storage;
    data.run = [] { util::openPath(util::appDataDir()); };
    commands_.push_back(data);
}

void App::drawCommandPalette() {
    if (!paletteOpen_) return;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float appear = std::min(1.f, (float)(ImGui::GetTime() - paletteOpened_) / 0.12f);
    ImGui::GetBackgroundDrawList()->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                                                 Fade(Alpha(IM_COL32(0, 0, 0, 255), (theme::g_pal.light ? 0.28f : 0.55f) * appear)));

    float w = S(620);
    float h = S(430);
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.16f + S(30) * (1.f - appear)), ImGuiCond_Always, ImVec2(0.5f, 0.f));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(D(14), D(14)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, RD(18));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, RGBA(col::panelSoft));
    ImGui::PushStyleColor(ImGuiCol_Border, RGBA(col::borderHi));
    ImGui::Begin("##palette", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) paletteOpen_ = false;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) paletteSel_ = std::max(0, paletteSel_ - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) paletteSel_++;

    static bool focusSearch = false;
    if (ImGui::GetTime() - paletteOpened_ < 0.2) focusSearch = true;
    if (focusSearch) { ImGui::SetKeyboardFocusHere(0); focusSearch = false; }
    InputText("##pq", &paletteQuery_, "Search servers and actions...", -1, ImGuiInputTextFlags_None);
    Gap(10);

    std::string needle = util::lower(paletteQuery_);
    std::vector<const Command*> matches;
    for (auto& c : commands_) {
        // Allocation-free folding: this ran over every command on every frame.
        if (needle.empty() || util::icontains(c.label, needle) || util::icontains(c.hint, needle)) matches.push_back(&c);
    }
    if (matches.empty()) {
        Label("Nothing matches that.", 14.f, col::mute);
    }
    paletteSel_ = std::clamp(paletteSel_, 0, std::max(0, (int)matches.size() - 1));
    int runIdx = -1;
    ImGui::BeginChild("##plist", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_None);
    for (int i = 0; i < (int)matches.size(); ++i) {
        const Command& c = *matches[i];
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float rw = ImGui::GetContentRegionAvail().x, rh = HS(46);
        bool clicked = ImGui::InvisibleButton("##pc", ImVec2(rw, rh));
        bool hov = ImGui::IsItemHovered();
        if (hov) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); paletteSel_ = i; }
        bool sel = paletteSel_ == i;
        if (sel || hov)
            dl->AddRectFilled(p, ImVec2(p.x + rw, p.y + rh), Fade(sel ? Accent(0.14f) : RGBA(col::rowHover)), RD(10));
        DrawIcon(dl, ImVec2(p.x + D(18), p.y + rh * 0.5f), c.icon ? c.icon : icon::ChevronRight, 15.f, sel ? Accent() : RGBA(col::dim));
        DrawStr(dl, fBold, 14.f, ImVec2(p.x + D(38), p.y + S(6)), RGBA(col::text), c.label.c_str());
        std::string hint = FitText(c.hint, fRegular, 12.f, rw - D(60));
        DrawStr(dl, fRegular, 12.f, ImVec2(p.x + D(38), p.y + S(24)), RGBA(col::mute), hint.c_str());
        if (clicked) runIdx = i;
        ImGui::PopID();
    }
    if (paletteSel_ >= 0 && !matches.empty() && ImGui::IsKeyPressed(ImGuiKey_Enter)) runIdx = paletteSel_;
    ImGui::EndChild();
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) && runIdx < 0 && !matches.empty()) runIdx = paletteSel_;
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    if (runIdx >= 0 && runIdx < (int)matches.size()) {
        auto fn = matches[runIdx]->run;
        paletteOpen_ = false;
        if (fn) fn();
    }
}

// ---------------------------------------------------------------------------
// modals
// ---------------------------------------------------------------------------
void App::drawModals() {
    if (confirmOpen_) { ImGui::OpenPopup("##confirm"); confirmOpen_ = false; }
    if (deleteOpen_) { ImGui::OpenPopup("##delete"); deleteOpen_ = false; }
    if (quitAsk_) { ImGui::OpenPopup("##quit"); quitAsk_ = false; }

    if (recoveryOpen_) { ImGui::OpenPopup("##recovery"); recoveryOpen_ = false; }
    ImGui::PushFont(fRegular, FS(15));
    if (BeginModal("##recovery", 520)) {
        Heading(VX_APP_NAME " closed unexpectedly last time", 20);
        Gap(8);
        std::string log = crashlog::lastCrashLogPath();
        LabelWrapped(log.empty() ? "A crash report was written next to your settings, in %APPDATA%\\Voxual."
                                 : ("A crash report was written to:  " + log).c_str(),
                     13.5f, col::dim);
        Gap(6);
        LabelWrapped("If it happened while changing the appearance, resetting those options usually clears it. "
                     "Your servers, folders and Java runtimes are never touched.",
                     13.5f, col::dim);
        Gap(22);
        float b1 = ButtonWidth("Reset appearance", true), b2 = ButtonWidth("Keep settings", false),
              b3 = ButtonWidth("Open log folder", true);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - b1 - b2 - b3 - S(16)));
        if (Button("Open log folder", icon::FolderOpen, Btn::Ghost, ImVec2(0, HS(40)))) util::openPath(util::appDataDir());
        ImGui::SameLine(0, S(8));
        if (Button("Keep settings", nullptr, Btn::Secondary, ImVec2(0, HS(40)))) ImGui::CloseCurrentPopup();
        ImGui::SameLine(0, S(8));
        if (Button("Reset appearance", icon::Restart, Btn::Primary, ImVec2(0, HS(40)))) {
            resetPersonalization();
            ImGui::CloseCurrentPopup();
        }
        EndModal();
    }
    if (BeginModal("##confirm", 460)) {
        Heading(confirm_.title.c_str(), 20);
        Gap(8);
        LabelWrapped(confirm_.message.c_str(), 14.5f, col::dim);
        Gap(22);
        float bw = ButtonWidth(confirm_.confirmLabel.c_str(), false), cw = ButtonWidth("Cancel", false);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - bw - cw - S(10)));
        if (Button("Cancel", nullptr, Btn::Ghost)) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (Button(confirm_.confirmLabel.c_str(), nullptr, confirm_.danger ? Btn::Danger : Btn::Primary)) {
            auto fn = confirm_.onConfirm;
            ImGui::CloseCurrentPopup();
            if (fn) fn();
        }
        EndModal();
    }
    if (BeginModal("##delete", 480)) {
        Heading("Remove server?", 20);
        Gap(8);
        std::string nm = deleteTarget_ ? deleteTarget_->cfg.name : "";
        LabelWrapped(("\"" + nm + "\" will be removed from " VX_APP_NAME ".").c_str(), 14.5f, col::dim);
        Gap(14);
        Toggle("delfiles", &deleteFiles_);
        ImGui::SameLine(0, S(12));
        ImGui::BeginGroup();
        Label("Also delete the server files", 14.5f, col::text, true);
        Label("Worlds, configs and mods are moved to the Recycle Bin.", 12.5f, col::mute);
        ImGui::EndGroup();
        Gap(22);
        float bw = ButtonWidth("Remove", false), cw = ButtonWidth("Cancel", false);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - bw - cw - S(10)));
        if (Button("Cancel", nullptr, Btn::Ghost)) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (Button("Remove", nullptr, Btn::Danger)) {
            ServerInstance* t = deleteTarget_;
            bool files = deleteFiles_;
            ImGui::CloseCurrentPopup();
            removeServer(t, files);
        }
        EndModal();
    }
    if (BeginModal("##quit", 480)) {
        Heading(quitting_ ? "Stopping servers..." : "Servers are still running", 20);
        Gap(8);
        if (quitting_) {
            LabelWrapped("Saving worlds and shutting everything down safely. This only takes a moment.", 14.5f, col::dim);
            Gap(14);
            ProgressBar(-1.f, ImVec2(-1, S(8)));
        } else {
            LabelWrapped("Closing " VX_APP_NAME " will stop all running servers. They are shut down gracefully so no world data is lost.",
                         14.5f, col::dim);
            Gap(22);
            float bw = ButtonWidth("Stop servers & quit", false), cw = ButtonWidth("Cancel", false);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - bw - cw - S(10)));
            if (Button("Cancel", nullptr, Btn::Ghost)) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (Button("Stop servers & quit", nullptr, Btn::Danger)) {
                quitting_ = true;
                quitStart_ = ImGui::GetTime();
                for (auto& s : servers_) {
                    if (s->state == State::Starting || s->state == State::Running) s->stop();
                }
                saveAll();
            }
        }
        EndModal();
    }
    ImGui::PopFont();
}

// ---------------------------------------------------------------------------
// server lifecycle
// ---------------------------------------------------------------------------
void App::removeServer(ServerInstance* t, bool deleteFiles) {
    if (!t) return;
    if (t->isActive()) t->kill();
    fs::path dir = t->cfg.path();
    std::string name = t->cfg.name;
    if (sel_ == t) sel_ = nullptr;
    if (deleteTarget_ == t) deleteTarget_ = nullptr;
    servers_.erase(std::remove_if(servers_.begin(), servers_.end(),
                                  [&](const std::shared_ptr<ServerInstance>& s) { return s.get() == t; }),
                   servers_.end());
    if (deleteFiles && !demo_) util::recycle(dir);
    saveAll();
    buildCommands();
    if (page_ == Page::Detail) navigate(Page::Servers);
    Toast("Removed " + name, ToastKind::Info);
}

void App::startServer(ServerInstance& s) {
    if (!demo_) {
        std::string eula;
        bool accepted = util::readFile(s.cfg.path() / "eula.txt", eula) && util::lower(eula).find("eula=true") != std::string::npos;
        if (!accepted) {
            s.needsEula = true;
            s.appendLog("[" VX_APP_NAME "] The Minecraft EULA has not been accepted yet.", LogWarn);
            Toast("Accept the Minecraft EULA first", ToastKind::Warning);
            if (page_ != Page::Detail || sel_ != &s) openServer(&s);
            return;
        }
        int port = s.configuredPort();
        if (util::isPortInUse(port)) {
            std::string msg = "Port " + std::to_string(port) + " is already in use by another program";
            s.appendLog("[" VX_APP_NAME "] " + msg + ". Change the port in the server settings.", LogError);
            Toast(msg, ToastKind::Error);
            return;
        }
    }
    std::string err;
    if (!s.start(err)) {
        Toast(err, ToastKind::Error);
        return;
    }
    saveAll();
}
