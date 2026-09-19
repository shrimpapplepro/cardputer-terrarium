// Rendering. Draws into a 16-bit 240x135 sprite (65 KB; affordable now that the
// firmware keeps ~190 KB of heap free even mid-scan), then pushes it to the panel.
#pragma once
#include <M5GFX.h>

#include "signals.h"
#include "terrarium.h"

namespace view {
enum Page : uint8_t { PAGE_JAR, PAGE_SUMMARY, PAGE_JOURNAL, PAGE_SIGNALS, kPages };

struct State {
    Page page = PAGE_JAR;
    int journalScroll = 0;
    uint8_t speed = 0;  // index into main's speed table, for the HUD
    const char* speedLabel = "1x";
    float dayFrac = 0;
    char toast[40] = "";
    uint32_t toastUntil = 0;
};

bool begin(M5GFX* display);
void draw(const terra::World& w, const State& st, const Signals& sg);
void push();
const uint8_t* framebuffer(size_t* len);  // for serial screenshots
void logText(char* buf, size_t n, const terra::LogEntry& e);
}  // namespace view
