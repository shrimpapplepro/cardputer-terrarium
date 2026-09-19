// Terrarium for the M5Stack Cardputer ADV.
// A sealed jar ecosystem (sim/terrarium.*) perturbed weakly by every sensor
// the board has. Serial (USB-Serial/JTAG) is a full debug channel: type `h`.
#include <M5Cardputer.h>
#include <Preferences.h>

#include "help_text.h"
#include "signals.h"
#include "terrarium.h"
#include "version.h"
#include "view.h"

using namespace terra;

namespace {

World world;
view::State vs;
Preferences prefs;

const float kSpeeds[] = {1, 60, 600, 3600};
const char* kSpeedLabels[] = {"1x", "60x", "600x", "3600x"};
uint8_t speedIdx = 0;

uint32_t lastStepMs = 0, lastFrameMs = 0, lastSaveMs = 0, lastKeyMs = 0, resetArmedUntil = 0;
double simAccumDays = 0;
float simFrac = 0.5f;
bool displayOn = true;
uint8_t dimLevel = 255;
String serialLine;

// ---- time -----------------------------------------------------------------

int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

// No RTC on this board: a software clock (millis-driven, trimmed from the
// keyboard or serial). Time of day only drives light; the sim never needs a date.
float clockBaseFrac = 0.5f;
uint32_t clockBaseMs = 0;

float clockFrac() {
    double f = clockBaseFrac + (double)(millis() - clockBaseMs) / 86400000.0;
    return (float)(f - floor(f));
}
void setClockFrac(float f) { clockBaseFrac = f - floorf(f); clockBaseMs = millis(); }
void trimClock(int minutes) { setClockFrac(clockFrac() + minutes / 1440.0f); }

// ---- persistence -----------------------------------------------------------

void saveWorld(const char* why) {
    prefs.begin("terra", false);
    prefs.putBytes("w", &world, sizeof(World));
    prefs.putFloat("fr", clockFrac());
    prefs.end();
    lastSaveMs = millis();
    Serial.printf("[save] %s, %u bytes\n", why, (unsigned)sizeof(World));
}

void freshWorld(uint32_t seed) {
    init(world, seed ^ esp_random() ^ sig::s.entropy);
    Serial.printf("[world] new jar\n");
}

void loadWorld() {
    prefs.begin("terra", true);
    size_t n = prefs.getBytesLength("w");
    bool ok = false;
    float savedFrac = 0.5f;
    if (n == sizeof(World)) {
        prefs.getBytes("w", &world, sizeof(World));
        savedFrac = prefs.getFloat("fr", 0.5f);
        ok = world.version == kSaveVersion && sane(world);
    }
    prefs.end();
    if (!ok) { freshWorld(0xC0FFEE); return; }
    setClockFrac(savedFrac);
    simFrac = savedFrac;
    Serial.printf("[wake] restored jar age %.1f d, health %.2f (no RTC: time away is unknown, not simulated)\n", world.age, health(world));
}

// ---- actions & keys ---------------------------------------------------------

void toast(const char* s) {
    snprintf(vs.toast, sizeof vs.toast, "%s", s);
    vs.toastUntil = millis() + 1800;
}

void handleKey(char c) {
    lastKeyMs = millis();
    if (c == 'h') {  // help toggles from anywhere
        if (vs.page == view::PAGE_HELP) vs.page = vs.helpReturn;
        else { vs.helpReturn = vs.page; vs.page = view::PAGE_HELP; vs.helpPage = 0; }
        return;
    }
    if (vs.page == view::PAGE_HELP) {  // in help: only paging and leaving; nothing acts on the tank
        int n = help::kPages;
        if (c == ',' || c == ';') vs.helpPage = (vs.helpPage + n - 1) % n;
        else if (c == '/' || c == '.') vs.helpPage = (vs.helpPage + 1) % n;
        else if (c >= '1' && c <= '4') vs.page = (view::Page)(c - '1');
        return;
    }
    switch (c) {
        case '1': vs.page = view::PAGE_JAR; break;
        case '2': vs.page = view::PAGE_SUMMARY; break;
        case '3': vs.page = view::PAGE_JOURNAL; vs.journalScroll = 0; break;
        case '4': vs.page = view::PAGE_SIGNALS; break;
        case ',': vs.page = (view::Page)((vs.page + view::kPages - 1) % view::kPages); break;
        case '/': vs.page = (view::Page)((vs.page + 1) % view::kPages); break;
        case ';': if (vs.journalScroll > 0) vs.journalScroll--; break;
        case '.': if (vs.journalScroll < kLogSize) vs.journalScroll++; break;
        case 'm': mist(world); toast("Misted"); break;
        case 's': plantSeed(world); toast("Seed planted"); break;
        case 'p': prune(world); toast("Pruned"); break;
        case 'l': lampPulse(world); toast("Lamp on"); break;
        case 'r': sig::scanNow(); toast("Scanning the air..."); break;
        case 'f':
            speedIdx = (speedIdx + 1) % (sizeof kSpeeds / sizeof kSpeeds[0]);
            simAccumDays = 0;
            toast(kSpeedLabels[speedIdx]);
            break;
        case '[': trimClock(-60); toast("Clock -1h"); break;
        case ']': trimClock(60); toast("Clock +1h"); break;
        case '0':
            if (millis() < resetArmedUntil) {
                freshWorld(millis());
                saveWorld("reset");
                toast("New jar");
                resetArmedUntil = 0;
            } else {
                resetArmedUntil = millis() + 3000;
                toast("Press 0 again: NEW JAR");
            }
            break;
        default: break;
    }
}

void pollKeys() {
    M5Cardputer.update();
    // isChange() is a destructive latch: it must be called on every poll,
    // releases included, or the next press compares equal and is dropped.
    bool changed = M5Cardputer.Keyboard.isChange();
    bool pressed = M5Cardputer.Keyboard.isPressed() != 0;
    if (!pressed || !changed) return;
    auto keys = M5Cardputer.Keyboard.keysState();
    bool wasOff = !displayOn;
    lastKeyMs = millis();
    if (wasOff) return;  // first key only wakes the screen
    for (char c : keys.word) handleKey(c);
}

// ---- simulation clock ---------------------------------------------------------

void stepSim() {
    uint32_t now = millis();
    uint32_t interval = speedIdx == 0 ? 10000 : 1000;
    if (now - lastStepMs < interval) return;
    double realDays = (double)(now - lastStepMs) / 86400000.0;
    lastStepMs = now;
    float dt = (float)(realDays * kSpeeds[speedIdx]);
    if (speedIdx == 0) simFrac = clockFrac();
    step(world, dt, simFrac, sig::inputs());
    if (speedIdx != 0) { simFrac += dt; simFrac -= floorf(simFrac); }
    if (!sane(world)) { Serial.println("[world] INSANE state, resetting"); freshWorld(now); }
}

// ---- serial debug channel --------------------------------------------------

void printStatus() {
    const Signals& s = sig::s;
    Serial.printf("terrarium v%s %s\n", TERRARIUM_VERSION, TERRARIUM_GIT_SHA);
    Serial.printf("age=%.3f health=%.2f speed=%s page=%d frac=%.3f\n", world.age, health(world), kSpeedLabels[speedIdx], vs.page, simFrac);
    Serial.printf("jar T=%.2f H=%.3f soil=%.3f nut=%.3f air=%.3f det=%.3f lamp=%.2f ev=%.2f/%.2f/%.2f\n", world.temp, world.hum,
                  world.soil, world.nut, world.air, world.det, world.lamp, world.evTemp, world.evHum, world.evLeft);
    Serial.printf("pop");
    for (int i = 0; i < kSpecies; i++) Serial.printf(" %s=%.3f", speciesName((Species)i), density(world, (Species)i));
    Serial.println();
    Serial.printf("imu a=%.3f,%.3f,%.3f jerk=%.3f imuT=%.1f chipT=%.1f | mic rms=%.1f lvl=%.2f | bat %.2fV %d%% chg=%d\n", s.ax, s.ay,
                  s.az, s.jerk, s.imuTempC, s.chipTempC, s.micRms, s.micLevel, s.battV, s.battPct, s.charging);
    Serial.printf("rf wifi n=%d %.0fdBm d=%.2f | ble n=%d %.0fdBm d=%.2f | scans=%lu state=%d | in shake=%.2f sound=%.2f drift=%+.2f rf=%.2f ent=%08lX\n",
                  s.wifiN, s.wifiRssi, s.wifiDelta, s.bleN, s.bleRssi, s.bleDelta, (unsigned long)s.scans, s.scanState, s.in.shake,
                  s.in.sound, s.in.tempDrift, s.in.rfDensity, (unsigned long)s.entropy);
    Serial.printf("heap free=%lu min=%lu big=%lu | scan before=%lu after=%lu low=%lu\n", (unsigned long)s.heapFree,
                  (unsigned long)s.heapMin, (unsigned long)s.heapLargest, (unsigned long)s.heapBeforeScan, (unsigned long)s.heapAfterScan,
                  (unsigned long)s.heapLow);
}

void printJournal() {
    for (int i = 0; i < world.logCount; i++) {
        const LogEntry& e = world.log[(world.logHead + kLogSize - world.logCount + i) % kLogSize];
        char b[64];
        view::logText(b, sizeof b, e);
        Serial.printf("  d%-3d %s\n", e.day, b);
    }
}

void dumpFramebuffer() {
    size_t len;
    const uint8_t* fb = view::framebuffer(&len);  // 240*135 RGB565, big-endian in memory
    Serial.printf("FBSTART 240 135 16\n");
    static const char* hex = "0123456789abcdef";
    char line[961];
    for (int y = 0; y < 135; y++) {
        for (int i = 0; i < 480; i++) {
            uint8_t v = fb[y * 480 + i];
            line[i * 2] = hex[v >> 4]; line[i * 2 + 1] = hex[v & 15];
        }
        line[960] = 0;
        Serial.println(line);
    }
    Serial.println("FBEND");
}

void handleSerialLine(String l) {
    l.trim();
    if (!l.length()) return;
    char c = l[0];
    String arg = l.substring(1);
    switch (c) {
        case 'h':
            Serial.println("s status | j journal | P screenshot | k<keys> inject keys | w scan now | f speed | v save\n"
                           "x<days> fast-forward | z<seed> new jar | T<HHMM> set clock | o open/wake screen | G<n> show page n\n(keys: kh opens the on-device help)");
            break;
        case 's': printStatus(); break;
        case 'j': printJournal(); break;
        case 'P': view::draw(world, vs, sig::s); dumpFramebuffer(); break;
        case 'k': for (char k : arg) handleKey(k); Serial.printf("ok keys=%s\n", arg.c_str()); break;
        case 'w': sig::scanNow(); Serial.println("ok scan scheduled"); break;
        case 'f': handleKey('f'); Serial.printf("speed=%s\n", kSpeedLabels[speedIdx]); break;
        case 'v': saveWorld("serial"); break;
        case 'x': {
            float d = arg.toFloat();
            float done = fastForward(world, d, simFrac, 30.0f, false);  // debug: not a real absence
            Serial.printf("fast-forwarded %.2f d, health %.2f\n", done, health(world));
            break;
        }
        case 'z': freshWorld(arg.toInt()); Serial.println("ok new jar"); break;
        case 'T': {
            int v = arg.toInt();
            setClockFrac(((v / 100) * 60 + v % 100) / 1440.0f);
            simFrac = clockFrac();
            Serial.printf("clock set, frac=%.3f\n", clockFrac());
            break;
        }
        case 'o': lastKeyMs = millis(); Serial.println("ok"); break;
        case 'G': vs.page = (view::Page)(arg.toInt() % view::kPages); Serial.printf("page=%d\n", vs.page); break;
        default: Serial.println("? try h"); break;
    }
}

void pollSerial() {
    while (Serial.available()) {
        char ch = (char)Serial.read();
        if (ch == '\n' || ch == '\r') { handleSerialLine(serialLine); serialLine = ""; }
        else if (serialLine.length() < 64) serialLine += ch;
    }
}

}  // namespace

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    Serial.begin(115200);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(90);
    if (!view::begin(&M5Cardputer.Display)) Serial.println("[view] sprite alloc FAILED");
    {  // start the clock near build time; trim with [ ] or serial T<HHMM>
        int hh = atoi(__TIME__), mm = atoi(__TIME__ + 3);
        setClockFrac((hh * 60 + mm) / 1440.0f);
    }
    sig::begin();
    loadWorld();
    lastStepMs = millis();
    lastKeyMs = millis();
    vs.speedLabel = kSpeedLabels[0];
    toast("Press H for help");
    Serial.printf("[boot] terrarium v%s %s up, heap free=%lu big=%lu frac=%.3f\n", TERRARIUM_VERSION, TERRARIUM_GIT_SHA, (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), clockFrac());
}

void loop() {
    pollKeys();
    pollSerial();
    sig::update();
    stepSim();

    uint32_t now = millis();
    // Dim after 30 s idle, screen off after 3 min. The jar keeps living.
    uint32_t idle = now - lastKeyMs;
    bool wantOn = idle < 180000;
    uint8_t wantDim = idle < 30000 ? 90 : 12;
    if (wantOn != displayOn) {
        displayOn = wantOn;
        M5Cardputer.Display.setBrightness(displayOn ? wantDim : 0);
        dimLevel = wantDim;
    } else if (displayOn && wantDim != dimLevel) {
        M5Cardputer.Display.setBrightness(wantDim);
        dimLevel = wantDim;
    }

    if (displayOn && now - lastFrameMs >= 100) {
        lastFrameMs = now;
        vs.dayFrac = speedIdx == 0 ? clockFrac() : simFrac;
        vs.speedLabel = kSpeedLabels[speedIdx];
        view::draw(world, vs, sig::s);
        view::push();
    }

    if (now - lastSaveMs > 10UL * 60UL * 1000UL) saveWorld("autosave");
    delay(2);
}
