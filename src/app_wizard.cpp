// New-server wizard and "import an existing server" page.
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <regex>
#include <thread>

#include "app.h"
#include "imgui_internal.h"
#include "java.h"
#include "motd.h"

using namespace ui;
namespace P = providers;

void App::resetWizard() {
    if (job_) job_->cancel();
    job_.reset();
    jobHandled_ = false;
    createdId_.clear();
    wStep_ = 0;
    wFilter_.clear();
    wVersion_.clear();
    wShowUnstable_ = false;
    wEula_ = false;
    wRam_ = settings_.defaultRamMB;
    wMotd_.clear();
    wName_.clear();
    wPort_ = freePort();
}

std::shared_ptr<VersionFetch> App::versionsFor(P::Software sw) {
    auto it = wFetch_.find((int)sw);
    if (it != wFetch_.end() && !(it->second->done && !it->second->ok)) return it->second;
    auto vf = std::make_shared<VersionFetch>();
    wFetch_[(int)sw] = vf;
    // Tracked, not detached: it reaches the provider caches, which must not outlive the process.
    spawnBackground([vf, sw] {
        std::vector<P::Version> list;
        std::string err;
        bool ok = P::listVersions(sw, list, err);
        std::lock_guard<std::mutex> lk(vf->mu);
        vf->ok = ok;
        vf->err = err;
        vf->list = std::move(list);
        vf->done = true;
    });
    return vf;
}

std::string App::uniqueName(const std::string& base) {
    auto exists = [&](const std::string& n) {
        for (auto& s : servers_)
            if (s->cfg.name == n) return true;
        return false;
    };
    if (!exists(base)) return base;
    for (int i = 2;; ++i) {
        std::string n = base + " " + std::to_string(i);
        if (!exists(n)) return n;
    }
}

fs::path App::uniqueServerDir(const std::string& name) {
    fs::path root = settings_.root();
    fs::path base = root / util::fromUtf8(util::sanitizeFileName(name));
    std::error_code ec;
    fs::path p = base;
    for (int i = 2; fs::exists(p, ec); ++i) p = fs::path(base.wstring() + L"-" + std::to_wstring(i));
    return p;
}

int App::freePort() {
    for (int port = 25565; port < 25665; ++port) {
        bool taken = false;
        for (auto& s : servers_) taken = taken || s->configuredPort() == port;
        if (!taken && !util::isPortInUse(port)) return port;
    }
    return 25565;
}

void App::pollJob() {
    if (!job_ || !job_->finished() || jobHandled_) return;
    jobHandled_ = true;
    if (job_->failed()) return;
    auto s = std::make_shared<ServerInstance>(job_->result);
    createdId_ = s->cfg.id;
    lastState_[s->cfg.id] = s->state;
    servers_.push_back(std::move(s));
    saveAll();
    buildCommands();
    Toast("Server created", ToastKind::Success);
}

// ---------------------------------------------------------------------------
// stepper
// ---------------------------------------------------------------------------
void App::drawWizardStepper() {
    const char* names[3] = {"Software", "Configure", "Install"};
    const char* blurbs[3] = {"Vanilla, Paper, Purpur, Fabric, Forge...", "Version, name, memory, port", "Download and set up"};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float nodeR = HS(16), stepW = S(230);
    ImGui::Dummy(ImVec2(stepW * 3, nodeR * 2 + S(4)));
    for (int i = 0; i < 3; ++i) {
        ImVec2 c(p.x + nodeR + i * stepW, p.y + nodeR);
        bool done = i < wStep_, cur = i == wStep_;
        if (i < 2) {
            float x0 = c.x + nodeR + S(112), x1 = c.x + stepW - nodeR - S(10);
            dl->AddLine(ImVec2(x0, c.y), ImVec2(x1, c.y), Fade(done ? Accent(0.7f) : RGBA(col::border)), 2.f);
        }
        if (done) {
            DrawCheckCircle(dl, c, nodeR, Accent(), OnAccent());
        } else {
            dl->AddCircleFilled(c, nodeR, Fade(cur ? Accent(0.16f) : RGBA(col::panel)), 32);
            dl->AddCircle(c, nodeR, Fade(cur ? Accent() : RGBA(col::borderHi)), 32, cur ? 2.f : 1.f);
            char n[2] = {(char)('1' + i), 0};
            ImVec2 ts = TextSize(fBold, 13.f, n);
            DrawStr(dl, fBold, 13.f, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), cur ? Accent() : RGBA(col::mute), n);
        }
        DrawStr(dl, fBold, 14.f, ImVec2(c.x + nodeR + S(12), c.y - S(17)), (cur || done) ? RGBA(col::text) : RGBA(col::mute), names[i]);
        DrawStr(dl, fRegular, 11.5f, ImVec2(c.x + nodeR + S(12), c.y + S(1)), RGBA(col::mute), blurbs[i]);
    }
}

