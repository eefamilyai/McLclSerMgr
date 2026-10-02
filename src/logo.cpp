#include "logo.h"

#include <algorithm>
#include <cmath>

namespace logo {

namespace {

struct V3 {
    float r, g, b;
};

V3 hex(unsigned h) { return {((h >> 16) & 255) / 255.f, ((h >> 8) & 255) / 255.f, (h & 255) / 255.f}; }
V3 mix(V3 a, V3 b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

bool inPoly(const float (*p)[2], int n, float x, float y) {
    bool in = false;
    for (int i = 0, j = n - 1; i < n; j = i++)
        if (((p[i][1] > y) != (p[j][1] > y)) && (x < (p[j][0] - p[i][0]) * (y - p[i][1]) / (p[j][1] - p[i][1]) + p[i][0])) in = !in;
    return in;
}

}  // namespace

std::vector<uint32_t> render(int size) {
    std::vector<uint32_t> px((size_t)size * size, 0);
    const int SS = size >= 256 ? 3 : 4;
    const float S = (float)size, c = S * 0.5f;
    const float margin = S * 0.035f, rad = S * 0.225f;

    // cube geometry
    const float r = S * 0.285f, k = 0.8660254f, cy = c + S * 0.012f;
    float top[4][2] = {{c, cy - r}, {c + k * r, cy - r * .5f}, {c, cy}, {c - k * r, cy - r * .5f}};
    float left[4][2] = {{c - k * r, cy - r * .5f}, {c, cy}, {c, cy + r}, {c - k * r, cy + r * .5f}};
    float right[4][2] = {{c, cy}, {c + k * r, cy - r * .5f}, {c + k * r, cy + r * .5f}, {c, cy + r}};

    const V3 bgTop = hex(0x153A2E), bgBot = hex(0x06110D), glow = hex(0x34D399), rim = hex(0x3FA67F);
    const V3 faceTopA = hex(0xC4FFE6), faceTopB = hex(0x6FEFBC), faceLeft = hex(0x34D399), faceRight = hex(0x168A62);

    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            float sr = 0, sg = 0, sb = 0, sa = 0;
            for (int sy = 0; sy < SS; ++sy)
                for (int sx = 0; sx < SS; ++sx) {
                    float fx = x + (sx + 0.5f) / SS, fy = y + (sy + 0.5f) / SS;
                    float dx = std::max(std::fabs(fx - c) - (c - margin - rad), 0.f), dy = std::max(std::fabs(fy - c) - (c - margin - rad), 0.f);
                    float d = std::sqrt(dx * dx + dy * dy) - rad;  // < 0 inside the tile
                    if (d > 0) continue;
                    // background: vertical gradient + soft emerald glow behind the cube
                    float t = fy / S;
                    V3 col = mix(bgTop, bgBot, t);
                    float gd = std::sqrt((fx - c) * (fx - c) + (fy - cy) * (fy - cy)) / (S * 0.52f);
                    col = mix(col, glow, 0.30f * (1.f - clamp01(gd)) * (1.f - clamp01(gd)));
                    // inner rim light (stronger at the top)
                    float rimT = clamp01(1.f + d / (S * 0.022f));
                    col = mix(col, rim, rimT * (0.55f - 0.35f * t));
                    // cube
                    if (inPoly(top, 4, fx, fy)) {
                        float u = clamp01((fy - (cy - r)) / (r * 0.9f));
                        col = mix(faceTopA, faceTopB, u);
                    } else if (inPoly(left, 4, fx, fy)) {
                        col = mix(faceLeft, mix(faceLeft, hex(0x1FA678), 1.f), clamp01((fy - cy) / r));
                    } else if (inPoly(right, 4, fx, fy)) {
                        col = faceRight;
                    }
                    sr += col.r; sg += col.g; sb += col.b; sa += 1.f;
                }
            if (sa <= 0) continue;
            float n = (float)(SS * SS);
            auto ch = [&](float v) { return (uint32_t)std::min(255.f, v / sa * 255.f + 0.5f); };
            px[(size_t)y * size + x] = ((uint32_t)(sa / n * 255.f + 0.5f) << 24) | (ch(sr) << 16) | (ch(sg) << 8) | ch(sb);
        }
    return px;
}

static HICON fromPixels(int size) {
    std::vector<uint32_t> px = render(size);
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof bi;
    bi.bV5Width = size;
    bi.bV5Height = -size;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    HDC dc = GetDC(nullptr);
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    memcpy(bits, px.data(), px.size() * 4);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii{TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    ReleaseDC(nullptr, dc);
    return icon;
}

HICON load(HINSTANCE inst, int pixelSize) {
    HICON h = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, pixelSize, pixelSize, LR_DEFAULTCOLOR);
    return h ? h : fromPixels(pixelSize);
}

}  // namespace logo
