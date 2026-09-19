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

void plant(int col, const World& w, float t, float wind) {
    int k = kKind[col];
    float p = w.p[col];
    int eff = plantHeight(w, col);
    int x0 = stemX(col), y0 = surfaceY(x0) < kSoilTop - 2 ? kSoilTop : kSoilTop;
    // roots into the soil
    int rl = (int)fminf(12.0f, p * 18.0f) + 1;
    uint16_t root = rgb(170, 138, 104);
    C->drawLine(x0, y0, x0, y0 + rl, root);
    if (rl > 6) { C->drawLine(x0, y0 + 4, x0 - 4, y0 + 4 + rl / 3, root); C->drawLine(x0, y0 + 6, x0 + 4, y0 + 6 + rl / 3, root); }

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

void springtail(int x, int y, float hop) {
    C->drawPixel(x, y + 1, rgb(90, 70, 50));
    C->drawPixel(x, y - (int)hop, rgb(240, 244, 250));
    if (hop > 0.5f) C->drawPixel(x - 1, y - (int)hop + 1, rgb(200, 205, 215));
}

void isopod(int x, int y, int kind, bool right) {
    uint16_t base, seg;
    switch (kind % 4) {
        case 0: base = rgb(128, 132, 144); seg = rgb(84, 88, 100); break;              // common gray
        case 1: base = rgb(232, 148, 60);  seg = rgb(160, 90, 30); break;              // orange
        case 2: base = rgb(236, 236, 230); seg = rgb(60, 60, 64); break;              // dalmatian
        default: base = rgb(110, 130, 150); seg = rgb(210, 214, 220); break;           // zebra
    }
    C->fillEllipse(x, y, 3, 2, base);
    for (int i = -1; i <= 1; i++) C->drawFastVLine(x + i * 2, y - 1, 3, seg);
    C->drawPixel(x + (right ? 4 : -4), y - 1, rgb(60, 60, 60));
    C->drawPixel(x + (right ? 4 : -4), y + 1, rgb(60, 60, 60));
    C->drawPixel(x - 2, y + 2, rgb(50, 50, 50)); C->drawPixel(x + 2, y + 2, rgb(50, 50, 50));
}

void mushroom(int x, int y, int kind) {
    C->drawFastVLine(x, y - 4, 5, rgb(236, 232, 214));
    if (kind & 1) { C->fillEllipse(x, y - 5, 2, 1, rgb(196, 158, 112)); }
    else { C->fillEllipse(x, y - 5, 2, 1, rgb(228, 220, 196)); C->drawPixel(x, y - 6, rgb(245, 240, 225)); }
}

void drawCreatures(const World& w, float dayFrac, float t) {
    (void)dayFrac;
    // leaf litter is underfoot for everything below
    drawLitter(w);

    // springtails: white specks on the soil, hopping; a few on the glass
    int nsp = (int)roundf(w.spring * 44);
    if (nsp > 14) nsp = 14;
    for (int k = 0; k < nsp; k++) {
        bool glass = (k % 4) == 0;
        float ph = t * 0.9f + k * 1.7f;
        float hopPhase = fmodf(t * 0.5f + k * 0.37f, 3.0f);
        float hop = hopPhase < 0.35f ? sinf(hopPhase / 0.35f * 3.14159f) * 3.0f : 0.0f;
        if (glass) {
            int gx = (k & 4) ? kJarL + 2 : kJarR - 2;
            int gy = kSoilTop - 4 - (int)(fmodf(t * 0.6f + k * 11.0f, 30.0f));
            springtail(gx, gy, 0);
        } else {
            int x = kJarL + 6 + (int)(hash32(k + 50) % 205 + sinf(ph) * 5 + t * 0.4f * (1 + k % 3)) % 205;
            springtail(x, surfaceY(x) - 1, hop);
        }
    }
    // isopods on the soil, litter and wood
    int niso = (int)roundf(w.worm * 10);
    if (niso > 5) niso = 5;
    for (int k = 0; k < niso; k++) {
        float sp = 0.10f + k * 0.03f;
        int x = kJarL + 8 + (int)fmodf(hash32(k + 90) % 205 + t * sp * 10.0f, 205.0f);
        isopod(x, surfaceY(x) - 2, k, true);
    }
    // gnat larvae: pale wisps in the top of the soil, against the glass
    int nl = (int)roundf(w.cat * 25);
    if (nl > 5) nl = 5;
    for (int k = 0; k < nl; k++) {
        int x = kJarL + 12 + hash32(k + 700) % 200, y = kSoilTop + 3 + (int)(hash32(k + 720) % 9);
        int wig = (int)roundf(sinf(t * 2 + k) * 1.0f);
        C->drawFastHLine(x, y + wig, 4, rgb(240, 240, 224));
        C->drawPixel(x + 4, y + wig, rgb(30, 30, 30));
    }
    // aphids: tiny dots on leaves of the taller plants (they like ferns and pilea)
    int na = (int)roundf(w.aphid * 34);
    if (na > 10) na = 10;
    for (int k = 0; k < na; k++) {
        int c = hash32(k * 31 + 7) % kCols;
        int h = plantHeight(w, c);
        if (h < 10) continue;
        int y = 4 + (int)(hash32(k) % (h - 3));
        int x = stemX(c) + (int)(hash32(k + 3) % 7) - 3;
        C->drawPixel(x, kSoilTop - y, (k & 1) ? rgb(150, 210, 90) : rgb(226, 150, 150));
        C->drawPixel(x + 1, kSoilTop - y, (k & 1) ? rgb(110, 170, 70) : rgb(190, 110, 110));
    }
    // ladybugs
    int nlb = (int)roundf(w.lady * 20);
    if (nlb > 3) nlb = 3;
    for (int k = 0; k < nlb; k++) {
        int c = hash32(k * 71 + 11) % kCols, h = plantHeight(w, c);
        if (h < 12) continue;
        int y = 6 + (int)(hash32(k + 5) % (h - 6));
        int x = stemX(c) + 3;
        C->fillCircle(x, kSoilTop - y, 1, rgb(226, 60, 50));
        C->drawFastHLine(x - 1, kSoilTop - y, 3, rgb(20, 20, 20));
    }
    // adult fungus gnats: tiny erratic flyers
    int ng = (int)roundf(w.butter * 30);
    if (ng > 6) ng = 6;
    for (int k = 0; k < ng; k++) {
        float cx = kJarL + 30 + hash32(k + 2000) % 160, cy = 40 + hash32(k + 2100) % 40;
        int x = (int)(cx + sinf(t * 1.3f + k * 2.0f) * 24 + sinf(t * 4.7f + k) * 3);
        int y = (int)(cy + cosf(t * 1.1f + k * 1.4f) * 16 + cosf(t * 5.3f + k) * 3);
        C->drawPixel(x, y, rgb(20, 20, 24));
        C->drawPixel(x + 1, y, rgb(20, 20, 24));
        C->drawPixel(x, y - 1, rgb(200, 210, 220));
    }
    // jumping spider on the wood or glass
    int nsp2 = (int)roundf(w.spider * 12);
    if (nsp2 > 2) nsp2 = 2;
    for (int k = 0; k < nsp2; k++) {
        int x = k == 0 ? kWoodX0 + 24 + (int)(sinf(t * 0.2f) * 6) : kJarR - 3;
        int y = k == 0 ? woodTop(x) - 2 : kSoilTop - 26 + (int)(sinf(t * 0.3f) * 8);
        if (fmodf(t * 0.4f + k, 5.0f) < 0.3f) y -= 3;  // a jump
        uint16_t sc = rgb(96, 70, 52);
        C->fillCircle(x, y, 2, sc);
        C->drawPixel(x + 2, y - 1, rgb(30, 30, 30));
        for (int j = 0; j < 3; j++) { C->drawLine(x, y, x - 3, y + j, sc); C->drawLine(x, y, x + 3, y + j, sc); }
        C->drawPixel(x - 1, y - 1, rgb(230, 230, 220));
    }
    // small mushrooms
    int nm = (int)roundf(w.fungus * 10);
    if (nm > 4) nm = 4;
    for (int k = 0; k < nm; k++) {
        int x = kJarL + 30 + (int)(hash32(k + 800) % 180);
        if (surfaceY(x) == kSoilTop) mushroom(x, kSoilTop, k);
    }
}

}  // namespace

int plantHeight(const World& w, int col) {
    return (int)(w.p[col] * 100.0f * kScale[kKind[col]]);
}

void draw(M5Canvas& cv, const World& w, float dayFrac, float t, float wind) {
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
    drawCreatures(w, dayFrac, t);

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
    int nd = (int)fmaxf(0.0f, (w.hum - 0.82f) * 90.0f);
    for (int i = 0; i < nd && i < 8; i++) {  // a few droplets, only on the glass edges
        int x = (i & 1) ? kJarR - 2 - (int)(hash32(i) % 3) : kJarL + 2 + (int)(hash32(i) % 3);
        int y = kJarT + 8 + (int)(hash32(i * 7 + 1) % (kSoilTop - 20));
        cv.drawPixel(x, y, raw(226, 240, 246));
        if (i % 3 == 0) cv.drawFastVLine(x, y + 1, 3, raw(190, 214, 224));
    }
}

}  // namespace scene