void App::drawWizard() {
    ImVec2 rowStart = ImGui::GetCursorScreenPos();
    float W = ImGui::GetContentRegionAvail().x;
    const char* subtitles[3] = {"Pick the server software you want to run.", "Choose the version and how the server should start.",
                                "Downloading official files and setting everything up."};
    drawTopBar("New server", subtitles[std::clamp(wStep_, 0, 2)], [&] {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(4));
        if (wStep_ > 0 && wStep_ < 2) {
            if (Button("Back", icon::Back, Btn::Secondary, ImVec2(0, HS(40)))) wStep_ = 0;
            ImGui::SameLine(0, S(8));
        }
        if (wStep_ == 0) {
            if (Button("Continue", icon::ChevronRight, Btn::Primary, ImVec2(0, HS(40)))) enterConfigStep();
        }
    }, page_ == Page::Wizard ? "Back to servers" : nullptr);
    (void)rowStart;
    (void)W;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##wizardstep", ImVec2(0, HS(60)), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    drawWizardStepper();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    Gap(18);

    if (wStep_ == 0) wizardSoftwareStep();
    else if (wStep_ == 1) wizardConfigStep();
    else wizardInstallStep();
    Gap(10);
}

// ---------------------------------------------------------------------------
// step 1: software
// ---------------------------------------------------------------------------
void App::wizardSoftwareStep() {
    float W = ImGui::GetContentRegionAvail().x, gap = S(14);
    int cols = W > S(1000) ? 3 : (W > S(660) ? 2 : 1);
    float cw = (W - gap * (cols - 1)) / cols, ch = HS(184);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < P::kSoftwareCount; ++i) {
        const auto& inf = P::infoByIndex(i);
        ImVec2 p(origin.x + (i % cols) * (cw + gap), origin.y + (i / cols) * (ch + gap)), q(p.x + cw, p.y + ch);
        ImGui::SetCursorScreenPos(p);
        ImGui::PushID(i);
        bool clicked = ImGui::InvisibleButton("##sw", ImVec2(cw, ch));
        bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        float hv = Anim(ImGui::GetID("sw") ^ (ImGuiID)(i * 7919 + 13), hov ? 1.f : 0.f, 14.f);
        bool sel = wSoftware_ == i;
        float selT = Anim(ImGui::GetID("selsw") ^ (ImGuiID)(i * 104729 + 7), sel ? 1.f : 0.f, 16.f);
        if (theme::g_opt.shadows) DrawShadow(dl, ImVec2(p.x, p.y + S(3)), q, RD(16), 0.3f + hv * 0.3f);
        dl->AddRectFilled(p, q, Fade(Mix(RGBA(col::panel), RGBA(col::panelHi), hv)), RD(16));
        if (selT > 0.01f)
            dl->AddRectFilledMultiColor(p, ImVec2(q.x, p.y + S(92)), Fade(Alpha(RGBA(inf.color), 0.10f * selT)), Fade(Alpha(RGBA(inf.color), 0.10f * selT)),
                                        Fade(0), Fade(0));
        dl->AddRect(p, q, Fade(Mix(Mix(RGBA(col::border), RGBA(col::borderHi), hv), RGBA(inf.color), selT)), RD(16), 0, 1.f + selT * 0.8f);
        float tile = HS(48);
        DrawTile(dl, ImVec2(p.x + D(18), p.y + D(18)), tile, inf.color);
        DrawStr(dl, fBold, 17.5f, ImVec2(p.x + D(18) + tile + S(12), p.y + D(18) + S(2)), RGBA(col::text), inf.name);
        DrawStr(dl, fRegular, 12.5f, ImVec2(p.x + D(18) + tile + S(12), p.y + D(18) + S(25)), RGBA(col::dim), inf.tagline);
        dl->AddText(fRegular, FS(13.f), ImVec2(std::floor(p.x + D(18)), std::floor(p.y + D(18) + tile + S(12))), Fade(RGBA(col::dim)), inf.description, nullptr,
                    cw - D(36));
        ImGui::SetCursorScreenPos(ImVec2(p.x + D(18), q.y - HS(38)));
        Chip(inf.chip1, RGBA(inf.color));
        if (inf.chip2[0]) {
            ImGui::SetCursorScreenPos(ImVec2(p.x + D(18) + TextSize(fBold, 11.5f, inf.chip1).x + D(26), q.y - HS(38)));
            Chip(inf.chip2, RGBA(col::mute));
        }
        if (selT > 0.01f) DrawCheckCircle(dl, ImVec2(q.x - D(24), p.y + D(24)), S(12) * selT, RGBA(inf.color), RGBA(0x08100C));
        if (clicked) {
            wSoftware_ = i;
            wVersion_.clear();
            versionsFor((P::Software)i);
        }
    }
    int rows = (P::kSoftwareCount + cols - 1) / cols;
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rows * (ch + gap)));
    ImGui::Dummy(ImVec2(1, S(4)));
}

