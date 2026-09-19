#include "view.h"

#include <math.h>

#include "scene.h"

using namespace terra;

namespace view {
namespace {

constexpr int W = 240, H = 135;
constexpr int kJarL = scene::kJarL, kJarR = scene::kJarR, kJarT = scene::kJarT, kJarB = scene::kJarB;

M5GFX* lcd = nullptr;
M5Canvas cv;

inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return cv.color565(r, g, b); }
inline float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
inline uint16_t lerp565(uint8_t r0, uint8_t g0, uint8_t b0, uint8_t r1, uint8_t g1, uint8_t b1, float t) {
    return rgb(r0 + (r1 - r0) * t, g0 + (g1 - g0) * t, b0 + (b1 - b0) * t);
}

const uint16_t* speciesColors() {
    static uint16_t c[kSpecies];
    static bool init = false;
    if (!init) {
        const uint8_t t[kSpecies][3] = {{80, 200, 90},   {170, 220, 90},  {235, 235, 205}, {170, 180, 200}, {230, 70, 60},
                                        {190, 140, 110}, {235, 150, 70}, {225, 215, 185}, {150, 210, 245}};
        for (int i = 0; i < kSpecies; i++) c[i] = rgb(t[i][0], t[i][1], t[i][2]);
        init = true;
    }
    return c;
}

// Density that reads as "full" for each species' bar.
const float kBarMax[kSpecies] = {0.8f, 0.5f, 0.25f, 0.2f, 0.25f, 0.2f, 0.5f, 0.5f, 0.5f};

void hudText(int x, int y, const char* s, uint16_t c) {
    cv.setTextColor(rgb(0, 0, 0));
    cv.drawString(s, x + 1, y + 1);
    cv.setTextColor(c);
    cv.drawString(s, x, y);
}

void drawJarPage(const World& w, const State& st, float t) {
    scene::draw(cv, w, st.dayFrac, t, clamp01(w.fSound + w.fShake));
    cv.setTextSize(1);
    cv.setTextDatum(top_left);
    char b[48];
    snprintf(b, sizeof b, "D%d %d%% %.0fC", (int)w.age, (int)(health(w) * 100), w.temp);
    hudText(kJarL + 4, kJarT + 8, b, rgb(255, 255, 255));
    if (w.evLeft > 0) {
        const char* ev = w.evTemp < 0 ? "COLD" : (w.evTemp > 0 ? "HEAT" : "DRY");
        hudText(kJarR - 34, kJarT + 8, ev, rgb(255, 200, 120));
    }
    cv.setTextDatum(bottom_right);
    hudText(kJarR - 4, kJarB - 12, st.speedLabel, rgb(255, 255, 255));
    cv.setTextDatum(top_left);
}

void header(const char* title, const World& w) {
    cv.fillRect(0, 0, W, 14, rgb(20, 40, 28));
    cv.setTextColor(rgb(200, 240, 200));
    cv.setTextSize(1);
    cv.setTextDatum(top_left);
    cv.drawString(title, 4, 3);
    char b[40];
    snprintf(b, sizeof b, "day %d  %d%% alive", (int)w.age, (int)(health(w) * 100));
    cv.setTextDatum(top_right);
    cv.drawString(b, W - 4, 3);
    cv.setTextDatum(top_left);
}

void drawSummary(const World& w) {
    cv.fillRect(0, 0, W, H, rgb(12, 18, 14));
    header("SUMMARY", w);
    const uint16_t* col = speciesColors();
    int days = (int)fminf((float)kHistDays, floorf(w.age));
    for (int s = 0; s < kSpecies; s++) {
        int y = 16 + s * 13;
        float d = density(w, (Species)s);
        cv.setTextColor(col[s]);
        cv.setTextDatum(top_left);
        cv.drawString(speciesName((Species)s), 4, y + 1);
        // bar
        cv.drawRect(84, y + 1, 48, 8, rgb(60, 80, 64));
        int bw = (int)(clamp01(d / kBarMax[s]) * 46);
        cv.fillRect(85, y + 2, bw, 6, col[s]);
        // trend
        int tr = trend(w, (Species)s);
        uint16_t tc = tr > 0 ? rgb(120, 230, 120) : (tr < 0 ? rgb(240, 130, 110) : rgb(140, 150, 140));
        int ax = 138;
        if (tr > 0) cv.fillTriangle(ax, y + 8, ax + 6, y + 8, ax + 3, y + 2, tc);
        else if (tr < 0) cv.fillTriangle(ax, y + 2, ax + 6, y + 2, ax + 3, y + 8, tc);
        else cv.fillRect(ax, y + 4, 7, 2, tc);
        // 30-day sparkline
        int sx = 152;
        cv.drawRect(sx - 1, y, 88, 10, rgb(38, 52, 42));
        for (int k = 0; k < days; k++) {
            int idx = (w.histHead + kHistDays - (days - 1 - k)) % kHistDays;  // oldest -> newest
            int v = (int)(clamp01(w.hist[idx][s] / 255.0f / kBarMax[s]) * 8);
            cv.drawFastVLine(sx + k * 88 / kHistDays, y + 9 - v, v + 1, col[s]);
        }
        (void)d;
    }
}

void drawJournal(const World& w, const State& st) {
    cv.fillRect(0, 0, W, H, rgb(18, 16, 12));
    header("JOURNAL", w);
    cv.setTextSize(1);
    const int rows = 10;
    int n = w.logCount;
    int maxScroll = n > rows ? n - rows : 0;
    int scroll = st.journalScroll > maxScroll ? maxScroll : st.journalScroll;
    for (int r = 0; r < rows; r++) {
        int i = r + scroll;  // 0 = newest
        if (i >= n) break;
        const LogEntry& e = w.log[(w.logHead + kLogSize - 1 - i) % kLogSize];
        char b[64], line[80];
        view::logText(b, sizeof b, e);
        snprintf(line, sizeof line, "d%-3d %s", e.day, b);
        cv.setTextColor(e.kind == LG_CRASH || e.kind == LG_COLD || e.kind == LG_HEAT || e.kind == LG_DRY
                            ? rgb(240, 150, 120)
                            : (e.kind >= LG_MIST && e.kind <= LG_LAMP ? rgb(140, 200, 240) : rgb(220, 220, 200)));
        cv.setTextDatum(top_left);
        cv.drawString(line, 4, 17 + r * 11);
    }
    if (n == 0) { cv.setTextColor(rgb(150, 150, 140)); cv.drawString("Nothing has happened yet.", 4, 20); }
    if (maxScroll > 0) {
        cv.setTextColor(rgb(110, 110, 100));
        cv.setTextDatum(bottom_right);
        char b[16]; snprintf(b, sizeof b, "; . %d/%d", scroll + 1, n);
        cv.drawString(b, W - 3, H - 1);
    }
}

void drawSignals(const World& w, const State& st, const Signals& s) {
    cv.fillRect(0, 0, W, H, rgb(8, 12, 22));
    header("SIGNALS", w);
    cv.setTextSize(1);
    cv.setTextDatum(top_left);
    cv.setTextColor(rgb(190, 220, 255));
    char b[64];
    int y = 16;
    auto line = [&](const char* fmt, auto... a) { snprintf(b, sizeof b, fmt, a...); cv.drawString(b, 3, y); y += 10; };
    line("acc %+.2f %+.2f %+.2f  jerk %.2f", s.ax, s.ay, s.az, s.jerk);
    line("mic rms %.0f lvl %.2f  shake %.2f", s.micRms, s.micLevel, s.in.shake);
    line("bat %.2fV %d%%%s chip %.1fC imu %.1fC", s.battV, s.battPct, s.charging ? "+" : "", s.chipTempC, s.imuTempC);
    line("wifi n=%d %.0fdBm d=%.2f", s.wifiN, s.wifiRssi, s.wifiDelta);
    line("ble  n=%d %.0fdBm d=%.2f", s.bleN, s.bleRssi, s.bleDelta);
    line("rf %.2f drift %+.2f scans %lu%s", s.in.rfDensity, s.in.tempDrift, (unsigned long)s.scans,
         s.scanState == 1 ? " [WIFI]" : (s.scanState == 2 ? " [BLE]" : ""));
    line("entropy %08lX", (unsigned long)s.entropy);
    line("heap %luK min %luK big %luK", (unsigned long)(s.heapFree / 1024), (unsigned long)(s.heapMin / 1024), (unsigned long)(s.heapLargest / 1024));
    line("scan heap %luK>%luK low %luK", (unsigned long)(s.heapBeforeScan / 1024), (unsigned long)(s.heapAfterScan / 1024), (unsigned long)(s.heapLow / 1024));
    line("jar T%.1f H%.2f S%.2f N%.2f A%.2f", w.temp, w.hum, w.soil, w.nut, w.air);
    int hh = (int)(st.dayFrac * 24), mm = (int)(fmodf(st.dayFrac * 1440.0f, 60.0f));
    line("clock %02d:%02d  speed %s  up %lus", hh, mm, st.speedLabel, (unsigned long)(millis() / 1000));
}

}  // namespace

