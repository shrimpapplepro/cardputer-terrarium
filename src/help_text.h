// On-device help (press H). One place for every key and how to play.
// Small 6x8 font: keep each line <= 38 characters.
#pragma once

namespace help {

constexpr int kPages = 3;
constexpr int kMaxLines = 12;

const char* const kTitles[kPages] = {"KEYS", "HOW TO PLAY", "SENSORS+JOURNAL"};

const char* const kLines[kPages][kMaxLines] = {
    {
        "1 Tank 2 Summary 3 Journal 4 Signals",
        ",  /      previous / next page",
        ";  .      scroll Journal / Help",
        "m         mist: humidity + soil up",
        "s         plant a seed (emptiest slot)",
        "p         prune the tallest plant",
        "l         grow light on for some hours",
        "r         scan WiFi + BLE right now",
        "f         speed 1x 60x 600x 3600x",
        "[  ]      clock -1h / +1h (no RTC)",
        "0  0      new tank (press twice)",
        "h         open / close this help",
    },
    {
        "A sealed tank mostly looks after",
        "itself. You only nudge it.",
        "",
        "Tank: plants, bugs, day and night.",
        "Summary: who is thriving - a bar,",
        "  an arrow, a 30-day line each.",
        "Journal: what happened, and when.",
        "Mist when the Journal says DRY or",
        "  plants look droopy. Don't flood.",
        "Seed when plants look sparse. Prune",
        "  when crowded: cuttings feed the",
        "  isopods and springtails.",
    },
    {
        "Shake it, talk to it, walk around:",
        "the sensors add tiny random nudges.",
        "WiFi + Bluetooth traffic counts too.",
        "Nothing can wipe the tank out.",
        "",
        "Journal words:",
        "Tremor - you shook it",
        "Radio storm - WiFi/BLE got busy",
        "Cold / warm / dry spell - weather",
        "Boomed / crashed / recovering -",
        "  a population swung",
        "f speeds time up to watch it grow.",
    },
};

}  // namespace help
