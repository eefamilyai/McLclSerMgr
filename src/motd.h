#pragma once
// Minecraft MOTD helpers: colour codes and a multiplayer-list preview.
#include <string>
#include <vector>

#include "imgui.h"

namespace motd {

struct ColorCode {
    char code;
    unsigned rgb;
    const char* name;
};
const ColorCode* colors();
int colorCount();

struct Run {
    std::string text;
    ImU32 color;
    bool bold = false, italic = false, underline = false;
};

// Splits a MOTD into coloured runs; understands both the section sign and '&' codes.
std::vector<Run> parse(const std::string& raw);
// '&a' input -> real section-sign codes, and back again.
std::string toSectionCodes(const std::string& in);
std::string fromSectionCodes(const std::string& in);

// Draws a Minecraft-style multiplayer list entry (icon, name, status, MOTD).
void drawListEntry(ImDrawList* dl, ImVec2 p, float width, float height, unsigned tileRgb, const std::string& name,
                   const std::string& version, const std::string& rawMotd);

}  // namespace motd
