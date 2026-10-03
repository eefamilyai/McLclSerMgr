#pragma once
// Voxual UI toolkit: theme-aware implicit-immediate widgets drawn on Dear ImGui.
#include <string>

#include "imgui.h"
#include "theme.h"

namespace ui {

// ---- theme re-exports (so page code can keep writing ui::S / ui::FS / ui::Accent) ----
using theme::D;
using theme::DensityScale;
using theme::FS;
using theme::g_opt;
using theme::g_pal;
using theme::g_scale;
using theme::HS;
using theme::Mono;
using theme::RD;
using theme::S;
using theme::fBold;
using theme::fIcon;
using theme::fMono;
using theme::fRegular;

void LoadFonts();                 // initial font atlas build (from stored settings)
void SetupStyle();
void ApplyTheme(const theme::Options& opt);        // palette + metrics + fonts (between frames)
void ApplyThemeStyle(const theme::Options& opt);   // palette + metrics only (safe anywhere)
void ApplyThemeFonts(const theme::Options& opt);   // atlas only - between frames
bool ThemeFontsPending();

// ---- colours -----------------------------------------------------------------------
constexpr ImU32 RGBA(unsigned hex, int a = 255) { return IM_COL32((hex >> 16) & 255, (hex >> 8) & 255, hex & 255, a); }

// Live palette aliases: reading col::x always yields the active theme's colour.
namespace col {
extern unsigned& bg;
extern unsigned& bgAlt;
extern unsigned& sidebar;
extern unsigned& panel;
extern unsigned& panelHi;
extern unsigned& panelSoft;
extern unsigned& field;
extern unsigned& fieldHover;
extern unsigned& sunken;
extern unsigned& sunkenSoft;
extern unsigned& track;
extern unsigned& rowHover;
extern unsigned& rowActive;
extern unsigned& border;
extern unsigned& borderHi;
extern unsigned& borderSoft;
extern unsigned& text;
extern unsigned& textStrong;
extern unsigned& dim;
extern unsigned& mute;
extern unsigned& faint;
extern unsigned& onAccent;
extern unsigned& onDanger;
extern unsigned& scrollThumb;
extern unsigned& green;
extern unsigned& amber;
extern unsigned& red;
extern unsigned& blue;
extern unsigned& violet;
extern unsigned& teal;
extern unsigned& pink;
extern unsigned& orange;
extern unsigned& codeKey;
extern unsigned& codeStr;
extern unsigned& codeNum;
extern unsigned& codeKw;
extern unsigned& codeCom;
extern unsigned& codePunct;
extern unsigned& codeDef;
extern unsigned& codeSec;
}  // namespace col

ImU32 Fade(ImU32 c);                       // multiply by the current style alpha
ImU32 Mix(ImU32 a, ImU32 b, float t);
ImU32 Alpha(ImU32 c, float a);             // multiply the alpha channel
ImU32 Accent(float a = 1.f);
ImU32 AccentBright();
ImU32 OnAccent();
ImVec4 ToVec4(ImU32 c);

// ---- icons (Segoe Fluent Icons) --------------------------------------------
namespace icon {
inline constexpr const char* Play = "\uE768";
inline constexpr const char* Stop = "\uE71A";
inline constexpr const char* Restart = "\uE72C";
inline constexpr const char* Add = "\uE710";
inline constexpr const char* Settings = "\uE713";
inline constexpr const char* Trash = "\uE74D";
inline constexpr const char* FolderOpen = "\uE838";
inline constexpr const char* Back = "\uE72B";
inline constexpr const char* Home = "\uE80F";
inline constexpr const char* Console = "\uE756";
inline constexpr const char* People = "\uE716";
inline constexpr const char* Info = "\uE946";
inline constexpr const char* Copy = "\uE8C8";
inline constexpr const char* Download = "\uE896";
inline constexpr const char* Search = "\uE721";
inline constexpr const char* Save = "\uE74E";
inline constexpr const char* Edit = "\uE70F";
inline constexpr const char* Globe = "\uE774";
inline constexpr const char* More = "\uE712";
inline constexpr const char* Check = "\uE73E";
inline constexpr const char* Close = "\uE711";
inline constexpr const char* Warning = "\uE7BA";
inline constexpr const char* Package = "\uE7B8";
inline constexpr const char* History = "\uE81C";
inline constexpr const char* Bolt = "\uE945";
inline constexpr const char* ChevronRight = "\uE76C";
inline constexpr const char* ChevronDown = "\uE70D";
inline constexpr const char* ChevronLeft = "\uE76B";
inline constexpr const char* Link = "\uE71B";
inline constexpr const char* Send = "\uE725";
inline constexpr const char* Clear = "\uE894";
inline constexpr const char* Power = "\uE7E8";
inline constexpr const char* Clock = "\uE121";
inline constexpr const char* Sliders = "\uE9E9";
inline constexpr const char* Cpu = "\uE950";
inline constexpr const char* Upload = "\uE898";
inline constexpr const char* Java = "\uEA86";
inline constexpr const char* Shield = "\uEA18";
inline constexpr const char* Wifi = "\uE701";
inline constexpr const char* Pin = "\uE718";
inline constexpr const char* Paint = "\uE790";      // personalization
inline constexpr const char* Palette = "\uE790";
inline constexpr const char* Font = "\uE8D2";
inline constexpr const char* FontSize = "\uE8E9";
inline constexpr const char* Grid = "\uE80A";
inline constexpr const char* List = "\uEA37";
inline constexpr const char* Filter = "\uE71C";
inline constexpr const char* Sort = "\uE8CB";
inline constexpr const char* Star = "\uE734";
inline constexpr const char* StarFill = "\uE735";
inline constexpr const char* Image = "\uEB9F";
inline constexpr const char* World = "\uE909";
inline constexpr const char* Game = "\uE7FC";
inline constexpr const char* Lock = "\uE72E";
inline constexpr const char* Eye = "\uE7B3";
inline constexpr const char* Bell = "\uEA8F";
inline constexpr const char* Keyboard = "\uE765";
inline constexpr const char* Window = "\uE737";
inline constexpr const char* Cloud = "\uE753";
inline constexpr const char* Storage = "\uE7F1";  // hdd
inline constexpr const char* Refresh = "\uE72C";
inline constexpr const char* Zoom = "\uE71E";
inline constexpr const char* Star2 = "\uE113";
}  // namespace icon

// ---- drawing helpers ------------------------------------------------------------
float Anim(ImGuiID id, float target, float speed = 14.f);
void DrawStr(ImDrawList* dl, ImFont* f, float px, ImVec2 pos, ImU32 c, const char* text);
ImVec2 TextSize(ImFont* f, float px, const char* text);
void DrawIcon(ImDrawList* dl, ImVec2 center, const char* glyph, float px, ImU32 c);
void DrawCube(ImDrawList* dl, ImVec2 center, float radius, ImU32 color);
void DrawShadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, float strength = 1.f);
void DrawGlow(ImDrawList* dl, ImVec2 a, ImVec2 b);
void DrawTile(ImDrawList* dl, ImVec2 p, float size, unsigned rgb);   // rounded icon tile with a cube
void DrawSpinner(ImDrawList* dl, ImVec2 c, float r, float thickness, ImU32 color);
void DrawCheckCircle(ImDrawList* dl, ImVec2 c, float r, ImU32 fill, ImU32 fg);
void DrawAvatar(ImDrawList* dl, ImVec2 p, float size, const std::string& name, float rounding);
std::string FitText(const std::string& text, ImFont* font, float px, float maxW, bool fromStart = false);