bool begin(M5GFX* display) {
    lcd = display;
    cv.setColorDepth(16);
    cv.setPsram(false);
    if (!cv.createSprite(W, H)) return false;
    cv.setFont(&fonts::Font0);
    return true;
}

void draw(const World& w, const State& st, const Signals& sg) {
    float t = millis() / 1000.0f;
    switch (st.page) {
        case PAGE_JAR: drawJarPage(w, st, t); break;
        case PAGE_SUMMARY: drawSummary(w); break;
        case PAGE_JOURNAL: drawJournal(w, st); break;
        case PAGE_SIGNALS: drawSignals(w, st, sg); break;
        default: break;
    }
    if (st.toast[0] && millis() < st.toastUntil) {
        cv.setTextSize(1);
        cv.setTextDatum(bottom_left);
        int tw = cv.textWidth(st.toast) + 8;
        cv.fillRoundRect(4, H - 16, tw, 13, 3, rgb(0, 0, 0));
        cv.setTextColor(rgb(255, 240, 160));
        cv.drawString(st.toast, 8, H - 5);
        cv.setTextDatum(top_left);
    }
}

void push() { cv.pushSprite(lcd, 0, 0); }

const uint8_t* framebuffer(size_t* len) {
    if (len) *len = (size_t)W * H * 2;
    return (const uint8_t*)cv.getBuffer();
}

