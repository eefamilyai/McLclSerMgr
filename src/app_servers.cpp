// Servers page: the dashboard listing every managed server.
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "app.h"
#include "imgui_internal.h"

using namespace ui;
namespace P = providers;

namespace {

unsigned parseHex(const std::string& hex, unsigned fallback) {
    if (hex.size() != 6) return fallback;
    char* end = nullptr;
    unsigned v = (unsigned)strtoul(hex.c_str(), &end, 16);
    return (end && *end == 0) ? v : fallback;
}

int statusRank(State s) {
    switch (s) {
    case State::Running: return 0;
    case State::Starting: return 1;
    case State::Stopping: return 2;
    case State::Crashed: return 3;
    default: return 4;
    }
}

}  // namespace

unsigned serverColor(const ServerInstance& s) {
    return parseHex(s.cfg.color, P::info(s.cfg.sw()).color);
}

std::vector<ServerInstance*> App::ordered() {
    std::vector<ServerInstance*> out;
    std::string needle = util::lower(util::trim(serverFilter_));
    for (auto& s : servers_) {
        if (!needle.empty()) {
            std::string hay = util::lower(s->cfg.name + " " + s->cfg.software + " " + s->cfg.mcVersion + " " + s->cfg.note);
            if (!util::contains(hay, needle)) continue;
        }
        out.push_back(s.get());
    }
    std::stable_sort(out.begin(), out.end(), [&](ServerInstance* a, ServerInstance* b) {
        if (a->cfg.pinned != b->cfg.pinned) return a->cfg.pinned;
        switch (settings_.sortMode) {
        case 1: return statusRank(a->state) < statusRank(b->state);
        case 2: return a->playerCount() > b->playerCount();
        case 3: return a->cfg.lastStarted > b->cfg.lastStarted;
        default: return util::lower(a->cfg.name) < util::lower(b->cfg.name);
        }
    });
    return out;
}

