#pragma once
#include <cstdint>
#include <vector>

#include <windows.h>

namespace logo {

// Renders the Voxual logo (isometric emerald cube on a dark rounded tile).
// Returns size*size pixels, 0xAARRGGBB, straight (non-premultiplied) alpha.
std::vector<uint32_t> render(int size);

// Window/taskbar icon. Prefers the icon embedded in the exe, falls back to a rendered one.
HICON load(HINSTANCE inst, int pixelSize);

}  // namespace logo
