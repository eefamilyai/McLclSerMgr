#include "ui.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "imgui_internal.h"
#include "imgui_stdlib.h"

namespace ui {

// Live aliases into the active palette: col::text is whatever the theme says it is.
namespace col {
unsigned &bg = theme::g_pal.bg;
unsigned &bgAlt = theme::g_pal.bgAlt;
unsigned &sidebar = theme::g_pal.sidebar;
unsigned &panel = theme::g_pal.panel;
unsigned &panelHi = theme::g_pal.panelHi;
unsigned &panelSoft = theme::g_pal.panelSoft;
unsigned &field = theme::g_pal.field;
unsigned &fieldHover = theme::g_pal.fieldHover;
unsigned &sunken = theme::g_pal.sunken;
unsigned &sunkenSoft = theme::g_pal.sunkenSoft;
unsigned &track = theme::g_pal.track;
unsigned &rowHover = theme::g_pal.rowHover;
unsigned &rowActive = theme::g_pal.rowActive;
unsigned &border = theme::g_pal.border;
unsigned &borderHi = theme::g_pal.borderHi;
unsigned &borderSoft = theme::g_pal.borderSoft;
unsigned &text = theme::g_pal.text;
unsigned &textStrong = theme::g_pal.textStrong;
unsigned &dim = theme::g_pal.dim;
unsigned &mute = theme::g_pal.mute;
unsigned &faint = theme::g_pal.faint;
unsigned &onAccent = theme::g_pal.onAccent;
unsigned &onDanger = theme::g_pal.onDanger;
unsigned &scrollThumb = theme::g_pal.scrollThumb;
unsigned &green = theme::g_pal.green;
unsigned &amber = theme::g_pal.amber;
unsigned &red = theme::g_pal.red;
unsigned &blue = theme::g_pal.blue;
unsigned &violet = theme::g_pal.violet;
unsigned &teal = theme::g_pal.teal;
unsigned &pink = theme::g_pal.pink;
unsigned &orange = theme::g_pal.orange;
unsigned &codeKey = theme::g_pal.codeKey;
unsigned &codeStr = theme::g_pal.codeStr;
unsigned &codeNum = theme::g_pal.codeNum;
unsigned &codeKw = theme::g_pal.codeKw;
unsigned &codeCom = theme::g_pal.codeCom;
unsigned &codePunct = theme::g_pal.codePunct;
unsigned &codeDef = theme::g_pal.codeDef;
unsigned &codeSec = theme::g_pal.codeSec;
}  // namespace col

void LoadFonts() { theme::LoadFonts(theme::g_opt); }
void SetupStyle() { theme::SetupStyle(); }
void ApplyTheme(const theme::Options &opt) { theme::Apply(opt); }
void ApplyThemeStyle(const theme::Options &opt) { theme::ApplyStyle(opt); }
void ApplyThemeFonts(const theme::Options &opt) { theme::LoadFonts(opt); }
bool ThemeFontsPending() { return theme::FontsPending(); }

static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// ---------------------------------------------------------------------------
// colour helpers
// ---------------------------------------------------------------------------
ImU32 Fade(ImU32 c) {
    float a = ImGui::GetStyle().Alpha;
    if (a >= 0.999f) return c;
    unsigned alpha = (unsigned)(((c >> IM_COL32_A_SHIFT) & 255) * a);
    return (c & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}

ImU32 Mix(ImU32 a, ImU32 b, float t) {
    t = clamp01(t);
    auto ch = [&](int shift) {
        float x = (float)((a >> shift) & 255), y = (float)((b >> shift) & 255);
        return (unsigned)(x + (y - x) * t + 0.5f);
    };
    return IM_COL32(ch(IM_COL32_R_SHIFT), ch(IM_COL32_G_SHIFT), ch(IM_COL32_B_SHIFT), ch(IM_COL32_A_SHIFT));
}

ImU32 Alpha(ImU32 c, float a) {
    unsigned alpha = (unsigned)(((c >> IM_COL32_A_SHIFT) & 255) * clamp01(a));
    return (c & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}

ImU32 Accent(float a) { return Alpha(RGBA(theme::AccentRgb()), a); }
ImU32 AccentBright() { return Mix(RGBA(theme::AccentRgb()), IM_COL32(255, 255, 255, 255), 0.20f); }
ImU32 OnAccent() { return RGBA(col::onAccent); }
ImVec4 ToVec4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

// ---------------------------------------------------------------------------
// drawing helpers
// ---------------------------------------------------------------------------
float Anim(ImGuiID id, float target, float speed) {
    float &v = *ImGui::GetStateStorage()->GetFloatRef(id, target);
    float a = theme::g_opt.animSpeed;
    if (a <= 0.001f) { v = target; return v; }
    float dt = ImGui::GetIO().DeltaTime;
    v += (target - v) * (1.f - std::exp(-speed * a * dt));
    if (std::fabs(target - v) < 0.002f) v = target;
    return v;
}

void DrawStr(ImDrawList *dl, ImFont *f, float px, ImVec2 pos, ImU32 c, const char *text) {
    dl->AddText(f, FS(px), ImVec2(std::floor(pos.x), std::floor(pos.y)), Fade(c), text);
}

ImVec2 TextSize(ImFont *f, float px, const char *text) { return f->CalcTextSizeA(FS(px), FLT_MAX, 0.f, text); }

void DrawIcon(ImDrawList *dl, ImVec2 center, const char *glyph, float px, ImU32 c) {
    ImVec2 sz = TextSize(fIcon, px, glyph);
    dl->AddText(fIcon, FS(px), ImVec2(std::floor(center.x - sz.x * 0.5f), std::floor(center.y - sz.y * 0.5f)), Fade(c), glyph);
}

void DrawCube(ImDrawList *dl, ImVec2 c, float r, ImU32 color) {
    const float k = 0.8660254f;
    ImVec2 top(c.x, c.y - r), ur(c.x + k * r, c.y - r * 0.5f), lr(c.x + k * r, c.y + r * 0.5f), bot(c.x, c.y + r),
        ll(c.x - k * r, c.y + r * 0.5f), ul(c.x - k * r, c.y - r * 0.5f);
    ImU32 light = Mix(color, IM_COL32(255, 255, 255, 255), 0.32f), mid = color, dark = Mix(color, IM_COL32(0, 0, 0, 255), 0.36f);
    ImVec2 t[4] = {top, ur, c, ul};
    ImVec2 l[4] = {ul, c, bot, ll};
    ImVec2 rr[4] = {c, ur, lr, bot};
    dl->AddConvexPolyFilled(t, 4, Fade(light));
    dl->AddConvexPolyFilled(l, 4, Fade(mid));
    dl->AddConvexPolyFilled(rr, 4, Fade(dark));
    ImU32 edge = Fade(Alpha(IM_COL32(0, 0, 0, 255), 0.22f));
    dl->AddLine(ul, c, edge, 1.f);
    dl->AddLine(ur, c, edge, 1.f);
    dl->AddLine(c, bot, edge, 1.f);
}

void DrawTile(ImDrawList *dl, ImVec2 p, float size, unsigned rgb) {
    ImU32 col = RGBA(rgb);
    ImVec2 q(p.x + size, p.y + size);
    float r = size * 0.28f;
    dl->AddRectFilled(p, q, Fade(Alpha(col, col::panel == 0xFFFFFF ? 0.16f : 0.14f)), r);
    dl->AddRect(p, q, Fade(Alpha(col, 0.30f)), r, 0, 1.f);
    DrawCube(dl, ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), size * 0.30f, col);
}

void DrawShadow(ImDrawList *dl, ImVec2 a, ImVec2 b, float rounding, float strength) {
    if (!theme::g_opt.shadows) return;
    strength *= theme::g_pal.shadow;
    if (strength <= 0.001f) return;
    for (int i = 0; i < 6; ++i) {
        float grow = S(2.f + i * 3.f);
        float al = (0.08f - i * 0.012f) * strength;
        if (al <= 0) break;
        dl->AddRectFilled(ImVec2(a.x - grow, a.y - grow * 0.4f + S(6)), ImVec2(b.x + grow, b.y + grow + S(6)),
                          Fade(IM_COL32(0, 0, 0, (int)(255 * al))), rounding + grow);
    }
}

void DrawGlow(ImDrawList *dl, ImVec2 a, ImVec2 b) { theme::DrawPageGlow(dl, a, b); }

void DrawSpinner(ImDrawList *dl, ImVec2 c, float r, float thickness, ImU32 color) {
    float t = (float)ImGui::GetTime() * (theme::g_opt.animSpeed > 0.001f ? 1.f : 0.f);
    dl->PathClear();
    dl->PathArcTo(c, r, t * 5.f, t * 5.f + 4.2f, 28);
    dl->PathStroke(Fade(color), 0, thickness);
}

void DrawCheckCircle(ImDrawList *dl, ImVec2 c, float r, ImU32 fill, ImU32 fg) {
    dl->AddCircleFilled(c, r, Fade(fill), 32);
    DrawIcon(dl, c, icon::Check, r * 1.05f, fg);
}

static ImU32 avatarColor(const std::string &name) {
    static const unsigned pal[] = {0x4F9DFF, 0xB66DFF, 0x2DD4BF, 0xFFA726, 0xF2683C, 0x7BC043, 0xFB7185, 0x38BDF8};
    unsigned h = 0;
    for (char c : name) h = h * 131 + (unsigned char)c;
    return RGBA(pal[h % 8]);
}

void DrawAvatar(ImDrawList *dl, ImVec2 p, float size, const std::string &name, float rounding) {
    ImU32 c = avatarColor(name);
    dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), Fade(c), rounding);
    char ini[3] = {0, 0, 0};
    size_t i = 0;
    for (char ch : name) {
        if (i >= 2) break;
        if (isalnum((unsigned char)ch)) ini[i++] = (char)toupper((unsigned char)ch);
    }
    if (i == 0) { ini[0] = '?'; i = 1; }
    ImVec2 ts = TextSize(fBold, size * 0.42f, ini);
    DrawStr(dl, fBold, size * 0.42f, ImVec2(p.x + (size - ts.x) * 0.5f, p.y + (size - ts.y) * 0.5f),
            Fade(Mix(c, IM_COL32(0, 0, 0, 255), 0.55f)), ini);
}

