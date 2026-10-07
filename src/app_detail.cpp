// Server detail view: console, overview, players, plugins/mods, backups, settings.
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <thread>

#include "app.h"
#include "imgui_internal.h"
#include "java.h"

using namespace ui;
namespace P = providers;

static std::string stripExt(std::string n) {
    for (const char* e : {".disabled", ".jar"})
        if (util::endsWith(n, e)) n.resize(n.size() - strlen(e));
    return n;
}

static std::string lowerStr(std::string s) { return util::lower(std::move(s)); }

// Case-insensitive filename compare: the list sorts used to lower two strings per comparison.
static bool nameLess(const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) < 0; }

// ---------------------------------------------------------------------------
// detail page
// ---------------------------------------------------------------------------
void App::openServer(ServerInstance* s) {
    sel_ = s;
    ds_ = DetailState();
    ds_.serverId = s->cfg.id;
    ds_.autoscroll = settings_.consoleAutoScroll;
    navigate(Page::Detail);
    pageT_ = 0.f;
}

void App::drawDetailPage() {
    ServerInstance* sp = find(ds_.serverId);
    if (!sp) { navigate(Page::Servers); return; }
    ServerInstance& s = *sp;
    const auto& info = P::info(s.cfg.sw());
    State st = s.state;
    ImU32 sc = stateColor(st);
    bool canStart = st == State::Stopped || st == State::Crashed;

    std::string sub = std::string(info.name) + (s.cfg.mcVersion.empty() ? "" : "  \xC2\xB7  " + s.cfg.mcVersion) +
                      (s.cfg.build.empty() ? "" : "  \xC2\xB7  " + s.cfg.build);
    drawTopBar(s.cfg.name.c_str(), sub.c_str(),
               [&] {
                   ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(4));
                   Badge(stateTip(st), sc, true, (st == State::Starting || st == State::Stopping) ? 1.f : 0.f);
                   ImGui::SameLine(0, S(10));
                   ImGui::SetCursorPosY(ImGui::GetCursorPosY() - S(3));
                   if (st == State::Running || st == State::Starting) {
                       if (IconButton("restart", icon::Restart, Btn::Secondary, 40, "Restart")) s.restart();
                       ImGui::SameLine(0, S(8));
                   }
                   if (IconButton("detailfolder", icon::FolderOpen, Btn::Secondary, 40, "Open server folder")) util::openPath(s.cfg.path());
                   ImGui::SameLine(0, S(8));
                   if (canStart) {
                       if (Button("Start", icon::Play, Btn::Primary, ImVec2(S(112), HS(40)))) startServer(s);
                   } else if (st == State::Stopping) {
                       if (Button("Kill", icon::Close, Btn::Danger, ImVec2(S(112), HS(40)))) s.kill();
                   } else if (Button("Stop", icon::Stop, Btn::Danger, ImVec2(S(112), HS(40)))) {
                       s.stop();
                   }
               },
               "Back to servers", serverColor(s));

    // ---- tabs -----------------------------------------------------------------
    bool hasExt = !s.extensionFolder().empty();
    std::vector<const char*> labels = {"Console", "Overview", "Players"};
    std::vector<const char*> icons = {icon::Console, icon::Info, icon::People};
    std::vector<int> ids = {0, 1, 2};
    if (hasExt) {
        labels.push_back(s.extensionFolder() == "mods" ? "Mods" : "Plugins");
        icons.push_back(icon::Package);
        ids.push_back(3);
    }
    labels.push_back("Files"); icons.push_back(icon::Edit); ids.push_back(4);
    labels.push_back("Backups"); icons.push_back(icon::History); ids.push_back(5);
    labels.push_back("Personalize"); icons.push_back(icon::Palette); ids.push_back(6);
    labels.push_back("Settings"); icons.push_back(icon::Sliders); ids.push_back(7);
    ds_.tab = std::clamp(ds_.tab, 0, (int)ids.size() - 1);
    int prevTab = ds_.tab;
    Tabs("detailtabs", labels.data(), icons.data(), (int)labels.size(), &ds_.tab);
    if (prevTab != ds_.tab) ds_.lastScan = -100;
    int tabId = ids[ds_.tab];
    Gap(16);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##tabbody", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    switch (tabId) {
    case 0: drawConsoleTab(s); break;
    case 1: drawOverviewTab(s); break;
    case 2: drawPlayersTab(s); break;
    case 3: drawExtensionsTab(s); break;
    case 4: drawFilesTab(s); break;
    case 5: drawBackupsTab(s); break;
    case 6: drawPersonalizeTab(s); break;
    case 7: drawServerSettingsTab(s); break;
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// console
// ---------------------------------------------------------------------------
static int historyCallback(ImGuiInputTextCallbackData* d) {
    auto* st = (DetailState*)d->UserData;
    if (d->EventFlag != ImGuiInputTextFlags_CallbackHistory || st->history.empty()) return 0;
    if (d->EventKey == ImGuiKey_UpArrow) {
        if (st->histPos < 0) st->histPos = (int)st->history.size() - 1;
        else if (st->histPos > 0) --st->histPos;
    } else if (d->EventKey == ImGuiKey_DownArrow) {
        if (st->histPos >= 0 && ++st->histPos >= (int)st->history.size()) st->histPos = -1;
    }
    const char* txt = st->histPos >= 0 ? st->history[st->histPos].c_str() : "";
    d->DeleteChars(0, d->BufTextLen);
    d->InsertChars(0, txt);
    return 0;
}

