// Render the tank scene on the host: grows a jar for N days with the real sim,
// then writes F frames (RGB, 4x) to stdout as raw video for ffmpeg.
//   ./preview [days] [seconds] [hourOfDay] [seed] [shakeAt] > frames.rgb
#include <stdio.h>
#include <stdlib.h>
#include "scene.h"

int main(int argc, char** argv) {
    float days = argc > 1 ? atof(argv[1]) : 30, secs = argc > 2 ? atof(argv[2]) : 12;
    float hour = argc > 3 ? atof(argv[3]) : 12;
    uint32_t seed = argc > 4 ? atoi(argv[4]) : 7;
    float shakeAt = argc > 5 ? atof(argv[5]) : -1;
    static terra::World w;
    terra::init(w, seed);
    terra::Inputs in;
    for (float d = 0; d < days; d += 0.05f) terra::step(w, 0.05f, d - (int)d, in);
    fprintf(stderr, "spring=%.3f worm=%.3f cat=%.3f aphid=%.3f lady=%.3f butter=%.3f spider=%.3f fungus=%.3f\n", w.spring, w.worm,
            w.cat, w.aphid, w.lady, w.butter, w.spider, w.fungus);
    static M5Canvas cv;
    const int fps = 20, S = 4;
    static uint8_t row[M5Canvas::W * S * 3];
    for (int f = 0; f < secs * fps; f++) {
        float t = 1000.0f + f / (float)fps;
        float jolt = (shakeAt >= 0 && f / (float)fps > shakeAt && f / (float)fps < shakeAt + 0.3f) ? 0.8f : 0.0f;
        scene::draw(cv, w, hour / 24.0f, t, 0.0f, jolt);
        for (int y = 0; y < M5Canvas::H; y++) {
            for (int x = 0; x < M5Canvas::W; x++) {
                uint16_t c = cv.fb[y * M5Canvas::W + x];
                uint8_t r = (c >> 11) << 3, g = ((c >> 5) & 63) << 2, b = (c & 31) << 3;
                for (int k = 0; k < S; k++) { uint8_t* p = row + (x * S + k) * 3; p[0] = r; p[1] = g; p[2] = b; }
            }
            for (int k = 0; k < S; k++) fwrite(row, 1, sizeof row, stdout);
        }
    }
}
