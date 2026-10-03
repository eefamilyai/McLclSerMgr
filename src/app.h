#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "crashlog.h"
#include "installer.h"
#include "store.h"
#include "ui.h"
#include "version.h"

enum class Page { Servers, Wizard, Detail, Personalize, Settings, Import, About };

struct VersionFetch {
    std::mutex mu;
    bool done = false, ok = false;
    std::string err;
    std::vector<providers::Version> list;
};

struct JavaDownload {
    std::atomic<bool> active{false};
    std::atomic<float> progress{0.f};
    std::atomic<bool> cancel{false};
    std::mutex mu;
    std::string msg, err;
    int major = 0;
};

struct FileEntry {
    fs::path path;
    std::string name;
    uint64_t size = 0;
    bool enabled = true;
};

struct ConfigFile {
    fs::path path;
    std::string rel;      // path relative to the server folder (forward slashes)
    std::string group;
    uint64_t size = 0;
};

struct BackupEntry {
    fs::path path;
    std::string name;
    uint64_t size = 0;
    int64_t time = 0;
};

// Transient UI state for the server currently open in the detail view
struct DetailState {
    std::string serverId;
    int tab = 0;
    // console
    std::string input, filter;
    int logLevel = 0;             // 0 all, 1 warnings+, 2 errors only
    std::vector<std::string> history;
    int histPos = -1;
    bool autoscroll = true, focusInput = false;
    uint64_t lastSerial = 0;
    // settings draft
    bool loaded = false, dirty = false;
    Properties props;
    ServerConfig draft;
    int javaChoice = 0;
    // players
    std::string whitelistName, broadcast;
    // files / backups
    std::vector<FileEntry> files;
    std::string extFilter;
    std::vector<BackupEntry> backups;
    double lastScan = -100;
    std::string deleteConfirm;
    // config file editor
    std::vector<ConfigFile> cfgFiles;
    bool cfgScanned = false;
    std::string cfgFilter;
    bool editOpen = false, editCRLF = false, editOk = true;
    fs::path editPath;
    std::string editRel, editText, editOrig, editStatus;
    uint32_t editRev = 0, cachedRev = 0xffffffffu;
    std::vector<int> lineStarts;
    int maxCols = 0, lastCaret = -1;
    // personalization draft
    bool lookLoaded = false;
    std::string motdDraft;
    std::string iconPatternNote;
    int iconPattern = 0;
    unsigned iconColor1 = 0x34D399, iconColor2 = 0x1F2937;
    std::string noteDraft, colorDraft;
    bool wrapped = false;
};

struct ConfirmReq {
    std::string title, message, confirmLabel = "Confirm";
    bool danger = false;
    std::function<void()> onConfirm;
};

// One entry in the Ctrl+K palette.
struct Command {
    std::string label, hint, keys;
    const char* icon;
    std::function<void()> run;
};

class App {
public:
    App();
    ~App();
    void init(void* hwnd);
    void frame();
    bool requestClose();
    bool shouldQuit() const { return quitReady_; }
    void* hwnd() const { return hwnd_; }
    Settings& settings() { return settings_; }
    void applyPersonalization(bool immediate = false);
    void prepareFrame();   // applies deferred theme changes between frames (font atlas work)
    void rememberWindowSize();

    // --- command line / testing helpers
    void loadDemo();
    void gotoPage(const std::string& name);

private:
    // shell
    void tickServers();
    void drawShell();
    void drawSidebar(float w);
    void drawRail(float w);                                        // collapsed variant
    bool navItem(const char* label, const char* icon, bool active, int badge = 0, const char* tip = nullptr);
    void navigate(Page p);
    void drawTopBar(const char* title, const char* subtitle, const std::function<void()>& actions, const char* backTip = nullptr,
                    unsigned brandRgb = 0);
    void drawModals();
    void drawCommandPalette();
    void buildCommands();
    void askConfirm(ConfirmReq r);
    void drawClock(ImVec2 rightEdge);

    // servers page
    void drawServersPage();
    void drawServerCard(ServerInstance& s, ImVec2 size);
    void drawServerRow(ServerInstance& s);
    void drawEmptyState();
    void statTile(float w, const char* icon, const char* label, const std::string& value, ImU32 color);
    std::vector<ServerInstance*> ordered();

    // detail page
    void openServer(ServerInstance* s);
    void drawDetailPage();
    void drawConsoleTab(ServerInstance& s);
    void drawOverviewTab(ServerInstance& s);
    void drawPlayersTab(ServerInstance& s);
    void drawExtensionsTab(ServerInstance& s);
    void drawBackupsTab(ServerInstance& s);
    void drawServerSettingsTab(ServerInstance& s);
    void drawFilesTab(ServerInstance& s);
    void drawPersonalizeTab(ServerInstance& s);
    void drawSaveBar(ServerInstance& s, float width);
    void writeServerIcon(ServerInstance& s);
    void applyGameplayPreset(ServerInstance& s, int preset);
    void scanConfigFiles(ServerInstance& s);
    void openConfigFile(int idx);
    void saveConfigFile();
    void loadDraft(ServerInstance& s);
    void saveDraft(ServerInstance& s);
    void startServer(ServerInstance& s);
    void removeServer(ServerInstance* s, bool deleteFiles);
    void refreshFiles(ServerInstance& s);
    void refreshBackups(ServerInstance& s);
    template <class F> void settingRow(const char* label, const char* help, F&& control);
    template <class F> void settingSection(const char* title, const char* subtitle, F&& body);