void App::drawConsoleTab(ServerInstance& s) {
    if (s.needsEula) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x, h = HS(66);
        ImGui::Dummy(ImVec2(w, h));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Fade(Alpha(RGBA(col::amber), 0.10f)), RD(14));
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), Fade(Alpha(RGBA(col::amber), 0.4f)), RD(14), 0, 1.f);
        DrawIcon(dl, ImVec2(p.x + D(30), p.y + h * 0.5f), icon::Warning, 20.f, RGBA(col::amber));
        DrawStr(dl, fBold, 14.5f, ImVec2(p.x + D(58), p.y + S(13)), RGBA(col::text), "Minecraft requires you to accept the EULA before the server can run.");
        DrawStr(dl, fRegular, 13.f, ImVec2(p.x + D(58), p.y + S(35)), RGBA(col::dim), "By accepting you agree to Mojang's terms (aka.ms/MinecraftEULA).");
        float bw = ButtonWidth("Accept EULA", true);
        ImGui::SetCursorScreenPos(ImVec2(p.x + w - bw - D(14), p.y + (h - HS(38)) * 0.5f));
        if (Button("Accept EULA", icon::Check, Btn::Primary, ImVec2(0, HS(38)))) {
            s.acceptEula();
            startServer(s);
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
        Gap(12);
    }

    BeginCard("##console", ImVec2(0, ImGui::GetContentRegionAvail().y), 14);
    // toolbar: filter, level filter, then the log actions right-aligned
    {
        const float gap = S(10);
        float clearW = ButtonWidth("Clear", true), copyW = ButtonWidth("Copy", true), segW = S(196);
        SearchBox("##filter", &ds_.filter, "Filter output...", -(segW + clearW + copyW + gap * 3));
        ImGui::SameLine(0, gap);
        const char* levels[3] = {"All", "Warnings", "Errors"};
        if (Segmented("##lvl", levels, 3, &ds_.logLevel, segW)) ds_.autoscroll = true;
        ImGui::SameLine(0, gap);
        if (Button("Clear", icon::Clear, Btn::Ghost, ImVec2(clearW, HS(38)))) s.clearLog();
        ImGui::SameLine(0, S(4));
        if (Button("Copy", icon::Copy, Btn::Ghost, ImVec2(copyW, HS(38)))) {
        std::string all;
        std::lock_guard<std::mutex> lk(s.logMutex);
        for (auto& l : s.log) { all += l.text; all += "\r\n"; }
            util::setClipboard(all);
            Toast("Console copied to clipboard", ToastKind::Success);
        }
    }
    Gap(8);

    // log
    float inputH = HS(52);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, RGBA(col::sunken));
    ImGui::PushStyleColor(ImGuiCol_Border, RGBA(col::borderSoft));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, RD(12));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(D(14), D(10)));
    ImGuiWindowFlags logFlags = ImGuiWindowFlags_HorizontalScrollbar;
    if (settings_.logWrap) logFlags &= ~ImGuiWindowFlags_HorizontalScrollbar;
    ImGui::BeginChild("##log", ImVec2(0, ImGui::GetContentRegionAvail().y - inputH), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                      logFlags);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, S(2.5f)));
    {
        ImGui::PushFont(fMono, Mono(13.5f));
        std::lock_guard<std::mutex> lk(s.logMutex);
        std::vector<int> filtered;
        bool useFilter = !ds_.filter.empty() || ds_.logLevel > 0;
        const std::string needle = util::lower(util::trim(ds_.filter));
        if (useFilter)
            for (int i = 0; i < (int)s.log.size(); ++i) {
                const LogLine& L = s.log[i];
                if (ds_.logLevel == 1 && L.level != LogWarn && L.level != LogError) continue;
                if (ds_.logLevel == 2 && L.level != LogError) continue;
                // Filtered in place: lowercasing each line allocated once per line per frame,
                // which is thousands of strings a second on a busy console.
                if (!needle.empty() && !util::icontains(L.text, needle)) continue;
                filtered.push_back(i);
            }
        int count = useFilter ? (int)filtered.size() : (int)s.log.size();
        if (count == 0) {
            const char* msg = useFilter ? "No lines match the current filter."
                                        : (s.isActive() ? "Waiting for output..." : "The console is empty. Press Start to launch the server.");
            DrawIcon(ImGui::GetWindowDrawList(), ImVec2(ImGui::GetCursorScreenPos().x + S(9), ImGui::GetCursorScreenPos().y + S(9)), icon::Console, 15.f,
                     RGBA(col::faint));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + S(24));
            ImGui::PushStyleColor(ImGuiCol_Text, RGBA(col::mute));
            ImGui::TextUnformatted(msg);
            ImGui::PopStyleColor();
        }
        ImGuiListClipper clip;
        clip.Begin(count, ImGui::GetTextLineHeight() + S(2.5f));
        while (clip.Step()) {
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                const LogLine& L = s.log[useFilter ? filtered[r] : r];
                ImU32 c;
                switch (L.level) {
                case LogWarn: c = RGBA(col::amber); break;
                case LogError: c = RGBA(col::red); break;
                case LogSystem: c = AccentBright(); break;
                case LogCommand: c = RGBA(col::blue); break;
                default: c = RGBA(col::text); break;
                }
                std::string text = L.text;
                size_t pos = text.find("]: ");
                if (!settings_.logTimestamps && L.level == LogInfo && !text.empty() && text[0] == '[' && pos != std::string::npos && pos < 70) {
                    text = text.substr(pos + 3);
                    pos = std::string::npos;
                } else if (settings_.logTimestamps && L.level == LogInfo && !text.empty() && text[0] == '[' && pos != std::string::npos && pos < 70) {
                    // dim the "[12:01:02 INFO]:" prefix, colour the rest
                    const char* b = text.c_str();
                    ImGui::PushStyleColor(ImGuiCol_Text, RGBA(col::faint));
                    ImGui::TextUnformatted(b, b + pos + 2);
                    ImGui::PopStyleColor();
                    ImGui::SameLine(0, 0);
                    ImGui::PushStyleColor(ImGuiCol_Text, c);
                    ImGui::TextUnformatted(b + pos + 2);
                    ImGui::PopStyleColor();
                    continue;
                }
                ImGui::PushStyleColor(ImGuiCol_Text, c);
                ImGui::TextUnformatted(text.c_str());
                ImGui::PopStyleColor();
            }
        }
        clip.End();
        ImGui::PopFont();
    }
    {
        float maxY = ImGui::GetScrollMaxY();
        if (ds_.autoscroll) ImGui::SetScrollY(maxY);
        else if (ImGui::GetScrollY() >= maxY - 2.f && maxY > 0) ds_.autoscroll = true;
        if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel > 0) ds_.autoscroll = false;
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();

    // command input
    Gap(8);
    bool active = s.isActive();
    ImGui::BeginDisabled(!active);
    float sendW = ButtonWidth("Send", true) + S(8);
    if (ds_.focusInput) { ImGui::SetKeyboardFocusHere(0); ds_.focusInput = false; }
    ImGui::SetNextItemWidth(-(sendW + S(10)));
    bool enter = InputText("##cmd", &ds_.input,
                           active ? "Type a command (say hello, op Steve, whitelist add Alex)  -  no leading /" : "Start the server to send commands",
                           -1, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory, historyCallback, &ds_);
    ImGui::SameLine(0, S(10));
    bool send = Button("Send", icon::Send, Btn::Primary, ImVec2(sendW, HS(40)));
    if ((enter || send) && !util::trim(ds_.input).empty()) {
        std::string cmd = util::trim(ds_.input);
        if (cmd[0] == '/') cmd.erase(0, 1);
        s.sendCommand(cmd);
        s.appendLog("> " + cmd, LogCommand);
        ds_.history.push_back(cmd);
        ds_.histPos = -1;
        ds_.input.clear();
        ds_.autoscroll = true;
        ds_.focusInput = true;
    }
    ImGui::EndDisabled();
    EndCard();
}