std::string FitText(const std::string &text, ImFont *font, float px, float maxW, bool fromStart) {
    if (maxW <= 0 || text.empty() || !font) return text;
    const float size = FS(px);
    if (font->CalcTextSizeA(size, FLT_MAX, 0.f, text.c_str()).x <= maxW) return text;
    const char *s = text.c_str();
    const char *end = s + text.size();
    const char *ell = "...";
    const float budget = maxW - font->CalcTextSizeA(size, FLT_MAX, 0.f, ell).x;
    if (budget <= 0) return ell;
    // Walk whole code points instead of shaving bytes: the old loop re-measured the entire
    // remaining string for every character it removed, and cut multi-byte characters in half.
    if (!fromStart) {
        float acc = 0;
        const char *p = s;
        while (p < end) {
            const char *next = p + 1;
            while (next < end && ((unsigned char)*next & 0xC0) == 0x80) ++next;
            float w = font->CalcTextSizeA(size, FLT_MAX, 0.f, p, next).x;
            if (acc + w > budget) break;
            acc += w;
            p = next;
        }
        return p > s ? text.substr(0, (size_t)(p - s)) + ell : ell;
    }
    float acc = 0;
    const char *p = end;
    while (p > s) {
        const char *prev = p - 1;
        while (prev > s && ((unsigned char)*prev & 0xC0) == 0x80) --prev;
        float w = font->CalcTextSizeA(size, FLT_MAX, 0.f, prev, p).x;
        if (acc + w > budget) break;
        acc += w;
        p = prev;
    }
    return p > s ? ell + std::string(p, end) : ell;
}

