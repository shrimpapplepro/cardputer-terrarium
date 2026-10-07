#include "scene.h"

#include <math.h>

using namespace terra;

// A fish-tank terrarium seen through the front glass: grow-light bar on top,
// drainage / charcoal / soil layers, leaf litter, driftwood and a stone, and the
// plants and bugs people actually keep in sealed tanks (moss, ferns, fittonia,
// pilea, peperomia, creeping fig, baby tears, selaginella; springtails, isopods,
// fungus gnats, aphids...).
namespace scene {
namespace {

M5Canvas* C;
float gDim = 1.0f;  // ambient brightness: the room and tank dim when the grow light is off

inline int clampi(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
inline uint16_t rgb(int r, int g, int b) { return C->color565(clampi(r * gDim), clampi(g * gDim), clampi(b * gDim)); }
inline uint16_t raw(int r, int g, int b) { return C->color565(clampi(r), clampi(g), clampi(b)); }
inline float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
inline float frand(uint32_t seed) { return (hash32(seed) & 0xFFFF) / 65535.0f; }
struct RGB { float r, g, b; };
inline uint16_t mixc(RGB a, RGB b, float t) {
    return rgb((int)(a.r + (b.r - a.r) * t), (int)(a.g + (b.g - a.g) * t), (int)(a.b + (b.b - a.b) * t));
}

// ---- layout ---------------------------------------------------------------

enum Kind : uint8_t { K_FIG, K_MOUND, K_FERN, K_FITT, K_PILEA, K_SELAG, K_BABY, K_MAT, K_PEPER };
const uint8_t kKind[kCols] = {K_FIG,  K_MOUND, K_FERN, K_FITT, K_PILEA, K_SELAG, K_BABY, K_MAT,
                              K_FITT, K_FERN,  K_MOUND, K_PEPER, K_BABY, K_FERN, K_SELAG, K_FIG};
//                              fig   mound   fern    fitt    pilea   selag   baby    mat
const float kScale[9] = {1.00f, 0.24f, 1.05f, 0.40f, 0.95f, 0.38f, 0.24f, 0.12f, 0.75f};

int stemX(int col) { return kJarL + 8 + col * 14 + 4; }

constexpr int kSoilBot = 117;  // soil ends, charcoal begins

// Driftwood: a branch lying on the soil, left of centre.
const int kWoodX0 = 46, kWoodX1 = 104;
int woodTop(int x) {  // surface y of the driftwood at x, or soil top if none
    if (x < kWoodX0 || x > kWoodX1) return kSoilTop;
    float u = (float)(x - kWoodX0) / (kWoodX1 - kWoodX0);
    return kSoilTop - 4 - (int)(9.0f * sinf(3.14159f * u) + 3.0f * sinf(u * 9.0f) * u);
}
// Stone on the right.
const int kStoneX = 178, kStoneW = 15, kStoneH = 13;
int surfaceY(int x) {
    int y = woodTop(x);
    if (x > kStoneX - kStoneW && x < kStoneX + kStoneW) {
        float u = (float)(x - kStoneX) / kStoneW;
        int sy = kSoilTop - (int)(kStoneH * sqrtf(fmaxf(0.0f, 1.0f - u * u))) + 3;
        if (sy < y) y = sy;
    }
    return y;
}

// ---- backdrop, substrate, hardscape ----------------------------------------

void drawBackdrop() {
    // Soft sage-grey background panel behind the plants.
    for (int b = 0; b < 8; b++) {
        int y0 = kJarT + (kSoilTop - kJarT) * b / 8, y1 = kJarT + (kSoilTop - kJarT) * (b + 1) / 8;
        C->fillRect(kJarL, y0, kJarR - kJarL, y1 - y0, mixc(RGB{150, 176, 168}, RGB{198, 214, 196}, b / 7.0f));
    }
    for (int i = 0; i < 90; i++)
        C->drawPixel(kJarL + 1 + hash32(i) % 216, kJarT + 5 + hash32(i + 500) % 88, rgb(132 + i % 3 * 14, 158 + i % 3 * 12, 150 + i % 3 * 10));
}

void drawSubstrate(const World& w, float t) {
    float wet = w.soil;
    // soil (topsoil, darker when wet)
    C->fillRect(kJarL, kSoilTop, kJarR - kJarL, kSoilBot - kSoilTop + 1, mixc(RGB{120, 84, 56}, RGB{58, 38, 26}, wet));
    for (int i = 0; i < 110; i++) {
        int v = 20 + (i % 4) * 14;
        C->drawPixel(kJarL + 2 + hash32(i * 3) % 215, kSoilTop + 1 + hash32(i * 3 + 1) % (kSoilBot - kSoilTop - 1), rgb(v + 45, v + 28, v + 16));
    }
    // bits of perlite / bark in the soil
    for (int i = 0; i < 22; i++)
        C->drawPixel(kJarL + 3 + hash32(i + 700) % 213, kSoilTop + 3 + hash32(i + 740) % (kSoilBot - kSoilTop - 4), rgb(230, 228, 215));
    // activated charcoal band
    C->fillRect(kJarL, kSoilBot + 1, kJarR - kJarL, 3, rgb(26, 28, 32));
    for (int i = 0; i < 30; i++) C->drawPixel(kJarL + hash32(i + 60) % 218, kSoilBot + 1 + i % 3, rgb(70, 74, 80));
    // mesh barrier
    for (int x = kJarL; x < kJarR; x += 2) C->drawPixel(x, kSoilBot + 4, rgb(150, 160, 170));
    // LECA / drainage layer with a little water at the bottom
    int gy = kSoilBot + 5;
    C->fillRect(kJarL, gy, kJarR - kJarL, kJarB - gy, rgb(150, 92, 55));
    for (int i = 0; i < 60; i++) {
        int x = kJarL + 2 + hash32(i + 200) % 216, y = gy + 2 + hash32(i + 260) % (kJarB - gy - 3);
        C->fillCircle(x, y, 2, (i & 1) ? rgb(196, 122, 70) : rgb(170, 104, 60));
        C->drawPixel(x - 1, y - 1, rgb(226, 160, 106));
    }
    C->fillRect(kJarL, kJarB - 2, kJarR - kJarL, 2, mixc(RGB{90, 130, 160}, RGB{120, 170, 200}, wet));  // false-bottom water
    (void)t;
}

void drawWood(const World& w) {
    uint16_t bark = rgb(112, 78, 50), dark = rgb(66, 44, 30), lite = rgb(160, 118, 78);
    for (int x = kWoodX0; x <= kWoodX1; x++) {
        int top = woodTop(x);
        float u = (float)(x - kWoodX0) / (kWoodX1 - kWoodX0);
        int th = 3 + (int)(3.0f * sinf(3.14159f * u));       // thicker in the middle
        C->drawFastVLine(x, top, th, bark);
        C->drawPixel(x, top, lite);
        C->drawPixel(x, top + th - 1, dark);
        if ((x & 3) == 0) C->drawPixel(x, top + 1 + (x % 3), dark);
    }
    // both ends rest on the soil
    C->fillRect(kWoodX0 - 1, woodTop(kWoodX0), 4, kSoilTop - woodTop(kWoodX0) + 2, bark);
    C->fillRect(kWoodX1 - 3, woodTop(kWoodX1), 4, kSoilTop - woodTop(kWoodX1) + 2, bark);
    // side twigs
    int t1 = kWoodX0 + 14, t2 = kWoodX0 + 38;
    C->drawLine(t1, woodTop(t1), t1 - 4, woodTop(t1) - 8, bark);
    C->drawLine(t1 - 4, woodTop(t1) - 8, t1 - 7, woodTop(t1) - 10, dark);
    C->drawLine(t2, woodTop(t2), t2 + 5, woodTop(t2) - 9, bark);
    // moss on the wood
    for (int x = kWoodX0 + 6; x < kWoodX0 + 22; x++) { C->drawPixel(x, woodTop(x) - 1, rgb(84 + x % 3 * 12, 152, 66)); C->drawPixel(x, woodTop(x), rgb(70, 132, 58)); }
    // white mold fuzz follows the fungus population
    int n = (int)roundf(w.fungus * 16);
    for (int k = 0; k < n && k < 5; k++) {
        int x = kWoodX0 + 24 + k * 7;
        int y = woodTop(x);
        C->fillEllipse(x, y, 3, 1, rgb(240, 240, 232));
        C->drawPixel(x - 1, y - 1, rgb(252, 252, 246));
    }
}

void drawStone() {
    int cy = kSoilTop - kStoneH / 2 + 3;
    C->fillEllipse(kStoneX + 2, cy + 2, kStoneW, kStoneH / 2 + 1, rgb(50, 54, 60));
    C->fillEllipse(kStoneX, cy, kStoneW, kStoneH / 2 + 1, rgb(112, 116, 124));
    C->fillEllipse(kStoneX - 5, cy - 3, kStoneW / 2, kStoneH / 4, rgb(150, 154, 162));
    C->drawLine(kStoneX - 2, cy - 4, kStoneX + 5, cy + 1, rgb(72, 76, 84));
    C->drawLine(kStoneX + 5, cy - 3, kStoneX + 9, cy + 1, rgb(80, 84, 92));
    for (int i = 0; i < 12; i++) C->drawPixel(kStoneX + 4 + i, cy - kStoneH / 2 + 3 + (i & 1), rgb(84, 150, 70));  // moss cap
}

void drawLitter(const World& w) {
    int n = (int)roundf(w.det * 34);
    if (n > 10) n = 10;
    static const uint8_t lc[3][3] = {{140, 86, 42}, {176, 118, 56}, {104, 64, 34}};
    for (int k = 0; k < n; k++) {
        int x = kJarL + 8 + hash32(k + 1200) % 205;
        int y = surfaceY(x) - 1;
        if (surfaceY(x) != kSoilTop) continue;
        const uint8_t* c = lc[k % 3];
        C->fillEllipse(x, y, 3, 1, rgb(c[0], c[1], c[2]));
        C->drawFastHLine(x - 2, y, 4, rgb(c[0] / 2, c[1] / 2, c[2] / 2));
    }
}

// ---- plants ------------------------------------------------------------------

void dome(int x, int y, int r, int h, uint16_t c1, uint16_t c2, uint32_t seed) {
    for (int dy = 0; dy < h; dy++) {
        float f = 1.0f - (float)(dy * dy) / (float)(h * h);
        int wd = (int)(r * sqrtf(f > 0 ? f : 0));
        C->drawFastHLine(x - wd, y - dy, 2 * wd + 1, (dy & 1) ? c1 : c2);
    }
    for (int i = 0; i < r + h; i++) {
        int dx = (int)(frand(seed + i) * 2 * r) - r, dy = (int)(frand(seed * 3 + i) * h);
        C->drawPixel(x + dx, y - dy, (i & 1) ? rgb(150, 205, 100) : rgb(52, 110, 50));
    }
}

void bezier(float x0, float y0, float cx, float cy, float x1, float y1, float u, int& px, int& py) {
    float a = (1 - u) * (1 - u), b = 2 * (1 - u) * u, c = u * u;
    px = (int)(a * x0 + b * cx + c * x1);
    py = (int)(a * y0 + b * cy + c * y1);
}

void sprout(int x, int y) {
    C->drawLine(x, y, x, y - 3, rgb(90, 160, 70));
    C->fillEllipse(x - 2, y - 4, 2, 1, rgb(120, 200, 90));
    C->fillEllipse(x + 2, y - 5, 2, 1, rgb(120, 200, 90));
}

void heart(int x, int y, uint16_t c) {
    C->drawPixel(x - 1, y, c); C->drawPixel(x + 1, y, c); C->drawFastHLine(x - 1, y + 1, 3, c); C->drawPixel(x, y + 2, c);
}

// ---- roots ---------------------------------------------------------------------
// Seen through the glass, each plant type has its own root form, and every
// plant's branching comes from its own seed, so no two root systems match.

// One root: five jittered segments that bend down (gravitropism) and may fork.
void rootLine(float x, float y, float ang, float len, int depth, uint16_t c, uint32_t seed) {
    float seg = len / 5.0f;
    for (int i = 0; i < 5; i++) {
        ang = ang * 0.85f + (frand(seed + i * 17) - 0.5f) * 0.7f;
        float nx = x + sinf(ang) * seg, ny = y + cosf(ang) * seg;
        if (ny > kSoilBot - 1) ny = kSoilBot - 1;
        C->drawLine((int)x, (int)y, (int)nx, (int)ny, c);
        if (depth > 0 && i < 4 && frand(seed * 7 + i) < 0.45f) {
            float side = frand(seed * 13 + i) < 0.5f ? -1.0f : 1.0f;
            rootLine(nx, ny, ang + side * (0.6f + 0.5f * frand(seed + i * 5)), len * (0.35f + 0.2f * frand(seed * 3 + i)), depth - 1, c,
                     hash32(seed + i * 101));
        }
        x = nx; y = ny;
    }
}

void roots(int col, int k, int x0, int y0, float p) {
    uint32_t sd = hash32(col * 131 + 7);
    float g = fminf(1.0f, 0.3f + p * 1.2f);  // root mass follows the plant
    switch (k) {
        case K_MAT:
        case K_MOUND: {  // moss has no true roots: a fringe of short brown rhizoids
            int wd = k == K_MAT ? 6 + (int)(p * 14) : 4 + (int)(p * 9);
            for (int x = x0 - wd + 1; x < x0 + wd; x += 1 + (int)(frand(sd + x) * 2.0f))
                C->drawFastVLine(x, y0 + 1, 1 + (int)(frand(sd * 3 + x) * 3.0f * g), rgb(104, 76, 48));
            break;
        }
        case K_BABY: {  // a dense, shallow mat of fine white roots
            int r = 4 + (int)(p * 8);
            for (int i = 0; i < 2 + r / 3; i++) {
                float fx = x0 - r + frand(sd + i) * 2 * r;
                rootLine(fx, y0 + 1, (frand(sd * 5 + i) - 0.5f) * 1.6f, (3.0f + frand(sd * 9 + i) * 4.0f) * g, 0, rgb(222, 212, 190), sd + i * 31);
            }
            break;
        }
        case K_FERN: {  // a creeping rhizome just under the surface, hung with wiry dark roots
            int rw = 3 + (int)(6 * g);
            uint16_t rh = rgb(140, 96, 58);
            C->drawFastHLine(x0 - rw, y0 + 2, 2 * rw + 1, rh);
            C->drawFastHLine(x0 - rw + 1, y0 + 3, 2 * rw - 1, rgb(104, 70, 42));
            for (int i = 0; i < 3 + (int)(p * 4); i++) {
                float fx = x0 - rw + frand(sd + i * 3) * 2 * rw;
                rootLine(fx, y0 + 3, (frand(sd * 7 + i) - 0.5f) * 0.9f, (5.0f + frand(sd * 11 + i) * 8.0f) * g, 1, rgb(40, 28, 22), sd + i * 53);
            }
            break;
        }
        case K_FITT:  // fibrous and spreading, pale
            for (int i = 0; i < 5; i++)
                rootLine(x0, y0 + 1, -1.2f + 2.4f * i / 4.0f + (frand(sd + i) - 0.5f) * 0.4f, (5.0f + frand(sd * 3 + i) * 7.0f) * g, 1,
                         rgb(208, 186, 150), sd + i * 71);
            break;
        case K_PILEA:  // a compact white root ball
            for (int i = 0; i < 5; i++)
                rootLine(x0, y0 + 1, -0.9f + 1.8f * i / 4.0f, (3.0f + frand(sd * 3 + i) * 5.0f) * g, 1, rgb(230, 220, 198), sd + i * 37);
            break;
        case K_PEPER:  // a few fine, shallow, reddish roots that run sideways
            for (int i = 0; i < 4; i++) {
                float side = (i & 1) ? 1.0f : -1.0f;
                rootLine(x0, y0 + 1, side * (0.9f + 0.4f * frand(sd + i)), (6.0f + frand(sd * 5 + i) * 7.0f) * g, 1, rgb(196, 140, 124), sd + i * 43);
            }
            break;
        case K_SELAG: {  // rhizophores: pale props that drop from the stems into the soil
            uint16_t rc = rgb(196, 214, 182);
            for (int i = 0; i < 3; i++) {
                int dx = (i - 1) * (3 + (int)(p * 4)) + (int)(frand(sd + i) * 3.0f) - 1;
                C->drawLine(x0 + dx / 2, y0 - 3, x0 + dx, y0, rc);
                rootLine(x0 + dx, y0 + 1, dx * 0.08f, (4.0f + frand(sd * 3 + i) * 5.0f) * g, 1, rc, sd + i * 59);
            }
            break;
        }
        case K_FIG: {  // a woody, branching main root that goes deep
            uint16_t rc = rgb(156, 112, 70);
            rootLine(x0, y0 + 1, (frand(sd) - 0.5f) * 0.5f, 18.0f * g, 3, rc, sd);
            rootLine(x0 + 1, y0 + 1, (frand(sd) - 0.5f) * 0.5f, 10.0f * g, 1, rc, sd);  // doubled near the crown: thicker
            break;
        }
    }
}

void plant(int col, const World& w, float t, float wind) {
    int k = kKind[col];
    float p = w.p[col];
    int eff = plantHeight(w, col);
    int x0 = stemX(col), y0 = surfaceY(x0) < kSoilTop - 2 ? kSoilTop : kSoilTop;
    roots(col, k, x0, y0, p);

    float sway = sinf(t * 0.7f + col * 0.9f) * (0.2f + wind * 3.0f) * (0.4f + eff / 40.0f);
    uint16_t dg = rgb(44, 112, 58), mg = rgb(70, 150, 72), lg = rgb(120, 196, 96);

    switch (k) {
        case K_MAT: {
            int wd = 6 + (int)(p * 14), hh = 1 + (int)(p * 3);
            dome(x0, y0, wd, hh + 1, mg, dg, col * 17);
            break;
        }
        case K_MOUND: {
            dome(x0, y0, 4 + (int)(p * 9), 3 + (int)(p * 8), rgb(96, 170, 74), rgb(66, 134, 58), col * 29);
            break;
        }
        case K_BABY: {
            int r = 4 + (int)(p * 8), hh = 3 + (int)(p * 7);
            dome(x0, y0, r, hh, rgb(150, 212, 90), rgb(96, 170, 70), col * 31);
            for (int i = 0; i < r + hh; i++) C->drawPixel(x0 - r + (int)(frand(col + i) * 2 * r), y0 - (int)(frand(col * 5 + i) * hh), rgb(200, 240, 130));
            break;
        }
        case K_SELAG: {
            int n = 6 + (int)(p * 6);
            for (int f = 0; f < n; f++) {
                float a = -1.25f + 2.5f * f / (n - 1) + sway * 0.03f;
                int len = 4 + (int)(eff * (0.7f + 0.3f * frand(col * 7 + f)));
                int ex = x0 + (int)(sinf(a) * len * 1.3f), ey = y0 - (int)(cosf(a) * len);
                C->drawLine(x0, y0, ex, ey, (f & 1) ? rgb(70, 158, 140) : rgb(96, 190, 160));
                for (int s = 2; s < len; s += 2)
                    C->drawLine(x0 + (ex - x0) * s / len, y0 + (ey - y0) * s / len, x0 + (ex - x0) * s / len + ((s & 2) ? 2 : -2), y0 + (ey - y0) * s / len + 1, rgb(140, 214, 178));
            }
            break;
        }
        case K_FERN: {
            if (eff < 6) { sprout(x0, y0); break; }
            bool lemon = (col % 2) == 1;
            uint16_t fa = lemon ? rgb(150, 200, 70) : rgb(48, 128, 62), fb = lemon ? rgb(196, 226, 96) : rgb(96, 180, 86);
            int n = 5 + (int)(p * 4);
            for (int f = 0; f < n; f++) {
                float dir = (f - (n - 1) * 0.5f) / (n * 0.5f);
                float len = eff * (0.9f + 0.3f * frand(col * 9 + f));
                for (int s = 1; s <= 9; s++) {
                    int px, py;
                    bezier(x0, y0, x0 + dir * len * 0.4f, y0 - len * 1.1f, x0 + dir * len * 1.0f + sway, y0 - len * 0.5f, s / 9.0f, px, py);
                    C->drawPixel(px, py, fa);
                    C->drawLine(px, py, px - 2, py + 2, (s & 1) ? fb : fa);
                    C->drawLine(px, py, px + 2, py + 2, (s & 1) ? fa : fb);
                }
            }
            break;
        }
        case K_FITT: {
            if (eff < 4) { sprout(x0, y0); break; }
            int variant = col % 3;  // pink / white / red veins
            static const uint8_t vein[3][3] = {{250, 150, 190}, {240, 248, 240}, {210, 60, 70}};
            static const uint8_t leaf[3][3] = {{48, 118, 60}, {44, 108, 62}, {40, 84, 50}};
            int n = 5 + (int)(p * 4);
            for (int f = 0; f < n; f++) {
                float a = -1.35f + 2.7f * f / (n - 1);
                float lr = 3 + eff * (0.6f + 0.8f * frand(col * 3 + f));
                int lx = x0 + (int)(sinf(a) * lr * 1.4f), ly = y0 - 2 - (int)(cosf(a) * lr * 0.55f) - (f & 1) * 2;
                int lw = 4 + (int)(p * 3.0f), lh = 3 + (int)(p * 2.0f);
                C->drawLine(x0, y0, lx, ly, rgb(120, 170, 100));
                C->fillEllipse(lx, ly, lw, lh, rgb(leaf[variant][0], leaf[variant][1], leaf[variant][2]));
                uint16_t vc = rgb(vein[variant][0], vein[variant][1], vein[variant][2]);
                // fittonia's signature: a fine net of veins (midrib + cross veins)
                C->drawFastHLine(lx - lw + 1, ly, 2 * lw - 1, vc);
                for (int vx = lx - lw + 2; vx < lx + lw - 1; vx += 2) C->drawFastVLine(vx, ly - lh + 1, 2 * lh - 1, vc);
            }
            break;
        }
        case K_PILEA:
        case K_PEPER: {
            if (eff < 5) { sprout(x0, y0); break; }
            bool pep = (k == K_PEPER);
            int tx = x0 + (int)sway, ty = y0 - eff;
            C->drawLine(x0, y0, tx, ty, pep ? rgb(150, 90, 90) : rgb(120, 170, 100));
            for (int y = 5, i = 0; y <= eff; y += 6, i++) {
                int px = x0 + (int)(sway * y / (float)eff) + ((i & 1) ? 3 : -3);
                int r = 2 + (int)(p * 2);
                C->fillCircle(px, y0 - y, r, pep ? rgb(56, 110, 70) : rgb(76, 156, 84));
                if (pep) C->drawFastHLine(px - r + 1, y0 - y, 2 * r - 1, rgb(160, 200, 176));
                else C->drawPixel(px - 1, y0 - y - 1, rgb(160, 220, 140));
            }
            C->fillCircle(tx, ty, 2 + (int)(p * 2), pep ? rgb(56, 110, 70) : rgb(90, 172, 92));
            C->drawPixel(tx - 1, ty - 1, rgb(190, 230, 170));
            break;
        }
        case K_FIG: {
            // Creeping fig climbs the glass at the edge of the tank.
            bool left = col < kCols / 2;
            int gx = left ? kJarL + 3 : kJarR - 3;
            int reach = eff > 88 ? 88 : eff;
            uint16_t vine = rgb(96, 84, 52), la = rgb(52, 124, 62), lb = rgb(104, 180, 84);
            int lastx = x0;
            for (int y = 0; y <= reach; y += 2) {
                float u = (float)y / (reach > 0 ? reach : 1);
                int px = (int)(x0 + (gx - x0) * fminf(1.0f, u * 2.2f) + sinf(y * 0.35f + col) * 1.5f * (1 - u * 0.5f));
                C->drawLine(lastx, y0 - y + 2, px, y0 - y, vine);
                lastx = px;
                if ((y & 3) == 0 && y > 6) heart(px + ((y & 7) ? 2 : -2), y0 - y - 1, (y & 4) ? la : lb);
            }
            if (reach > 20)  // a tendril reaching across the top
                for (int i = 0; i < 12; i++) { int px = gx + (left ? i * 2 : -i * 2); C->drawPixel(px, y0 - reach + (i >> 2), vine); if (i % 3 == 0) heart(px, y0 - reach + 2, lb); }
            break;
        }
    }
}

// ---- creatures -----------------------------------------------------------------
// Each visible creature is a small persistent agent with a behaviour state,
// stepped by real frame time: isopods wander and pause, springtails spring,
// gnats flit and land, ladybugs climb stems and fly between plants, and the
// jumping spider stalks and pounces. The sim decides HOW MANY are shown; the
// agents only decide what those few are doing right now. Nothing here feeds
// back into the sim (a "caught" gnat just re-enters elsewhere).

uint32_t gR = 0x9E3779B9u;
float gLastT = -1, gJoltHold = 0, gDt = 0;  // frame clock, startle refractory
float rnd() { gR ^= gR << 13; gR ^= gR >> 17; gR ^= gR << 5; return (gR & 0xFFFFFF) / 16777216.0f; }
float rr(float a, float b) { return a + (b - a) * rnd(); }
inline int ir(float v) { return (int)lroundf(v); }
inline float approach(float v, float target, float rate, float dt) { return v + (target - v) * fminf(1.0f, rate * dt); }

constexpr int kXL = kJarL + 6, kXR = kJarR - 6;  // walkable span
constexpr float kG = 150.0f;                     // px/s^2, for hops and jumps

struct Bug {
    float x, y, vx, vy, timer, phase;
    uint8_t st;
    int8_t dir;  // facing: -1 left / up, +1 right / down
    int8_t col;  // ladybug: plant column; spider: prey (-1 none, 0.. gnat, 16.. springtail)
    bool on;
};

enum { ISO_WALK, ISO_PAUSE, ISO_ROLL };
enum { ST_SIT, ST_HOP, ST_GLASS };
enum { GN_FLY, GN_LAND };
enum { LB_CLIMB, LB_PAUSE, LB_FLY, LB_GROUND };
enum { JS_SIT, JS_STALK, JS_CROUCH, JS_JUMP, JS_EAT };

constexpr int kMaxIso = 6, kMaxSpring = 14, kMaxGnat = 6, kMaxLady = 3, kMaxSpider = 2;
Bug iso[kMaxIso], spr[kMaxSpring], gnat[kMaxGnat], lady[kMaxLady], spi[kMaxSpider];

float clampX(float x) { return x < kXL ? kXL : (x > kXR ? kXR : x); }
float ground(float x) { return (float)surfaceY((int)clampX(x)); }

// Agents [0, n) are shown. A newly shown one is spawned; hidden ones are dropped.
template <int N, typename F>
void setCount(Bug (&a)[N], int n, F spawn) {
    for (int k = 0; k < N; k++) {
        if (k < n && !a[k].on) { a[k] = Bug{}; a[k].on = true; spawn(a[k], k); }
        else if (k >= n) a[k].on = false;
    }
}

// --- isopods: wander the surface (soil, wood, stone), pause and feel about with
// their antennae, busier at night. A jolt rolls the pill bugs into balls.
void isoStep(Bug& b, int k, float dt, float act, bool startle) {
    if (startle) {
        if (k % 4 != 3) { b.st = ISO_ROLL; b.timer = rr(3.5f, 6.0f); }
        else { b.st = ISO_WALK; b.timer = rr(1.0f, 2.0f); b.vx = 2.5f; }  // the zebra one bolts instead
    }
    switch (b.st) {
        case ISO_WALK: {
            float sp = (3.0f + (k % 3) * 0.9f) * act * (1.0f + b.vx);
            b.vx = fmaxf(0.0f, b.vx - dt * 1.5f);
            b.x += b.dir * sp * dt;
            b.phase += dt * sp * 0.9f;
            if (b.x < kXL) { b.x = kXL; b.dir = 1; }
            if (b.x > kXR) { b.x = kXR; b.dir = -1; }
            if ((b.timer -= dt) <= 0) { b.st = ISO_PAUSE; b.timer = rr(0.6f, 2.8f); }
            break;
        }
        case ISO_PAUSE:
            b.phase += dt * 2.5f;
            if ((b.timer -= dt) <= 0) {
                b.st = ISO_WALK;
                b.timer = rr(2.0f, 7.0f);
                if (rnd() < 0.35f) b.dir = -b.dir;
            }
            break;
        case ISO_ROLL:
            if ((b.timer -= dt) <= 0) { b.st = ISO_PAUSE; b.timer = rr(0.8f, 1.6f); }
            break;
    }
    b.y = approach(b.y, ground(b.x) - 4, 10.0f, dt);
}

void isoDraw(const Bug& b, int kind) {
    uint16_t base, seg, lite;
    switch (kind % 4) {
        case 0: base = rgb(136, 140, 152); seg = rgb(78, 82, 94); lite = rgb(184, 188, 198); break;    // common grey
        case 1: base = rgb(236, 150, 58); seg = rgb(150, 82, 26); lite = rgb(255, 196, 120); break;    // orange
        case 2: base = rgb(238, 238, 232); seg = rgb(54, 54, 60); lite = rgb(255, 255, 252); break;    // dalmatian
        default: base = rgb(108, 128, 150); seg = rgb(214, 218, 224); lite = rgb(160, 178, 196); break;  // zebra
    }
    int x = ir(b.x), y = ir(b.y), d = b.dir;
    uint16_t ink = rgb(34, 28, 24);
    if (b.st == ISO_ROLL) {  // conglobated: a little banded ball
        C->fillCircle(x, y + 1, 2, base);
        C->drawPixel(x - 1, y, lite);
        C->drawFastVLine(x, y, 3, seg);
        C->drawPixel(x + 1, y + 2, seg);
        C->drawFastHLine(x - 1, y + 3, 3, ink);
        return;
    }
    // legs: a ripple that runs along the body while walking
    int step = b.st == ISO_WALK ? ((int)(b.phase * 3.0f) & 1) : 0;
    for (int i = -3; i <= 3; i += 2) C->drawPixel(x + i + step, y + 3, ink);
    C->fillEllipse(x, y, 4, 2, base);
    C->drawFastHLine(x - 2, y - 2, 5, lite);
    for (int i = -2; i <= 2; i += 2) C->drawFastVLine(x + i - d, y - 1, 3, seg);
    C->drawPixel(x - d * 5, y + 1, seg);  // uropods
    // head and antennae; the antennae sweep while it pauses and probes
    int a = (int)(sinf(b.phase * 2.2f) * 1.6f);
    C->drawPixel(x + d * 4, y, ink);
    C->drawLine(x + d * 4, y - 1, x + d * 7, y - 2 + a, ink);
    C->drawLine(x + d * 4, y, x + d * 7, y + 1 - a / 2, ink);
}

// --- springtails: sit and graze, then flick the furcula and spring off in an
// arc many body lengths long. A few walk on the glass. A jolt makes them all jump.
void sprSpawn(Bug& b, int k) {
    b.dir = rnd() < 0.5f ? -1 : 1;
    b.timer = rr(0.3f, 4.0f);
    if (k % 4 == 0) {
        b.st = ST_GLASS;
        b.x = (k & 4) ? kJarL + 2 : kJarR - 2;
        b.y = rr(kSoilTop - 40, kSoilTop - 8);
    } else {
        b.st = ST_SIT;
        b.x = rr(kXL, kXR);
        b.y = ground(b.x) - 1;
    }
}

void sprHop(Bug& b, float power) {
    if (rnd() < 0.4f) b.dir = -b.dir;
    b.vx = b.dir * rr(10.0f, 30.0f) * power;
    b.vy = -rr(28.0f, 46.0f) * power;
    b.st = ST_HOP;
}

void sprStep(Bug& b, float dt, bool startle) {
    switch (b.st) {
        case ST_SIT:
            b.phase += dt;
            if (fmodf(b.phase, 2.4f) < 0.9f) {  // amble a little between hops
                b.x += b.dir * 2.0f * dt;
                if (b.x < kXL || b.x > kXR) b.dir = -b.dir;
            }
            b.y = ground(b.x) - 1;
            if (startle) sprHop(b, 1.3f);
            else if ((b.timer -= dt) <= 0) sprHop(b, 1.0f);
            break;
        case ST_HOP:
            b.x += b.vx * dt;
            b.y += b.vy * dt;
            b.vy += kG * dt;
            if (b.x < kXL) { b.x = kXL; b.vx = -b.vx; b.dir = 1; }
            if (b.x > kXR) { b.x = kXR; b.vx = -b.vx; b.dir = -1; }
            if (b.vy > 0 && b.y >= ground(b.x) - 1) { b.y = ground(b.x) - 1; b.st = ST_SIT; b.timer = rr(1.0f, 6.0f); }
            break;
        case ST_GLASS:
            b.phase += dt;
            if (fmodf(b.phase, 3.0f) < 1.8f) b.y += b.dir * 3.0f * dt;
            if (b.y < kSoilTop - 46) b.dir = 1;
            if (b.y > kSoilTop - 6) b.dir = -1;
            if (startle) {  // knocked off the glass
                b.vx = (b.x < 120 ? 1 : -1) * rr(6.0f, 14.0f);
                b.vy = 0;
                b.dir = b.vx > 0 ? 1 : -1;
                b.st = ST_HOP;
            }
            break;
    }
}

void sprDraw(const Bug& b) {
    int x = ir(b.x), y = ir(b.y);
    uint16_t white = rgb(252, 252, 255), ink = rgb(40, 34, 30);
    if (b.st == ST_GLASS) {
        C->drawFastVLine(x, y, 2, white);
        C->drawPixel(x, y + (b.dir > 0 ? 2 : -1), ink);  // head
        return;
    }
    if (b.st == ST_HOP) {
        int gy = (int)ground(b.x);
        if (gy - y > 2) C->drawFastHLine(x - 1, gy - 1, 2, rgb(44, 32, 24));  // shadow, so the height reads
    } else {
        C->drawFastHLine(x - 1, y + 1, 3, ink);  // dark underside: keeps the speck readable on leaves
    }
    C->drawFastHLine(x - 1, y, 3, white);
    C->drawPixel(x + 2 * b.dir, y, ink);  // head
}

// --- fungus gnats: erratic, jinking flight low over the soil, then they land,
// twitch about for a bit and take off again.
void gnatSpawn(Bug& b, int) {
    b.x = rr(kXL + 10, kXR - 10);
    b.y = rr(kJarT + 22, kSoilTop - 20);
    b.vx = rr(-20, 20);
    b.vy = rr(-10, 10);
    b.st = GN_FLY;
    b.timer = rr(2.0f, 7.0f);
    b.col = 0;
}

void gnatStep(Bug& b, float dt, bool startle) {
    if (b.st == GN_FLY) {
        float home = b.col ? ground(b.x) + 2 : kSoilTop - 26;  // hover low; dive when landing
        b.vx += rr(-1, 1) * 260.0f * dt;
        b.vy += (rr(-1, 1) * 260.0f + (home - b.y) * 3.0f) * dt;
        float damp = 1.0f - fminf(1.0f, 1.6f * dt);
        b.vx *= damp; b.vy *= damp;
        float sp = sqrtf(b.vx * b.vx + b.vy * b.vy);
        if (sp > 38.0f) { b.vx *= 38.0f / sp; b.vy *= 38.0f / sp; }
        b.x += b.vx * dt;
        b.y += b.vy * dt;
        if (b.x < kXL) { b.x = kXL; b.vx = fabsf(b.vx); }
        if (b.x > kXR) { b.x = kXR; b.vx = -fabsf(b.vx); }
        if (b.y < kJarT + 16) { b.y = kJarT + 16; b.vy = fabsf(b.vy); }
        b.dir = b.vx >= 0 ? 1 : -1;
        if ((b.timer -= dt) <= 0) b.col = 1;
        if (b.y >= ground(b.x) - 1) {
            b.y = ground(b.x) - 1;
            if (b.col) { b.st = GN_LAND; b.timer = rr(1.5f, 6.0f); }
            else b.vy = -fabsf(b.vy);
        }
    } else {
        if (rnd() < dt * 1.5f) { b.x += b.dir * rr(1.0f, 3.0f); if (rnd() < 0.3f) b.dir = -b.dir; }
        b.x = clampX(b.x);
        b.y = ground(b.x) - 1;
        if ((b.timer -= dt) <= 0 || startle) { b.st = GN_FLY; b.vy = -30; b.vx = rr(-15, 15); b.timer = rr(3.0f, 9.0f); b.col = 0; }
    }
}

void gnatDraw(const Bug& b, int frame) {
    int x = ir(b.x), y = ir(b.y), d = b.dir;
    uint16_t body = rgb(22, 22, 26), wing = rgb(206, 216, 226);
    C->drawPixel(x, y, body);
    C->drawPixel(x - d, y, body);
    if (b.st == GN_FLY) {  // wings beating: alternate between up and spread
        if (frame & 1) { C->drawPixel(x - d, y - 1, wing); C->drawPixel(x, y - 2, wing); }
        else { C->drawPixel(x - 2 * d, y - 1, wing); C->drawPixel(x + d, y - 1, wing); }
    } else {
        C->drawPixel(x - 2 * d, y - 1, wing);  // folded over the back
        C->drawPixel(x + d, y - 1, body);      // long antenna
    }
}

// --- ladybugs: climb up and down plant stems hunting aphids, pause, and now
// and then open their wing cases and fly to another plant.
int ladyCols[kCols], nLadyCols = 0;

void ladyTarget(int col, const World& w, float& tx, float& ty, float u) {
    int h = plantHeight(w, col);
    tx = stemX(col) + 2;
    ty = kSoilTop - 3 - u * (h * 0.72f - 3);
}

void ladyStep(Bug& b, int k, const World& w, float dt) {
    if (nLadyCols == 0 && b.st != LB_GROUND) { b.st = LB_GROUND; b.timer = rr(2, 5); }
    switch (b.st) {
        case LB_CLIMB:
        case LB_PAUSE: {
            int h = plantHeight(w, b.col);
            float top = h * 0.72f - 3;
            if (top < 4) { b.st = LB_PAUSE; b.timer = 0; }
            if (b.st == LB_CLIMB) {
                b.phase -= b.dir * 5.0f * dt;  // phase = height above the soil, px
                if (b.phase >= top) { b.phase = top; b.st = LB_PAUSE; b.timer = rr(1.0f, 4.0f); }
                if (b.phase <= 0) { b.phase = 0; b.st = LB_PAUSE; b.timer = rr(1.0f, 3.0f); }
            } else if ((b.timer -= dt) <= 0) {
                if (nLadyCols > 1 && (rnd() < 0.35f || top < 4)) {  // take off
                    b.vx = b.x; b.vy = b.y;
                    int c = ladyCols[(int)(rnd() * nLadyCols) % nLadyCols];
                    if (c == b.col) c = ladyCols[(k + 1 + (int)(rnd() * 7)) % nLadyCols];
                    b.col = c;
                    b.timer = 0;
                    b.st = LB_FLY;
                    break;
                }
                b.dir = b.phase > top * 0.5f ? 1 : -1;
                b.st = LB_CLIMB;
            }
            b.phase = fminf(b.phase, fmaxf(top, 0.0f));
            b.x = stemX(b.col) + 2 + (int)(sinf(b.phase * 0.5f + k) * 1.5f);
            b.y = kSoilTop - 3 - b.phase;
            break;
        }
        case LB_FLY: {
            float tx, ty;
            float u0 = 0.3f + 0.5f * frand(k * 13 + b.col);
            ladyTarget(b.col, w, tx, ty, u0);
            float dist = fabsf(tx - b.vx) + fabsf(ty - b.vy);
            b.timer += dt / fmaxf(0.8f, dist / 34.0f);
            float u = fminf(1.0f, b.timer);
            b.x = b.vx + (tx - b.vx) * u;
            b.y = b.vy + (ty - b.vy) * u - sinf(u * 3.14159f) * (10 + dist * 0.15f);
            b.dir = tx >= b.vx ? 1 : -1;
            if (u >= 1.0f) {
                b.phase = kSoilTop - 3 - ty;
                b.st = LB_PAUSE;
                b.timer = rr(0.5f, 2.0f);
                b.dir = -1;
            }
            break;
        }
        case LB_GROUND:
            if (rnd() < dt * 0.5f) b.dir = -b.dir;
            b.x = clampX(b.x + (b.dir > 0 ? 1 : -1) * 4.0f * dt);
            b.y = approach(b.y, ground(b.x) - 3, 10.0f, dt);
            if (nLadyCols > 0 && (b.timer -= dt) <= 0) {
                b.col = ladyCols[(int)(rnd() * nLadyCols) % nLadyCols];
                b.vx = b.x; b.vy = b.y; b.timer = 0; b.st = LB_FLY;
            }
            break;
    }
}

void ladySpawn(Bug& b, int k, const World& w) {
    b.st = LB_GROUND;
    b.x = rr(kXL, kXR);
    b.y = ground(b.x) - 3;
    b.dir = 1;
    b.timer = rr(0.5f, 3.0f);
    if (nLadyCols > 0) {
        b.col = ladyCols[(k * 5 + (int)(rnd() * nLadyCols)) % nLadyCols];
        b.phase = rr(0, plantHeight(w, b.col) * 0.6f);
        b.st = LB_CLIMB;
        b.dir = rnd() < 0.5f ? -1 : 1;
    }
}

void ladyDraw(const Bug& b, int frame) {
    int x = ir(b.x), y = ir(b.y);
    uint16_t red = rgb(232, 44, 36), ink = rgb(16, 16, 18);
    if (b.st == LB_FLY) {  // hind wings buzzing out from under the open elytra
        uint16_t wing = rgb(220, 228, 236);
        int wy = (frame & 1) ? y - 2 : y;
        C->drawLine(x - 2, y - 1, x - 4, wy, wing);
        C->drawLine(x + 2, y - 1, x + 4, wy, wing);
    }
    C->fillCircle(x, y, 2, red);
    if (b.st == LB_GROUND || b.st == LB_FLY) {  // side view
        int d = b.dir;
        C->drawFastVLine(x + d * 2, y - 1, 3, ink);  // head
        C->drawPixel(x + d * 2, y - 1, rgb(240, 240, 236));
        C->drawPixel(x - d, y, ink);
        C->drawPixel(x + d, y + 1, ink);
    } else {  // top view, head leading the way up or down the stem
        int s = b.dir < 0 ? 1 : -1;  // +1 when the head is at the top
        C->drawFastHLine(x - 1, y - 2 * s, 3, ink);
        C->drawPixel(x, y - s, ink);  // seam
        C->drawPixel(x, y, ink);
        C->drawPixel(x - 1, y + s, ink);  // spots
        C->drawPixel(x + 1, y + s, ink);
        C->drawPixel(x - 1, y - s, rgb(255, 150, 140));  // shine
        return;
    }
    C->drawPixel(x - 1, y - 1, rgb(255, 150, 140));  // shine
}

// --- jumping spider: looks around, picks out a landed gnat or a springtail,
// creeps toward it in stop-go bursts, crouches, and pounces.
bool preyPos(int id, float& px, float& py) {
    if (id >= 0 && id < kMaxGnat) {
        const Bug& g = gnat[id];
        if (!g.on || g.st != GN_LAND) return false;
        px = g.x; py = g.y; return true;
    }
    if (id >= 16 && id < 16 + kMaxSpring) {
        const Bug& s = spr[id - 16];
        if (!s.on || s.st != ST_SIT) return false;
        px = s.x; py = s.y; return true;
    }
    return false;
}

int findPrey(const Bug& b) {
    int best = -1;
    float bd = 70.0f;
    for (int i = 0; i < kMaxGnat; i++) {
        float px, py;
        if (preyPos(i, px, py) && fabsf(py - b.y) < 18 && fabsf(px - b.x) < bd) { bd = fabsf(px - b.x); best = i; }
    }
    for (int i = 0; i < kMaxSpring; i++) {
        float px, py;
        if (preyPos(16 + i, px, py) && fabsf(py - b.y) < 18 && fabsf(px - b.x) + 15 < bd) { bd = fabsf(px - b.x) + 15; best = 16 + i; }
    }
    return best;
}

void spiStep(Bug& b, float dt, float act) {
    float px = 0, py = 0;
    bool seen = preyPos(b.col, px, py);
    switch (b.st) {
        case JS_SIT:
            if (rnd() < dt * 0.5f) b.dir = -b.dir;  // turning to look about
            if ((b.timer -= dt) <= 0) {
                b.col = findPrey(b);
                if (b.col >= 0) { b.st = JS_STALK; b.phase = 0; }
                else if (rnd() < 0.5f) { b.col = -1; b.vx = (float)clampX(b.x + rr(-35, 35)); b.st = JS_STALK; b.phase = 0; }
                else b.timer = rr(1.0f, 3.0f);
            }
            break;
        case JS_STALK: {
            float tx = b.col >= 0 ? px : b.vx;
            if (b.col >= 0 && !seen) { b.st = JS_SIT; b.timer = rr(0.5f, 1.5f); break; }
            b.dir = tx >= b.x ? 1 : -1;
            b.phase += dt;
            bool moving = fmodf(b.phase, 0.9f) < 0.4f;  // stop-go
            if (moving) b.x += b.dir * (b.col >= 0 ? 16.0f : 22.0f) * act * dt;
            b.x = clampX(b.x);
            if (b.col >= 0 && fabsf(tx - b.x) < 16) { b.st = JS_CROUCH; b.timer = 0.45f; }
            else if (b.col < 0 && fabsf(tx - b.x) < 2) { b.st = JS_SIT; b.timer = rr(1.0f, 4.0f); }
            break;
        }
        case JS_CROUCH:
            if (seen) b.dir = px >= b.x ? 1 : -1;
            if ((b.timer -= dt) <= 0) {
                if (!seen) { b.st = JS_SIT; b.timer = 1.0f; break; }
                const float T = 0.42f;
                b.vx = (px - b.x) / T;
                b.vy = (py - 2 - b.y) / T - 0.5f * kG * T;
                b.st = JS_JUMP;
            }
            break;
        case JS_JUMP:
            b.x += b.vx * dt;
            b.y += b.vy * dt;
            b.vy += kG * dt;
            if (b.x < kXL || b.x > kXR) { b.x = clampX(b.x); b.vx = 0; }
            if (b.vy > 0 && b.y >= ground(b.x) - 3) {
                b.y = ground(b.x) - 3;
                if (seen && fabsf(px - b.x) < 5) {  // caught it; it re-enters elsewhere
                    if (b.col < 16) gnat[b.col].on = false;
                    else spr[b.col - 16].on = false;
                    b.st = JS_EAT;
                    b.timer = rr(5.0f, 9.0f);
                } else { b.st = JS_SIT; b.timer = rr(0.5f, 1.5f); }
                b.col = -1;
            }
            return;
        case JS_EAT:
            if ((b.timer -= dt) <= 0) { b.st = JS_SIT; b.timer = rr(4.0f, 8.0f); }
            break;
    }
    b.y = approach(b.y, ground(b.x) - 3 + (b.st == JS_CROUCH ? 1 : 0), 12.0f, dt);
}

void spiDraw(const Bug& b) {
    int x = ir(b.x), y = ir(b.y), d = b.dir;
    uint16_t body = rgb(30, 28, 32), leg = rgb(70, 60, 54), white = rgb(246, 246, 240);
    bool walking = b.st == JS_STALK && fmodf(b.phase, 0.9f) < 0.4f;
    int s = walking ? ((int)(b.phase * 14.0f) & 1) : 0;
    bool air = b.st == JS_JUMP;
    // legs: three pairs splay down to the surface (tucked in mid-air)
    for (int i = 0; i < 3; i++) {
        int lx = x - d * (3 - i * 2), off = ((i + s) & 1) ? 1 : -1;
        if (air) C->drawLine(lx, y + 1, lx - d, y + 2, leg);
        else C->drawLine(lx, y + 1, lx + off, y + 3, leg);
    }
    // front legs: raised while stalking and crouching, stretched forward when it leaps
    if (air) C->drawLine(x + d * 2, y, x + d * 5, y - 1, leg);
    else if (b.st == JS_STALK || b.st == JS_CROUCH) C->drawLine(x + d * 2, y, x + d * 4, y - 2, leg);
    else C->drawLine(x + d * 2, y, x + d * 4, y + 3, leg);
    C->fillEllipse(x - d * 3, y, 3, 2, body);  // abdomen
    C->drawPixel(x - d * 3, y - 1, white);     // the white spot
    C->drawPixel(x - d * 4, y, white);
    C->fillCircle(x + d, y - 1, 2, body);      // cephalothorax
    C->drawPixel(x + d * 3, y - 1, rgb(110, 110, 120));  // big front eye...
    C->drawPixel(x + d * 2, y - 2, white);                // ...and its glint
    C->drawPixel(x + d * 3, y, rgb(60, 196, 160));        // iridescent chelicerae
    if (b.st == JS_EAT) C->drawPixel(x + d * 4, y, rgb(200, 206, 214));  // the catch
}

void mushroom(int x, int y, int kind) {
    C->drawFastVLine(x, y - 4, 5, rgb(236, 232, 214));
    if (kind & 1) { C->fillEllipse(x, y - 5, 2, 1, rgb(196, 158, 112)); }
    else { C->fillEllipse(x, y - 5, 2, 1, rgb(228, 220, 196)); C->drawPixel(x, y - 6, rgb(245, 240, 225)); }
}

// Shown count for a species: round(density * per), capped, and at least one
// whenever the population is present at all.
int shown(float density, float per, int cap) {
    if (density < 0.012f) return 0;
    int n = (int)roundf(density * per);
    return n < 1 ? 1 : (n > cap ? cap : n);
}

void drawCreatures(const World& w, float day, float t, float jolt) {
    float dt = gLastT < 0 ? 0.05f : t - gLastT;
    gLastT = t;
    gDt = dt;
    if (dt < 0 || dt > 0.12f) dt = dt < 0 ? 0 : 0.12f;  // a long gap pauses the bugs instead of teleporting them
    gR ^= (uint32_t)(t * 1000.0f);
    if (gR == 0) gR = 1;
    // a jolt (the IMU felt the board knocked) startles everything once
    bool startle = jolt > 0.3f && gJoltHold <= 0;
    if (startle) gJoltHold = 2.0f;
    gJoltHold -= dt;
    int frame = (int)(t * 20.0f);

    drawLitter(w);  // underfoot for everything below

    // small mushrooms
    int nm = (int)roundf(w.fungus * 10);
    if (nm > 4) nm = 4;
    for (int k = 0; k < nm; k++) {
        int x = kJarL + 30 + (int)(hash32(k + 800) % 180);
        if (surfaceY(x) == kSoilTop) mushroom(x, kSoilTop, k);
    }
    // gnat larvae: translucent wisps in the top of the soil, inching along the glass
    int nl = (int)roundf(w.cat * 25);
    if (nl > 5) nl = 5;
    for (int k = 0; k < nl; k++) {
        int x = kJarL + 14 + hash32(k + 700) % 196 + (int)(sinf(t * 0.12f + k * 2.1f) * 8);
        int y = kSoilTop + 3 + (int)(hash32(k + 720) % 9);
        int len = 3 + (int)(sinf(t * 2.4f + k) * 1.5f + 1.0f);  // stretch and contract
        int wig = (int)roundf(sinf(t * 1.7f + k));
        C->drawFastHLine(x - len, y + wig, len + 1, rgb(236, 236, 220));
        C->drawPixel(x + 1, y + wig, rgb(24, 24, 24));
    }
    // aphids: little colonies on the taller plants; they shuffle now and then
    int na = (int)roundf(w.aphid * 34);
    if (na > 10) na = 10;
    for (int k = 0; k < na; k++) {
        int c = hash32(k * 31 + 7) % kCols;
        int h = plantHeight(w, c);
        if (h < 10) continue;
        int y = 4 + (int)(hash32(k) % (h - 3));
        int x = stemX(c) + (int)(hash32(k + 3) % 7) - 3 + (int)(hash32(k * 7 + (int)(t / 3.0f + k * 0.37f)) % 3) - 1;
        uint16_t a = (k & 1) ? rgb(156, 214, 92) : rgb(230, 156, 156), b = (k & 1) ? rgb(100, 160, 60) : rgb(186, 104, 104);
        C->drawPixel(x, kSoilTop - y, a);
        C->drawPixel(x + 1, kSoilTop - y, b);
        C->drawPixel(x, kSoilTop - y + 1, b);
    }

    // plants a ladybug can climb: an upright stem, tall enough
    nLadyCols = 0;
    for (int c = 0; c < kCols; c++) {
        int kd = kKind[c];
        if (kd == K_FIG || kd == K_MAT || kd == K_MOUND || kd == K_BABY) continue;
        if (plantHeight(w, c) >= 14) ladyCols[nLadyCols++] = c;
    }

    float act = 0.8f + 0.7f * (1.0f - day);  // isopods are nocturnal
    setCount(iso, shown(w.worm, 10, kMaxIso), [](Bug& b, int) {
        b.x = rr(kXL, kXR); b.y = ground(b.x) - 4; b.dir = rnd() < 0.5f ? -1 : 1; b.st = ISO_WALK; b.timer = rr(1, 6);
    });
    setCount(spr, shown(w.spring, 44, kMaxSpring), sprSpawn);
    setCount(gnat, shown(w.butter, 30, kMaxGnat), gnatSpawn);
    setCount(lady, shown(w.lady, 20, kMaxLady), [&w](Bug& b, int k) { ladySpawn(b, k, w); });
    setCount(spi, w.spider < 0.012f ? 0 : (w.spider > 0.12f ? 2 : 1), [](Bug& b, int k) {
        b.x = k == 0 ? kWoodX0 + 24 : rr(kXL + 100, kXR); b.y = ground(b.x) - 3; b.dir = k ? -1 : 1;
        b.st = JS_SIT; b.timer = rr(1, 3); b.col = -1;
    });

    for (int k = 0; k < kMaxIso; k++) if (iso[k].on) { isoStep(iso[k], k, dt, act, startle); isoDraw(iso[k], k); }
    for (int k = 0; k < kMaxSpring; k++) if (spr[k].on) { sprStep(spr[k], dt, startle); sprDraw(spr[k]); }
    for (int k = 0; k < kMaxSpider; k++) if (spi[k].on) { spiStep(spi[k], dt, 0.7f + 0.4f * day); spiDraw(spi[k]); }
    for (int k = 0; k < kMaxLady; k++) if (lady[k].on) { ladyStep(lady[k], k, w, dt); ladyDraw(lady[k], frame + k); }
    for (int k = 0; k < kMaxGnat; k++) if (gnat[k].on) { gnatStep(gnat[k], dt, startle); gnatDraw(gnat[k], frame + k); }
}

// A condensation drop now and then runs down the front glass when it is humid.
float gDropX = -1, gDropY = 0, gDropV = 0, gDropY0 = 0;
void drawDrop(const World& w, float dt) {
    if (gDropX < 0) {
        if (w.hum > 0.8f && rnd() < dt * (w.hum - 0.75f) * 0.4f) {
            gDropX = rr(kJarL + 6, kJarR - 6);
            gDropY = gDropY0 = rr(kJarT + 6, kJarT + 50);
            gDropV = 0;
        }
        return;
    }
    gDropV = fminf(gDropV + 30.0f * dt, 22.0f);
    gDropY += gDropV * dt;
    int x = ir(gDropX), y = ir(gDropY);
    for (int yy = (int)gDropY0; yy < y - 1; yy += 2) C->drawPixel(x, yy, raw(196, 216, 226));  // its wet trail
    C->drawFastVLine(x, y - 1, 2, raw(236, 246, 252));
    C->drawPixel(x, y + 1, raw(150, 176, 190));
    if (gDropY > kSoilTop - 2) gDropX = -1;
}

}  // namespace

int plantHeight(const World& w, int col) {
    return (int)(w.p[col] * 100.0f * kScale[kKind[col]]);
}

void draw(M5Canvas& cv, const World& w, float dayFrac, float t, float wind, float jolt) {
    C = &cv;
    float day = clamp01((light(dayFrac) - 0.05f) / 0.4f);
    float lit = fmaxf(day, w.lamp);
    gDim = 0.34f + 0.66f * lit;

    cv.fillRect(0, 0, 240, 135, raw(14, 16, 20));  // the room around the tank
    cv.setClipRect(kJarL + 1, kJarT + 1, kJarR - kJarL - 1, kJarB - kJarT - 1);  // nothing grows through the glass
    drawBackdrop();
    drawSubstrate(w, t);
    for (int i = 0; i < kCols; i++) plant(i, w, t, wind);
    drawWood(w);
    drawStone();
    drawCreatures(w, day, t, jolt);

    cv.clearClipRect();

    // grow-light bar on top
    cv.fillRect(kJarL - 1, 0, kJarR - kJarL + 3, 5, raw(58, 60, 66));
    cv.fillRect(kJarL + 4, 3, kJarR - kJarL - 6, 2, lit > 0.05f ? raw(250 * (0.6f + 0.4f * lit), 240 * (0.6f + 0.4f * lit), 200 * (0.5f + 0.5f * lit)) : raw(70, 70, 76));

    // glass: silicone-black frame, rim highlight, streaks, condensation
    cv.drawRect(kJarL - 1, kJarT - 1, kJarR - kJarL + 3, kJarB - kJarT + 3, raw(24, 26, 30));
    cv.drawRect(kJarL, kJarT, kJarR - kJarL + 1, kJarB - kJarT + 1, raw(120, 150, 160));
    cv.drawFastVLine(kJarL + 12, kJarT + 8, 26, raw(200, 225, 232));
    cv.drawFastVLine(kJarL + 14, kJarT + 14, 12, raw(160, 190, 200));
    cv.drawLine(kJarR - 30, kJarT + 2, kJarR - 44, kJarT + 26, raw(150, 180, 190));
    drawDrop(w, gDt);
    int nd = (int)fmaxf(0.0f, (w.hum - 0.82f) * 90.0f);
    for (int i = 0; i < nd && i < 8; i++) {  // a few droplets, only on the glass edges
        int x = (i & 1) ? kJarR - 2 - (int)(hash32(i) % 3) : kJarL + 2 + (int)(hash32(i) % 3);
        int y = kJarT + 8 + (int)(hash32(i * 7 + 1) % (kSoilTop - 20));
        cv.drawPixel(x, y, raw(226, 240, 246));
        if (i % 3 == 0) cv.drawFastVLine(x, y + 1, 3, raw(190, 214, 224));
    }
}

}  // namespace scene