// ---------------------------------------------------------------------------
// overview
// ---------------------------------------------------------------------------
void App::drawOverviewTab(ServerInstance& s) {
    BeginScroll("##ov", ImVec2(0, 0));
    float W = ImGui::GetContentRegionAvail().x - S(4);
    float gap = S(14);
    int cols = W > S(600) ? 3 : 2;
    float tw = (W - gap * (cols - 1)) / cols;
    char buf[64];
    State st = s.state;
    snprintf(buf, sizeof buf, "%d / %d", s.playerCount(), s.maxPlayers);
    std::string players = buf;
    snprintf(buf, sizeof buf, "%.0f%%", s.cpuPercent);
    std::string cpu = s.isActive() ? buf : "-";
    std::string mem = s.isActive() ? util::formatBytes(s.memBytes) : "-";
    struct T { const char* ic; const char* label; std::string v; ImU32 c; };
    std::vector<T> tiles = {{icon::Power, "Status", stateTip(st), stateColor(st)},
                            {icon::People, "Players", players, RGBA(col::violet)},
                            {icon::Clock, "Uptime", s.isActive() ? util::formatDuration(s.uptimeSeconds()) : "-", RGBA(col::blue)},
                            {icon::Cpu, "CPU", cpu, RGBA(col::amber)},
                            {icon::Bolt, "Memory", mem, RGBA(col::green)},
                            {icon::Sliders, "Allocated", util::formatBytes((uint64_t)s.cfg.maxRamMB * 1024 * 1024), RGBA(col::mute)}};
    for (size_t i = 0; i < tiles.size(); ++i) {
        if (i % cols != 0) ImGui::SameLine(0, gap);
        statTile(tw, tiles[i].ic, tiles[i].label, tiles[i].v, tiles[i].c);
        if (i % cols == cols - 1) Gap(12);
    }
    Gap(2);

    float cw = (W - gap) / 2;
    bool twoCol = W > S(820);
    if (!twoCol) cw = W;
    int port = s.configuredPort();
    std::string local = "localhost:" + std::to_string(port);
    std::string lan = util::localIPv4() + ":" + std::to_string(port);
    float cardH = HS(300);

    BeginCard("##connect", ImVec2(cw, cardH), 22);
    SectionTitle("Connect", "Share these addresses so players can join.", icon::Wifi);
    Gap(14);
    auto addr = [&](const char* label, const std::string& a, const char* ic) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x, h = HS(52);
        ImGui::Dummy(ImVec2(w, h));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Fade(RGBA(col::field)), RD(12));
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), Fade(RGBA(col::border)), RD(12), 0, 1.f);
        DrawIcon(dl, ImVec2(p.x + D(24), p.y + h * 0.5f), ic, 16.f, Accent());
        DrawStr(dl, fRegular, 12.f, ImVec2(p.x + D(46), p.y + S(9)), RGBA(col::mute), label);
        DrawStr(dl, fMono, 14.5f, ImVec2(p.x + D(46), p.y + S(26)), RGBA(col::text), a.c_str());
        ImGui::SetCursorScreenPos(ImVec2(p.x + w - D(46), p.y + S(7)));
        if (IconButton(label, icon::Copy, Btn::Ghost, 38, "Copy address")) {
            util::setClipboard(a);
            Toast("Copied " + a, ToastKind::Success);
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + S(10)));
        ImGui::Dummy(ImVec2(1, 1));
    };
    addr("This computer", local, icon::Console);
    addr("Local network", lan, icon::Globe);
    ImGui::PushTextWrapPos(0);
    Label("Friends over the internet need port forwarding on your router (TCP).", 12.5f, col::mute);
    ImGui::PopTextWrapPos();
    if (Link("How to port forward")) util::openUrl("https://minecraft.wiki/w/Tutorials/Setting_up_a_server#Configuring_port_forwarding");
    EndCard();
    if (twoCol) ImGui::SameLine(0, gap);
    else Gap(14);

    BeginCard("##details", ImVec2(cw, cardH), 22);
    SectionTitle("Details", nullptr, icon::Info);
    Gap(10);
    auto row = [&](const char* k, const std::string& v) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::Dummy(ImVec2(w, HS(24)));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        DrawStr(dl, fRegular, 13.5f, p, RGBA(col::dim), k);
        std::string vv = FitText(v, fBold, 13.5f, w - S(140), true);
        ImVec2 ts = TextSize(fBold, 13.5f, vv.c_str());
        DrawStr(dl, fBold, 13.5f, ImVec2(p.x + w - ts.x, p.y), RGBA(col::text), vv.c_str());
    };
    auto jv = java::pick(s.cfg.javaMajor, s.cfg.javaPath);
    row("Software", std::string(P::info(s.cfg.sw()).name));
    row("Version", s.cfg.mcVersion.empty() ? "unknown" : s.cfg.mcVersion);
    row("Java", jv ? "Java " + std::to_string(jv->major) : "not installed");
    row("Memory", std::to_string(s.cfg.minRamMB) + " - " + std::to_string(s.cfg.maxRamMB) + " MB");
    row("Last started", util::formatAgo(s.cfg.lastStarted));
    row("Folder", s.cfg.dir);
    if (!s.cfg.note.empty()) {
        Gap(6);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x, h = HS(52);
        ImGui::Dummy(ImVec2(w, h));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Fade(Alpha(RGBA(col::amber), 0.08f)), RD(12));
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), Fade(Alpha(RGBA(col::amber), 0.28f)), RD(12), 0, 1.f);
        DrawIcon(dl, ImVec2(p.x + D(22), p.y + h * 0.5f), icon::Pin, 15.f, RGBA(col::amber));
        dl->AddText(fRegular, FS(13), ImVec2(std::floor(p.x + D(42)), std::floor(p.y + S(10))), Fade(RGBA(col::text)), s.cfg.note.c_str(), nullptr,
                    w - D(56));
    }
    EndCard();
    Gap(16);

    if (Button("Open folder", icon::FolderOpen, Btn::Secondary)) util::openPath(s.cfg.path());
    ImGui::SameLine();
    if (Button("Open server.properties", icon::Edit, Btn::Secondary)) {
        if (fs::exists(s.propertiesPath())) ShellExecuteW(nullptr, L"open", s.propertiesPath().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        else Toast("Start the server once to generate server.properties", ToastKind::Warning);
    }
    ImGui::SameLine();
    if (Button("Save world", icon::Save, Btn::Secondary)) {
        if (s.state == State::Running) {
            s.sendCommand("save-all flush");
            Toast("World saved", ToastKind::Success);
        } else Toast("The server is not running", ToastKind::Warning);
    }
    ImGui::SameLine();
    if (Button("Copy connect address", icon::Link, Btn::Secondary)) {
        std::string addr = util::localIPv4() + ":" + std::to_string(s.configuredPort());
        util::setClipboard(addr);
        Toast("Copied " + addr, ToastKind::Success);
    }
    Gap(20);
    EndScroll();
}

