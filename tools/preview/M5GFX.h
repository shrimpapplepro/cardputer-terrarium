// Host-side stand-in for the slice of M5GFX that src/scene.cpp uses, so the
// tank can be rendered on the Mac (tools/preview). Not pixel-identical to
// M5GFX's rasteriser for ellipses, but close enough to judge motion and size.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

class M5Canvas {
   public:
    static constexpr int W = 240, H = 135;
    uint16_t fb[W * H] = {};
    int cx0 = 0, cy0 = 0, cx1 = W, cy1 = H;

    uint16_t color565(uint8_t r, uint8_t g, uint8_t b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
    void setClipRect(int x, int y, int w, int h) { cx0 = x; cy0 = y; cx1 = x + w; cy1 = y + h; }
    void clearClipRect() { cx0 = cy0 = 0; cx1 = W; cy1 = H; }
    void drawPixel(int x, int y, uint16_t c) {
        if (x < cx0 || y < cy0 || x >= cx1 || y >= cy1) return;
        fb[y * W + x] = c;
    }
    void drawFastHLine(int x, int y, int w, uint16_t c) { for (int i = 0; i < w; i++) drawPixel(x + i, y, c); }
    void drawFastVLine(int x, int y, int h, uint16_t c) { for (int i = 0; i < h; i++) drawPixel(x, y + i, c); }
    void fillRect(int x, int y, int w, int h, uint16_t c) { for (int j = 0; j < h; j++) drawFastHLine(x, y + j, w, c); }
    void drawRect(int x, int y, int w, int h, uint16_t c) {
        drawFastHLine(x, y, w, c); drawFastHLine(x, y + h - 1, w, c);
        drawFastVLine(x, y, h, c); drawFastVLine(x + w - 1, y, h, c);
    }
    void drawLine(int x0, int y0, int x1, int y1, uint16_t c) {
        int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, e = dx + dy;
        for (;;) {
            drawPixel(x0, y0, c);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * e;
            if (e2 >= dy) { e += dy; x0 += sx; }
            if (e2 <= dx) { e += dx; y0 += sy; }
        }
    }
    void fillCircle(int x, int y, int r, uint16_t c) { fillEllipse(x, y, r, r, c); }
    void drawCircle(int x, int y, int r, uint16_t c) {
        for (int a = 0; a < 64; a++) drawPixel(x + (int)lroundf(r * cosf(a * 0.0982f)), y + (int)lroundf(r * sinf(a * 0.0982f)), c);
    }
    void fillEllipse(int x, int y, int rx, int ry, uint16_t c) {
        if (ry <= 0) { drawFastHLine(x - rx, y, 2 * rx + 1, c); return; }
        for (int dy = -ry; dy <= ry; dy++) {
            float f = 1.0f - (float)(dy * dy) / (float)(ry * ry + ry);
            int wd = (int)(rx * sqrtf(f > 0 ? f : 0) + 0.5f);
            drawFastHLine(x - wd, y + dy, 2 * wd + 1, c);
        }
    }
};