// ---------------------------------------------------------------------------
// text helpers
// ---------------------------------------------------------------------------
void Label(const char *text, float px, unsigned color, bool bold) {
    ImGui::PushFont(bold ? fBold : fRegular, FS(px));
    ImGui::PushStyleColor(ImGuiCol_Text, RGBA(color));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void LabelWrapped(const char *text, float px, unsigned color) {
    ImGui::PushFont(fRegular, FS(px));
    ImGui::PushStyleColor(ImGuiCol_Text, RGBA(color));
    ImGui::PushTextWrapPos(0.f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void Heading(const char *text, float px) { Label(text, px, col::text, true); }
void SubHeading(const char *text) { Label(text, 17.f, col::text, true); }
void Caption(const char *text) { Label(text, 12.5f, col::mute); }
void Gap(float px) { ImGui::Dummy(ImVec2(1, D(px))); }

void Divider(float padY) {
    ImGui::Dummy(ImVec2(1, D(padY)));
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, std::floor(p.y) + 0.5f), ImVec2(p.x + w, std::floor(p.y) + 0.5f), Fade(RGBA(col::border)), 1.f);
    ImGui::Dummy(ImVec2(1, D(padY)));
}

void FieldLabel(const char *text, const char *help) {
    Label(text, 13.f, col::dim, true);
    if (help && *help) {
        ImGui::SameLine(0, S(6));
        Label(help, 12.f, col::mute, false);
    }
    ImGui::Dummy(ImVec2(1, D(3)));
}

void SectionTitle(const char *title, const char *subtitle, const char *ic) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float size = S(38);
    if (ic) {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), Fade(Accent(0.14f)), RD(11));
        DrawIcon(dl, ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), ic, 17.f, Accent());
        ImGui::Dummy(ImVec2(size + S(12), size));
        ImGui::SameLine(0, 0);
    }
    ImGui::BeginGroup();
    if (subtitle && *subtitle) {
        Label(title, 16.5f, col::text, true);
        Label(subtitle, 12.5f, col::mute);
    } else {
        ImGui::Dummy(ImVec2(1, ic ? S(9) : 0));
        Label(title, 16.5f, col::text, true);
    }
    ImGui::EndGroup();
}

void Hint(const char *text) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::PushFont(fRegular, FS(13));
    ImGui::PushTextWrapPos(0.f);
    ImVec2 ts = ImGui::CalcTextSize(text, nullptr, false, w - S(24));
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    float h = std::max(S(24), ts.y + S(4));
    ImGui::Dummy(ImVec2(w, h));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    DrawIcon(dl, ImVec2(p.x + S(7), p.y + h * 0.5f), icon::Info, 13.f, RGBA(col::mute));
    dl->AddText(fRegular, FS(13), ImVec2(std::floor(p.x + S(22)), std::floor(p.y + (h - ts.y) * 0.5f)), Fade(RGBA(col::dim)), text, nullptr, w - S(24));
}