// ---------------------------------------------------------------------------
// players
// ---------------------------------------------------------------------------
void App::drawPlayersTab(ServerInstance& s) {
    BeginScroll("##pl", ImVec2(0, 0));
    float W = ImGui::GetContentRegionAvail().x - S(4), gap = S(14);
    bool twoCol = W > S(860);
    float leftW = twoCol ? W * 0.58f : W, rightW = twoCol ? W - leftW - gap : W;
    bool running = s.state == State::Running;
    auto names = s.players();

    BeginCard("##online", ImVec2(leftW, 0), 22);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(D(10), D(10)));
    char sub[64];
    snprintf(sub, sizeof sub, "%d of %d slots in use", (int)names.size(), s.maxPlayers);
    SectionTitle("Online players", sub, icon::People);
    Gap(12);
    if (names.empty()) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::Dummy(ImVec2(w, S(120)));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        DrawIcon(dl, ImVec2(p.x + w * 0.5f, p.y + S(38)), icon::People, 34.f, RGBA(col::faint));
        const char* t = running ? "Nobody is online right now" : "Start the server to see players here";
        ImVec2 ts = TextSize(fRegular, 14.f, t);
        DrawStr(dl, fRegular, 14.f, ImVec2(p.x + (w - ts.x) * 0.5f, p.y + S(72)), RGBA(col::mute), t);
    }
    for (auto& n : names) {
        ImGui::PushID(n.c_str());
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x, h = HS(58);
        ImGui::Dummy(ImVec2(w, h));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Fade(RGBA(col::sunkenSoft)), RD(12));
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), Fade(RGBA(col::borderSoft)), RD(12), 0, 1.f);
        DrawAvatar(dl, ImVec2(p.x + D(11), p.y + (h - HS(38)) * 0.5f), HS(38), n, RD(10));
        DrawStr(dl, fBold, 14.5f, ImVec2(p.x + D(60), p.y + S(11)), RGBA(col::text), n.c_str());
        DrawStr(dl, fRegular, 12.5f, ImVec2(p.x + D(60), p.y + S(31)), RGBA(col::green), "Online");
        float bx = p.x + w - D(12);
        float bwK = ButtonWidth("Ban", false), bwO = ButtonWidth("Op", false), bwKi = ButtonWidth("Kick", false);
        ImGui::SetCursorScreenPos(ImVec2(bx - bwK, p.y + (h - HS(36)) * 0.5f));
        if (Button("Ban", nullptr, Btn::Danger, ImVec2(bwK, HS(36)))) {
            s.sendCommand("ban " + n);
            Toast("Banned " + n, ToastKind::Warning);
        }
        ImGui::SetCursorScreenPos(ImVec2(bx - bwK - S(8) - bwKi, p.y + (h - HS(36)) * 0.5f));
        if (Button("Kick", nullptr, Btn::Secondary, ImVec2(bwKi, HS(36)))) s.sendCommand("kick " + n);
        ImGui::SetCursorScreenPos(ImVec2(bx - bwK - S(8) - bwKi - S(8) - bwO, p.y + (h - HS(36)) * 0.5f));
        if (Button("Op", nullptr, Btn::Secondary, ImVec2(bwO, HS(36)))) {
            s.sendCommand("op " + n);
            Toast(n + " is now an operator", ToastKind::Success);
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + S(8)));
        ImGui::Dummy(ImVec2(1, 1));
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
    EndCard();

    if (twoCol) ImGui::SameLine(0, gap);
    else Gap(14);
    BeginCard("##quick", ImVec2(rightW, 0), 22);
    SectionTitle("Quick actions", "Run common commands without typing them.", icon::Bolt);
    Gap(14);
    ImGui::BeginDisabled(!running);
    FieldLabel("Whitelist a player");
    float addW = ButtonWidth("Add", false);
    InputText("##wl", &ds_.whitelistName, "Username", ImGui::GetContentRegionAvail().x / g_scale - addW / g_scale - 10);
    ImGui::SameLine(0, S(10));
    if (Button("Add", nullptr, Btn::Secondary, ImVec2(addW, HS(40))) && !util::trim(ds_.whitelistName).empty()) {
        s.sendCommand("whitelist add " + util::trim(ds_.whitelistName));
        Toast("Whitelisted " + util::trim(ds_.whitelistName), ToastKind::Success);
        ds_.whitelistName.clear();
    }
    Gap(8);
    FieldLabel("Broadcast a message");
    float sw = ButtonWidth("Send", false);
    InputText("##bc", &ds_.broadcast, "Message to all players", ImGui::GetContentRegionAvail().x / g_scale - sw / g_scale - 10);
    ImGui::SameLine(0, S(10));
    if (Button("Send", nullptr, Btn::Secondary, ImVec2(sw, HS(40))) && !util::trim(ds_.broadcast).empty()) {
        s.sendCommand("say " + util::trim(ds_.broadcast));
        ds_.broadcast.clear();
    }
    Gap(14);
    float half = (ImGui::GetContentRegionAvail().x - S(10)) / 2;
    if (Button("Time: day", icon::Bolt, Btn::Secondary, ImVec2(half, HS(40)))) s.sendCommand("time set day");
    ImGui::SameLine(0, S(10));
    if (Button("Weather: clear", icon::Globe, Btn::Secondary, ImVec2(half, HS(40)))) s.sendCommand("weather clear");
    if (Button("Save world", icon::Save, Btn::Secondary, ImVec2(half, HS(40)))) s.sendCommand("save-all flush");
    ImGui::SameLine(0, S(10));
    if (Button("Reload whitelist", icon::Restart, Btn::Secondary, ImVec2(half, HS(40)))) s.sendCommand("whitelist reload");
    if (Button("List players", icon::People, Btn::Secondary, ImVec2(half, HS(40)))) s.sendCommand("list");
    ImGui::SameLine(0, S(10));
    if (Button("Server stats", icon::Cpu, Btn::Secondary, ImVec2(half, HS(40)))) s.sendCommand("tps");
    ImGui::EndDisabled();
    if (!running) { Gap(8); Label("Available while the server is running.", 12.5f, col::mute); }
    EndCard();
    Gap(16);
    EndScroll();
}