void App::statTile(float w, const char* ic, const char* label, const std::string& value, ImU32 color) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = HS(88);
    ImGui::Dummy(ImVec2(w, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 q(p.x + w, p.y + h);
    dl->AddRectFilled(p, q, Fade(RGBA(col::panel)), RD(16));
    dl->AddRect(p, q, Fade(RGBA(col::border)), RD(16), 0, 1.f);
    dl->AddRectFilled(p, ImVec2(q.x, p.y + S(3)), Fade(Alpha(color, 0.55f)), RD(16), ImDrawFlags_RoundCornersTop);
    float t = HS(44);
    dl->AddRectFilled(ImVec2(p.x + D(18), p.y + (h - t) * 0.5f), ImVec2(p.x + D(18) + t, p.y + (h + t) * 0.5f), Fade(Alpha(color, 0.14f)), RD(13));
    DrawIcon(dl, ImVec2(p.x + D(18) + t * 0.5f, p.y + h * 0.5f), ic, 19.f, color);
    DrawStr(dl, fRegular, 13.f, ImVec2(p.x + D(76), p.y + S(18)), RGBA(col::dim), label);
    DrawStr(dl, fBold, 23.f, ImVec2(p.x + D(76), p.y + S(38)), RGBA(col::text), value.c_str());
}

void App::drawServersPage() {
    float W = ImGui::GetContentRegionAvail().x;
    int running = activeCount(), players = 0;
    uint64_t mem = 0, reserved = 0;
    for (auto& s : servers_) {
        players += s->playerCount();
        if (s->isActive()) {
            mem += s->memBytes;
            reserved += (uint64_t)s->cfg.maxRamMB * 1024 * 1024;
        }
    }

    char sub[128];
    snprintf(sub, sizeof sub, "%d server%s  \xC2\xB7  %d running  \xC2\xB7  %d player%s online", (int)servers_.size(), servers_.size() == 1 ? "" : "s",
             running, players, players == 1 ? "" : "s");

    drawTopBar("Servers", sub, [&] {
        if (!servers_.empty()) {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(1));
            SearchBox("##srvfilter", &serverFilter_, "Search servers...", S(210));
            ImGui::SameLine(0, S(10));
        }
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(1));
        const char* views[2] = {"Grid", "List"};
        int view = settings_.serverView;
        if (Segmented("##view", views, 2, &view, S(150))) {
            settings_.serverView = view;
            saveSettings();
        }
        ImGui::SameLine(0, S(8));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(1));
        if (IconButton("##sort", icon::Sort, Btn::Secondary, 38, "Sort order")) ImGui::OpenPopup("##sortmenu");
        if (BeginCardMenu("##sortmenu")) {
            const char* names[4] = {"Name (A-Z)", "Status", "Players online", "Recently started"};
            for (int i = 0; i < 4; ++i) {
                bool sel = settings_.sortMode == i;
                if (ImGui::Selectable(names[i], sel, 0, ImVec2(S(170), HS(28)))) {
                    settings_.sortMode = i;
                    saveSettings();
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::Separator();
            if (ImGui::Selectable("Show stat tiles", settings_.showStatTiles, 0, ImVec2(S(170), HS(28)))) {
                settings_.showStatTiles = !settings_.showStatTiles;
                saveSettings();
            }
            if (ImGui::Selectable("Card actions", settings_.showCardActions, 0, ImVec2(S(170), HS(28)))) {
                settings_.showCardActions = !settings_.showCardActions;
                saveSettings();
            }
            ImGui::Separator();
            if (ImGui::Selectable("Personalize...", false, 0, ImVec2(S(170), HS(28)))) {
                persTab_ = 2;
                navigate(Page::Personalize);
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine(0, S(10));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(1));
        if (Button("Import", icon::FolderOpen, Btn::Secondary, ImVec2(0, HS(38)))) navigate(Page::Import);
        ImGui::SameLine(0, S(8));
        if (Button("New server", icon::Add, Btn::Primary, ImVec2(0, HS(38)))) {
            resetWizard();
            navigate(Page::Wizard);
        }
    });

    if (servers_.empty()) {
        drawEmptyState();
        return;
    }

    if (settings_.showStatTiles) {
        float gap = S(14), tw = (W - gap * 3) / 4;
        statTile(tw, icon::Package, "Servers", std::to_string(servers_.size()), RGBA(col::blue));
        ImGui::SameLine(0, gap);
        statTile(tw, icon::Power, "Running", std::to_string(running), RGBA(col::green));
        ImGui::SameLine(0, gap);
        statTile(tw, icon::People, "Players online", std::to_string(players), RGBA(col::violet));
        ImGui::SameLine(0, gap);
        char mbuf[64];
        if (reserved > 0) snprintf(mbuf, sizeof mbuf, "%s / %s", util::formatBytes(mem).c_str(), util::formatBytes(reserved).c_str());
        else snprintf(mbuf, sizeof mbuf, "0 B");
        statTile(tw, icon::Cpu, "Memory in use", mbuf, RGBA(col::amber));
        Gap(22);
    }

    auto list = ordered();
    if (list.empty()) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float h = S(120);
        ImGui::Dummy(ImVec2(W, h));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + W, p.y + h), Fade(RGBA(col::panel)), RD(16));
        DrawIcon(dl, ImVec2(p.x + D(30), p.y + h * 0.5f), icon::Search, 24.f, RGBA(col::mute));
        DrawStr(dl, fBold, 16.f, ImVec2(p.x + D(58), p.y + h * 0.5f - S(18)), RGBA(col::text), "No servers match that search");
        DrawStr(dl, fRegular, 13.f, ImVec2(p.x + D(58), p.y + h * 0.5f + S(4)), RGBA(col::dim), "Clear the search box to see them all again.");
        return;
    }

    if (settings_.serverView == 1) {
        float gap = S(10);
        ImVec2 origin = ImGui::GetCursorScreenPos();
        float rowH = HS(70);
        int i = 0;
        for (auto* s : list) {
            ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + i * (rowH + gap)));
            ImGui::PushID(s->cfg.id.c_str());
            drawServerRow(*s);
            ImGui::PopID();
            ++i;
        }
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + list.size() * (rowH + gap)));
        ImGui::Dummy(ImVec2(1, S(10)));
        return;
    }

    float gap = S(14);
    float scale = settings_.cardSize == 0 ? 0.86f : (settings_.cardSize == 2 ? 1.14f : 1.f);
    float ch = HS(196) * scale;
    float target = S(400) * (settings_.cardSize == 2 ? 1.08f : 1.f);
    int cols = std::max(1, (int)((W + gap) / (target + gap)));
    float cw = (W - gap * (cols - 1)) / cols;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    int i = 0;
    for (auto* s : list) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + (i % cols) * (cw + gap), origin.y + (i / cols) * (ch + gap)));
        ImGui::PushID(s->cfg.id.c_str());
        drawServerCard(*s, ImVec2(cw, ch));
        ImGui::PopID();
        ++i;
    }
    int rows = (i + cols - 1) / cols;
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rows * (ch + gap)));
    ImGui::Dummy(ImVec2(1, S(10)));
}