void Tooltip(const char *text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(D(10), D(7)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, RD(9));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, RGBA(col::panelSoft));
    ImGui::PushStyleColor(ImGuiCol_Border, RGBA(col::borderHi));
    if (ImGui::BeginTooltip()) {
        ImGui::PushFont(fRegular, FS(13));
        ImGui::PushStyleColor(ImGuiCol_Text, RGBA(col::text));
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::EndTooltip();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

// ---------------------------------------------------------------------------
// buttons
// ---------------------------------------------------------------------------
namespace {
bool itemDisabled() { return (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0; }

struct BtnColors {
    ImU32 bg, border, fg;
};
BtnColors btnColors(Btn v, float hover) {
    switch (v) {
    case Btn::Primary: {
        ImU32 base = RGBA(theme::AccentRgb());
        return {Mix(Mix(base, IM_COL32(0, 0, 0, 255), 0.10f), AccentBright(), hover * 0.55f), Alpha(base, 0.9f), RGBA(col::onAccent)};
    }
    case Btn::Danger:
        return {Mix(Alpha(RGBA(col::red), 0.14f), Alpha(RGBA(col::red), 0.30f), hover), Alpha(RGBA(col::red), 0.34f + hover * 0.22f),
                col::panel == 0xFFFFFF ? RGBA(0x8A1414) : Mix(RGBA(col::red), IM_COL32(255, 255, 255, 255), 0.25f)};
    case Btn::Ghost:
        return {Mix(Alpha(RGBA(col::panelHi), 0.0f), RGBA(col::rowHover), hover), 0, Mix(RGBA(col::dim), RGBA(col::text), hover)};
    case Btn::Subtle:
        return {Mix(Alpha(RGBA(col::panelHi), 0.0f), RGBA(col::rowHover), hover), 0, Mix(RGBA(col::mute), RGBA(col::text), hover)};
    case Btn::Secondary:
    default:
        return {Mix(RGBA(col::panelHi), RGBA(col::rowActive), hover), Mix(RGBA(col::borderHi), RGBA(col::text), hover * 0.35f), RGBA(col::text)};
    }
}

std::string visibleLabel(const char *l) {
    std::string s = l;
    auto p = s.find("##");
    return p == std::string::npos ? s : s.substr(0, p);
}
}  // namespace

float ButtonWidth(const char *label, bool withIcon) {
    std::string t = visibleLabel(label);
    return D(18) * 2 + (withIcon ? HS(16) + (t.empty() ? 0.f : D(8)) : 0.f) + (t.empty() ? 0.f : TextSize(fBold, 14.f, t.c_str()).x);
}

bool Button(const char *label, const char *ic, Btn v, ImVec2 size) {
    ImGui::PushID(label);
    std::string text = visibleLabel(label);
    const float fpx = 14.f;
    ImVec2 ts = text.empty() ? ImVec2(0, 0) : TextSize(fBold, fpx, text.c_str());
    float ip = ic ? HS(16) : 0.f, gap = (ic && !text.empty()) ? D(8) : 0.f;
    float w = size.x > 0 ? size.x : (size.x < 0 ? ImGui::GetContentRegionAvail().x : D(18) * 2 + ip + gap + ts.x);
    float h = size.y > 0 ? size.y : HS(38);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##btn", ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive(), disabled = itemDisabled();
    if (hovered && !disabled) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    float hv = Anim(ImGui::GetItemID() ^ 0x9e37, (hovered && !disabled) ? 1.f : 0.f, 18.f);
    BtnColors c = btnColors(v, hv);
    if (disabled) {   // a faded accent reads as "broken" - use a neutral surface instead
        c.bg = RGBA(col::panelHi);
        c.border = RGBA(col::border);
        c.fg = RGBA(col::mute);
    }
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 q(p.x + w, p.y + h);
    float r = RD(10);
    ImU32 bg = held ? Mix(c.bg, IM_COL32(0, 0, 0, 255), 0.14f) : c.bg;
    if (v == Btn::Primary && hv > 0.01f && !disabled)
        dl->AddRectFilled(ImVec2(p.x - S(1), p.y - S(1)), ImVec2(q.x + S(1), q.y + S(1)), Fade(Accent(0.20f * hv)), r + S(2));
    dl->AddRectFilled(p, q, Fade(bg), r);
    if ((c.border >> IM_COL32_A_SHIFT) > 0) dl->AddRect(p, q, Fade(c.border), r, 0, 1.f);
    float total = ip + gap + ts.x;
    float x = p.x + (w - total) * 0.5f, ny = held ? S(1) : 0.f;
    ImU32 fg = c.fg;
    if (ic) {
        DrawIcon(dl, ImVec2(x + ip * 0.5f, p.y + h * 0.5f + ny), ic, 15.f, fg);
        x += ip + gap;
    }
    if (!text.empty()) DrawStr(dl, fBold, fpx, ImVec2(std::floor(x), std::floor(p.y + (h - ts.y) * 0.5f + ny)), fg, text.c_str());
    ImGui::PopID();
    return clicked;
}

bool IconButton(const char *id, const char *ic, Btn v, float size, const char *tip) {
    ImGui::PushID(id);
    float sz = HS(size);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##ib", ImVec2(sz, sz));
    bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive(), disabled = itemDisabled();
    if (hovered && !disabled) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    float hv = Anim(ImGui::GetItemID() ^ 0x7131, (hovered && !disabled) ? 1.f : 0.f, 18.f);
    BtnColors c = btnColors(v, hv);
    if (disabled) {
        c.bg = RGBA(col::panelHi);
        c.border = RGBA(col::border);
        c.fg = RGBA(col::mute);
    }
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 q(p.x + sz, p.y + sz);
    ImU32 bg = held ? Mix(c.bg, IM_COL32(0, 0, 0, 255), 0.14f) : c.bg;
    dl->AddRectFilled(p, q, Fade(bg), RD(10));
    if ((c.border >> IM_COL32_A_SHIFT) > 0) dl->AddRect(p, q, Fade(c.border), RD(10), 0, 1.f);
    if (ImGui::IsItemFocused()) dl->AddRect(p, q, Fade(Accent(0.7f)), RD(10), 0, 1.5f);
    DrawIcon(dl, ImVec2(p.x + sz * 0.5f, p.y + sz * 0.5f + (held ? S(1) : 0)), ic, size * 0.46f, c.fg);
    if (tip) Tooltip(tip);
    ImGui::PopID();
    return clicked;
}

bool Link(const char *text) {
    ImVec2 ts = TextSize(fRegular, 13.f, text);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(text);
    bool clicked = ImGui::InvisibleButton("##lnk", ImVec2(ts.x, ts.y));
    bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImU32 c = hovered ? AccentBright() : Accent();
    DrawStr(dl, fRegular, 13.f, p, c, text);
    if (hovered) dl->AddLine(ImVec2(p.x, p.y + ts.y), ImVec2(p.x + ts.x, p.y + ts.y), Fade(c), 1.f);
    return clicked;
}

// ---------------------------------------------------------------------------
// toggle / inputs
// ---------------------------------------------------------------------------
bool Toggle(const char *id, bool *v) {
    float w = HS(44), h = HS(25);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    bool clicked = ImGui::InvisibleButton("##tg", ImVec2(w, h));
    bool disabled = itemDisabled();
    if (clicked && !disabled) *v = !*v;
    if (ImGui::IsItemHovered() && !disabled) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    float t = Anim(ImGui::GetItemID() ^ 0x55aa, *v ? 1.f : 0.f, 16.f);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImU32 off = RGBA(col::track), on = Accent();
    if (disabled) { off = RGBA(col::panelHi); on = RGBA(col::borderHi); }
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Fade(Mix(off, on, t)), h * 0.5f);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), Fade(Mix(RGBA(col::borderHi), Alpha(on, 0.f), t)), h * 0.5f, 0, 1.f);
    if (ImGui::IsItemFocused()) dl->AddRect(ImVec2(p.x - S(2), p.y - S(2)), ImVec2(p.x + w + S(2), p.y + h + S(2)), Fade(Accent(0.65f)), h * 0.5f + S(2), 0, 1.5f);
    float r = h * 0.5f - S(3.2f);
    float cx = p.x + h * 0.5f + (w - h) * t;
    // The previous first line added 0x202020 to a packed colour - which carries between
    // channels - and was overwritten by both branches below anyway.
    const ImU32 knobOff = col::panel == 0xFFFFFF ? IM_COL32(0xFF, 0xFF, 0xFF, 255) : IM_COL32(0xA7, 0xB0, 0xC4, 255);
    ImU32 knob = Mix(knobOff, RGBA(col::onAccent), t);
    dl->AddCircleFilled(ImVec2(cx, p.y + h * 0.5f), r, Fade(knob), 24);
    ImGui::PopID();
    return clicked;
}