// ---------------------------------------------------------------------------
// plugins / mods
// ---------------------------------------------------------------------------
void App::refreshFiles(ServerInstance& s) {
    ds_.files.clear();
    std::error_code ec;
    fs::path dir = s.cfg.path() / s.extensionFolder();
    if (!fs::is_directory(dir, ec)) return;
    util::forEachDirEntry(dir, [&](const fs::directory_entry& e, std::error_code& dec) {
        if (!e.is_regular_file(dec)) return;
        const std::string n = util::pathStr(e.path().filename());
        const bool dis = util::endsWith(n, ".jar.disabled");
        if (!dis && !util::endsWith(n, ".jar")) return;
        FileEntry f;
        f.path = e.path();
        f.name = n;
        f.size = e.file_size(dec);
        f.enabled = !dis;
        ds_.files.push_back(f);
    });
    std::sort(ds_.files.begin(), ds_.files.end(), [](const FileEntry& a, const FileEntry& b) { return nameLess(a.name, b.name); });
}

void App::drawExtensionsTab(ServerInstance& s) {
    bool mods = s.extensionFolder() == "mods";
    const char* kind = mods ? "Mods" : "Plugins";
    const char* folder = mods ? "mods" : "plugins";
    if (ImGui::GetTime() - ds_.lastScan > 2.0) { refreshFiles(s); ds_.lastScan = ImGui::GetTime(); }
    BeginScroll("##ext", ImVec2(0, 0));
    float W = ImGui::GetContentRegionAvail().x - S(4);

    char sub[128];
    int enabled = 0;
    for (auto& f : ds_.files) enabled += f.enabled ? 1 : 0;
    snprintf(sub, sizeof sub, "%d installed  \xC2\xB7  %d enabled  \xC2\xB7  changes apply after a restart", (int)ds_.files.size(), enabled);
    SectionTitle(kind, sub, icon::Package);
    Gap(14);

    SearchBox("##extfilter", &ds_.extFilter, mods ? "Search mods..." : "Search plugins...", S(260));
    ImGui::SameLine(0, S(10));
    float rw = ButtonWidth("Browse online", true) + ButtonWidth("Folder", true) + ButtonWidth("Add file", true) + S(20);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - rw));
    if (Button("Browse online", icon::Globe, Btn::Secondary, ImVec2(0, HS(38)))) util::openUrl(mods ? "https://modrinth.com/mods" : "https://hangar.papermc.io");
    ImGui::SameLine(0, S(8));
    if (Button("Folder", icon::FolderOpen, Btn::Secondary, ImVec2(0, HS(38)))) util::openPath(s.cfg.path() / folder);
    ImGui::SameLine(0, S(8));
    if (Button("Add file", icon::Add, Btn::Primary, ImVec2(0, HS(38)))) {
        std::string f;
        if (util::pickFile(hwnd_, L"Java archives (*.jar)\0*.jar\0All files\0*.*\0", f)) {
            std::error_code ec;
            fs::path dst = s.cfg.path() / folder;
            fs::create_directories(dst, ec);
            fs::copy_file(util::fromUtf8(f), dst / util::fromUtf8(f).filename(), fs::copy_options::overwrite_existing, ec);
            Toast(ec ? "Could not copy file: " + ec.message() : std::string("Added to ") + kind, ec ? ToastKind::Error : ToastKind::Success);
            refreshFiles(s);
        }
    }
    Gap(14);

    std::string needle = util::lower(ds_.extFilter);
    std::vector<int> shown;
    for (int i = 0; i < (int)ds_.files.size(); ++i)
        if (needle.empty() || util::contains(util::lower(ds_.files[i].name), needle)) shown.push_back(i);

    if (ds_.files.empty() || shown.empty()) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(W, S(180)));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + W, p.y + S(180)), Fade(RGBA(col::panel)), RD(16));
        dl->AddRect(p, ImVec2(p.x + W, p.y + S(180)), Fade(RGBA(col::border)), RD(16), 0, 1.f);
        DrawIcon(dl, ImVec2(p.x + W * 0.5f, p.y + S(58)), icon::Package, 34.f, RGBA(col::faint));
        std::string t = ds_.files.empty() ? std::string("No ") + util::lower(kind) + " installed yet" : std::string("Nothing matches that search");
        ImVec2 ts = TextSize(fBold, 16.f, t.c_str());
        DrawStr(dl, fBold, 16.f, ImVec2(p.x + (W - ts.x) * 0.5f, p.y + S(96)), RGBA(col::text), t.c_str());
        const char* hint = ds_.files.empty() ? "Drop .jar files in with \"Add file\", or browse online to find some."
                                             : "Try a different name.";
        ImVec2 hs = TextSize(fRegular, 13.5f, hint);
        DrawStr(dl, fRegular, 13.5f, ImVec2(p.x + (W - hs.x) * 0.5f, p.y + S(124)), RGBA(col::dim), hint);
    }
    int toToggle = -1, toDelete = -1;
    for (int idx : shown) {
        auto& f = ds_.files[idx];
        ImGui::PushID(idx);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float h = HS(62);
        ImGui::Dummy(ImVec2(W, h));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + W, p.y + h), Fade(RGBA(col::panel)), RD(14));
        dl->AddRect(p, ImVec2(p.x + W, p.y + h), Fade(RGBA(col::border)), RD(14), 0, 1.f);
        float a = f.enabled ? 1.f : 0.5f;
        dl->AddRectFilled(ImVec2(p.x + D(14), p.y + (h - HS(36)) * 0.5f), ImVec2(p.x + D(14) + HS(36), p.y + (h + HS(36)) * 0.5f),
                          Fade(Alpha(Accent(), 0.14f * a)), RD(10));
        DrawIcon(dl, ImVec2(p.x + D(14) + HS(18), p.y + h * 0.5f), icon::Package, 17.f, Alpha(Accent(), a));
        DrawStr(dl, fBold, 14.5f, ImVec2(p.x + D(62), p.y + S(12)), Alpha(RGBA(col::text), a), stripExt(f.name).c_str());
        std::string meta = util::formatBytes(f.size) + (f.enabled ? "" : "  \xC2\xB7  disabled");
        DrawStr(dl, fRegular, 12.5f, ImVec2(p.x + D(62), p.y + S(34)), RGBA(col::mute), meta.c_str());
        ImGui::SetCursorScreenPos(ImVec2(p.x + W - D(52), p.y + (h - HS(36)) * 0.5f));
        if (IconButton("del", icon::Trash, Btn::Ghost, 36, "Move to Recycle Bin")) toDelete = idx;
        ImGui::SetCursorScreenPos(ImVec2(p.x + W - D(52) - S(62), p.y + (h - HS(24)) * 0.5f));
        bool en = f.enabled;
        if (Toggle("en", &en)) toToggle = idx;
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + S(8)));
        ImGui::Dummy(ImVec2(1, 1));
        ImGui::PopID();
    }
    if (toToggle >= 0) {
        auto& f = ds_.files[toToggle];
        std::error_code ec;
        fs::path np = f.path;
        if (f.enabled) np += L".disabled";
        else np = fs::path(f.path.wstring().substr(0, f.path.wstring().size() - 9));
        fs::rename(f.path, np, ec);
        if (ec) Toast("Could not rename: " + ec.message(), ToastKind::Error);
        refreshFiles(s);
    }
    if (toDelete >= 0) {
        util::recycle(ds_.files[toDelete].path);
        Toast("Moved " + stripExt(ds_.files[toDelete].name) + " to the Recycle Bin", ToastKind::Info);
        refreshFiles(s);
    }
    Gap(16);
    EndScroll();
}

