// "Files" tab: browse and edit a server's config files (yml, json, properties, toml...)
// with syntax highlighting, line numbers, Ctrl+S and basic validation.
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>

#include "app.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "nlohmann/json.hpp"

using namespace ui;

namespace {

enum class Kind { Yaml, Json, Props, Toml, Plain };

Kind kindOf(const std::string& rel) {
    std::string ext = util::lower(fs::path(util::fromUtf8(rel)).extension().string());
    if (ext == ".yml" || ext == ".yaml") return Kind::Yaml;
    if (ext == ".json" || ext == ".json5" || ext == ".mcmeta") return Kind::Json;
    if (ext == ".properties") return Kind::Props;
    if (ext == ".toml" || ext == ".cfg" || ext == ".ini" || ext == ".conf") return Kind::Toml;
    return Kind::Plain;
}

const char* extLabel(const std::string& rel) {
    switch (kindOf(rel)) {
    case Kind::Yaml: return "YML";
    case Kind::Json: return "JSON";
    case Kind::Props: return "PROP";
    case Kind::Toml: return "CFG";
    default: return "TXT";
    }
}

ImU32 extColor(const std::string& rel) {
    switch (kindOf(rel)) {
    case Kind::Yaml: return RGBA(col::blue);
    case Kind::Json: return RGBA(col::amber);
    case Kind::Props: return RGBA(col::teal);
    case Kind::Toml: return RGBA(col::violet);
    default: return RGBA(col::dim);
    }
}

// Indentation width used when Tab or Enter is pressed in the editor.
int g_tabSpaces = 2;

// ---- syntax highlighting -----------------------------------------------------
struct Tok {
    int start, len;
    ImU32 col;
};


bool isNumber(const std::string& w) {
    if (w.empty()) return false;
    size_t i = (w[0] == '-' || w[0] == '+') ? 1 : 0;
    bool digit = false;
    for (; i < w.size(); ++i) {
        char c = w[i];
        if (isdigit((unsigned char)c)) digit = true;
        else if (c != '.' && c != '_' && c != 'e' && c != 'E' && c != '-' && c != '+' && c != 'x' && c != 'L' && c != 'f' && c != 'd') return false;
    }
    return digit;
}

bool isKeyword(const std::string& w) {
    std::string l = util::lower(w);
    return l == "true" || l == "false" || l == "null" || l == "yes" || l == "no" || l == "on" || l == "off" || l == "~";
}

// Value part of a line: strings, numbers, keywords, comments.
void valueTokens(const char* s, int from, int n, bool hashComments, std::vector<Tok>& out) {
    int i = from;
    while (i < n) {
        char c = s[i];
        if (c == ' ') { ++i; continue; }
        if (hashComments && c == '#' && (i == 0 || s[i - 1] == ' ')) { out.push_back({i, n - i, RGBA(col::codeCom)}); return; }
        if (c == '"' || c == '\'') {
            int j = i + 1;
            while (j < n && s[j] != c) { if (c == '"' && s[j] == '\\') ++j; ++j; }
            j = std::min(n, j + 1);
            out.push_back({i, j - i, RGBA(col::codeStr)});
            i = j;
            continue;
        }
        if (c == '[' || c == ']' || c == '{' || c == '}' || c == ',') { out.push_back({i, 1, RGBA(col::codePunct)}); ++i; continue; }
        int j = i;
        while (j < n && s[j] != ' ' && s[j] != ',' && s[j] != ']' && s[j] != '}') ++j;
        if (j == i) { ++i; continue; }
        std::string w(s + i, j - i);
        ImU32 col = RGBA(col::codeDef);
        if (isNumber(w)) col = RGBA(col::codeNum);
        else if (isKeyword(w) || w[0] == '&' || w[0] == '*' || w[0] == '!' || w == "|" || w == ">" || w == "|-" || w == ">-") col = RGBA(col::codeKw);
        out.push_back({i, j - i, col});
        i = j;
    }
}

void tokenize(Kind k, const char* s, int n, std::vector<Tok>& out) {
    out.clear();
    int i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t')) ++i;
    if (i >= n) return;
    if (k == Kind::Json) {
        while (i < n) {
            char c = s[i];
            if (c == '"') {
                int j = i + 1;
                while (j < n && s[j] != '"') { if (s[j] == '\\') ++j; ++j; }
                j = std::min(n, j + 1);
                int q = j;
                while (q < n && s[q] == ' ') ++q;
                out.push_back({i, j - i, (q < n && s[q] == ':') ? RGBA(col::codeKey) : RGBA(col::codeStr)});
                i = j;
            } else if (c == '/' && i + 1 < n && s[i + 1] == '/') { out.push_back({i, n - i, RGBA(col::codeCom)}); return; }
            else if (strchr("{}[],:", c)) { out.push_back({i, 1, RGBA(col::codePunct)}); ++i; }
            else if (c == ' ') ++i;
            else {
                int j = i;
                while (j < n && !strchr(" ,]}:\"", s[j])) ++j;
                if (j == i) { ++i; continue; }
                std::string w(s + i, j - i);
                out.push_back({i, j - i, isNumber(w) ? RGBA(col::codeNum) : (isKeyword(w) ? RGBA(col::codeKw) : RGBA(col::codeDef))});
                i = j;
            }
        }
        return;
    }
    if (k == Kind::Yaml) {
        if (s[i] == '#') { out.push_back({i, n - i, RGBA(col::codeCom)}); return; }
        while (i + 1 <= n && s[i] == '-' && (i + 1 == n || s[i + 1] == ' ')) {
            out.push_back({i, 1, RGBA(col::codePunct)});
            ++i;
            while (i < n && s[i] == ' ') ++i;
        }
        if (i >= n) return;
        int keyEnd = -1;  // index of ':' that ends the key
        if (s[i] == '"' || s[i] == '\'') {
            char q = s[i];
            int j = i + 1;
            while (j < n && s[j] != q) ++j;
            if (j + 1 < n && s[j + 1] == ':' && (j + 2 >= n || s[j + 2] == ' ')) keyEnd = j + 1;
        } else if (s[i] != '[' && s[i] != '{' && s[i] != '#') {
            for (int j = i; j < n; ++j) {
                if (s[j] == '#' && j > i && s[j - 1] == ' ') break;
                if (s[j] == ':' && (j + 1 >= n || s[j + 1] == ' ')) { keyEnd = j; break; }
            }
        }
        if (keyEnd >= 0) {
            out.push_back({i, keyEnd - i, RGBA(col::codeKey)});
            out.push_back({keyEnd, 1, RGBA(col::codePunct)});
            valueTokens(s, keyEnd + 1, n, true, out);
        } else {
            valueTokens(s, i, n, true, out);
        }
        return;
    }
    // properties / toml / ini / plain
    if (s[i] == '#' || s[i] == ';' || (s[i] == '/' && i + 1 < n && s[i + 1] == '/')) { out.push_back({i, n - i, RGBA(col::codeCom)}); return; }
    if (k == Kind::Toml && s[i] == '[') {
        int j = i;
        while (j < n && s[j] != ']') ++j;
        j = std::min(n, j + 1);
        out.push_back({i, j - i, RGBA(col::codeSec)});
        return;
    }
    if (k == Kind::Plain) { out.push_back({i, n - i, RGBA(col::codeDef)}); return; }
    int eq = -1;
    for (int j = i; j < n; ++j)
        if (s[j] == '=') { eq = j; break; }
    if (eq < 0) { valueTokens(s, i, n, k == Kind::Toml, out); return; }
    int ke = eq;
    while (ke > i && s[ke - 1] == ' ') --ke;
    out.push_back({i, ke - i, RGBA(col::codeKey)});
    out.push_back({eq, 1, RGBA(col::codePunct)});
    valueTokens(s, eq + 1, n, k == Kind::Toml, out);
}

// ---- validation ------------------------------------------------------------------------
std::string lint(Kind k, const std::string& text, bool& ok) {
    ok = true;
    if (k == Kind::Json) {
        try {
            const nlohmann::json parsed = nlohmann::json::parse(text);   // [[nodiscard]]
            (void)parsed;
            return "Valid JSON";
        } catch (const std::exception& e) {
            ok = false;
            std::string m = e.what();
            auto p = m.find("parse error");
            return p != std::string::npos ? m.substr(p) : m;
        }
    }
    if (k == Kind::Yaml) {
        int line = 1;
        size_t i = 0;
        while (i <= text.size()) {
            size_t e = text.find('\n', i);
            if (e == std::string::npos) e = text.size();
            std::string ln = text.substr(i, e - i);
            size_t ind = 0;
            while (ind < ln.size() && (ln[ind] == ' ' || ln[ind] == '\t')) {
                if (ln[ind] == '\t') { ok = false; return "Line " + std::to_string(line) + ": tabs are not allowed for indentation, use spaces"; }
                ++ind;
            }
            std::string t = ln.substr(ind);
            if (!t.empty() && t[0] != '#') {
                auto colon = t.find(": ");
                if (colon != std::string::npos) {
                    size_t v = colon + 2;
                    while (v < t.size() && t[v] == ' ') ++v;
                    if (v < t.size() && (t[v] == '"' || t[v] == '\'')) {
                        char q = t[v];
                        size_t j = v + 1;
                        bool closed = false;
                        for (; j < t.size(); ++j) {
                            if (q == '"' && t[j] == '\\') { ++j; continue; }
                            if (t[j] == q) { closed = true; break; }
                        }
                        if (!closed) { ok = false; return "Line " + std::to_string(line) + ": quote is never closed"; }
                    }
                }
            }
            ++line;
            if (e == text.size()) break;
            i = e + 1;
        }
        return "No syntax problems found";
    }
    return "Ready";
}

void rebuildLines(DetailState& d) {
    d.lineStarts.clear();
    d.lineStarts.push_back(0);
    int cols = 0, maxc = 0;
    for (size_t i = 0; i < d.editText.size(); ++i) {
        unsigned char c = (unsigned char)d.editText[i];
        if (c == '\n') {
            d.lineStarts.push_back((int)i + 1);
            maxc = std::max(maxc, cols);
            cols = 0;
        } else if ((c & 0xC0) != 0x80) {
            ++cols;
        }
    }
    d.maxCols = std::min(std::max(maxc, cols), 3000);
    d.cachedRev = d.editRev;
}

int lineOf(const DetailState& d, int pos) {
    auto it = std::upper_bound(d.lineStarts.begin(), d.lineStarts.end(), pos);
    return (int)(it - d.lineStarts.begin()) - 1;
}

int colsBetween(const std::string& t, int from, int to) {
    int c = 0;
    for (int i = from; i < to && i < (int)t.size(); ++i)
        if (((unsigned char)t[i] & 0xC0) != 0x80) ++c;
    return c;
}

// Tab -> two spaces, Enter keeps the indentation of the previous line (+2 after a colon)
int editCallback(ImGuiInputTextCallbackData* d) {
    if (d->EventFlag != ImGuiInputTextFlags_CallbackAlways) return 0;
    int cur = d->CursorPos;
    int tabs = 0;
    while (d->CursorPos > 0 && d->Buf[d->CursorPos - 1] == '\t') { d->DeleteChars(d->CursorPos - 1, 1); ++tabs; }
    if (tabs > 0) {
        d->InsertChars(d->CursorPos, std::string((size_t)(g_tabSpaces * std::max(1, tabs)), ' ').c_str());
    } else if (cur > 0 && d->Buf[cur - 1] == '\n' && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) {
        int ls = cur - 1;  // start of previous line
        while (ls > 0 && d->Buf[ls - 1] != '\n') --ls;
        int ind = 0;
        while (ls + ind < cur - 1 && d->Buf[ls + ind] == ' ') ++ind;
        int lastNonSpace = cur - 2;
        while (lastNonSpace > ls && d->Buf[lastNonSpace] == ' ') --lastNonSpace;
        bool colon = lastNonSpace >= ls && d->Buf[lastNonSpace] == ':';
        std::string pad(ind + (colon ? g_tabSpaces : 0), ' ');
        if (!pad.empty()) d->InsertChars(cur, pad.c_str());
    }
    return 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// scanning / loading / saving
// ---------------------------------------------------------------------------
void App::scanConfigFiles(ServerInstance& s) {
    ds_.cfgFiles.clear();
    ds_.cfgScanned = true;
    fs::path root = s.cfg.path();
    std::error_code ec;
    auto okExt = [](const fs::path& p) {
        std::string e = util::lower(p.extension().string());
        return e == ".yml" || e == ".yaml" || e == ".json" || e == ".json5" || e == ".toml" || e == ".properties" || e == ".cfg" ||
               e == ".conf" || e == ".ini" || e == ".txt" || e == ".mcmeta";
    };
    auto add = [&](const fs::path& p, const char* group) {
        if (ds_.cfgFiles.size() >= 900) return;
        std::error_code e2;
        uint64_t sz = fs::file_size(p, e2);
        if (e2 || sz > 1500000) return;
        ConfigFile f;
        f.path = p;
        f.rel = util::pathStr(fs::relative(p, root, e2));
        std::replace(f.rel.begin(), f.rel.end(), '\\', '/');
        f.group = group;
        f.size = sz;
        ds_.cfgFiles.push_back(std::move(f));
    };
    auto walk = [&](const fs::path& dir, const char* group, int depth) {
        if (!fs::is_directory(dir, ec)) return;
        fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            if (it.depth() >= depth) { it.disable_recursion_pending(); }
            if (it->is_directory(ec)) {
                std::string n = util::lower(util::pathStr(it->path().filename()));
                if (n == "cache" || n == "logs" || n == "libs" || n == "libraries" || n == ".paper-remapped") it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file(ec) && okExt(it->path())) add(it->path(), group);
        }
    };
    util::forEachDirEntry(root, [&](const fs::directory_entry& e, std::error_code& dec) {
        if (e.is_regular_file(dec) && okExt(e.path())) {
            const std::string n = util::lower(util::pathStr(e.path().filename()));
            if (n == "usercache.json") return;
            add(e.path(), "Server");
        }
    });
    walk(root / "config", "Config", 4);
    walk(root / "plugins", "Plugins", 3);
    walk(root / "defaultconfigs", "Default configs", 3);
    walk(root / "world" / "serverconfig", "World config", 2);
    std::stable_sort(ds_.cfgFiles.begin(), ds_.cfgFiles.end(), [](const ConfigFile& a, const ConfigFile& b) {
        if (a.group != b.group) {
            static const char* order[] = {"Server", "Config", "Plugins", "Default configs", "World config"};
            auto idx = [](const std::string& g) { for (int i = 0; i < 5; ++i) if (g == order[i]) return i; return 9; };
            return idx(a.group) < idx(b.group);
        }
        bool ap = a.rel == "server.properties", bp = b.rel == "server.properties";
        if (ap != bp) return ap;
        return util::lower(a.rel) < util::lower(b.rel);
    });
}

void App::openConfigFile(int idx) {
    if (idx < 0 || idx >= (int)ds_.cfgFiles.size()) return;
    auto& f = ds_.cfgFiles[idx];
    std::string txt;
    if (!util::readFile(f.path, txt)) { Toast("Could not read " + f.rel, ToastKind::Error); return; }
    if (txt.size() >= 3 && (unsigned char)txt[0] == 0xEF && (unsigned char)txt[1] == 0xBB && (unsigned char)txt[2] == 0xBF) txt.erase(0, 3);
    ds_.editCRLF = txt.find("\r\n") != std::string::npos;
    if (ds_.editCRLF) {
        std::string out;
        out.reserve(txt.size());
        for (char c : txt)
            if (c != '\r') out += c;
        txt.swap(out);
    }
    ds_.editPath = f.path;
    ds_.editRel = f.rel;
    ds_.editText = ds_.editOrig = txt;
    ds_.editOpen = true;
    ++ds_.editRev;
    ds_.editStatus = lint(kindOf(f.rel), ds_.editText, ds_.editOk);
    ds_.lastCaret = -1;
}

void App::saveConfigFile() {
    if (!ds_.editOpen) return;
    std::string out = ds_.editText;
    if (ds_.editCRLF) {
        std::string c;
        for (char ch : out) { if (ch == '\n') c += '\r'; c += ch; }
        out.swap(c);
    }
    if (!demo_ && !util::writeFile(ds_.editPath, out)) { Toast("Could not save " + ds_.editRel, ToastKind::Error); return; }
    ds_.editOrig = ds_.editText;
    Toast("Saved " + ds_.editRel, ToastKind::Success);
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------
void App::drawFilesTab(ServerInstance& s) {
    if (!ds_.cfgScanned) {
        scanConfigFiles(s);
        if (demo_ && ds_.cfgFiles.empty()) {
            // demo content so the editor can be previewed without a real server folder
            const char* names[] = {"server.properties", "bukkit.yml", "spigot.yml", "config/paper-global.yml", "config/paper-world-defaults.yml", "commands.yml"};
            const char* groups[] = {"Server", "Server", "Server", "Config", "Config", "Server"};
            for (int i = 0; i < 6; ++i) ds_.cfgFiles.push_back({fs::path(names[i]), names[i], groups[i], 2048});
            ds_.editOpen = true;
            ds_.editRel = "config/paper-global.yml";
            ds_.editText = ds_.editOrig =
                "# Paper Global Configuration\n"
                "# Documentation: https://docs.papermc.io/paper/reference/global-configuration\n"
                "_version: 29\n"
                "chunk-loading:\n"
                "  autoconfig-send-distance: true\n"
                "  enable-frustum-priority: false\n"
                "  global-max-chunk-load-rate: -1.0\n"
                "  max-concurrent-sends: 2\n"
                "  min-load-radius: 2\n"
                "collisions:\n"
                "  enable-player-collisions: true\n"
                "  send-full-pos-for-hard-colliding-entities: true\n"
                "messages:\n"
                "  kick:\n"
                "    authentication-servers-down: <lang:multiplayer.disconnect.authservers_down>\n"
                "    connection-throttle: Connection throttled! Please wait before reconnecting.\n"
                "  no-permission: '<red>I''m sorry, but you do not have permission to perform this command.'\n"
                "spam-limiter:\n"
                "  incoming-packet-threshold: 300\n"
                "  tab-spam-increment: 1\n"
                "  tab-spam-limit: 500\n"
                "unsupported-settings:\n"
                "  allow-headless-pistons: false\n"
                "  allow-permanent-block-break-exploits: false\n"
                "  skip-vanilla-damage-tick-when-shield-blocked: false\n"
                "watchdog:\n"
                "  early-warning-delay: 10000\n"
                "  early-warning-every: 5000\n"
                "worlds:\n"
                "  - name: world\n"
                "    keep-spawn-loaded: true\n";
            ++ds_.editRev;
            ds_.editStatus = lint(Kind::Yaml, ds_.editText, ds_.editOk);
        }
    }

    g_tabSpaces = settings_.editorTabSize == 4 ? 4 : 2;
    float W = ImGui::GetContentRegionAvail().x, H = ImGui::GetContentRegionAvail().y, gap = S(14);
    float listW = std::min(S(330), W * 0.34f);
    bool dirty = ds_.editOpen && ds_.editText != ds_.editOrig;

    // ================= file list =================
    BeginCard("##cfglist", ImVec2(listW, H), 16);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    char sub[48];
    snprintf(sub, sizeof sub, "%d found", (int)ds_.cfgFiles.size());
    ImGui::BeginGroup();
    SectionTitle("Config files", sub, icon::Edit);
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - HS(38)));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(4));
    if (IconButton("rescan", icon::Restart, Btn::Secondary, 38, "Rescan folder")) scanConfigFiles(s);
    Gap(6);
    InputText("##cfgfilter", &ds_.cfgFilter, "Search files...");
    Gap(2);
    BeginScroll("##cfgscroll", ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, S(2)));
    const std::string needle = util::lower(util::trim(ds_.cfgFilter));
    const ImVec2 clipMin = ImGui::GetWindowDrawList()->GetClipRectMin();
    const ImVec2 clipMax = ImGui::GetWindowDrawList()->GetClipRectMax();
    std::string lastGroup;
    int clicked = -1;
    for (int i = 0; i < (int)ds_.cfgFiles.size(); ++i) {
        auto& f = ds_.cfgFiles[i];
        if (!needle.empty() && !util::icontains(f.rel, needle)) continue;
        if (f.group != lastGroup) {
            lastGroup = f.group;
            Gap(8);
            Label(util::lower(f.group) == "server" ? "SERVER" : [&] { static std::string g; g = f.group; for (auto& c : g) c = (char)toupper((unsigned char)c); return g.c_str(); }(), 11.f, col::mute, true);
            Gap(4);
        }
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x, h = S(44);
        // A server can carry hundreds of plugin configs, and fitting the name and the folder of
        // every one of them on every frame was the expensive part. Offscreen rows just take space.
        if (p.y + h < clipMin.y || p.y > clipMax.y) {
            ImGui::Dummy(ImVec2(w, h));
            ImGui::PopID();
            continue;
        }
        bool pressed = ImGui::InvisibleButton("##f", ImVec2(w, h));
        bool hov = ImGui::IsItemHovered();
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        bool sel = ds_.editOpen && f.rel == ds_.editRel;
        ImDrawList* ld = ImGui::GetWindowDrawList();
        if (sel) {
            ld->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Fade(Accent(0.14f)), S(10));
            ld->AddRect(p, ImVec2(p.x + w, p.y + h), Fade(Accent(0.5f)), S(10), 0, 1.f);
        } else if (hov) ld->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Fade(RGBA(col::rowHover)), S(10));
        // extension chip
        ImU32 ec = extColor(f.rel);
        const char* el = extLabel(f.rel);
        ImVec2 es = TextSize(fBold, 10.5f, el);
        float cw = S(40), ch = S(22);
        ImVec2 cp(p.x + S(10), p.y + (h - ch) * 0.5f);
        ld->AddRectFilled(cp, ImVec2(cp.x + cw, cp.y + ch), Fade(Alpha(ec, 0.16f)), S(7));
        DrawStr(ld, fBold, 10.5f, ImVec2(cp.x + (cw - es.x) * 0.5f, cp.y + (ch - es.y) * 0.5f), ec, el);
        std::string name = util::pathStr(fs::path(util::fromUtf8(f.rel)).filename());
        std::string dir = util::pathStr(fs::path(util::fromUtf8(f.rel)).parent_path());
        std::replace(dir.begin(), dir.end(), '\\', '/');
        float tx = p.x + S(58), maxW = w - S(66);
        if (dir.empty()) {
            std::string nm = FitText(name, fBold, 13.5f, maxW, false);
            DrawStr(ld, fBold, 13.5f, ImVec2(tx, p.y + (h - TextSize(fBold, 13.5f, "A").y) * 0.5f), sel ? RGBA(col::text) : RGBA(col::dim), nm.c_str());
        } else {
            std::string nm = FitText(name, fBold, 13.5f, maxW, false), dr = FitText(dir, fRegular, 11.5f, maxW, true);
            DrawStr(ld, fBold, 13.5f, ImVec2(tx, p.y + S(6)), sel ? RGBA(col::text) : RGBA(col::dim), nm.c_str());
            DrawStr(ld, fRegular, 11.5f, ImVec2(tx, p.y + S(24)), RGBA(col::mute), dr.c_str());
        }
        ImGui::PopID();
        if (pressed) clicked = i;
    }
    if (ds_.cfgFiles.empty()) LabelWrapped("No config files found yet. Start the server once to generate them.", 13.f, col::mute);
    ImGui::PopStyleVar();
    EndScroll();
    EndCard();
    if (clicked >= 0 && !(ds_.editOpen && ds_.cfgFiles[clicked].rel == ds_.editRel)) {
        if (dirty) {
            int idx = clicked;
            askConfirm({"Discard unsaved changes?", "You have unsaved edits in " + ds_.editRel + ". Opening another file will discard them.", "Discard", true,
                        [this, idx] { openConfigFile(idx); }});
        } else openConfigFile(clicked);
    }

    // ================= editor =================
    ImGui::SameLine(0, gap);
    BeginCard("##cfgedit", ImVec2(0, H), 18);
    if (!ds_.editOpen) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetContentRegionAvail().y;
        ImGui::Dummy(ImVec2(w, h));
        ImDrawList* d2 = ImGui::GetWindowDrawList();
        DrawIcon(d2, ImVec2(p.x + w * 0.5f, p.y + h * 0.42f), icon::Edit, 40.f, RGBA(col::faint));
        const char* t = "Pick a file to edit";
        const char* t2 = "bukkit.yml, spigot.yml, paper-global.yml, plugin configs and more.";
        ImVec2 ts = TextSize(fBold, 18.f, t), t2s = TextSize(fRegular, 13.5f, t2);
        DrawStr(d2, fBold, 18.f, ImVec2(p.x + (w - ts.x) * 0.5f, p.y + h * 0.42f + S(34)), RGBA(col::text), t);
        DrawStr(d2, fRegular, 13.5f, ImVec2(p.x + (w - t2s.x) * 0.5f, p.y + h * 0.42f + S(64)), RGBA(col::dim), t2);
        EndCard();
        return;
    }

    Kind kind = kindOf(ds_.editRel);
    std::string fname = util::pathStr(fs::path(util::fromUtf8(ds_.editRel)).filename());
    // header: file name and actions on one row, the relative path underneath
    {
        float saveW = ButtonWidth("Save", true), revertW = ButtonWidth("Revert", true), openW = ButtonWidth("Open", true);
        float total = saveW + revertW + openW + S(20);
        float avail = ImGui::GetContentRegionAvail().x;
        ImGui::BeginGroup();
        Label(fname.c_str(), 18.f, col::text, true);
        if (dirty) {
            ImGui::SameLine(0, S(12));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(3));
            Badge("Unsaved", RGBA(col::amber), true);
        }
        ImGui::EndGroup();
        if (avail > total + S(120)) {
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - total));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - S(2));
        } else {
            Gap(6);
        }
        if (Button("Open", icon::FolderOpen, Btn::Ghost, ImVec2(0, HS(40))) && !demo_)
            ShellExecuteW(nullptr, L"open", ds_.editPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        Tooltip("Open in your default editor");
        ImGui::SameLine(0, S(8));
        ImGui::BeginDisabled(!dirty);
        if (Button("Revert", icon::Restart, Btn::Secondary, ImVec2(0, HS(40)))) {
            ds_.editText = ds_.editOrig;
            ++ds_.editRev;
            ds_.editStatus = lint(kind, ds_.editText, ds_.editOk);
        }
        ImGui::SameLine(0, S(8));
        bool save = Button("Save", icon::Save, Btn::Primary, ImVec2(0, HS(40)));
        ImGui::EndDisabled();
        Tooltip("Ctrl+S");
        Gap(2);
        Label(ds_.editRel.c_str(), 12.5f, col::mute);
        if (dirty && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) save = true;
        if (save && dirty) saveConfigFile();
    }
    Gap(4);
    if (s.isActive()) {
        Label("The server is running. Many plugins rewrite their config on shutdown and most settings need a restart or reload.", 12.5f, col::amber);
        Gap(2);
    }

    // ---- editor surface ---------------------------------------------------------------
    float statusH = S(34);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, RGBA(col::sunken));
    ImGui::PushStyleColor(ImGuiCol_Border, RGBA(col::borderHi));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(12));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##editor", ImVec2(0, ImGui::GetContentRegionAvail().y - statusH), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    ImDrawList* ed = ImGui::GetWindowDrawList();
    ImGui::PushFont(fMono, FS(13.5f));
    float lh = ImGui::GetFontSize();
    float cw = fMono->CalcTextSizeA(lh, FLT_MAX, 0.f, "M").x;
    if (ds_.cachedRev != ds_.editRev) rebuildLines(ds_);
    int lines = (int)ds_.lineStarts.size();
    float padX = S(14), padY = S(12);
    int digits = (int)std::to_string(lines).size();
    float gutterW = std::max(digits, 2) * cw + S(30);
    ImVec2 winPos = ImGui::GetWindowPos(), winSize = ImGui::GetWindowSize();
    float availW = winSize.x - 2.f;
    float inputW = std::max(availW - gutterW, ds_.maxCols * cw + padX * 2 + cw * 6);
    float inputH = std::max((lines + 2) * lh + padY * 2, ImGui::GetContentRegionAvail().y);
    ImVec2 start = ImGui::GetCursorScreenPos();

    ImGui::SetCursorScreenPos(ImVec2(start.x + gutterW, start.y));
    ImGuiID inputId = ImGui::GetID("##edit");
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    bool changed = ImGui::InputTextMultiline("##edit", &ds_.editText, ImVec2(inputW, inputH),
                                             ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways |
                                                 (settings_.editorWrap ? 0 : ImGuiInputTextFlags_NoHorizontalScroll),
                                             editCallback);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);
    ImVec2 imin = ImGui::GetItemRectMin();
    ImVec2 origin(imin.x + padX, imin.y + padY);
    if (changed) {
        ++ds_.editRev;
        rebuildLines(ds_);
        ds_.editStatus = lint(kind, ds_.editText, ds_.editOk);
    }

    // caret info
    int caretLine = 0, caretCol = 0;
    bool active = false;
    if (ImGuiInputTextState* st = ImGui::GetInputTextState(inputId)) {
        active = true;
        int pos = std::min(st->GetCursorPos(), (int)ds_.editText.size());
        caretLine = std::clamp(lineOf(ds_, pos), 0, lines - 1);
        caretCol = colsBetween(ds_.editText, ds_.lineStarts[caretLine], pos);
        // keep the caret in view
        int sig = caretLine * 10000 + caretCol;
        if (sig != ds_.lastCaret) {
            ds_.lastCaret = sig;
            float cy = origin.y + caretLine * lh, cx = origin.x + caretCol * cw;
            float top = winPos.y + lh, bottom = winPos.y + winSize.y - lh * 2;
            if (cy < top) ImGui::SetScrollY(ImGui::GetScrollY() - (top - cy));
            else if (cy > bottom) ImGui::SetScrollY(ImGui::GetScrollY() + (cy - bottom));
            float left = winPos.x + gutterW + cw * 2, right = winPos.x + winSize.x - cw * 4;
            if (cx < left) ImGui::SetScrollX(ImGui::GetScrollX() - (left - cx));
            else if (cx > right) ImGui::SetScrollX(ImGui::GetScrollX() + (cx - right));
        }
    } else {
        ds_.lastCaret = -1;
    }

    // visible line range (clip rect of the editor window)
    float scrollTop = winPos.y, scrollBottom = winPos.y + winSize.y;
    int first = std::max(0, (int)std::floor((scrollTop - origin.y) / lh) - 1);
    int last = std::min(lines - 1, (int)std::ceil((scrollBottom - origin.y) / lh) + 1);

    // gutter background (stays under the line numbers) + current line band
    ed->AddRectFilled(ImVec2(start.x, start.y), ImVec2(start.x + gutterW - S(6), start.y + std::max(inputH, 1.f)), Fade(RGBA(col::sunkenSoft)));
    ed->AddLine(ImVec2(start.x + gutterW - S(6), start.y), ImVec2(start.x + gutterW - S(6), start.y + inputH), Fade(RGBA(col::borderSoft)), 1.f);
    if (active)
        ed->AddRectFilled(ImVec2(start.x + gutterW - S(6), origin.y + caretLine * lh), ImVec2(start.x + gutterW + inputW, origin.y + (caretLine + 1) * lh),
                          Fade(Alpha(RGBA(col::rowActive), 0.55f)));

    std::vector<Tok> toks;
    char num[16];
    for (int ln = first; ln <= last; ++ln) {
        float y = origin.y + ln * lh;
        snprintf(num, sizeof num, "%d", ln + 1);
        float nw = (float)strlen(num) * cw;
        ed->AddText(fMono, lh, ImVec2(std::floor(start.x + gutterW - S(16) - nw), std::floor(y)),
                    Fade(ln == caretLine && active ? RGBA(col::dim) : RGBA(col::faint)), num);
        int ls = ds_.lineStarts[ln];
        int le = ln + 1 < lines ? ds_.lineStarts[ln + 1] - 1 : (int)ds_.editText.size();
        int n = std::min(le - ls, 1500);
        if (n <= 0) continue;
        const char* base = ds_.editText.c_str() + ls;
        tokenize(kind, base, n, toks);
        bool ascii = true;
        for (int i = 0; i < n && ascii; ++i) ascii = (unsigned char)base[i] < 0x80;
        for (auto& t : toks) {
            int col = ascii ? t.start : colsBetween(ds_.editText, ls, ls + t.start);
            ed->AddText(fMono, lh, ImVec2(std::floor(origin.x + col * cw), std::floor(y)), Fade(t.col), base + t.start, base + t.start + t.len);
        }
    }
    if (active && std::fmod(ImGui::GetTime(), 1.1) < 0.65)
        ed->AddRectFilled(ImVec2(std::floor(origin.x + caretCol * cw), std::floor(origin.y + caretLine * lh)),
                          ImVec2(std::floor(origin.x + caretCol * cw) + std::max(1.5f, S(1.6f)), std::floor(origin.y + (caretLine + 1) * lh)), Fade(AccentBright()));
    ImGui::PopFont();
    ImGui::EndChild();

    // ---- status bar -------------------------------------------------------------------
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::Dummy(ImVec2(w, statusH));
        ImDrawList* sd = ImGui::GetWindowDrawList();
        ImU32 sc = ds_.editOk ? RGBA(col::green) : RGBA(col::red);
        float cy = p.y + statusH * 0.5f + S(2);
        DrawIcon(sd, ImVec2(p.x + S(10), cy), ds_.editOk ? icon::Check : icon::Warning, 14.f, sc);
        DrawStr(sd, fRegular, 13.f, ImVec2(p.x + S(28), cy - TextSize(fRegular, 13.f, "A").y * 0.5f), sc, ds_.editStatus.c_str());
        char info[96];
        snprintf(info, sizeof info, "Ln %d, Col %d   \xC2\xB7   %d lines   \xC2\xB7   UTF-8", caretLine + 1, caretCol + 1, lines);
        ImVec2 isz = TextSize(fRegular, 12.5f, info);
        DrawStr(sd, fRegular, 12.5f, ImVec2(p.x + w - isz.x - S(6), cy - isz.y * 0.5f), RGBA(col::mute), info);
    }
    EndCard();
}