    // wizard / import
    void resetWizard();
    void drawWizard();
    void drawWizardStepper();
    void wizardSoftwareStep();
    void wizardConfigStep();
    void wizardInstallStep();
    void enterConfigStep();
    std::shared_ptr<VersionFetch> versionsFor(providers::Software sw);
    fs::path uniqueServerDir(const std::string& name);
    std::string uniqueName(const std::string& base);
    int freePort();
    void pollJob();
    void drawImport();
    void scanImportFolder(const std::string& dir);

    // personalization / settings
    void drawPersonalizePage();
    void drawSettingsPage();
    void personalizeAppearance();
    void personalizeInterface();
    void personalizeServersPage();
    void personalizeConsole();
    void personalizeStorage();
    void personalizeJava();
    void personalizeAbout();
    void drawThemePreview(float width);
    void exportTheme();
    void importTheme();
    void resetPersonalization();
    void startJavaInstall(int major);

    // helpers
    ServerInstance* find(const std::string& id);
    void saveAll();
    void saveSettings();
    int activeCount();

    void* hwnd_ = nullptr;
    Settings settings_;
    std::vector<std::unique_ptr<ServerInstance>> servers_;
    std::map<std::string, State> lastState_;
    bool demo_ = false;
    bool pendingTheme_ = false;
    int fontSettle_ = 0;    // frames to wait before rebuilding the font atlas

    Page page_ = Page::Servers;
    float pageT_ = 0.f;
    int scrollGuard_ = 8;   // frames to keep a freshly opened page pinned to the top
    ServerInstance* sel_ = nullptr;
    DetailState ds_;

    ConfirmReq confirm_;
    bool confirmOpen_ = false;
    bool deleteOpen_ = false, deleteFiles_ = false;
    ServerInstance* deleteTarget_ = nullptr;
    bool quitAsk_ = false, quitting_ = false, quitReady_ = false;
    bool recoveryOpen_ = false;   // last run ended unexpectedly
    double quitStart_ = 0;

    // personalization page state
    int persTab_ = 0;
    std::string serverFilter_;
    int appliedPreset_ = -1;

    // command palette
    bool paletteOpen_ = false;
    std::string paletteQuery_;
    int paletteSel_ = 0;
    std::vector<Command> commands_;
    double paletteOpened_ = 0;

    // wizard
    int wStep_ = 0;
    int wSoftware_ = 1;
    std::map<int, std::shared_ptr<VersionFetch>> wFetch_;
    std::string wFilter_, wVersion_, wName_, wMotd_;
    bool wShowUnstable_ = false, wEula_ = false;
    int wPort_ = 25565, wRam_ = 2048;
    std::unique_ptr<InstallJob> job_;
    bool jobHandled_ = false;
    std::string createdId_;

    // import
    std::string iDir_, iName_, iVersion_, iError;
    struct Target { std::string label, mode, target; };
    std::vector<Target> iTargets_;
    int iTarget_ = 0, iSoftware_ = 0, iRam_ = 2048;

    // settings page
    JavaDownload javaJob_;
    std::string rootDraft_;
    bool autoStartDone_ = false;
};

// ---------------------------------------------------------------------------
// layout helpers shared by every settings-style page
// ---------------------------------------------------------------------------
template <class F>
void App::settingRow(const char* label, const char* help, F&& control) {
    using namespace ui;
    float w = ImGui::GetContentRegionAvail().x;
    float ctlW = std::min(S(300), w * 0.44f);
    ImVec2 start = ImGui::GetCursorScreenPos();
    ImGui::BeginGroup();
    Label(label, 14.5f, col::text, true);
    if (help) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + (w - ctlW - S(24)));
        Label(help, 12.5f, col::mute);
        ImGui::PopTextWrapPos();
    }
    ImGui::EndGroup();
    float labelH = ImGui::GetItemRectSize().y;
    ImGui::SetCursorScreenPos(ImVec2(start.x + w - ctlW, start.y + std::max(0.f, (labelH - HS(40)) * 0.5f)));
    ImGui::PushItemWidth(ctlW);
    control();
    ImGui::PopItemWidth();
    float endY = std::max(start.y + labelH, ImGui::GetItemRectMax().y);
    ImGui::SetCursorScreenPos(ImVec2(start.x, endY + D(10)));
    ImGui::GetWindowDrawList()->AddLine(ImVec2(start.x, endY + D(10)), ImVec2(start.x + w, endY + D(10)),
                                        Fade(Alpha(RGBA(col::border), 0.75f)), 1.f);
    ImGui::Dummy(ImVec2(1, D(12)));
}

template <class F>
void App::settingSection(const char* title, const char* subtitle, F&& body) {
    using namespace ui;
    BeginCard(title, ImVec2(std::min(ImGui::GetContentRegionAvail().x, S(960)), 0), 22);
    SectionTitle(title, subtitle);
    Gap(14);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(D(10), D(8)));
    body();
    ImGui::PopStyleVar();
    EndCard();
    Gap(14);
}

// shared helpers
ImU32 stateColor(State s);
unsigned serverColor(const ServerInstance& s);
const char* stateTip(State s);