// ---- widgets -----------------------------------------------------------------------
enum class Btn { Primary, Secondary, Danger, Ghost, Subtle };
bool Button(const char* label, const char* icon = nullptr, Btn v = Btn::Secondary, ImVec2 size = ImVec2(0, 0));
float ButtonWidth(const char* label, bool withIcon);
bool IconButton(const char* id, const char* icon, Btn v = Btn::Ghost, float size = 34.f, const char* tip = nullptr);
bool Toggle(const char* id, bool* v);
bool InputText(const char* id, std::string* s, const char* hint = nullptr, float width = -1, int flags = 0,
               ImGuiInputTextCallback cb = nullptr, void* userData = nullptr);
bool InputInt(const char* id, int* v, int lo, int hi, float width = 120);
bool Combo(const char* id, int* cur, const char* const* items, int n, float width = -1);
bool SliderInt(const char* id, int* v, int lo, int hi, const char* fmt = "%d", float width = -1);
bool SliderFloat(const char* id, float* v, float lo, float hi, const char* fmt = "%.2f", float width = -1);
void ProgressBar(float frac, ImVec2 size);   // frac < 0: indeterminate
bool Tabs(const char* id, const char* const* labels, const char* const* icons, int n, int* cur);
bool Segmented(const char* id, const char* const* labels, int n, int* cur, float width = -1);
void Badge(const char* text, ImU32 color, bool dot = false, float pulse = 0.f);
float BadgeWidth(const char* text, bool dot);
void Chip(const char* text, ImU32 color);
bool Link(const char* text);
void Tooltip(const char* text);
bool ColorSwatch(const char* id, unsigned rgb, bool selected, float size = 34.f);
bool SearchBox(const char* id, std::string* text, const char* hint, float width = -1);

void Label(const char* text, float px = 14.f, unsigned color = col::dim, bool bold = false);
void LabelWrapped(const char* text, float px = 14.f, unsigned color = col::dim);
void Heading(const char* text, float px = 26.f);
void SubHeading(const char* text);           // 17px semibold
void Caption(const char* text);              // 12.5px muted
void Gap(float px);
void Divider(float padY = 12.f);
void FieldLabel(const char* text, const char* help = nullptr);
void SectionTitle(const char* title, const char* subtitle = nullptr, const char* icon = nullptr);
void Hint(const char* text);                 // small info line with an icon

bool BeginCard(const char* id, ImVec2 size = ImVec2(0, 0), float pad = 20.f, unsigned bg = 0);
void EndCard();
bool BeginScroll(const char* id, ImVec2 size);   // thin-scrollbar child, no background
void EndScroll();
bool BeginModal(const char* id, float width);
void EndModal();
bool BeginCardMenu(const char* id);              // themed context menu (pair with EndCardMenu)
void EndCardMenu();

// ---- toasts -----------------------------------------------------------------------
enum class ToastKind { Info, Success, Warning, Error };
void Toast(const std::string& msg, ToastKind kind = ToastKind::Info);
void RenderToasts();
void SetToastPlacement(int corner, float seconds);   // 0 TR, 1 BR, 2 BL, 3 TL

}  // namespace ui