// ---------------------------------------------------------------------------
// step 2: configure
// ---------------------------------------------------------------------------
void App::enterConfigStep() {
    wStep_ = 1;
    versionsFor((P::Software)wSoftware_);
    const auto& inf = P::infoByIndex(wSoftware_);
    wName_ = uniqueName(std::string("My ") + inf.name + " Server");
    wMotd_ = wName_;
    wPort_ = freePort();
    wVersion_.clear();
}

void App::wizardConfigStep() {
    const auto& inf = P::infoByIndex(wSoftware_);
    auto vf = versionsFor((P::Software)wSoftware_);
    float W = ImGui::GetContentRegionAvail().x, gap = S(16);
    bool twoCol = W > S(900);
    float leftW = twoCol ? W * 0.44f : W, rightW = twoCol ? W - leftW - gap : W;
    float cardH = std::max(HS(430), ImGui::GetContentRegionAvail().y - HS(70));

    std::vector<P::Version> list;
    bool done, ok;
    std::string err;
    {
        std::lock_guard<std::mutex> lk(vf->mu);
        list = vf->list;
        done = vf->done;
        ok = vf->ok;
        err = vf->err;
    }
    if (done && ok && wVersion_.empty()) {
        for (auto& v : list)
            if (v.stable) { wVersion_ = v.id; break; }
        if (wVersion_.empty() && !list.empty()) wVersion_ = list[0].id;
    }

    // ---- left: versions -------------------------------------------------------
    BeginCard("##versions", ImVec2(leftW, cardH), 20);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        DrawTile(dl, p, HS(38), inf.color);
        ImGui::Dummy(ImVec2(HS(38), HS(38)));
        ImGui::SameLine(0, S(12));
        ImGui::BeginGroup();
        Label((std::string(inf.name) + " version").c_str(), 16.5f, col::text, true);
        Label("Pick the Minecraft release to run.", 12.5f, col::mute);
        ImGui::EndGroup();
    }
    Gap(10);
    SearchBox("##vfilter", &wFilter_, "Search versions...", -1);
    Gap(4);
    ImGui::BeginGroup();
    Toggle("unstable", &wShowUnstable_);
    ImGui::SameLine(0, S(10));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(3));
    Label("Show snapshots & pre-releases", 13.f, col::dim);
    ImGui::EndGroup();
    Gap(4);

    if (!done) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::Dummy(ImVec2(w, S(150)));
        DrawSpinner(dl, ImVec2(p.x + w * 0.5f, p.y + S(56)), S(15), S(3), Accent());
        const char* t = "Fetching versions from the official source...";
        ImVec2 ts = TextSize(fRegular, 13.5f, t);
        DrawStr(dl, fRegular, 13.5f, ImVec2(p.x + (w - ts.x) * 0.5f, p.y + S(92)), RGBA(col::dim), t);
    } else if (!ok) {
        LabelWrapped(("Could not load versions: " + err).c_str(), 13.5f, col::red);
        Gap(6);
        if (Button("Retry", icon::Restart, Btn::Secondary)) versionsFor((P::Software)wSoftware_);
    } else {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, RGBA(col::sunkenSoft));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, RD(12));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(D(6), D(6)));
        ImGui::BeginChild("##vlist", ImVec2(0, ImGui::GetContentRegionAvail().y), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_None);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImDrawList* ldl = ImGui::GetWindowDrawList();
        const ImVec2 clipMin = ldl->GetClipRectMin(), clipMax = ldl->GetClipRectMax();
        const std::string needle = util::lower(util::trim(wFilter_));
        bool latestMarked = false;
        int shown = 0;
        for (auto& v : list) {
            if (!v.stable && !wShowUnstable_) continue;
            if (!needle.empty() && !util::icontains(v.id, needle)) continue;
            ++shown;
            ImVec2 p = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x, h = HS(42);
            // Paper and Forge list well over a hundred releases; only the visible ones need
            // their chips measured and drawn.
            if (p.y + h < clipMin.y || p.y > clipMax.y) {
                ImGui::Dummy(ImVec2(w, h + S(2)));
                continue;
            }
            ImGui::PushID(v.id.c_str());
            bool clicked = ImGui::InvisibleButton("##v", ImVec2(w, h));
            bool hov = ImGui::IsItemHovered();
            ImGui::PopID();
            if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            bool sel = v.id == wVersion_;
            if (sel) {
                ldl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Fade(Accent(0.14f)), RD(10));
                ldl->AddRect(p, ImVec2(p.x + w, p.y + h), Fade(Accent(0.5f)), RD(10), 0, 1.f);
            } else if (hov) ldl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Fade(RGBA(col::rowHover)), RD(10));
            DrawStr(ldl, fBold, 14.5f, ImVec2(p.x + D(14), p.y + (h - S(20)) * 0.5f), sel ? Accent() : RGBA(col::text), v.id.c_str());
            float cx = p.x + D(14) + TextSize(fBold, 14.5f, v.id.c_str()).x + S(12);
            auto chip = [&](const char* t, ImU32 c) {
                ImGui::SetCursorScreenPos(ImVec2(cx, p.y + (h - HS(22)) * 0.5f));
                Chip(t, c);
                cx += TextSize(fBold, 11.5f, t).x + D(24);
            };
            if (v.stable && !latestMarked) {
                chip("Latest", RGBA(col::green));
                latestMarked = true;
            }
            if (!v.stable) chip("Pre-release", RGBA(col::amber));
            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + S(2)));
            ImGui::Dummy(ImVec2(1, 0));
            if (clicked) wVersion_ = v.id;
        }
        if (shown == 0) Label("No versions match.", 13.5f, col::mute);
        ImGui::PopStyleVar();
        ImGui::EndChild();
    }
    EndCard();

    if (twoCol) ImGui::SameLine(0, gap);
    else Gap(14);

    // ---- right: details --------------------------------------------------------
    BeginCard("##opts", ImVec2(rightW, 0), 20);
    SectionTitle("Server details", "Everything here can be changed later.", icon::Sliders);
    Gap(14);
    FieldLabel("Server name");
    InputText("##wname", &wName_, "My Minecraft Server");
    Gap(4);
    FieldLabel("Message of the day", "(shown in the server list)");
    InputText("##wmotd", &wMotd_, "A Minecraft Server");
    Gap(8);
    {
        float previewW = std::min(ImGui::GetContentRegionAvail().x, S(520));
        float previewH = HS(92);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(previewW, previewH + S(8)));
        motd::drawListEntry(dl, p, previewW, previewH, inf.color, wName_.empty() ? "My Server" : wName_,
                            std::string(inf.name) + (wVersion_.empty() ? "" : " " + wVersion_), wMotd_);
    }
    Gap(6);
    float half = (ImGui::GetContentRegionAvail().x - S(14)) / 2;
    ImGui::BeginGroup();
    FieldLabel("Port");
    InputInt("##wport", &wPort_, 1024, 65535, half / g_scale);
    ImGui::EndGroup();
    ImGui::SameLine(0, S(14));
    ImGui::BeginGroup();
    FieldLabel("Memory");
    uint64_t totalMB = util::totalRamMB();
    int cap = (int)std::max<uint64_t>(2048, totalMB * 80 / 100);
    if (SliderInt("##wram", &wRam_, 1024, cap, "%d MB", half / g_scale)) wRam_ = std::max(1024, (wRam_ / 256) * 256);
    ImGui::EndGroup();
    {
        bool modded = inf.extensionKind[0] == 'm';
        Label(modded ? "Modded servers usually want 4-8 GB." : "2-4 GB is plenty for most plugin servers.", 12.5f, col::mute);
    }
    Gap(6);
    FieldLabel("Location");
    {
        fs::path dir = uniqueServerDir(wName_);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x, h = HS(40);
        ImGui::Dummy(ImVec2(w, h));
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Fade(RGBA(col::field)), RD(10));
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), Fade(RGBA(col::borderHi)), RD(10), 0, 1.f);
        DrawIcon(dl, ImVec2(p.x + D(20), p.y + h * 0.5f), icon::FolderOpen, 15.f, RGBA(col::mute));
        std::string ds2 = FitText(util::pathStr(dir), fRegular, 13.f, w - D(48), true);
        DrawStr(dl, fRegular, 13.f, ImVec2(p.x + D(38), p.y + (h - S(18)) * 0.5f), RGBA(col::dim), ds2.c_str());
    }
    Gap(10);
    ImGui::BeginGroup();
    Toggle("eula", &wEula_);
    ImGui::SameLine(0, S(12));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(3));
    Label("I accept the Minecraft EULA", 14.5f, col::text, true);
    ImGui::SameLine(0, S(10));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(4));
    if (Link("Read it")) util::openUrl("https://aka.ms/MinecraftEULA");
    ImGui::EndGroup();
    Gap(14);

    {
        bool valid = ok && !wVersion_.empty() && wEula_ && !util::trim(wName_).empty();
        ImGui::BeginDisabled(!valid);
        if (Button("Install server", icon::Download, Btn::Primary, ImVec2(-1, HS(46))) && valid) {
            {
                InstallRequest rq;
                rq.sw = (P::Software)wSoftware_;
                rq.mcVersion = wVersion_;
                rq.name = util::trim(wName_);
                rq.port = wPort_;
                rq.ramMB = wRam_;
                rq.motd = motd::toSectionCodes(util::trim(wMotd_));
                rq.acceptEula = wEula_;
                rq.dir = uniqueServerDir(rq.name);
                jobHandled_ = false;
                createdId_.clear();
                job_ = std::make_unique<InstallJob>(rq);
                wStep_ = 2;
            }
        }
        ImGui::EndDisabled();
        if (!valid)
            Tooltip(!ok ? "Still loading versions" : wVersion_.empty() ? "Pick a version" : !wEula_ ? "Accept the EULA to continue" : "Give the server a name");
    }
    EndCard();
}