static void pushFieldStyle(float padX) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(D(padX), D(10)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, RD(10));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, RGBA(col::field));
    ImGui::PushStyleColor(ImGuiCol_Border, RGBA(col::borderHi));
}

static void popFieldStyle() {
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

static void focusRing() {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    if (ImGui::IsItemActive() || ImGui::IsItemFocused())
        dl->AddRect(a, b, Fade(Accent(0.9f)), RD(10), 0, 1.5f);
    else if (ImGui::IsItemHovered())
        dl->AddRect(a, b, Fade(RGBA(col::borderHi)), RD(10), 0, 1.f);
}

bool InputText(const char *id, std::string *s, const char *hint, float width, int flags, ImGuiInputTextCallback cb, void *ud, float padX) {
    pushFieldStyle(padX);
    ImGui::PushFont(fRegular, FS(14.5f));
    // width == -1: full width. width < -1: leave |width| px free on the right (ImGui's idiom).
    ImGui::SetNextItemWidth(width < -1 ? width : (width < 0 ? ImGui::GetContentRegionAvail().x : S(width)));
    bool r = hint ? ImGui::InputTextWithHint(id, hint, s, flags, cb, ud) : ImGui::InputText(id, s, flags, cb, ud);
    focusRing();
    ImGui::PopFont();
    popFieldStyle();
    return r;
}

// width == -1: full width. width < -1: flexible, leaving |width| px free on the right.
bool SearchBox(const char *id, std::string *text, const char *hint, float width) {
    ImGui::PushID(id);
    float h = HS(38);
    float reserve = text->empty() ? 0.f : h + S(2);
    // The padding has to reach the field itself: pushing it here did nothing because InputText
    // pushed its own afterwards, which left the search icon sitting on top of the placeholder.
    bool changed = InputText("##q", text, hint, width < -1 ? width + reserve : (width < 0 ? -1 : width), 0, nullptr, nullptr, 34.f);
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    DrawIcon(dl, ImVec2(a.x + D(18), (a.y + b.y) * 0.5f), icon::Search, 15.f, RGBA(col::mute));
    bool clear = false;
    if (!text->empty()) {
        ImGui::SetCursorScreenPos(ImVec2(b.x - h + S(2), a.y - S(1)));
        if (IconButton("clr", icon::Close, Btn::Ghost, h - S(2), "Clear")) {
            text->clear();
            clear = true;
        }
    }
    // The clear button is positioned by hand, so restore the layout cursor under the field:
    // it used to stay on the button's row and the next widget could overlap the box.
    ImGui::SetCursorScreenPos(ImVec2(a.x, b.y));
    ImGui::PopID();
    return changed || clear;
}

bool InputInt(const char *id, int *v, int lo, int hi, float width) {
    pushFieldStyle(12.f);
    ImGui::PushFont(fRegular, FS(14.5f));
    ImGui::SetNextItemWidth(S(width));
    bool r = ImGui::InputInt(id, v, 0, 0);
    focusRing();
    if (r) *v = std::clamp(*v, lo, hi);
    if (ImGui::IsItemDeactivatedAfterEdit()) *v = std::clamp(*v, lo, hi);
    ImGui::PopFont();
    popFieldStyle();
    return r;
}

bool Combo(const char *id, int *cur, const char *const *items, int n, float width) {
    pushFieldStyle(12.f);
    ImGui::PushFont(fRegular, FS(14.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, RD(12));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(D(6), D(6)));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, RGBA(col::panelSoft));
    ImGui::PushStyleColor(ImGuiCol_Button, RGBA(col::field));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, RGBA(col::panelHi));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, RGBA(col::panelHi));
    ImGui::SetNextItemWidth(width < 0 ? ImGui::GetContentRegionAvail().x : S(width));
    bool changed = false;
    if (ImGui::BeginCombo(id, (*cur >= 0 && *cur < n) ? items[*cur] : "")) {
        for (int i = 0; i < n; ++i) {
            bool sel = i == *cur;
            if (sel) ImGui::PushStyleColor(ImGuiCol_Text, Accent());
            if (ImGui::Selectable(items[i], sel, 0, ImVec2(0, HS(26)))) { *cur = i; changed = true; }
            if (sel) { ImGui::PopStyleColor(); ImGui::SetItemDefaultFocus(); }
        }
        ImGui::EndCombo();
    }
    focusRing();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
    ImGui::PopFont();
    popFieldStyle();
    return changed;
}

