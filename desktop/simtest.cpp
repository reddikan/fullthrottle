// Headless physics check: runs the race with the autopilot and prints telemetry.
#include "../app/src/main/cpp/core/game.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

void ftLog(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr); }

int main(int argc, char** argv) {
    float secs = argc > 1 ? atof(argv[1]) : 60;
    float every = argc > 2 ? atof(argv[2]) : 1.0f;
    Game g;
    g.autopilot = true;
    g.init();
    bool solo = getenv("SOLO") != nullptr;
    bool nodrift = getenv("NODRIFT") != nullptr;
    Input in;
    float t = 0, next = 0;
    while (t < secs) {
        if (solo) for (size_t i = 0; i < g.cars.size(); i++) if ((int)i != g.player) { g.cars[i].pos = V3(5000 + i * 20.0f, 0, 5000); g.cars[i].vel = V3(); }
        g.update(1.0f / 60, in);
        if (nodrift) g.cars[g.player].handbrake = false;
        t += 1.0f / 60;
        if (t >= next) {
            next += every;
            const Car& c = g.cars[g.player];
            float vx = dot(c.vel, c.fwd()), vy = dot(c.vel, c.left());
            float beta = atan2f(vy, fabsf(vx) + 0.1f) * 57.3f;
            int crashed = 0; float avgV = 0;
            for (auto& o : g.cars) { avgV += o.speed(); if (o.speed() < 10) crashed++; }
            printf("t=%5.1f st=%d s=%6.0f lat=%5.1f v=%5.1fmph gear=%d beta=%6.1f drift=%.2f hb=%d thr=%.2f brk=%.2f steer=%5.2f place=%2d timer=%4.1f | avgAI=%4.0fmph slow=%d skids=%lld\n",
                   t, (int)g.state, c.s, c.lat, c.speed() * 2.237f, c.gear, beta, c.driftAmt, (int)c.handbrake, c.throttle, c.brake, c.steer, c.place, g.timer,
                   avgV / g.cars.size() * 2.237f, crashed, g.skidWritten);
        }
    }
    return 0;
}
