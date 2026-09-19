// The jar scene: sky, sun/moon, plants (8 shapes), rocks, log, pond, soil
// strata and every creature. Purely a function of the World + wall clock.
#pragma once
#include <M5GFX.h>

#include "terrarium.h"

namespace scene {
constexpr int kJarL = 10, kJarR = 229, kJarT = 5, kJarB = 130, kSoilTop = 98;

// Drawn height in pixels of the plant in column `col` (shape-dependent).
int plantHeight(const terra::World& w, int col);

// dayFrac 0..1 (0 = midnight), t = seconds for animation, wind 0..1 (sound/shake).
void draw(M5Canvas& cv, const terra::World& w, float dayFrac, float t, float wind);
}  // namespace scene