static void pushSliderStyle() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(D(10), D(8)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, RD(10));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, RD(8));
    ImGui::PushStyleColor(ImGuiCol_Border, RGBA(col::borderHi));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, RGBA(col::field));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, RGBA(col::fieldHover));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, RGBA(col::fieldHover));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, Accent());
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, AccentBright());
}

static void popSliderStyle() {
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(4);
}

bool SliderInt(const char *id, int *v, int lo, int hi, const char *fmt, float width) {
    pushSliderStyle();
    ImGui::PushFont(fRegular, FS(14));
    ImGui::SetNextItemWidth(width < 0 ? ImGui::GetContentRegionAvail().x : S(width));
    bool r = ImGui::SliderInt(id, v, lo, hi, fmt, ImGuiSliderFlags_AlwaysClamp);
    ImGui::PopFont();
    popSliderStyle();
    return r;
}

bool SliderFloat(const char *id, float *v, float lo, float hi, const char *fmt, float width) {
    pushSliderStyle();
    ImGui::PushFont(fRegular, FS(14));
    ImGui::SetNextItemWidth(width < 0 ? ImGui::GetContentRegionAvail().x : S(width));
    bool r = ImGui::SliderFloat(id, v, lo, hi, fmt, ImGuiSliderFlags_AlwaysClamp);
    ImGui::PopFont();
    popSliderStyle();
    return r;
}

// ---------------------------------------------------------------------------
// progress / badges / tabs
// ---------------------------------------------------------------------------
void ProgressBar(float frac, ImVec2 size) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = size.x > 0 ? size.x : ImGui::GetContentRegionAvail().x, h = size.y > 0 ? size.y : S(8);
    ImGui::Dummy(ImVec2(w, h));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Fade(RGBA(col::track)), h * 0.5f);
    double t = ImGui::GetTime();
    if (frac < 0) {
        float seg = w * 0.32f;
        float x = (float)std::fmod(t * 0.9, 1.0) * (w + seg) - seg;
        float a = std::max(0.f, x), b = std::min(w, x + seg);
        if (b > a) dl->AddRectFilled(ImVec2(p.x + a, p.y), ImVec2(p.x + b, p.y + h), Fade(Accent()), h * 0.5f);
    } else {
        float fw = std::max(h, w * clamp01(frac));
        dl->AddRectFilled(p, ImVec2(p.x + fw, p.y + h), Fade(Accent()), h * 0.5f);
        float sx = (float)std::fmod(t * 0.8, 1.4) * fw - fw * 0.2f;
        dl->PushClipRect(p, ImVec2(p.x + fw, p.y + h), true);
        dl->AddRectFilledMultiColor(ImVec2(p.x + sx, p.y), ImVec2(p.x + sx + S(60), p.y + h), Fade(Alpha(IM_COL32_WHITE, 0.f)),
                                    Fade(Alpha(IM_COL32_WHITE, 0.26f)), Fade(Alpha(IM_COL32_WHITE, 0.26f)), Fade(Alpha(IM_COL32_WHITE, 0.f)));
        dl->PopClipRect();
    }
}

float BadgeWidth(const char *text, bool dot) { return D(12) * 2 + (dot ? D(14) : 0.f) + TextSize(fBold, 12.5f, text).x; }

void Badge(const char *text, ImU32 color, bool dot, float pulse) {
    float px = 12.5f;
    ImVec2 ts = TextSize(fBold, px, text);
    float dotW = dot ? D(14) : 0.f;
    float w = D(12) * 2 + dotW + ts.x, h = HS(26);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Fade(Alpha(color, 0.14f)), h * 0.5f);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), Fade(Alpha(color, 0.32f)), h * 0.5f, 0, 1.f);
    float x = p.x + D(12);
    if (dot) {
        ImVec2 c(x + S(3.5f), p.y + h * 0.5f);
        if (pulse > 0.f) {
            float t = (float)std::fmod(ImGui::GetTime() * 1.2, 1.0);
            dl->AddCircleFilled(c, S(3.5f) + t * S(6), Fade(Alpha(color, 0.35f * (1.f - t))), 20);
        }
        dl->AddCircleFilled(c, S(3.5f), Fade(color), 16);
        x += dotW;
    }
    DrawStr(dl, fBold, px, ImVec2(x, p.y + (h - ts.y) * 0.5f), color, text);
}

void Chip(const char *text, ImU32 color) {
    float px = 11.5f;
    ImVec2 ts = TextSize(fBold, px, text);
    float w = ts.x + D(16), h = HS(22);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Fade(Alpha(color, 0.13f)), RD(7));
    DrawStr(dl, fBold, px, ImVec2(p.x + D(8), p.y + (h - ts.y) * 0.5f), color, text);
}

