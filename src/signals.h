// Sensor layer: turns every number the board can measure into terra::Inputs
// plus an entropy hash. Radios run as short sequential bursts (WiFi, then BLE)
// and are torn down afterwards -- they are what exhausted the heap last time,
// so free heap is recorded around every burst and shown on the Signals page.
#pragma once
#include <Arduino.h>

#include "terrarium.h"

struct Signals {
    // motion / environment
    float ax = 0, ay = 0, az = 0;  // g
    float jerk = 0;                // |a - a_prev| in g, last sample
    float imuTempC = 0, chipTempC = 0;
    float micRms = 0, micLevel = 0;  // raw rms and 0..1 log-scaled
    float battV = 0;
    int battPct = 0;
    bool charging = false;
    // radio
    int wifiN = 0, bleN = 0;
    float wifiRssi = 0, bleRssi = 0;  // mean
    float wifiDelta = 0, bleDelta = 0;
    uint32_t scans = 0, lastScanMs = 0;
    uint8_t scanState = 0;  // 0 idle, 1 wifi, 2 ble
    // heap
    uint32_t heapFree = 0, heapMin = 0, heapLargest = 0;
    uint32_t heapBeforeScan = 0, heapAfterScan = 0, heapLow = 0;  // heapLow: lowest free seen mid-scan
    // outputs
    uint32_t entropy = 0;
    terra::Inputs in;
};

namespace sig {
extern Signals s;
void begin();
void update();    // call every loop; cheap unless a sample is due
void scanNow();   // schedule a WiFi+BLE burst immediately
terra::Inputs inputs();
}  // namespace sig
