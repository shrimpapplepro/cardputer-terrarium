#include "signals.h"

#include <M5Cardputer.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>

namespace sig {
Signals s;

namespace {

constexpr uint32_t kScanIntervalMs = 5UL * 60UL * 1000UL;
constexpr uint32_t kFirstScanMs = 20UL * 1000UL;
constexpr int kMaxDev = 32;

struct Dev { uint8_t id[6]; int8_t rssi; };
Dev prevWifi[kMaxDev], curWifi[kMaxDev];
Dev prevBle[kMaxDev], curBle[kMaxDev];
int nPrevWifi = 0, nPrevBle = 0;
bool haveWifi = false, haveBle = false;

uint32_t ent = 0x811C9DC5u;
inline void mix(uint32_t x) { ent = (ent ^ x) * 16777619u; ent ^= ent >> 15; }

int16_t micBuf[128];
bool micValid = false;
float shakeLevel = 0, prevAx = 0, prevAy = 0, prevAz = 1;
float chipEma = 0;
bool chipEmaInit = false;
uint32_t tImu = 0, tSlow = 0, tScan = 0, tBleStart = 0;
bool scanForced = false;
volatile bool bleDone = false;

inline float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

void noteHeap() {
    s.heapFree = ESP.getFreeHeap();
    s.heapMin = ESP.getMinFreeHeap();
    s.heapLargest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (s.scanState && s.heapFree < s.heapLow) s.heapLow = s.heapFree;
}

// How different is this scan from the last one? 0..1. `useSet` also compares
// which devices are present (stable for WiFi BSSIDs, meaningless for BLE whose
// addresses rotate).
float deltaOf(const Dev* prev, int np, const Dev* cur, int nc, bool useSet) {
    if (np == 0 && nc == 0) return 0;
    float countChange = fabsf((float)nc - (float)np) / fmaxf(1.0f, fmaxf((float)nc, (float)np));
    float meanP = 0, meanC = 0;
    for (int i = 0; i < np; i++) meanP += prev[i].rssi;
    for (int i = 0; i < nc; i++) meanC += cur[i].rssi;
    if (np) meanP /= np;
    if (nc) meanC /= nc;
    float meanShift = clamp01(fabsf(meanC - meanP) / 8.0f);
    if (!useSet) return clamp01(0.5f * countChange + 0.5f * meanShift);
    int match = 0;
    float rssiSum = 0;
    for (int i = 0; i < nc; i++)
        for (int j = 0; j < np; j++)
            if (!memcmp(cur[i].id, prev[j].id, 6)) { match++; rssiSum += fabsf((float)cur[i].rssi - prev[j].rssi); break; }
    float uni = (float)(nc + np - match);
    float setChange = uni > 0 ? (uni - match) / uni : 0;
    float rssiMove = match ? clamp01(rssiSum / match / 12.0f) : 0;
    return clamp01(0.35f * countChange + 0.35f * setChange + 0.30f * rssiMove);
}

volatile bool wifiDone = false;

void finishWifi() {
    // Arduino's own SCAN_DONE handler has already pulled the records out of the driver.
    int n = WiFi.scanComplete();
    if (n < 0) n = 0;
    int c = 0;
    float sum = 0;
    for (int i = 0; i < n && c < kMaxDev; i++) {
        uint8_t* b = WiFi.BSSID(i);
        if (!b) continue;
        memcpy(curWifi[c].id, b, 6);
        curWifi[c].rssi = (int8_t)WiFi.RSSI(i);
        for (int k = 0; k < 6; k++) mix(b[k]);
        mix((uint32_t)(int32_t)curWifi[c].rssi);
        sum += curWifi[c].rssi;
        c++;
    }
    WiFi.scanDelete();  // frees the wrapper's result buffer; forgetting this leaks ~650 B per scan
    s.wifiN = n;
    s.wifiRssi = c ? sum / c : 0;
    s.wifiDelta = haveWifi ? deltaOf(prevWifi, nPrevWifi, curWifi, c, true) : 0;
    memcpy(prevWifi, curWifi, sizeof(Dev) * c);
    nPrevWifi = c;
    haveWifi = true;
}

void bleDoneCb(NimBLEScanResults) { bleDone = true; }

void finishBle() {
    NimBLEScan* scan = NimBLEDevice::getScan();
    NimBLEScanResults r = scan->getResults();
    int n = r.getCount(), c = 0;
    float sum = 0;
    for (int i = 0; i < n && c < kMaxDev; i++) {
        NimBLEAdvertisedDevice d = r.getDevice(i);
        const uint8_t* a = d.getAddress().getNative();
        memcpy(curBle[c].id, a, 6);
        curBle[c].rssi = (int8_t)d.getRSSI();
        for (int k = 0; k < 6; k++) mix(a[k]);
        mix((uint32_t)(int32_t)curBle[c].rssi);
        sum += curBle[c].rssi;
        c++;
    }
    s.bleN = n;
    s.bleRssi = c ? sum / c : 0;
    s.bleDelta = haveBle ? deltaOf(prevBle, nPrevBle, curBle, c, false) : 0;
    memcpy(prevBle, curBle, sizeof(Dev) * c);
    nPrevBle = c;
    haveBle = true;
    scan->clearResults();
}

void sampleImu() {
    if (!M5.Imu.isEnabled()) return;
    if (!M5.Imu.update()) return;
    auto d = M5.Imu.getImuData();
    s.ax = d.accel.x; s.ay = d.accel.y; s.az = d.accel.z;
    float j = fabsf(s.ax - prevAx) + fabsf(s.ay - prevAy) + fabsf(s.az - prevAz);
    prevAx = s.ax; prevAy = s.ay; prevAz = s.az;
    s.jerk = j;
    shakeLevel = fmaxf(shakeLevel * 0.985f, clamp01(j / 1.2f));
    mix((uint32_t)(int32_t)(s.ax * 100000)); mix((uint32_t)(int32_t)(s.ay * 100000));
    mix((uint32_t)(int32_t)(s.az * 100000)); mix(micros());
}

void sampleMic() {
    if (!M5.Mic.isEnabled()) return;
    if (M5.Mic.isRecording()) return;
    if (micValid) {
        int64_t acc = 0;
        int32_t mean = 0;
        for (int i = 0; i < 128; i++) mean += micBuf[i];
        mean /= 128;
        for (int i = 0; i < 128; i++) { int32_t v = micBuf[i] - mean; acc += (int64_t)v * v; }
        s.micRms = sqrtf((float)acc / 128.0f);
        s.micLevel = clamp01((log10f(s.micRms + 1.0f) - 1.2f) / 2.4f);
        mix((uint32_t)acc);
    }
    M5.Mic.record(micBuf, 128, 16000);
    micValid = true;
}

void sampleSlow() {
    s.battPct = M5.Power.getBatteryLevel();
    s.battV = M5.Power.getBatteryVoltage() / 1000.0f;
    s.charging = M5.Power.isCharging() == m5::Power_Class::is_charging;
    s.chipTempC = temperatureRead();
    float t;
    if (M5.Imu.isEnabled() && M5.Imu.getTemp(&t)) s.imuTempC = t;
    if (!chipEmaInit) { chipEma = s.chipTempC; chipEmaInit = true; }
    chipEma += (s.chipTempC - chipEma) * 0.02f;  // ~ half-hour at the 5 s cadence
    mix((uint32_t)(s.battV * 1000)); mix((uint32_t)(s.chipTempC * 100)); mix((uint32_t)(s.imuTempC * 100));
}

void scanMachine() {
    uint32_t now = millis();
    switch (s.scanState) {
        case 0:
            if (scanForced || now - tScan >= (s.scans == 0 ? kFirstScanMs : kScanIntervalMs)) {
                scanForced = false;
                noteHeap();
                s.heapBeforeScan = s.heapFree;
                s.heapLow = s.heapFree;
                WiFi.mode(WIFI_STA);
                delay(100);
                wifi_scan_config_t cfg = {};
                cfg.show_hidden = true;
                cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
                cfg.scan_time.active.min = 100;
                cfg.scan_time.active.max = 300;
                // The Arduino scanNetworks() wrapper fails intermittently on this board; the raw
                // ESP-IDF call is reliable. Async, so the UI keeps drawing while it runs.
                wifiDone = false;
                esp_err_t e = esp_wifi_scan_start(&cfg, false);
                if (e != ESP_OK) Serial.printf("[wifi] scan start failed: %s\n", esp_err_to_name(e));
                s.scanState = 1;
                tScan = now;
            }
            break;
        case 1: {
            if (!wifiDone) {
                if (now - tScan < 15000) break;  // still scanning
                esp_wifi_scan_stop();
            }
            noteHeap();
            Serial.printf("[wifi] scan %s after %lums\n", wifiDone ? "done" : "TIMED OUT", (unsigned long)(now - tScan));
            finishWifi();
            WiFi.mode(WIFI_OFF);
            NimBLEDevice::init("");
            NimBLEScan* scan = NimBLEDevice::getScan();
            scan->setActiveScan(false);
            scan->setInterval(97);
            scan->setWindow(37);
            bleDone = false;
            scan->start(4, bleDoneCb, false);
            tBleStart = now;
            s.scanState = 2;
            break;
        }
        case 2:
            noteHeap();
            if (bleDone || now - tBleStart > 8000) {
                finishBle();
                NimBLEDevice::deinit(true);
                s.scans++;
                s.lastScanMs = now;
                s.scanState = 0;
                tScan = now;
                noteHeap();
                s.heapAfterScan = s.heapFree;
                Serial.printf("[scan %lu] wifi=%d(%.0fdBm d=%.2f) ble=%d(%.0fdBm d=%.2f) heap %lu -> %lu (low %lu)\n",
                              (unsigned long)s.scans, s.wifiN, s.wifiRssi, s.wifiDelta, s.bleN, s.bleRssi, s.bleDelta,
                              (unsigned long)s.heapBeforeScan, (unsigned long)s.heapAfterScan, (unsigned long)s.heapLow);
            }
            break;
    }
}

}  // namespace

void begin() {
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) { wifiDone = true; }, ARDUINO_EVENT_WIFI_SCAN_DONE);
    M5.Mic.begin();
    tScan = millis();
    sampleSlow();
}

void update() {
    uint32_t now = millis();
    if (now - tImu >= 50) { tImu = now; sampleImu(); sampleMic(); }
    if (now - tSlow >= 5000) { tSlow = now; sampleSlow(); noteHeap(); }
    scanMachine();
}

void scanNow() { scanForced = true; }

terra::Inputs inputs() {
    terra::Inputs in;
    in.shake = shakeLevel;
    in.sound = s.micLevel;
    float drift = (s.chipTempC - chipEma) / 3.0f;
    if (s.battPct > 0 && s.battPct < 20 && !s.charging) drift -= 0.5f;  // sagging battery cools the jar
    in.tempDrift = drift < -1 ? -1 : (drift > 1 ? 1 : drift);
    in.battery = s.battPct / 100.0f;
    in.wifiDelta = s.wifiDelta;
    in.bleDelta = s.bleDelta;
    in.rfDensity = clamp01((s.wifiN + s.bleN) / 40.0f);
    in.entropy = ent;
    s.entropy = ent;
    s.in = in;
    return in;
}

}  // namespace sig