bool Tabs(const char *id, const char *const *labels, const char *const *icons, int n, int *cur) {
    if (n <= 0 || !cur || !labels) return false;
    // widths[*cur] below indexes a vector, so a caller holding a stale tab index would read
    // past the end of it. Clamp once, up front, and hand the corrected index back.
    *cur = std::clamp(*cur, 0, n - 1);
    ImGui::PushID(id);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float h = HS(44), pad = D(4), gap = S(2);
    std::vector<float> widths(n);
    float total = pad * 2;
    for (int i = 0; i < n; ++i) {
        widths[i] = D(15) * 2 + TextSize(fBold, 13.5f, labels[i]).x + (icons && icons[i] ? D(23) : 0);
        total += widths[i] + (i + 1 < n ? gap : 0);
    }
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float availW = ImGui::GetContentRegionAvail().x;
    if (total > availW) {
        float scale = (availW - pad * 2 - gap * (n - 1)) / std::max(1.f, total - pad * 2 - gap * (n - 1));
        for (auto &w : widths) w *= scale;
        total = availW;
    }
    dl->AddRectFilled(origin, ImVec2(origin.x + total, origin.y + h), Fade(RGBA(col::sunkenSoft)), RD(13));
    dl->AddRect(origin, ImVec2(origin.x + total, origin.y + h), Fade(RGBA(col::border)), RD(13), 0, 1.f);

    float tx = origin.x + pad;
    std::vector<float> xs(n);
    for (int i = 0; i < n; ++i) { xs[i] = tx; tx += widths[i] + gap; }
    ImGuiID pid = ImGui::GetID("pill");
    float px = Anim(pid, xs[*cur], 18.f), pw = Anim(pid + 1, widths[*cur], 18.f);
    dl->AddRectFilled(ImVec2(px, origin.y + pad), ImVec2(px + pw, origin.y + h - pad), Fade(RGBA(col::panelHi)), RD(10));
    dl->AddRect(ImVec2(px, origin.y + pad), ImVec2(px + pw, origin.y + h - pad), Fade(Accent(0.38f)), RD(10), 0, 1.f);

    bool changed = false;
    for (int i = 0; i < n; ++i) {
        ImGui::SetCursorScreenPos(ImVec2(xs[i], origin.y + pad));
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##tab", ImVec2(widths[i], h - pad * 2)) && *cur != i) { *cur = i; changed = true; }
        bool hov = ImGui::IsItemHovered();
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        float a = Anim(ImGui::GetItemID() ^ 0x1234, (*cur == i) ? 1.f : (hov ? 0.6f : 0.f), 16.f);
        ImU32 c = Mix(RGBA(col::dim), RGBA(col::text), a);
        ImVec2 ts = TextSize(fBold, 13.5f, labels[i]);
        float iw = (icons && icons[i]) ? D(23) : 0.f;
        float cx = xs[i] + (widths[i] - ts.x - iw) * 0.5f, cy = origin.y + h * 0.5f;
        if (iw > 0) {
            DrawIcon(dl, ImVec2(cx + S(7), cy), icons[i], 14.f, (*cur == i) ? Accent() : c);
            cx += iw;
        }
        DrawStr(dl, fBold, 13.5f, ImVec2(cx, cy - ts.y * 0.5f), c, labels[i]);
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + h));
    ImGui::Dummy(ImVec2(total, 0));
    ImGui::PopID();
    return changed;
}

bool Segmented(const char *id, const char *const *labels, int n, int *cur, float width) {
    if (n <= 0 || !cur || !labels) return false;
    *cur = std::clamp(*cur, 0, n - 1);
    ImGui::PushID(id);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float h = HS(34), pad = D(3);
    float avail = width < 0 ? ImGui::GetContentRegionAvail().x : S(width);
    float segW = (avail - pad * 2) / (float)n;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(origin, ImVec2(origin.x + avail, origin.y + h), Fade(RGBA(col::sunkenSoft)), RD(10));
    dl->AddRect(origin, ImVec2(origin.x + avail, origin.y + h), Fade(RGBA(col::border)), RD(10), 0, 1.f);
    ImGuiID pid = ImGui::GetID("seg");
    float px = Anim(pid, origin.x + pad + segW * (*cur), 20.f);
    float pw = Anim(pid + 1, segW, 20.f);
    dl->AddRectFilled(ImVec2(px, origin.y + pad), ImVec2(px + pw, origin.y + h - pad), Fade(RGBA(col::panelHi)), RD(8));
    dl->AddRect(ImVec2(px, origin.y + pad), ImVec2(px + pw, origin.y + h - pad), Fade(Accent(0.35f)), RD(8), 0, 1.f);
    bool changed = false;
    for (int i = 0; i < n; ++i) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + pad + segW * i, origin.y + pad));
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##s", ImVec2(segW, h - pad * 2)) && *cur != i) { *cur = i; changed = true; }
        bool hov = ImGui::IsItemHovered();
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        float a = Anim(ImGui::GetItemID() ^ 0x77aa, (*cur == i) ? 1.f : (hov ? 0.6f : 0.f), 16.f);
        ImVec2 ts = TextSize(fBold, 13.f, labels[i]);
        DrawStr(dl, fBold, 13.f, ImVec2(origin.x + pad + segW * i + (segW - ts.x) * 0.5f, origin.y + (h - ts.y) * 0.5f),
                Mix(RGBA(col::dim), RGBA(col::text), a), labels[i]);
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + h));
    ImGui::Dummy(ImVec2(avail, 0));
    ImGui::PopID();
    return changed;
}

bool ColorSwatch(const char *id, unsigned rgb, bool selected, float size) {
    float d = HS(size);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    bool clicked = ImGui::InvisibleButton("##sw", ImVec2(d, d));
    bool hov = ImGui::IsItemHovered();
    ImGui::PopID();
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + d * 0.5f, p.y + d * 0.5f);
    float a = Anim(ImGui::GetItemID() ^ 0x31ab, selected ? 1.f : (hov ? 0.5f : 0.f), 16.f);
    if (a > 0.01f) dl->AddCircle(c, d * 0.5f + S(2) * a, Fade(Alpha(RGBA(rgb), 0.2f + 0.7f * a)), 40, S(2.f));
    dl->AddCircleFilled(c, d * 0.5f - S(5), Fade(RGBA(rgb)), 40);
    if (selected) DrawIcon(dl, c, icon::Check, d * 0.34f, Fade(Mix(RGBA(rgb), IM_COL32(0, 0, 0, 255), 0.72f)));
    return clicked;
}