// ---------------------------------------------------------------------------
// backups
// ---------------------------------------------------------------------------
void App::refreshBackups(ServerInstance& s) {
    ds_.backups.clear();
    std::error_code ec;
    fs::path dir = s.backupDir(settings_.root());
    if (!fs::is_directory(dir, ec)) return;
    util::forEachDirEntry(dir, [&](const fs::directory_entry& e, std::error_code& dec) {
        if (!e.is_regular_file(dec) || e.path().extension() != ".zip") return;
        BackupEntry b;
        b.path = e.path();
        b.name = util::pathStr(e.path().stem());
        b.size = e.file_size(dec);
        std::error_code tec;
        auto ft = fs::last_write_time(e.path(), tec);
        if (tec) return;
        b.time = util::nowUnix() - (int64_t)std::chrono::duration_cast<std::chrono::seconds>(fs::file_time_type::clock::now() - ft).count();
        ds_.backups.push_back(b);
    });
    std::sort(ds_.backups.begin(), ds_.backups.end(), [](const BackupEntry& a, const BackupEntry& b) { return a.name > b.name; });
}

void App::drawBackupsTab(ServerInstance& s) {
    if (ImGui::GetTime() - ds_.lastScan > 2.0) { refreshBackups(s); ds_.lastScan = ImGui::GetTime(); }
    BeginScroll("##bk", ImVec2(0, 0));
    float W = ImGui::GetContentRegionAvail().x - S(4);
    uint64_t total = 0;
    for (auto& b : ds_.backups) total += b.size;
    char sub[128];
    snprintf(sub, sizeof sub, "%d backup%s  \xC2\xB7  %s on disk  \xC2\xB7  a running server is saved first", (int)ds_.backups.size(),
             ds_.backups.size() == 1 ? "" : "s", util::formatBytes(total).c_str());
    SectionTitle("Backups", sub, icon::History);
    Gap(14);
    bool busy = s.backupRunning;
    float bw = ButtonWidth(busy ? "Backing up..." : "Back up now", true);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - bw - ButtonWidth("Folder", true) - S(16)));
    if (Button("Folder", icon::FolderOpen, Btn::Secondary, ImVec2(0, HS(40)))) util::openPath(s.backupDir(settings_.root()));
    ImGui::SameLine(0, S(8));
    ImGui::BeginDisabled(busy);
    if (Button(busy ? "Backing up..." : "Back up now", icon::Save, Btn::Primary, ImVec2(0, HS(40)))) {
        // The worker takes a share of the server so that removing it mid-backup cannot free the
        // object out from under the thread.
        std::shared_ptr<ServerInstance> keep = sharedOf(&s);
        fs::path root = settings_.root();
        s.backupRunning = true;
        if (keep)
            spawnBackground([keep, root] {
                bool ok = false;
                keep->createBackup(root, &ok);
            });
        Toast("Backup started...", ToastKind::Info);
    }
    ImGui::EndDisabled();
    Gap(14);

    if (ds_.backups.empty()) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(W, S(180)));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + W, p.y + S(180)), Fade(RGBA(col::panel)), RD(16));
        dl->AddRect(p, ImVec2(p.x + W, p.y + S(180)), Fade(RGBA(col::border)), RD(16), 0, 1.f);
        DrawIcon(dl, ImVec2(p.x + W * 0.5f, p.y + S(56)), icon::History, 34.f, RGBA(col::faint));
        const char* t = "No backups yet";
        ImVec2 ts = TextSize(fBold, 16.f, t);
        DrawStr(dl, fBold, 16.f, ImVec2(p.x + (W - ts.x) * 0.5f, p.y + S(96)), RGBA(col::text), t);
        const char* hint = "Create one before big changes like updating plugins or versions.";
        ImVec2 hs = TextSize(fRegular, 13.5f, hint);
        DrawStr(dl, fRegular, 13.5f, ImVec2(p.x + (W - hs.x) * 0.5f, p.y + S(124)), RGBA(col::dim), hint);
    }
    int restore = -1, del = -1;
    for (int i = 0; i < (int)ds_.backups.size(); ++i) {
        auto& b = ds_.backups[i];
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float h = HS(64);
        ImGui::Dummy(ImVec2(W, h));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + W, p.y + h), Fade(RGBA(col::panel)), RD(14));
        dl->AddRect(p, ImVec2(p.x + W, p.y + h), Fade(RGBA(col::border)), RD(14), 0, 1.f);
        dl->AddRectFilled(ImVec2(p.x + D(14), p.y + (h - HS(38)) * 0.5f), ImVec2(p.x + D(14) + HS(38), p.y + (h + HS(38)) * 0.5f),
                          Fade(Alpha(RGBA(col::blue), 0.14f)), RD(10));
        DrawIcon(dl, ImVec2(p.x + D(14) + HS(19), p.y + h * 0.5f), icon::History, 17.f, RGBA(col::blue));
        DrawStr(dl, fBold, 14.5f, ImVec2(p.x + D(64), p.y + S(13)), RGBA(col::text), b.name.c_str());
        std::string meta = util::formatBytes(b.size) + "  \xC2\xB7  " + util::formatAgo(b.time);
        DrawStr(dl, fRegular, 12.5f, ImVec2(p.x + D(64), p.y + S(35)), RGBA(col::mute), meta.c_str());
        float dw = ButtonWidth("Delete", false), rwd = ButtonWidth("Restore", false);
        ImGui::SetCursorScreenPos(ImVec2(p.x + W - D(14) - dw, p.y + (h - HS(36)) * 0.5f));
        if (Button("Delete", nullptr, Btn::Danger, ImVec2(dw, HS(36)))) del = i;
        ImGui::SetCursorScreenPos(ImVec2(p.x + W - D(14) - dw - S(8) - rwd, p.y + (h - HS(36)) * 0.5f));
        if (Button("Restore", nullptr, Btn::Secondary, ImVec2(rwd, HS(36)))) restore = i;
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + S(8)));
        ImGui::Dummy(ImVec2(1, 1));
        ImGui::PopID();
    }
    if (del >= 0) {
        fs::path bp = ds_.backups[del].path;
        if (settings_.confirmDestructive) {
            std::string nm = ds_.backups[del].name;
            // Capture a share, not the address: the confirmation can outlive the server list.
            std::shared_ptr<ServerInstance> keep = sharedOf(&s);
            askConfirm({"Delete this backup?", nm + " will be moved to the Recycle Bin.", "Delete", true, [this, bp, keep] {
                            util::recycle(bp);
                            Toast("Backup moved to the Recycle Bin", ToastKind::Info);
                            if (keep) refreshBackups(*keep);
                        }});
        } else {
            util::recycle(bp);
            Toast("Backup moved to the Recycle Bin", ToastKind::Info);
            refreshBackups(s);
        }
    }
    if (restore >= 0) {
        BackupEntry b = ds_.backups[restore];
        if (s.isActive()) Toast("Stop the server before restoring a backup", ToastKind::Warning);
        else {
            std::shared_ptr<ServerInstance> sp = sharedOf(&s);
            askConfirm({"Restore this backup?",
                        "Your current world will be replaced with the backup from " + b.name + ". The current world is moved to the Recycle Bin first.",
                        "Restore", true, [this, sp, b] {
                            if (!sp) return;
                            Properties p;
                            p.load(sp->propertiesPath());
                            std::string level = p.get("level-name", "world");
                            for (const std::string& n : {level, level + "_nether", level + "_the_end"}) {
                                fs::path wp = sp->cfg.path() / util::fromUtf8(n);
                                std::error_code ec;
                                if (fs::exists(wp, ec)) util::recycle(wp);
                            }
                            fs::path dir = sp->cfg.path();
                            spawnBackground([sp, b, dir] {
                                wchar_t sys[MAX_PATH];
                                GetSystemDirectoryW(sys, MAX_PATH);
                                std::wstring cmd = L"\"" + (fs::path(sys) / L"tar.exe").wstring() + L"\" -xf \"" + b.path.wstring() + L"\" -C \"" +
                                                   dir.wstring() + L"\"";
                                int code = util::runCapture(cmd, {}, nullptr);
                                sp->appendLog(code == 0 ? "[" VX_APP_NAME "] Backup restored: " + b.name : "[" VX_APP_NAME "] Restore failed",
                                              code == 0 ? LogSystem : LogError);
                            });
                            Toast("Restoring backup...", ToastKind::Info);
                        }});
        }
    }
    Gap(16);
    EndScroll();
}