// ---------------------------------------------------------------------------
// step 3: install
// ---------------------------------------------------------------------------
void App::wizardInstallStep() {
    if (!job_) { wStep_ = 1; return; }
    const auto& inf = P::info(job_->req.sw);
    float W = std::min(ImGui::GetContentRegionAvail().x, S(880));
    bool fin = job_->finished(), failed = job_->failed();
    auto steps = job_->steps();

    BeginCard("##install", ImVec2(W, 0), 24);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        DrawTile(dl, p, HS(54), inf.color);
        ImGui::Dummy(ImVec2(HS(54), HS(54)));
        ImGui::SameLine(0, S(16));
        ImGui::BeginGroup();
        std::string title = fin ? (failed ? "Installation failed" : "Your server is ready") : std::string("Installing ") + inf.name + " " + job_->req.mcVersion;
        Label(title.c_str(), 21.f, col::text, true);
        Label(job_->req.name.c_str(), 14.f, col::dim);
        ImGui::EndGroup();
    }
    Gap(18);
    for (auto& st : steps) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x, h = HS(48);
        ImGui::Dummy(ImVec2(w, h));
        ImVec2 c(p.x + D(16), p.y + h * 0.5f);
        using SS = InstallJob::StepState;
        switch (st.state) {
        case SS::Done: DrawCheckCircle(dl, c, S(11), Accent(), OnAccent()); break;
        case SS::Active: DrawSpinner(dl, c, S(10), S(2.5f), Accent()); break;
        case SS::Failed:
            dl->AddCircleFilled(c, S(11), Fade(RGBA(col::red)), 24);
            DrawIcon(dl, c, icon::Close, 11.f, RGBA(col::onDanger));
            break;
        default: dl->AddCircle(c, S(10), Fade(RGBA(col::borderHi)), 24, 1.5f); break;
        }
        ImU32 tc = st.state == SS::Pending ? RGBA(col::mute) : RGBA(col::text);
        DrawStr(dl, fBold, 14.5f, ImVec2(p.x + D(44), p.y + S(6)), tc, st.label.c_str());
        if (!st.detail.empty()) {
            std::string d = FitText(st.detail, fRegular, 12.5f, w - D(60));
            DrawStr(dl, fRegular, 12.5f, ImVec2(p.x + D(44), p.y + S(25)), st.state == SS::Failed ? RGBA(col::red) : RGBA(col::dim), d.c_str());
        }
    }
    Gap(10);
    if (!fin) {
        ProgressBar(job_->progress(), ImVec2(-1, HS(8)));
        Gap(12);
    }
    auto lines = job_->logTail(60);
    if (!lines.empty()) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, RGBA(col::sunken));
        ImGui::PushStyleColor(ImGuiCol_Border, RGBA(col::borderSoft));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, RD(12));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(D(14), D(10)));
        ImGui::BeginChild("##ilog", ImVec2(0, HS(150)), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, S(2)));
        ImGui::PushFont(fMono, Mono(12.5f));
        for (auto& l : lines) {
            bool isErr = util::startsWith(l, "ERROR");
            ImGui::PushStyleColor(ImGuiCol_Text, isErr ? RGBA(col::red) : RGBA(col::dim));
            ImGui::TextUnformatted(l.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::PopFont();
        ImGui::PopStyleVar();
        ImGui::SetScrollHereY(1.f);
        ImGui::EndChild();
        Gap(14);
    }

    if (!fin) {
        if (Button("Cancel", nullptr, Btn::Danger)) job_->cancel();
    } else if (failed) {
        if (Button("Back to options", icon::Back, Btn::Secondary)) {
            job_.reset();
            jobHandled_ = false;
            wStep_ = 1;
        }
        ImGui::SameLine();
        if (Button("Try again", icon::Restart, Btn::Primary)) {
            InstallRequest rq = job_->req;
            rq.dir = uniqueServerDir(rq.name);
            job_.reset();
            jobHandled_ = false;
            job_ = std::make_unique<InstallJob>(rq);
        }
    } else {
        ServerInstance* created = find(createdId_);
        if (created) {
            if (Button("Open server", icon::ChevronRight, Btn::Primary)) openServer(created);
            ImGui::SameLine();
            if (Button("Start it now", icon::Play, Btn::Secondary)) {
                startServer(*created);
                openServer(created);
            }
            ImGui::SameLine();
        }
        if (Button("Create another", icon::Add, Btn::Ghost)) resetWizard();
    }
    EndCard();
}