// ---------------------------------------------------------------------------
// containers
// ---------------------------------------------------------------------------
bool BeginCard(const char *id, ImVec2 size, float pad, unsigned bg) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, RGBA(bg ? bg : col::panel));
    ImGui::PushStyleColor(ImGuiCol_Border, RGBA(col::border));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, RD(16));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(D(pad), D(pad)));
    ImGuiChildFlags cf = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
    if (size.y == 0) cf |= ImGuiChildFlags_AutoResizeY;
    return ImGui::BeginChild(id, size, cf, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

void EndCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

bool BeginScroll(const char *id, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    return ImGui::BeginChild(id, size, ImGuiChildFlags_None, ImGuiWindowFlags_None);
}

void EndScroll() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

bool BeginModal(const char *id, float width) {
    ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(S(width), 0), ImVec2(S(width), FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, RD(18));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(D(26), D(24)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, RGBA(col::panelSoft));
    ImGui::PushStyleColor(ImGuiCol_Border, RGBA(col::borderHi));
    bool open = ImGui::BeginPopupModal(id, nullptr,
                                       ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                                           ImGuiWindowFlags_NoSavedSettings);
    if (!open) {
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }
    return open;
}

void EndModal() {
    ImGui::EndPopup();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

bool BeginCardMenu(const char *id) {
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, RD(12));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(D(6), D(6)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(D(6), D(4)));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, RGBA(col::panelSoft));
    ImGui::PushStyleColor(ImGuiCol_Border, RGBA(col::borderHi));
    if (!ImGui::BeginPopup(id)) {
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
        return false;
    }
    return true;
}

// Always pair with BeginCardMenu: the pushes above must be undone whether or not the popup opened.
void EndCardMenu() {
    ImGui::EndPopup();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

// ---------------------------------------------------------------------------
// toasts
// ---------------------------------------------------------------------------
namespace {
struct ToastItem {
    std::string msg;
    ToastKind kind;
    double born;
};
std::vector<ToastItem> g_toasts;
int g_toastCorner = 1;      // 0 TR, 1 BR, 2 BL, 3 TL
float g_toastLife = 4.2f;
}  // namespace

void SetToastPlacement(int corner, float seconds) {
    g_toastCorner = std::clamp(corner, 0, 3);
    g_toastLife = std::clamp(seconds, 1.5f, 12.f);
}

void Toast(const std::string &msg, ToastKind kind) {
    g_toasts.push_back({msg, kind, ImGui::GetTime()});
    if (g_toasts.size() > 5) g_toasts.erase(g_toasts.begin());
}

void RenderToasts() {
    const double life = (double)g_toastLife;
    double now = ImGui::GetTime();
    g_toasts.erase(std::remove_if(g_toasts.begin(), g_toasts.end(), [&](const ToastItem &t) { return now - t.born > life; }), g_toasts.end());
    if (g_toasts.empty()) return;
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    ImVec2 vs = ImGui::GetIO().DisplaySize;
    bool left = g_toastCorner >= 2;
    bool top = g_toastCorner == 0 || g_toastCorner == 3;
    float y = top ? S(24) : vs.y - S(24);
    for (int i = (int)g_toasts.size() - 1; i >= 0; --i) {
        auto &t = g_toasts[i];
        float age = (float)(now - t.born);
        float in = clamp01(age / 0.25f), out = clamp01((float)(life - (now - t.born)) / 0.35f);
        float a = std::min(in, out);
        float ease = 1.f - std::pow(1.f - in, 3.f);
        unsigned accentRgb = t.kind == ToastKind::Success ? col::green : t.kind == ToastKind::Warning ? col::amber : t.kind == ToastKind::Error ? col::red : col::blue;
        const char *ic = t.kind == ToastKind::Success ? icon::Check : t.kind == ToastKind::Warning ? icon::Warning : t.kind == ToastKind::Error ? icon::Close : icon::Info;
        ImVec2 ts = TextSize(fRegular, 14.f, t.msg.c_str());
        float w = ts.x + D(24) * 2 + D(30), h = HS(52);
        float slide = (1.f - ease) * S(40);
        float x = left ? S(24) - slide : vs.x - S(24) - w + slide;
        float y0 = top ? y : y - h;
        ImVec2 p(x, y0), q(x + w, y0 + h);
        auto A = [&](ImU32 c) { return Alpha(c, a); };
        if (theme::g_opt.shadows && theme::g_pal.shadow > 0.f)
            for (int k = 0; k < 5; ++k) {
                float g = S(2.f + k * 4.f);
                dl->AddRectFilled(ImVec2(p.x - g, p.y - g + S(6)), ImVec2(q.x + g, q.y + g + S(6)), A(IM_COL32(0, 0, 0, 34 - k * 6)), RD(14) + g);
            }
        dl->AddRectFilled(p, q, A(RGBA(col::panelSoft)), RD(14));
        dl->AddRect(p, q, A(Alpha(RGBA(accentRgb), 0.45f)), RD(14), 0, 1.f);
        dl->AddCircleFilled(ImVec2(p.x + D(26), p.y + h * 0.5f), S(14), A(Alpha(RGBA(accentRgb), 0.18f)), 24);
        ImVec2 isz = TextSize(fIcon, 14.f, ic);
        dl->AddText(fIcon, FS(14), ImVec2(p.x + D(26) - isz.x * 0.5f, p.y + h * 0.5f - isz.y * 0.5f), A(RGBA(accentRgb)), ic);
        dl->AddText(fRegular, FS(14), ImVec2(p.x + D(50), p.y + (h - ts.y) * 0.5f), A(RGBA(col::text)), t.msg.c_str());
        y += top ? (h + S(10)) : -(h + S(10));
    }
}

}  // namespace ui
