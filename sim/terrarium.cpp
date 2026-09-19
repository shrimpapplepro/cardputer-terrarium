#include "terrarium.h"

#include <math.h>
#include <string.h>

namespace terra {

namespace {

// ---- tunables (rates are per simulated day) --------------------------------
constexpr float kMaxSub = 1.0f / 96.0f;  // integrate in <=15 min chunks
constexpr float kFloor = 0.002f;         // dormant eggs/spores/seeds: refuge

constexpr float rP = 1.6f, senP = 0.06f, spreadP = 0.04f;
constexpr float gA = 0.25f, gC = 0.6f, kGraze = 0.15f;
constexpr float rA = 1.4f, dAd = 0.25f, kA = 0.15f, KA = 0.9f;
constexpr float mL = 1.3f, eL = 0.5f, dLd = 0.2f, kL = 0.3f;
constexpr float bC = 1.0f, mC = 0.25f, dCd = 0.10f, KC = 0.6f;
constexpr float dBd = 0.15f;
constexpr float mS = 0.5f, eS = 0.45f, dSd = 0.12f, kS = 0.2f, altPrey = 0.15f, KSs = 0.16f  /* territorial cap */;
constexpr float rW = 0.9f, dWd = 0.10f, KW = 0.6f, cW = 0.9f;
constexpr float rF = 0.9f, dFd = 0.10f, KF = 0.7f, cF = 0.7f;
constexpr float kD = 0.15f, yN = 1.2f;
constexpr float rSp = 1.0f, dSpd = 0.2f, KSp = 0.6f, cSp = 0.3f, gSpF = 0.06f, kFg = 0.15f;

// journal thresholds per species: {bloom, crash}
constexpr float kBloom[kSpecies] = {0.75f, 0.50f, 0.40f, 0.30f, 0.30f, 0.30f, 0.50f, 0.55f, 0.50f};
constexpr float kCrash[kSpecies] = {0.12f, 0.01f, 0.01f, 0.008f, 0.01f, 0.01f, 0.02f, 0.02f, 0.02f};

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float sq(float x) { return x * x; }

inline uint32_t rngNext(World& w) {
    uint32_t x = w.rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    w.rng = x ? x : 0x9E3779B9u;
    return w.rng;
}
inline float rand01(World& w) { return (rngNext(w) >> 8) * (1.0f / 16777216.0f); }

void logPush(World& w, LogKind k, uint8_t arg = 0, int16_t val = 0) {
    LogEntry& e = w.log[w.logHead];
    e.day = (uint16_t)clampf(w.age, 0, 65000);
    e.kind = k; e.arg = arg; e.val = val; e.pad = 0;
    w.logHead = (w.logHead + 1) % kLogSize;
    if (w.logCount < kLogSize) w.logCount++;
}

void detectAndRecord(World& w, float dt) {
    for (int s = 0; s < kSpecies; s++) {
        float d = density(w, (Species)s);
        if (d > w.peak[s]) w.peak[s] = d;
        w.histAcc[s] += d * dt;
        if (w.age < 0.25f) continue;  // let the founding population settle
        if (!w.bloomed[s] && d > kBloom[s]) { w.bloomed[s] = 1; logPush(w, LG_BLOOM, s, (int16_t)(d * 100)); }
        else if (w.bloomed[s] && d < kBloom[s] * 0.8f) w.bloomed[s] = 0;
        if (!w.crashed[s] && d < kCrash[s] * 2.0f) { w.crashed[s] = 1; logPush(w, LG_CRASH, s, (int16_t)(d * 100)); }
        else if (w.crashed[s] && d > kCrash[s] * 5.0f) { w.crashed[s] = 0; logPush(w, LG_RECOVER, s, (int16_t)(d * 100)); }
    }
    if (!w.seenButterfly && w.butter > 0.02f) { w.seenButterfly = 1; logPush(w, LG_FIRST_BUTTERFLY); }
    w.histT += dt;
    if ((uint16_t)w.age != w.histDay) {
        w.histHead = (w.histHead + 1) % kHistDays;
        for (int s = 0; s < kSpecies; s++) {
            float m = w.histT > 0 ? w.histAcc[s] / w.histT : 0;
            w.hist[w.histHead][s] = (uint8_t)clampf(m * 255.0f, 0, 255);
            w.histAcc[s] = 0;
        }
        w.histT = 0;
        w.histDay = (uint16_t)w.age;
    }
}

void substep(World& w, float dt, float dayFrac, const Inputs& in) {
    // 1. stir RNG with sensor entropy
    if (in.entropy) {
        w.rng = ((w.rng << 5) | (w.rng >> 27)) ^ (in.entropy * 2654435761u);
        if (!w.rng) w.rng = 0x9E3779B9u;
    }

    // 2. low-pass the inputs (~30 min time constant)
    float a = 1.0f - expf(-dt * 48.0f);
    w.fShake += (clampf(in.shake, 0, 1) - w.fShake) * a;
    w.fSound += (clampf(in.sound, 0, 1) - w.fSound) * a;
    w.fTemp += (clampf(in.tempDrift, -1, 1) - w.fTemp) * a;
    w.fWifi += (clampf(in.wifiDelta, 0, 1) - w.fWifi) * a;
    w.fBle += (clampf(in.bleDelta, 0, 1) - w.fBle) * a;
    w.fRf += (clampf(in.rfDensity, 0, 1) - w.fRf) * a;

    if (!w.shakeLatch && w.fShake > 0.6f) { w.shakeLatch = 1; logPush(w, LG_SHAKE); }
    else if (w.shakeLatch && w.fShake < 0.25f) w.shakeLatch = 0;
    float rfAct = 0.5f * (w.fWifi + w.fBle);
    if (!w.rfLatch && rfAct > 0.6f) { w.rfLatch = 1; logPush(w, LG_RF_STORM); }
    else if (w.rfLatch && rfAct < 0.3f) w.rfLatch = 0;

    // 3. rare, capped events (cold snap / heat / dry spell)
    if (w.evLeft > 0) {
        w.evLeft -= dt;
        if (w.evLeft <= 0) { w.evLeft = 0; w.evTemp = 0; w.evHum = 0; }
    } else if (rand01(w) < 0.15f * dt) {
        uint32_t k = rngNext(w) % 3;
        w.evLeft = 0.25f;
        if (k == 0) { w.evTemp = -3.0f; logPush(w, LG_COLD); }
        else if (k == 1) { w.evTemp = 3.0f; logPush(w, LG_HEAT); }
        else { w.evHum = -0.10f; logPush(w, LG_DRY); }
    }

    // 4. light
    float L = light(dayFrac) * (0.9f + 0.1f * clampf(in.battery, 0, 1));
    L = L * (1.0f - 0.08f * w.fSound);
    w.lamp = fmaxf(0.0f, w.lamp - dt * 4.0f);
    L = clampf(L + 0.6f * w.lamp, 0.0f, 1.0f);

    // 5. temperature (jar swings only a few degrees)
    float Teq = 20.0f + 3.0f * L + 1.5f * w.fTemp + w.evTemp;
    w.temp += (Teq - w.temp) * fminf(1.0f, 8.0f * dt);

    float Pm = plantMean(w);

    // 6. humidity: condensation buffers it hard
    float Heq = 0.88f - 0.02f * (w.temp - 22.0f) / 3.0f + w.evHum + 0.03f * (w.fWifi - w.fBle);
    w.hum += (Heq - w.hum) * fminf(1.0f, 6.0f * dt) + 0.08f * Pm * L * dt;
    w.hum = clampf(w.hum, 0.4f, 1.0f);

    // 7. factors
    float fT = expf(-sq((w.temp - 22.0f) / 7.0f));
    float fTa = expf(-sq((w.temp - 22.0f) / 9.0f));
    float fS = clampf(w.soil / (w.soil + 0.25f) * 1.25f, 0, 1);
    float fN = clampf(w.nut / (w.nut + 0.15f) * 1.15f, 0, 1);
    float fAir = clampf(1.0f - 1.5f * fmaxf(0.0f, 0.35f - w.air), 0.3f, 1.0f);
    float fHum = clampf((w.hum - 0.6f) / 0.3f, 0.0f, 1.0f);

    // 8. plants (per column)
    float A = w.aphid, C = w.cat, B = w.butter, Lb = w.lady, S = w.spider, W = w.worm, F = w.fungus, Sp = w.spring;
    float ftot = (gA * A + gC * C) * Pm / (Pm + kGraze);  // mean biomass eaten/day
    float pollin = 1.0f + 2.0f * B + 0.3f * w.fRf;         // pollinators + drifting spores
    float newP[kCols];
    float growthSum = 0, senSum = 0;
    float base = rP * L * fT * fminf(fS, fN);
    for (int i = 0; i < kCols; i++) {
        float pi = w.p[i];
        float l = w.p[(i + kCols - 1) % kCols], r = w.p[(i + 1) % kCols];
        float crowd = (pi + 0.5f * (l + r)) * 0.5f;
        float growth = base * pi * fmaxf(0.0f, 1.0f - crowd);
        float thirst = 0.1f * fmaxf(0.0f, 0.2f - w.soil) / 0.2f;
        float sen = (senP + thirst) * pi;
        float graze = ftot * pi / (Pm + 1e-3f);
        float spread = spreadP * pollin * 0.5f * (l + r) * (1.0f - pi) * (0.3f + L);
        newP[i] = clampf(pi + (growth - sen - graze + spread) * dt, kFloor, 1.0f);
        growthSum += growth; senSum += sen;
    }
    memcpy(w.p, newP, sizeof(newP));
    float growthMean = growthSum / kCols, senMean = senSum / kCols;

    // 9. animals & decomposers
    float nA = A, nC = C, nB = B, nL = Lb, nS = S, nW = W, nF = F, nSp = Sp;

    float predL = mL * Lb * A / (A + kL) * fTa;  // aphids eaten
    nA += (rA * A * fTa * fAir * (Pm / (Pm + kA)) * (1.0f - A / (KA * Pm + 0.02f)) - predL - dAd * A) * dt;
    nL += (eL * predL - dLd * Lb) * dt;

    float prey = C + 0.5f * B + 0.5f * Lb + 0.15f * Sp + altPrey;
    float intake = mS * S * prey / (prey + kS) * fTa;
    float lossC = intake * C / prey, lossB = intake * 0.5f * B / prey, lossL = intake * 0.5f * Lb / prey;
    nS += (eS * intake * fmaxf(0.0f, 1.0f - S / KSs) - dSd * S) * dt;
    nL -= lossL * dt;
    float lossSp = intake * 0.15f * Sp / prey;

    float brood = bC * B * fTa * fAir * (Pm / (Pm + kA)) * (1.0f - C / (KC * Pm + 0.02f));
    nC += (brood - mC * C - lossC - dCd * C) * dt;
    nB += (0.7f * mC * C - dBd * B - lossB) * dt;

    float detF = w.det / (w.det + kD);
    nW += (rW * W * detF * fTa * (1.0f - W / KW) - dWd * W - 0.15f * w.fShake * W) * dt;
    float fungF = F / (F + kFg);
    nF += (rF * F * fTa * fHum * detF * (1.0f - F / KF) - dFd * F - gSpF * Sp * fungF) * dt;
    nSp += (rSp * Sp * fTa * (1.0f * detF + 0.3f * fungF) * (1.0f - Sp / KSp) - dSpd * Sp - lossSp - 0.1f * w.fShake * Sp) * dt;

    float deaths = dAd * A + dLd * Lb + dSd * S + dCd * C + dBd * B + dWd * W + dFd * F + dSpd * Sp;

    w.aphid = clampf(nA, kFloor, 1);
    w.lady = clampf(nL, kFloor, 1);
    w.spider = clampf(nS, kFloor, 1);
    w.cat = clampf(nC, kFloor, 1);
    w.butter = clampf(nB, kFloor, 1);
    w.worm = clampf(nW, kFloor, 1);
    w.fungus = clampf(nF, kFloor, 1);
    w.spring = clampf(nSp, kFloor, 1);

    // 10. matter cycle: decomposition returns nutrients, everything else feeds detritus
    float dec = (cW * W + cF * F + cSp * Sp) * detF;
    w.det = clampf(w.det + (senMean + 0.7f * ftot + 0.3f * deaths - dec) * dt, 0.0f, 1.5f);
    float mix = 0.02f * w.fShake;  // shaking stirs the soil a little
    w.nut = clampf(w.nut + (dec - yN * growthMean + 0.01f - 0.01f * w.nut + mix) * dt, 0.0f, 1.0f);

    // 11. soil moisture and air
    float rain = 0.25f * fmaxf(0.0f, w.hum - 0.7f);
    w.soil = clampf(w.soil + (rain - 0.09f * w.soil - 0.05f * w.fSound * w.soil - 0.1f * growthMean) * dt, 0.0f, 1.0f);
    float animals = A + C + B + Lb + S;
    w.air = clampf(w.air + (0.6f * L * Pm - 0.3f * (animals + 0.5f * (W + F)) + (0.5f - w.air)) * dt, 0.0f, 1.0f);

    w.age += dt;
    w.ticks++;
    detectAndRecord(w, dt);
}

}  // namespace

float light(float dayFrac) {
    float h = (dayFrac - floorf(dayFrac)) * 24.0f;
    if (h < 6.0f || h > 18.0f) return 0.0f;
    return powf(fmaxf(0.0f, sinf(3.14159265f * (h - 6.0f) / 12.0f)), 0.8f);
}

float plantMean(const World& w) {
    float s = 0;
    for (int i = 0; i < kCols; i++) s += w.p[i];
    return s / kCols;
}

float density(const World& w, Species s) {
    switch (s) {
        case SP_PLANT: return plantMean(w);
        case SP_APHID: return w.aphid;
        case SP_CATERPILLAR: return w.cat;
        case SP_BUTTERFLY: return w.butter;
        case SP_LADYBUG: return w.lady;
        case SP_SPIDER: return w.spider;
        case SP_WORM: return w.worm;
        case SP_FUNGUS: return w.fungus;
        case SP_SPRINGTAIL: return w.spring;
        default: return 0;
    }
}

const char* speciesName(Species s) {
    static const char* n[kSpecies] = {"Plants", "Aphids", "Gnat larvae", "Fungus gnats",
                                      "Ladybugs", "Spiders", "Isopods", "Fungus", "Springtails"};
    return s < kSpecies ? n[s] : "?";
}

float health(const World& w) {
    // Each species scores 1.0 once it's comfortably above its crash line.
    float sum = 0;
    for (int s = 0; s < kSpecies; s++) sum += clampf(density(w, (Species)s) / (kCrash[s] * 8.0f), 0, 1);
    return sum / kSpecies;
}

int trend(const World& w, Species s) {
    if (w.age < 1.5f) return 0;
    float ref = w.hist[w.histHead][s] / 255.0f;
    float d = density(w, s);
    float diff = d - ref;
    if (fabsf(diff) < 0.01f + 0.1f * ref) return 0;
    return diff > 0 ? 1 : -1;
}

bool sane(const World& w) {
    auto ok = [](float x, float lo, float hi) { return x == x && x >= lo && x <= hi; };
    if (!ok(w.temp, -10, 50) || !ok(w.hum, 0, 1) || !ok(w.soil, 0, 1) || !ok(w.nut, 0, 1) ||
        !ok(w.air, 0, 1) || !ok(w.det, 0, 1.5f))
        return false;
    for (int i = 0; i < kCols; i++)
        if (!ok(w.p[i], 0, 1)) return false;
    for (int s = SP_APHID; s < kSpecies; s++)
        if (!ok(density(w, (Species)s), 0, 1)) return false;
    return true;
}

void init(World& w, uint32_t seed) {
    memset(&w, 0, sizeof(w));
    w.version = kSaveVersion;
    w.rng = seed ? seed : 0x1234567u;
    w.temp = 22; w.hum = 0.85f; w.soil = 0.5f; w.nut = 0.4f; w.air = 0.5f; w.det = 0.1f;
    for (int i = 0; i < kCols; i++) w.p[i] = 0.2f + 0.2f * rand01(w);
    w.aphid = 0.10f; w.cat = 0.05f; w.butter = 0.03f; w.lady = 0.04f;
    w.spider = 0.05f; w.worm = 0.20f; w.fungus = 0.20f; w.spring = 0.25f;
    logPush(w, LG_BORN);
}

void step(World& w, float dtDays, float dayFrac, const Inputs& in) {
    if (dtDays <= 0) return;
    int n = (int)ceilf(dtDays / kMaxSub);
    float sub = dtDays / n;
    for (int i = 0; i < n; i++) {
        substep(w, sub, dayFrac, in);
        dayFrac += sub;
        dayFrac -= floorf(dayFrac);
    }
}

float fastForward(World& w, float days, float startDayFrac, float maxDays, bool logAway) {
    if (days <= 0) return 0;
    if (days > maxDays) days = maxDays;
    if (logAway && days > 0.05f) logPush(w, LG_AWAY, 0, (int16_t)clampf(days * 24.0f, 0, 32000));
    const float chunk = 1.0f / 48.0f;
    float done = 0, dayFrac = startDayFrac;
    while (done < days) {
        float dt = fminf(chunk, days - done);
        Inputs in;
        in.shake = 0.05f * rand01(w);
        in.sound = 0.10f * rand01(w);
        in.tempDrift = (rand01(w) - 0.5f) * 0.4f;
        in.wifiDelta = 0.3f * rand01(w);
        in.bleDelta = 0.3f * rand01(w);
        in.rfDensity = 0.3f;
        in.entropy = rngNext(w);
        step(w, dt, dayFrac, in);
        dayFrac += dt; dayFrac -= floorf(dayFrac);
        done += dt;
    }
    return done;
}

void mist(World& w) {
    w.hum = clampf(w.hum + 0.10f, 0, 1);
    w.soil = clampf(w.soil + 0.15f, 0, 1);
    logPush(w, LG_MIST);
}

void plantSeed(World& w) {
    int best = 0;
    for (int i = 1; i < kCols; i++) if (w.p[i] < w.p[best]) best = i;
    w.p[best] = clampf(w.p[best] + 0.08f, 0, 1);
    logPush(w, LG_SEED);
}

void prune(World& w) {
    int best = 0;
    for (int i = 1; i < kCols; i++) if (w.p[i] > w.p[best]) best = i;
    w.det = clampf(w.det + 0.5f * w.p[best] * 0.2f, 0, 1.5f);
    w.p[best] = clampf(w.p[best] * 0.5f, kFloor, 1);
    logPush(w, LG_PRUNE);
}

void lampPulse(World& w) {
    w.lamp = 1.0f;
    logPush(w, LG_LAMP);
}

}  // namespace terra
