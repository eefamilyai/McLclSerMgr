#include "motd.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>

#include "ui.h"

namespace motd {

namespace {
const ColorCode kColors[] = {
    {'0', 0x000000, "Black"},        {'1', 0x0000AA, "Dark blue"}, {'2', 0x00AA00, "Dark green"}, {'3', 0x00AAAA, "Dark aqua"},
    {'4', 0xAA0000, "Dark red"},     {'5', 0xAA00AA, "Dark purple"}, {'6', 0xFFAA00, "Gold"},      {'7', 0xAAAAAA, "Gray"},
    {'8', 0x555555, "Dark gray"},    {'9', 0x5555FF, "Blue"},      {'a', 0x55FF55, "Green"},     {'b', 0x55FFFF, "Aqua"},
    {'c', 0xFF5555, "Red"},          {'d', 0xFF55FF, "Pink"},      {'e', 0xFFFF55, "Yellow"},    {'f', 0xFFFFFF, "White"}};
}  // namespace

const ColorCode* colors() { return kColors; }
int colorCount() { return (int)(sizeof kColors / sizeof kColors[0]); }

std::vector<Run> parse(const std::string& raw) {
    std::vector<Run> runs;
    Run cur;
    cur.text.clear();
    auto flush = [&] {
        if (!cur.text.empty()) runs.push_back(cur);
        cur.text.clear();
    };
    for (size_t i = 0; i < raw.size(); ++i) {
        unsigned char c = (unsigned char)raw[i];
        bool section = (c == 0xC2 && i + 2 < raw.size() && (unsigned char)raw[i + 1] == 0xA7);
        bool amp = c == '&';
        if ((section || amp) && i + (section ? 2 : 1) < raw.size()) {
            size_t at = i + (section ? 2 : 1);
            char code = (char)tolower((unsigned char)raw[at]);
            bool handled = false;
            for (auto& m : kColors)
                if (m.code == code) {
                    flush();
                    cur.color = ui::RGBA(m.rgb);
                    cur.bold = cur.italic = cur.underline = false;
                    handled = true;
                    break;
                }
            if (!handled) {
                if (code == 'l') { flush(); cur.bold = true; handled = true; }
                else if (code == 'o') { flush(); cur.italic = true; handled = true; }
                else if (code == 'n') { flush(); cur.underline = true; handled = true; }
                else if (code == 'r') {
                    flush();
                    cur.color = ui::RGBA(0xAAAAAA);
                    cur.bold = cur.italic = cur.underline = false;
                    handled = true;
                }
            }
            if (handled) {
                i = at;
                continue;
            }
        }
        cur.text += (char)c;
    }
    flush();
    if (runs.empty()) runs.push_back(cur);
    return runs;
}

// Only the codes Minecraft understands. Converting any letter turned ordinary text such as
// "Tom & Jerry" into a section sign followed by a stray character.
static bool isColorCode(char c) {
    const char l = (char)tolower((unsigned char)c);
    return (l >= '0' && l <= '9') || (l >= 'a' && l <= 'f') || l == 'k' || l == 'l' || l == 'm' || l == 'n' || l == 'o' || l == 'r';
}

std::string toSectionCodes(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '&' && i + 1 < in.size() && isColorCode(in[i + 1])) {
            out += "\xC2\xA7";
            out += (char)tolower((unsigned char)in[i + 1]);
            ++i;
        } else {
            out += in[i];
        }
    }
    return out;
}

std::string fromSectionCodes(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if ((unsigned char)in[i] == 0xC2 && i + 2 < in.size() && (unsigned char)in[i + 1] == 0xA7 && isColorCode(in[i + 2])) {
            out += '&';
            out += in[i + 2];
            i += 2;
        } else {
            out += in[i];
        }
    }
    return out;
}

void drawListEntry(ImDrawList* dl, ImVec2 p, float width, float height, unsigned tileRgb, const std::string& name, const std::string& version,
                   const std::string& rawMotd) {
    using namespace ui;
    ImVec2 q(p.x + width, p.y + height);
    dl->AddRectFilled(p, q, Fade(RGBA(col::sunken)), RD(12));
    dl->AddRect(p, q, Fade(RGBA(col::border)), RD(12), 0, 1.f);
    float tile = HS(46);
    DrawTile(dl, ImVec2(p.x + D(16), p.y + D(14)), tile, tileRgb);
    float tx = p.x + D(16) + tile + S(13);
    DrawStr(dl, fBold, 15.f, ImVec2(tx, p.y + D(14) + S(1)), RGBA(col::text), name.c_str());
    DrawStr(dl, fRegular, 12.f, ImVec2(tx, p.y + D(14) + S(24)), RGBA(col::green), version.c_str());
    auto runs = parse(rawMotd.empty() ? std::string("A Minecraft Server") : rawMotd);
    float y = p.y + D(14) + tile + S(9);
    float x = p.x + D(18);
    float maxX = q.x - D(18);
    const float lineH = TextSize(fRegular, 14.f, "A").y;
    const float glyphSize = FS(14.f);
    for (auto& r : runs) {
        if (x >= maxX) break;
        ImFont* font = r.bold ? fBold : fRegular;
        const char* s = r.text.c_str();
        const char* end = s + r.text.size();
        ImVec2 ts = TextSize(font, 14.f, s);
        if (x + ts.x <= maxX) {
            DrawStr(dl, font, 14.f, ImVec2(x, y), r.color, s);
        } else {
            // Walk whole code points until the run stops fitting. Measuring the remaining text
            // again for every trimmed character was quadratic, and trimming one byte at a time
            // could split a multi-byte character.
            const float budget = maxX - x;
            const char* p = s;
            float acc = 0;
            while (p < end) {
                const char* next = p + 1;
                while (next < end && ((unsigned char)*next & 0xC0) == 0x80) ++next;
                const float w = font->CalcTextSizeA(glyphSize, FLT_MAX, 0.f, p, next).x;
                if (acc + w > budget) break;
                acc += w;
                p = next;
            }
            if (p == s) break;   // none of this run fits on the remaining line
            ts = ImVec2(acc, ts.y);
            dl->AddText(font, glyphSize, ImVec2(x, std::floor(y)), Fade(r.color), s, p);
        }
        if (r.underline) dl->AddLine(ImVec2(x, y + lineH), ImVec2(x + ts.x, y + lineH), Fade(r.color), 1.f);
        x += ts.x;
    }
}

}  // namespace motd