// ---------------------------------------------------------------------------
// import
// ---------------------------------------------------------------------------
void App::scanImportFolder(const std::string& dirUtf8) {
    iDir_ = dirUtf8;
    iTargets_.clear();
    iError.clear();
    fs::path dir = util::fromUtf8(dirUtf8);
    std::error_code ec;
    for (const char* group : {"libraries/net/neoforged/neoforge", "libraries/net/minecraftforge/forge"}) {
        fs::path g = dir / group;
        if (!fs::is_directory(g, ec)) continue;
        util::forEachDirEntry(g, [&](const fs::directory_entry& v, std::error_code&) {
            if (!fs::exists(v.path() / "win_args.txt", ec)) return;
            std::string rel = std::string(group) + "/" + util::pathStr(v.path().filename()) + "/win_args.txt";
            iTargets_.push_back({std::string(util::contains(group, "neoforged") ? "NeoForge" : "Forge") + " launcher (" +
                                     util::pathStr(v.path().filename()) + ")",
                                 "args", rel});
        });
    }
    std::vector<std::string> jars;
    util::forEachDirEntry(dir, [&](const fs::directory_entry& e, std::error_code& dec) {
        if (!e.is_regular_file(dec) || e.path().extension() != ".jar") return;
        const std::string n = util::pathStr(e.path().filename());
        if (util::contains(util::lower(n), "installer")) return;
        jars.push_back(n);
    });
    auto score = [](const std::string& n) {
        std::string l = util::lower(n);
        int s = 0;
        for (const char* k : {"server", "paper", "purpur", "folia", "fabric", "forge", "spigot"}) s += util::contains(l, k) ? 1 : 0;
        return s;
    };
    std::sort(jars.begin(), jars.end(), [&](const std::string& a, const std::string& b) { return score(a) > score(b) || (score(a) == score(b) && a < b); });
    for (auto& j : jars) iTargets_.push_back({j, "jar", j});
    iTarget_ = 0;
    if (iTargets_.empty()) {
        iError = "No server jar was found in this folder.";
        return;
    }

    std::string probe = util::lower(iTargets_[0].target);
    iSoftware_ = 0;
    if (util::contains(probe, "neoforge")) iSoftware_ = (int)P::Software::NeoForge;
    else if (util::contains(probe, "forge")) iSoftware_ = (int)P::Software::Forge;
    else if (util::contains(probe, "fabric")) iSoftware_ = (int)P::Software::Fabric;
    else if (util::contains(probe, "purpur")) iSoftware_ = (int)P::Software::Purpur;
    else if (util::contains(probe, "folia")) iSoftware_ = (int)P::Software::Folia;
    else if (util::contains(probe, "paper") || util::contains(probe, "spigot") || util::contains(probe, "bukkit")) iSoftware_ = (int)P::Software::Paper;
    iVersion_.clear();
    static const std::regex re(R"((\d+\.\d+(?:\.\d+)?))");
    std::smatch m;
    std::string n0 = iTargets_[0].target;
    if (std::regex_search(n0, m, re)) iVersion_ = m[1];
    iName_ = uniqueName(util::pathStr(dir.filename()));
    iRam_ = settings_.defaultRamMB;
}