void App::drawServerCard(ServerInstance& s, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos(), q(p.x + size.x, p.y + size.y);
    ImGui::SetNextItemAllowOverlap();
    bool clicked = ImGui::InvisibleButton("##card", size);
    bool hov = ImGui::IsItemHovered() && !ImGui::IsAnyItemActive();
    float hv = Anim(ImGui::GetItemID() ^ 0xcafe, hov ? 1.f : 0.f, 14.f);
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    State st = s.state;
    unsigned brand = serverColor(s);
    ImU32 sc = stateColor(st);
    bool live = st == State::Running;
    bool compact = size.y < HS(170);
    float pad = D(18);

    if (theme::g_opt.shadows) DrawShadow(dl, ImVec2(p.x, p.y + S(4)), q, RD(16), 0.35f + hv * 0.3f);
    dl->AddRectFilled(p, q, Fade(Mix(RGBA(col::panel), RGBA(col::panelHi), hv * 0.8f)), RD(16));
    if (live)
        dl->AddRectFilledMultiColor(p, ImVec2(q.x, p.y + S(76)), Fade(Alpha(sc, 0.07f)), Fade(Alpha(sc, 0.07f)), Fade(Alpha(sc, 0.f)), Fade(Alpha(sc, 0.f)));
    dl->AddRect(p, q, Fade(Mix(live ? Alpha(sc, 0.32f) : RGBA(col::border), Accent(0.6f), hv)), RD(16), 0, 1.f);

    float tile = HS(46);
    DrawTile(dl, ImVec2(p.x + pad, p.y + pad), tile, brand);
    float nameX = p.x + pad + tile + S(13);

    // status badge (top right)
    const char* stText = stateTip(st);
    float bw = BadgeWidth(stText, true);
    ImGui::SetCursorScreenPos(ImVec2(q.x - bw - pad, p.y + pad - S(1)));
    Badge(stText, sc, true, (st == State::Starting || st == State::Stopping) ? 1.f : 0.f);

    // pin marker
    float badgeLeft = q.x - bw - pad;
    if (s.cfg.pinned) {
        ImGui::SetCursorScreenPos(ImVec2(badgeLeft - S(26), p.y + pad + S(1)));
        ImGui::PushStyleColor(ImGuiCol_Text, RGBA(col::amber));
        ImGui::PushFont(fIcon, FS(14));
        ImGui::TextUnformatted(icon::StarFill);
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }

    float availName = badgeLeft - S(30) - nameX;
    std::string nm = FitText(s.cfg.name, fBold, 17.f, std::max(S(60), availName));
    DrawStr(dl, fBold, 17.f, ImVec2(nameX, p.y + pad + S(1)), RGBA(col::text), nm.c_str());
    const auto& info = P::info(s.cfg.sw());
    std::string sline = std::string(info.name) + (s.cfg.mcVersion.empty() ? "" : "  \xC2\xB7  " + s.cfg.mcVersion);
    if (!s.cfg.build.empty() && !compact) sline += "  \xC2\xB7  " + s.cfg.build;
    std::string sfit = FitText(sline, fRegular, 13.f, std::max(S(60), availName));
    DrawStr(dl, fRegular, 13.f, ImVec2(nameX, p.y + pad + S(24)), RGBA(col::dim), sfit.c_str());

    // stats row
    float y = p.y + pad + tile + S(22);
    char buf[64];
    auto stat = [&](float x, const char* ic, const std::string& text, ImU32 c) {
        DrawIcon(dl, ImVec2(x + S(7), y + S(9)), ic, 14.f, c);
        DrawStr(dl, fRegular, 13.f, ImVec2(x + S(22), y + S(1)), RGBA(col::dim), text.c_str());
    };
    snprintf(buf, sizeof buf, "%d / %d", s.playerCount(), s.maxPlayers);
    bool anyPlayers = s.playerCount() > 0;
    stat(p.x + pad, icon::People, buf, anyPlayers ? RGBA(col::violet) : RGBA(col::mute));
    float x2 = p.x + pad + S(104);
    snprintf(buf, sizeof buf, ":%d", s.configuredPort());
    stat(x2, icon::Wifi, buf, RGBA(col::mute));
    if (!compact) {
        float x3 = p.x + pad + S(184);
        if (s.isActive()) stat(x3, icon::Cpu, util::formatBytes(s.memBytes) + "  \xC2\xB7  " + util::formatDuration(s.uptimeSeconds()), RGBA(col::green));
        else {
            snprintf(buf, sizeof buf, "%.1f GB", s.cfg.maxRamMB / 1024.0);
            stat(x3, icon::Cpu, buf, RGBA(col::mute));
        }
    }

    // note
    float noteY = q.y - HS(56);
    if (!s.cfg.note.empty() && !compact) {
        std::string note = FitText(s.cfg.note, fRegular, 12.f, size.x - pad * 2);
        DrawStr(dl, fRegular, 12.f, ImVec2(p.x + pad, noteY - S(20)), RGBA(col::mute), note.c_str());
    }
    dl->AddLine(ImVec2(p.x + pad, noteY), ImVec2(q.x - pad, noteY), Fade(RGBA(col::border)), 1.f);

    // actions
    float bh = HS(38);
    ImGui::SetCursorScreenPos(ImVec2(p.x + pad, noteY + S(9)));
    if (settings_.showCardActions) {
        if (st == State::Stopped || st == State::Crashed) {
            if (Button("Start", icon::Play, Btn::Primary, ImVec2(S(102), bh))) startServer(s);
        } else if (st == State::Starting || st == State::Running) {
            if (Button("Stop", icon::Stop, Btn::Danger, ImVec2(S(102), bh))) s.stop();
        } else {
            if (Button("Kill", icon::Close, Btn::Danger, ImVec2(S(102), bh))) s.kill();
        }
        if (live || st == State::Starting) {
            ImGui::SameLine(0, S(7));
            if (IconButton("restart", icon::Restart, Btn::Secondary, 38, "Restart")) s.restart();
        }
        ImGui::SameLine(0, S(7));
        if (IconButton("folder", icon::FolderOpen, Btn::Secondary, 38, "Open server folder")) util::openPath(s.cfg.path());
    }
    float mw = ButtonWidth("Manage", true);
    ImGui::SetCursorScreenPos(ImVec2(q.x - pad - mw, noteY + S(9)));
    if (Button("Manage", icon::Sliders, Btn::Secondary, ImVec2(0, bh))) openServer(&s);
    else if (clicked) openServer(&s);

    // context menu
    ImGui::SetCursorScreenPos(p);
    ImGui::InvisibleButton("##ctxarea", size, ImGuiButtonFlags_MouseButtonRight);
    if (ImGui::BeginPopupContextItem("##ctx")) {
        Label(s.cfg.name.c_str(), 14.f, col::text, true);
        (Label((std::string(info.name) + " " + s.cfg.mcVersion).c_str(), 12.f, col::mute));
        ImGui::Separator();
        if (ImGui::MenuItem("Open")) openServer(&s);
        if (s.isActive()) {
            if (ImGui::MenuItem("Stop")) s.stop();
            if (ImGui::MenuItem("Restart")) s.restart();
        } else if (ImGui::MenuItem("Start")) {
            startServer(s);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Open server folder")) util::openPath(s.cfg.path());
        if (ImGui::MenuItem("Copy address", nullptr, false, true)) {
            std::string addr = util::localIPv4() + ":" + std::to_string(s.configuredPort());
            util::setClipboard(addr);
            Toast("Copied " + addr, ToastKind::Success);
        }
        if (ImGui::MenuItem(s.cfg.pinned ? "Unpin" : "Pin to top")) {
            s.cfg.pinned = !s.cfg.pinned;
            saveAll();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Remove...")) {
            deleteTarget_ = &s;
            deleteFiles_ = false;
            if (settings_.confirmDestructive) deleteOpen_ = true;
            else removeServer(&s, false);
        }
        ImGui::EndPopup();
    }
}

void App::drawServerRow(ServerInstance& s) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float w = ImGui::GetContentRegionAvail().x, h = HS(70);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::SetNextItemAllowOverlap();
    bool clicked = ImGui::InvisibleButton("##row", ImVec2(w, h));
    bool hov = ImGui::IsItemHovered() && !ImGui::IsAnyItemActive();
    float hv = Anim(ImGui::GetItemID() ^ 0x77aa, hov ? 1.f : 0.f, 16.f);
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImVec2 q(p.x + w, p.y + h);

    State st = s.state;
    ImU32 sc = stateColor(st);
    unsigned brand = serverColor(s);
    dl->AddRectFilled(p, q, Fade(Mix(RGBA(col::panel), RGBA(col::panelHi), hv * 0.7f)), RD(14));
    dl->AddRect(p, q, Fade(Mix(RGBA(col::border), Accent(0.5f), hv)), RD(14), 0, 1.f);
    if (st == State::Running) dl->AddRectFilled(ImVec2(p.x, p.y + S(12)), ImVec2(p.x + S(3), q.y - S(12)), Fade(sc), S(2));

    float pad = D(16);
    float tile = HS(40);
    DrawTile(dl, ImVec2(p.x + pad, p.y + (h - tile) * 0.5f), tile, brand);
    float x = p.x + pad + tile + S(14);
    std::string nm = FitText(s.cfg.name, fBold, 15.5f, w * 0.34f);
    DrawStr(dl, fBold, 15.5f, ImVec2(x, p.y + h * 0.5f - S(18)), RGBA(col::text), nm.c_str());
    const auto& info = P::info(s.cfg.sw());
    std::string sline = std::string(info.name) + (s.cfg.mcVersion.empty() ? "" : " \xC2\xB7 " + s.cfg.mcVersion);
    DrawStr(dl, fRegular, 12.5f, ImVec2(x, p.y + h * 0.5f + S(3)), RGBA(col::mute), FitText(sline, fRegular, 12.5f, w * 0.34f).c_str());

    float cx = p.x + w * 0.44f;
    char buf[64];
    auto chip = [&](const char* label, const std::string& val, ImU32 c) {
        DrawStr(dl, fRegular, 11.5f, ImVec2(cx, p.y + h * 0.5f - S(14)), RGBA(col::mute), label);
        DrawStr(dl, fBold, 13.f, ImVec2(cx, p.y + h * 0.5f + S(1)), c, val.c_str());
        cx += S(96);
    };
    snprintf(buf, sizeof buf, "%d / %d", s.playerCount(), s.maxPlayers);
    chip("PLAYERS", buf, s.playerCount() ? RGBA(col::violet) : RGBA(col::dim));
    snprintf(buf, sizeof buf, ":%d", s.configuredPort());
    chip("PORT", buf, RGBA(col::dim));
    if (s.isActive()) chip("MEMORY", util::formatBytes(s.memBytes), RGBA(col::green));
    else chip("ALLOCATED", util::formatBytes((uint64_t)s.cfg.maxRamMB * 1024 * 1024), RGBA(col::dim));
    if (w > S(1080)) chip("UPTIME", s.isActive() ? util::formatDuration(s.uptimeSeconds()) : util::formatAgo(s.cfg.lastStarted), RGBA(col::dim));

    float bw = BadgeWidth(stateTip(st), true);
    ImGui::SetCursorScreenPos(ImVec2(q.x - pad - bw - (settings_.showCardActions ? S(160) : 0), p.y + (h - HS(26)) * 0.5f));
    Badge(stateTip(st), sc, true, (st == State::Starting || st == State::Stopping) ? 1.f : 0.f);

    if (settings_.showCardActions) {
        ImGui::SetCursorScreenPos(ImVec2(q.x - pad - S(140), p.y + (h - HS(36)) * 0.5f));
        if (st == State::Stopped || st == State::Crashed) {
            if (Button("Start", icon::Play, Btn::Primary, ImVec2(S(96), HS(36)))) startServer(s);
        } else if (st == State::Running || st == State::Starting) {
            if (Button("Stop", icon::Stop, Btn::Danger, ImVec2(S(96), HS(36)))) s.stop();
        } else if (Button("Kill", icon::Close, Btn::Danger, ImVec2(S(96), HS(36)))) {
            s.kill();
        }
        ImGui::SetCursorScreenPos(ImVec2(q.x - pad - S(38), p.y + (h - HS(36)) * 0.5f));
        if (IconButton("rowmanage", icon::Sliders, Btn::Secondary, 36, "Manage")) openServer(&s);
    }
    if (clicked) openServer(&s);

    ImGui::SetCursorScreenPos(p);
    ImGui::InvisibleButton("##rowctx", ImVec2(w, h), ImGuiButtonFlags_MouseButtonRight);
    if (ImGui::BeginPopupContextItem("##rowmenu")) {
        if (ImGui::MenuItem("Open")) openServer(&s);
        if (s.isActive()) {
            if (ImGui::MenuItem("Stop")) s.stop();
            if (ImGui::MenuItem("Restart")) s.restart();
        } else if (ImGui::MenuItem("Start")) {
            startServer(s);
        }
        if (ImGui::MenuItem("Open server folder")) util::openPath(s.cfg.path());
        if (ImGui::MenuItem(s.cfg.pinned ? "Unpin" : "Pin to top")) {
            s.cfg.pinned = !s.cfg.pinned;
            saveAll();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Remove...")) {
            deleteTarget_ = &s;
            deleteFiles_ = false;
            if (settings_.confirmDestructive) deleteOpen_ = true;
            else removeServer(&s, false);
        }
        ImGui::EndPopup();
    }
}