// ---------------------------------------------------------------------------
// per-server settings
// ---------------------------------------------------------------------------
void App::loadDraft(ServerInstance& s) {
    ds_.draft = s.cfg;
    ds_.props = Properties();
    ds_.props.load(s.propertiesPath());
    ds_.dirty = false;
    ds_.loaded = true;
}

void App::saveDraft(ServerInstance& s) {
    s.cfg.name = util::trim(ds_.draft.name).empty() ? s.cfg.name : util::trim(ds_.draft.name);
    s.cfg.minRamMB = std::min(ds_.draft.minRamMB, ds_.draft.maxRamMB);
    s.cfg.maxRamMB = ds_.draft.maxRamMB;
    s.cfg.javaPath = ds_.draft.javaPath;
    s.cfg.extraArgs = util::trim(ds_.draft.extraArgs);
    s.cfg.aikarFlags = ds_.draft.aikarFlags;
    s.cfg.autoRestart = ds_.draft.autoRestart;
    s.cfg.autoStart = ds_.draft.autoStart;
    s.maxPlayers = ds_.props.getInt("max-players", 20);
    if (!demo_) ds_.props.save(s.propertiesPath());
    saveAll();
    ds_.dirty = false;
    Toast(s.isActive() ? "Saved - restart the server to apply changes" : "Settings saved", ToastKind::Success);
}

