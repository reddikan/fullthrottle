// Headless physics check: runs the race with the autopilot and prints telemetry.
#include "../app/src/main/cpp/core/game.h"
#include <stdio.h>
#include <vector>
#include <stdarg.h>
#include <stdlib.h>

void ftLog(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr); }

int main(int argc, char** argv) {
    float secs = argc > 1 ? atof(argv[1]) : 60;
    float every = argc > 2 ? atof(argv[2]) : 1.0f;
    Game g;
    bool human = getenv("HUMAN") != nullptr;
    g.autopilot = !human;
    g.init();
    bool solo = getenv("SOLO") != nullptr;
    bool nodrift = getenv("NODRIFT") != nullptr;
    Input in;
    float t = 0, next = 0, maxBeta = 0;
    const char* apath = getenv("AUDIO");
    FILE* af = apath ? fopen(apath, "wb") : nullptr;
    Synth synth; std::vector<float> ab(1600);
    while (t < secs) {
        if (solo) for (size_t i = 0; i < g.cars.size(); i++) if ((int)i != g.player) { g.cars[i].pos = V3(5000 + i * 20.0f, 0, 5000); g.cars[i].vel = V3(); }
        if (human) {
            // crude "player": full gas, bang-bang steering at the racing line, brakes for corners
            const Car& c = g.cars[g.player];
            in = Input();
            in.confirmPressed = (g.state == State::Title);
            float v = c.speed();
            V3 tgt = g.track.at(c.s + 12 + v * 0.35f, 0);
            V3 to = tgt - c.pos;
            float err = wrapAngle(atan2f(to.x, to.z) - c.yaw);
            in.steer = err > 0.04f ? -1.0f : (err < -0.04f ? 1.0f : 0.0f);
            float k = 0; for (float d = 0; d < 150; d += 10) k = fmaxf(k, fabsf(g.track.curvatureAt(c.s + d)));
            float vmax = k > 1e-4f ? sqrtf(16.0f / k) : 999;
            in.throttle = v < vmax ? 1.0f : 0.0f;
            in.brake = v > vmax + 6 ? 1.0f : 0.0f;
            if (getenv("DRIFTBTN")) in.handbrake = k > 1.0f / 90.0f && v > 25;
            maxBeta = fmaxf(maxBeta, fabsf(atan2f(dot(c.vel, c.left()), fabsf(dot(c.vel, c.fwd())) + 0.1f)) * 57.3f);
        }
        g.update(1.0f / 60, in);
        if (af) { synth.render(g.audio, ab.data(), 800, 48000); fwrite(ab.data(), 4, 1600, af); }
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
    if (af) fclose(af);
    printf("maxBeta=%.1f lapDist=%.0f place=%d\n", maxBeta, g.cars[g.player].dist, g.cars[g.player].place);
    return 0;
}