void App::drawImport() {
    float W = std::min(ImGui::GetContentRegionAvail().x, S(880));
    drawTopBar("Import a server", "Already have a server folder? Add it to " VX_APP_NAME " without touching your files.", nullptr,
               "Back to servers");
    BeginCard("##import", ImVec2(W, 0), 24);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    FieldLabel("Server folder");
    ImVec2 p = ImGui::GetCursorScreenPos();
    float bw = ButtonWidth("Browse", true);
    float fw = ImGui::GetContentRegionAvail().x - bw - S(10);
    ImGui::Dummy(ImVec2(fw, HS(42)));
    dl->AddRectFilled(p, ImVec2(p.x + fw, p.y + HS(42)), Fade(RGBA(col::field)), RD(10));
    dl->AddRect(p, ImVec2(p.x + fw, p.y + HS(42)), Fade(RGBA(col::borderHi)), RD(10), 0, 1.f);
    DrawIcon(dl, ImVec2(p.x + D(18), p.y + HS(21)), icon::FolderOpen, 15.f, RGBA(col::mute));
    std::string shown = iDir_.empty() ? "No folder selected" : FitText(iDir_, fRegular, 14.f, fw - D(48), true);
    DrawStr(dl, fRegular, 14.f, ImVec2(p.x + D(36), p.y + (HS(42) - S(18)) * 0.5f), iDir_.empty() ? RGBA(col::mute) : RGBA(col::text), shown.c_str());
    ImGui::SameLine(0, S(10));
    if (Button("Browse", icon::FolderOpen, Btn::Secondary, ImVec2(0, HS(42)))) {
        std::string d;
        if (util::pickFolder(hwnd_, d)) scanImportFolder(d);
    }
    if (!iError.empty()) {
        Gap(10);
        Label(iError.c_str(), 13.5f, col::red);
    }

    if (!iTargets_.empty()) {
        Gap(16);
        FieldLabel("Server name");
        InputText("##iname", &iName_, "My server");
        Gap(6);
        FieldLabel("Start with", "(file used to launch the server)");
        std::vector<const char*> items;
        for (auto& t : iTargets_) items.push_back(t.label.c_str());
        Combo("##itarget", &iTarget_, items.data(), (int)items.size());
        Gap(6);
        float half = (ImGui::GetContentRegionAvail().x - S(14)) / 2;
        ImGui::BeginGroup();
        FieldLabel("Server software");
        std::vector<const char*> sw;
        for (int i = 0; i < P::kSoftwareCount; ++i) sw.push_back(P::infoByIndex(i).name);
        Combo("##isw", &iSoftware_, sw.data(), (int)sw.size(), half / g_scale);
        ImGui::EndGroup();
        ImGui::SameLine(0, S(14));
        ImGui::BeginGroup();
        FieldLabel("Minecraft version", "(optional)");
        InputText("##iver", &iVersion_, "e.g. 1.21.8", half / g_scale);
        ImGui::EndGroup();
        Gap(6);
        FieldLabel("Memory");
        uint64_t totalMB = util::totalRamMB();
        int cap = (int)std::max<uint64_t>(2048, totalMB * 80 / 100);
        if (SliderInt("##iram", &iRam_, 1024, cap, "%d MB")) iRam_ = std::max(1024, (iRam_ / 256) * 256);
        Gap(20);
        float ib = ButtonWidth("Import server", true);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - ib));
        ImGui::BeginDisabled(util::trim(iName_).empty());
        if (Button("Import server", icon::Check, Btn::Primary, ImVec2(0, HS(46)))) {
            ServerConfig c;
            c.id = util::newId();
            c.name = util::trim(iName_);
            c.software = P::infoByIndex(iSoftware_).key;
            c.mcVersion = util::trim(iVersion_);
            c.dir = iDir_;
            c.launchMode = iTargets_[iTarget_].mode;
            c.launchTarget = iTargets_[iTarget_].target;
            c.maxRamMB = iRam_;
            c.minRamMB = std::min(iRam_, 1024);
            c.javaMajor = c.mcVersion.empty() ? 0 : P::heuristicJava(c.mcVersion);
            c.createdAt = util::nowUnix();
            servers_.push_back(std::make_shared<ServerInstance>(c));
            lastState_[c.id] = State::Stopped;
            saveAll();
            buildCommands();
            Toast("Imported " + c.name, ToastKind::Success);
            ServerInstance* ns = servers_.back().get();
            iDir_.clear();
            iTargets_.clear();
            openServer(ns);
        }
        ImGui::EndDisabled();
    } else if (iDir_.empty()) {
        Gap(14);
        Hint("Pick the folder that holds your server jar, worlds, plugins and configs. Voxual reads it where it is - nothing is moved or copied.");
    }
    EndCard();
}