void App::drawEmptyState() {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float W = ImGui::GetContentRegionAvail().x, H = S(340);
    ImGui::Dummy(ImVec2(W, H));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 q(p.x + W, p.y + H);
    dl->AddRectFilled(p, q, Fade(RGBA(col::panel)), RD(20));
    dl->AddRect(p, q, Fade(RGBA(col::border)), RD(20), 0, 1.f);
    ImVec2 c(p.x + W * 0.5f, p.y + S(96));
    for (int i = 0; i < 4; ++i) dl->AddCircleFilled(c, S(84.f - i * 17.f), Fade(Accent(0.03f + i * 0.014f)), 48);
    DrawCube(dl, c, S(42), Accent());
    const char* t1 = "No servers yet";
    const char* t2 = "Create your first server in under a minute - pick software, pick a version, done.";
    ImVec2 s1 = TextSize(fBold, 23.f, t1), s2 = TextSize(fRegular, 14.f, t2);
    DrawStr(dl, fBold, 23.f, ImVec2(c.x - s1.x * 0.5f, p.y + S(172)), RGBA(col::text), t1);
    DrawStr(dl, fRegular, 14.f, ImVec2(c.x - s2.x * 0.5f, p.y + S(206)), RGBA(col::dim), t2);
    float bw = ButtonWidth("Create a server", true), bw2 = ButtonWidth("Import existing", true);
    ImGui::SetCursorScreenPos(ImVec2(c.x - (bw + bw2 + S(12)) * 0.5f, p.y + S(250)));
    if (Button("Create a server", icon::Add, Btn::Primary, ImVec2(0, HS(44)))) {
        resetWizard();
        navigate(Page::Wizard);
    }
    ImGui::SameLine(0, S(12));
    if (Button("Import existing", icon::FolderOpen, Btn::Secondary, ImVec2(0, HS(44)))) navigate(Page::Import);
    ImGui::SetCursorScreenPos(ImVec2(p.x, q.y + S(16)));
    ImGui::Dummy(ImVec2(1, 1));
}