void App::drawServerSettingsTab(ServerInstance& s) {
    if (!ds_.loaded) loadDraft(s);
    float barH = ds_.dirty ? HS(76) : 0.f;
    BeginScroll("##set", ImVec2(0, ImGui::GetContentRegionAvail().y - barH));
    float W = ImGui::GetContentRegionAvail().x - S(4);
    float cardW = std::min(W, S(960));
    Properties& pr = ds_.props;
    bool hasProps = fs::exists(s.propertiesPath());

    auto textProp = [&](const char* key, const char* label, const char* help, const char* def, const char* hint = nullptr) {
        settingRow(label, help, [&] {
            std::string v = pr.get(key, def);
            if (InputText((std::string("##") + key).c_str(), &v, hint)) { pr.set(key, v); ds_.dirty = true; }
        });
    };
    auto boolProp = [&](const char* key, const char* label, const char* help, bool def) {
        settingRow(label, help, [&] {
            bool v = pr.getBool(key, def);
            if (Toggle(key, &v)) { pr.setBool(key, v); ds_.dirty = true; }
        });
    };
    auto intProp = [&](const char* key, const char* label, const char* help, int def, int lo, int hi) {
        settingRow(label, help, [&] {
            int v = pr.getInt(key, def);
            if (SliderInt((std::string("##") + key).c_str(), &v, lo, hi)) { pr.setInt(key, v); ds_.dirty = true; }
        });
    };
    auto comboProp = [&](const char* key, const char* label, const char* help, std::vector<const char*> items, const char* def) {
        settingRow(label, help, [&] {
            std::string cur = pr.get(key, def);
            int idx = 0;
            for (int i = 0; i < (int)items.size(); ++i)
                if (cur == items[i]) idx = i;
            if (Combo((std::string("##") + key).c_str(), &idx, items.data(), (int)items.size())) { pr.set(key, items[idx]); ds_.dirty = true; }
        });
    };

    if (!hasProps) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(cardW, HS(56)));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + cardW, p.y + HS(56)), Fade(Alpha(RGBA(col::blue), 0.10f)), RD(14));
        dl->AddRect(p, ImVec2(p.x + cardW, p.y + HS(56)), Fade(Alpha(RGBA(col::blue), 0.35f)), RD(14), 0, 1.f);
        DrawIcon(dl, ImVec2(p.x + D(28), p.y + HS(28)), icon::Info, 18.f, RGBA(col::blue));
        DrawStr(dl, fRegular, 13.5f, ImVec2(p.x + D(52), p.y + HS(18)), RGBA(col::text),
                "server.properties does not exist yet. Saving creates it, or just start the server once.");
        Gap(14);
    }

    settingSection("General", "How your server appears to players.", [&] {
        settingRow("Server name", "Only shown inside " VX_APP_NAME ".", [&] {
            if (InputText("##name", &ds_.draft.name)) ds_.dirty = true;
        });
        textProp("motd", "Message of the day", "Shown under the server name in the multiplayer list.", "A Minecraft Server");
        settingRow("Port", "Players connect on this TCP port.", [&] {
            int v = pr.getInt("server-port", 25565);
            if (InputInt("##port", &v, 1, 65535, 140)) { pr.setInt("server-port", v); ds_.dirty = true; }
        });
        intProp("max-players", "Max players", "Number of player slots.", 20, 1, 200);
        comboProp("gamemode", "Default game mode", "Mode new players start in.", {"survival", "creative", "adventure", "spectator"}, "survival");
        comboProp("difficulty", "Difficulty", nullptr, {"peaceful", "easy", "normal", "hard"}, "easy");
    });

    settingSection("World & rules", "World name and seed changes apply to new worlds.", [&] {
        textProp("level-name", "World folder", "Name of the world folder.", "world");
        textProp("level-seed", "World seed", "Leave empty for a random seed.", "", "Random");
        intProp("view-distance", "View distance", "Chunks sent to players. Lower = faster.", 10, 2, 32);
        intProp("simulation-distance", "Simulation distance", "Chunks that are actively ticked.", 10, 3, 32);
        intProp("spawn-protection", "Spawn protection", "Radius where only operators can build.", 16, 0, 64);
        boolProp("pvp", "PvP", "Let players damage each other.", true);
        boolProp("hardcore", "Hardcore", "Players are banned after dying.", false);
        boolProp("allow-nether", "Nether", "Enable travel to the Nether.", true);
        boolProp("spawn-monsters", "Spawn monsters", nullptr, true);
        boolProp("spawn-animals", "Spawn animals", nullptr, true);
        boolProp("enable-command-block", "Command blocks", "Allow command blocks to run.", false);
        boolProp("allow-flight", "Allow flight", "Stops anti-fly kicks (needed for some mods).", false);
    });

    settingSection("Access", "Who is allowed to join.", [&] {
        boolProp("online-mode", "Online mode", "Verify players with Mojang. Turn off only for offline/LAN play.", true);
        boolProp("white-list", "Whitelist", "Only whitelisted players can join.", false);
        boolProp("enforce-whitelist", "Enforce whitelist", "Kick players that are removed from the whitelist.", false);
    });

    settingSection("Performance & Java", "Memory, Java version and launch behaviour.", [&] {
        uint64_t totalMB = util::totalRamMB();
        int maxCap = (int)std::max<uint64_t>(2048, totalMB * 85 / 100);
        settingRow("Maximum memory", "Most RAM the server may use (-Xmx).", [&] {
            if (SliderInt("##maxram", &ds_.draft.maxRamMB, 512, maxCap, "%d MB")) {
                ds_.draft.maxRamMB = (ds_.draft.maxRamMB / 256) * 256;
                ds_.draft.minRamMB = std::min(ds_.draft.minRamMB, ds_.draft.maxRamMB);
                ds_.dirty = true;
            }
        });
        settingRow("Starting memory", "Memory reserved at launch (-Xms).", [&] {
            if (SliderInt("##minram", &ds_.draft.minRamMB, 256, std::max(256, ds_.draft.maxRamMB), "%d MB")) {
                ds_.draft.minRamMB = (ds_.draft.minRamMB / 256) * 256;
                ds_.dirty = true;
            }
        });
        settingRow("Java", "\"Automatic\" picks the right version for this Minecraft release.", [&] {
            auto js = java::all();
            std::vector<std::string> labels = {"Automatic (recommended)"};
            for (auto& j : js) labels.push_back("Java " + std::to_string(j.major) + "  (" + j.version + ")");
            int idx = 0;
            for (int i = 0; i < (int)js.size(); ++i)
                if (!ds_.draft.javaPath.empty() && util::pathStr(js[i].exe) == ds_.draft.javaPath) idx = i + 1;
            std::vector<const char*> cs;
            for (auto& l : labels) cs.push_back(l.c_str());
            if (Combo("##java", &idx, cs.data(), (int)cs.size())) {
                ds_.draft.javaPath = idx == 0 ? "" : util::pathStr(js[idx - 1].exe);
                ds_.dirty = true;
            }
        });
        settingRow("Optimised JVM flags", "Aikar's flags: smoother garbage collection for Paper-style servers.", [&] {
            if (Toggle("aikar", &ds_.draft.aikarFlags)) ds_.dirty = true;
        });
        settingRow("Extra JVM arguments", "Advanced. Added before -jar.", [&] {
            if (InputText("##extra", &ds_.draft.extraArgs, "e.g. -Dsome.flag=true")) ds_.dirty = true;
        });
        settingRow("Restart after a crash", "Automatically restarts (up to 3 times in a row).", [&] {
            if (Toggle("autorestart", &ds_.draft.autoRestart)) ds_.dirty = true;
        });
        settingRow("Start with " VX_APP_NAME, "Launch this server when the app opens.", [&] {
            if (Toggle("autostart", &ds_.draft.autoStart)) ds_.dirty = true;
        });
    });

    BeginCard("##g5", ImVec2(cardW, 0), 22);
    SectionTitle("Danger zone", "Removing a server never touches your files unless you ask.", icon::Warning);
    Gap(12);
    ImGui::BeginGroup();
    Label("Remove this server", 14.5f, col::text, true);
    Label("Removes it from " VX_APP_NAME ". You can choose to delete its files too.", 12.5f, col::mute);
    ImGui::EndGroup();
    ImGui::SameLine();
    float bw = ButtonWidth("Remove server", true);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - bw));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(6));
    if (Button("Remove server", icon::Trash, Btn::Danger, ImVec2(0, HS(40)))) {
        if (s.backupRunning) Toast("A backup is running - try again in a moment", ToastKind::Warning);
        else {
            deleteTarget_ = &s;
            deleteFiles_ = false;
            deleteOpen_ = true;
        }
    }
    EndCard();
    Gap(24);
    EndScroll();

    if (ds_.dirty) drawSaveBar(s, cardW);
}