void logText(char* buf, size_t n, const LogEntry& e) {
    const char* sp = speciesName((Species)(e.arg < kSpecies ? e.arg : 0));
    switch (e.kind) {
        case LG_BORN: snprintf(buf, n, "Tank sealed. Life begins."); break;
        case LG_BLOOM: snprintf(buf, n, "%s boomed (%d%%)", sp, e.val); break;
        case LG_CRASH: snprintf(buf, n, "%s crashed to %d%%", sp, e.val); break;
        case LG_RECOVER: snprintf(buf, n, "%s recovering (%d%%)", sp, e.val); break;
        case LG_FIRST_BUTTERFLY: snprintf(buf, n, "First fungus gnats hatched"); break;
        case LG_COLD: snprintf(buf, n, "Cold snap in the tank"); break;
        case LG_HEAT: snprintf(buf, n, "Warm spell in the tank"); break;
        case LG_DRY: snprintf(buf, n, "Dry spell, humidity fell"); break;
        case LG_SHAKE: snprintf(buf, n, "Tremor! The tank was shaken"); break;
        case LG_RF_STORM: snprintf(buf, n, "Radio storm: the air got busy"); break;
        case LG_MIST: snprintf(buf, n, "You misted the tank"); break;
        case LG_SEED: snprintf(buf, n, "You planted a seed"); break;
        case LG_PRUNE: snprintf(buf, n, "You pruned the tallest plant"); break;
        case LG_LAMP: snprintf(buf, n, "You switched on the grow light"); break;
        case LG_AWAY: snprintf(buf, n, "Away %dh; life went on", e.val); break;
        default: snprintf(buf, n, "?"); break;
    }
}

}  // namespace view
