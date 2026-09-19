// Host-side stability harness: runs the *same* sim/terrarium.cpp the firmware
// builds, across many seeds and input amplitudes, and checks nothing dies.
//   ./harness            seed sweep + catch-up check (exit 1 on failure)
//   ./harness -v SEED    print one seed's population curve
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <random>

#include "../sim/terrarium.h"
using namespace terra;

struct Scenario { const char* name; float amp; bool worst; };

// Random-walk sensor stand-in. amp scales everything; worst pins to extremes.
struct Sensors {
    std::mt19937 g;
    float shake = 0, sound = 0, drift = 0, wifi = 0, ble = 0;
    Sensors(uint32_t s) : g(s) {}
    float u() { return std::uniform_real_distribution<float>(0, 1)(g); }
    Inputs next(float amp, bool worst) {
        auto walk = [&](float v, float lo, float hi) {
            v += (u() - 0.5f) * 0.3f;
            return std::min(hi, std::max(lo, v));
        };
        Inputs in;
        if (worst) {  // every channel slammed to the rail, drift flipping sign
            in.shake = 1; in.sound = 1; in.wifiDelta = 1; in.bleDelta = 1; in.rfDensity = 1;
            in.tempDrift = u() < 0.5f ? -1.f : 1.f;
            in.battery = 0;
        } else {
            shake = walk(shake, 0, 1); sound = walk(sound, 0, 1); drift = walk(drift, -1, 1);
            wifi = walk(wifi, 0, 1); ble = walk(ble, 0, 1);
            in.shake = shake * amp; in.sound = sound * amp; in.tempDrift = drift * amp;
            in.wifiDelta = wifi * amp; in.bleDelta = ble * amp; in.rfDensity = 0.5f * amp;
            in.battery = 1.0f - 0.3f * u();
        }
        in.entropy = (uint32_t)g();
        return in;
    }
};

static const float kTick = 1.0f / 288.0f;  // 5 min
static const float kSettle = 10.0f;        // days ignored at start

int main(int argc, char** argv) {
    if (argc >= 3 && !strcmp(argv[1], "-v")) {
        World w; init(w, (uint32_t)atoi(argv[2])); Sensors s(atoi(argv[2]));
        for (int d = 0; d <= 90; d++) {
            if (d % 3 == 0) {
                printf("d%2d", d);
                for (int k = 0; k < kSpecies; k++) printf(" %s=%.3f", speciesName((Species)k), density(w, (Species)k));
                printf(" | T%.1f H%.2f S%.2f N%.2f A%.2f D%.2f hp%.2f\n", w.temp, w.hum, w.soil, w.nut, w.air, w.det, health(w));
            }
            for (int i = 0; i < 288; i++) step(w, kTick, 0.25f + (d + i / 288.0f), s.next(0.5f, false));
        }
        printf("\njournal (%d entries):\n", w.logCount);
        for (int i = 0; i < w.logCount; i++) {
            const LogEntry& e = w.log[(w.logHead + kLogSize - w.logCount + i) % kLogSize];
            printf("  day %3d kind %2d arg %d val %d\n", e.day, e.kind, e.arg, e.val);
        }
        return 0;
    }

    const Scenario scen[] = {{"quiet", 0.0f, false}, {"mild", 0.4f, false}, {"strong", 1.0f, false}, {"WORST-CASE", 1.0f, true}};
    const int kSeeds = 200, kDays = 90;
    bool fail = false;

    for (const Scenario& sc : scen) {
        float minD[kSpecies], maxD[kSpecies]; double sumD[kSpecies];
        int lowSeeds[kSpecies] = {0};
        for (int k = 0; k < kSpecies; k++) { minD[k] = 9; sumD[k] = 0; maxD[k] = 0; }
        float tMin = 99, tMax = -99, hMin = 9, sMin = 9, nMin = 9, aMin = 9;
        long n = 0; int insane = 0;
        float hp = 0;
        for (int seed = 1; seed <= kSeeds; seed++) {
            World w; init(w, seed); Sensors s(seed * 7919);
            float lo[kSpecies]; for (int k = 0; k < kSpecies; k++) lo[k] = 9;
            for (int d = 0; d < kDays; d++)
                for (int i = 0; i < 288; i++) {
                    step(w, kTick, fmodf(0.25f + i / 288.0f, 1.0f), s.next(sc.amp, sc.worst));
                    if (!sane(w)) { insane++; break; }
                    if (d >= kSettle) {
                        for (int k = 0; k < kSpecies; k++) {
                            float v = density(w, (Species)k);
                            lo[k] = std::min(lo[k], v); sumD[k] += v; maxD[k] = std::max(maxD[k], v);
                        }
                        n++;
                        tMin = std::min(tMin, w.temp); tMax = std::max(tMax, w.temp);
                        hMin = std::min(hMin, w.hum); sMin = std::min(sMin, w.soil);
                        nMin = std::min(nMin, w.nut); aMin = std::min(aMin, w.air);
                    }
                }
            hp += health(w);
            // "alive" = never dropped to within 3x of the dormant refuge floor
            for (int k = 0; k < kSpecies; k++) {
                minD[k] = std::min(minD[k], lo[k]);
                if (lo[k] < 0.006f) lowSeeds[k]++;
            }
        }
        printf("== %-10s (%d seeds x %d d)  health@end %.2f  insane=%d\n", sc.name, kSeeds, kDays, hp / kSeeds, insane);
        printf("   %-13s %7s %7s %7s %s\n", "species", "min", "mean", "max", "seeds<0.006");
        for (int k = 0; k < kSpecies; k++) {
            printf("   %-13s %7.3f %7.3f %7.3f %d\n", speciesName((Species)k), minD[k], (double)sumD[k] / n, maxD[k], lowSeeds[k]);
            if (lowSeeds[k] > 0) fail = true;
        }
        printf("   jar: T %.1f..%.1f  hum>=%.2f  soil>=%.2f  nut>=%.2f  air>=%.2f\n\n", tMin, tMax, hMin, sMin, nMin, aMin);
        if (insane) fail = true;
    }

    // catch-up check: fastForward should land near a fine-grained run
    double diff = 0; int cnt = 0;
    for (int seed = 1; seed <= 30; seed++) {
        World a; init(a, seed); Sensors s(seed);
        for (int i = 0; i < 20 * 288; i++) step(a, kTick, fmodf(0.25f + i / 288.0f, 1.0f), s.next(0.4f, false));
        World b = a;
        for (int i = 0; i < 5 * 288; i++) step(a, kTick, fmodf(0.25f + i / 288.0f, 1.0f), s.next(0.4f, false));
        fastForward(b, 5.0f, 0.25f);
        for (int k = 0; k < kSpecies; k++) { diff += fabsf(density(a, (Species)k) - density(b, (Species)k)); cnt++; }
        if (!sane(b)) fail = true;
    }
    printf("catch-up 5d vs fine: mean |delta density| = %.4f\n", diff / cnt);
    printf("%s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
