// Portable terrarium simulation core. No Arduino/ESP includes: the same code
// is built for the device and for the host harness (tools/harness.cpp).
#pragma once
#include <stdint.h>

namespace terra {

constexpr int kCols = 16;        // plant slots across the jar
constexpr int kLogSize = 32;     // journal ring buffer entries
constexpr int kHistDays = 30;    // days of population history kept
constexpr uint32_t kSaveVersion = 3;

// A sealed glass tank. Identifiers predate the tank design; the mapping is:
//   SP_CATERPILLAR = fungus-gnat larvae   SP_BUTTERFLY = adult fungus gnats
//   SP_WORM        = isopods              SP_SPIDER    = jumping spiders
//   SP_FUNGUS      = mold & mushrooms     SP_SPRINGTAIL = springtails (mold grazers)
enum Species : uint8_t {
    SP_PLANT, SP_APHID, SP_CATERPILLAR, SP_BUTTERFLY,
    SP_LADYBUG, SP_SPIDER, SP_WORM, SP_FUNGUS, SP_SPRINGTAIL,
    kSpecies
};

// Normalised sensor readings, produced by the hardware layer. All 0..1 except
// tempDrift (-1..1). The hw layer reduces every raw number (RSSI values, AP
// counts, IMU axes, mic, battery, chip temp...) into these plus `entropy`.
// The sim treats all of it as WEAK: bounded nudges, never a lethal driver.
struct Inputs {
    float shake = 0;      // IMU jerk / shift energy
    float sound = 0;      // mic level
    float tempDrift = 0;  // slow drift from die temperature / battery sag
    float battery = 1;
    float wifiDelta = 0;  // how much the visible WiFi set/RSSIs changed since last scan
    float bleDelta = 0;   // same for Bluetooth
    float rfDensity = 0;  // how crowded the air is (AP + BLE count, normalised)
    uint32_t entropy = 0; // hash of every raw number this sample; stirs the RNG
};

enum LogKind : uint8_t {
    LG_BORN, LG_BLOOM, LG_CRASH, LG_RECOVER, LG_FIRST_BUTTERFLY,
    LG_COLD, LG_HEAT, LG_DRY, LG_SHAKE, LG_RF_STORM,
    LG_MIST, LG_SEED, LG_PRUNE, LG_LAMP, LG_AWAY,
};

struct LogEntry {
    uint16_t day;   // jar age in whole days when it happened
    uint8_t kind;   // LogKind
    uint8_t arg;    // Species, or 0
    int16_t val;    // kind-specific (e.g. percent, hours away)
    uint16_t pad;
};

// Everything that persists. Plain data so it can be memcpy'd to flash.
struct World {
    uint32_t version;
    uint32_t rng;
    uint32_t ticks;
    float age;  // days since the jar was created

    // Jar
    float temp, hum, soil, nut, air, det;
    float p[kCols];  // plants per column, 0..1 biomass
    // Animals / decomposers, relative density 0..1. Indexed by Species except
    // SP_PLANT (use plantMean()).
    float aphid, cat, butter, lady, spider, worm, fungus, spring;

    // Perturbation filter state (low-passed sensor input)
    float fShake, fSound, fTemp, fWifi, fBle, fRf;
    // Rare capped events
    float evTemp, evHum, evLeft;
    float lamp;  // player-added light, decays

    // Summary / journal
    float peak[kSpecies];
    uint8_t bloomed[kSpecies];  // detector latches
    uint8_t crashed[kSpecies];
    uint8_t seenButterfly;
    uint16_t logHead, logCount;
    LogEntry log[kLogSize];
    uint16_t histDay;   // whole day of the newest history slot
    uint8_t histHead;   // newest slot
    uint8_t hist[kHistDays][kSpecies];  // daily mean density, 0..255
    float histAcc[kSpecies];
    float histT;             // days accumulated into histAcc
    uint8_t shakeLatch, rfLatch;
};

void init(World& w, uint32_t seed);

// Advance by dtDays. dayFrac is wall-clock time of day at the START of the
// interval (0..1, 0 = midnight). Internally sub-stepped, so any dt is safe.
void step(World& w, float dtDays, float dayFrac, const Inputs& in);

// Catch-up after a power-off. Same step() with synthetic mild inputs, capped
// at maxDays. Returns days actually simulated.
float fastForward(World& w, float days, float startDayFrac, float maxDays = 7.0f, bool logAway = true);

// Player actions
void mist(World& w);
void plantSeed(World& w);
void prune(World& w);
void lampPulse(World& w);

// View / summary helpers
float density(const World& w, Species s);   // 0..1
float plantMean(const World& w);
float health(const World& w);               // 0..1 overall "how alive is it"
int trend(const World& w, Species s);       // -1 falling, 0 steady, +1 rising (vs ~1 day ago)
const char* speciesName(Species s);
float light(float dayFrac);
bool sane(const World& w);                  // false if any value is NaN/out of range

}  // namespace terra
