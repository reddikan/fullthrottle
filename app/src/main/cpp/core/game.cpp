// Full Throttle — simulation, AI, race flow and camera.
#include "render.h"
#include <algorithm>
#include <stdio.h>

static const float kMaxSpeed = 76.0f;    // m/s  (~170 mph)
static const float kBoostSpeed = 10.0f;  // extra top speed while boosting
static const float kGearTop[] = {0, 17, 29, 41, 53, 65, 90};

static float frand(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFFFF) / 16777216.0f; }

void Game::init() {
    track.build();
    const Livery livs[8] = {
        {V3(0.07f, 0.24f, 0.78f), V3(0.95f, 0.95f, 0.97f), V3(1, 1, 1), "FULL THROTTLE", "YOU", 7},
        {V3(0.78f, 0.07f, 0.06f), V3(0.97f, 0.97f, 0.97f), V3(1, 1, 1), "KOMET COLA", "KOM", 11},
        {V3(0.96f, 0.74f, 0.06f), V3(0.06f, 0.06f, 0.07f), V3(0.05f, 0.05f, 0.06f), "BIG SKY", "BSK", 24},
        {V3(0.06f, 0.44f, 0.22f), V3(0.98f, 0.82f, 0.10f), V3(1, 1, 1), "RIDGELINE", "RDG", 3},
        {V3(0.06f, 0.06f, 0.07f), V3(1.0f, 0.45f, 0.05f), V3(1.0f, 0.55f, 0.1f), "VOLT", "VLT", 88},
        {V3(0.90f, 0.90f, 0.92f), V3(0.80f, 0.08f, 0.12f), V3(0.80f, 0.08f, 0.12f), "PRISM BANK", "PRB", 19},
        {V3(0.95f, 0.42f, 0.06f), V3(0.08f, 0.12f, 0.35f), V3(1, 1, 1), "DUSK COFFEE", "DSK", 42},
        {V3(0.40f, 0.11f, 0.58f), V3(0.10f, 0.80f, 0.74f), V3(1, 1, 1), "NOVA", "NOV", 5},
    };
    cars.resize(8);
    for (int i = 0; i < 8; i++) cars[i].liv = livs[i];
    player = 0;
    resetRace();
    state = State::Title;
    stateTime = 0;
}

void Game::resetRace() {
    // grid: player starts 5th
    int order[8] = {1, 2, 3, 4, 0, 5, 6, 7};
    for (int slot = 0; slot < 8; slot++) {
        Car& c = cars[order[slot]];
        Livery liv = c.liv;
        c = Car();
        c.liv = liv;
        int row = slot / 2;
        float lat = (slot % 2) ? 3.6f : -3.6f;
        float s = -10.0f - row * 11.0f - (slot % 2) * 4.0f;
        c.pos = track.at(s, lat);
        V3 tg = track.tangentAt(s);
        c.yaw = atan2f(tg.x, tg.z);
        c.idx = track.nearest(c.pos, -1);
        c.s = track.wrapS(s);
        c.prevS = c.s;
        c.dist = s;
        c.ai = order[slot] != player;
        c.skill = 0.94f + 0.012f * (7 - slot) + frand(rngState) * 0.02f;
        c.laneBias = (slot % 2 ? 1.0f : -1.0f) * (1.5f + frand(rngState) * 2.0f);
    }
    cars[player].skill = autopilot ? 1.07f : 1.0f;
    spray.clear();
    raceTime = 0;
    finishOrder = 0;
    banner.clear();
    camYaw = cars[player].yaw;
    camPos = cars[player].pos - cars[player].fwd() * 7 + V3(0, 2.4f, 0);
}

// ------------------------------------------------------------------ AI
void Game::driveAI(Car& c, float dt) {
    float v = c.speed();
    // look-ahead target
    float look = 9.0f + v * 0.42f;
    float kAhead = track.curvatureAt(c.s + look * 0.8f + 15.0f);
    float racing = clampf(kAhead * 1100.0f, -6.0f, 6.0f);  // move to the inside of upcoming turns
    float want = racing * 0.75f + c.laneBias * 0.5f + c.avoid;
    want = clampf(want, -track.halfW + 1.6f, track.halfW - 1.6f);
    // avoidance: look for cars just ahead in our lane
    float avoidTarget = 0, slow = 1.0f;
    for (const Car& o : cars) {
        if (&o == &c) continue;
        float ds = o.dist - c.dist;
        if (ds < 0 || ds > 22) continue;
        float dl = o.lat - (c.lat);
        if (fabsf(dl) < 2.8f) {
            float sideRoom = o.lat > 0 ? -1.0f : 1.0f;
            avoidTarget = sideRoom * 3.6f;
            if (ds < 9 && o.speed() < v) slow = 0.97f;
            if (ds < 7 && fabsf(dl) < 2.2f) slow = fminf(slow, 0.92f);
        }
    }
    c.avoid = approach(c.avoid, avoidTarget, 1.5f, dt);
    V3 target = track.at(c.s + look, want);
    V3 to = target - c.pos;
    float targetYaw = atan2f(to.x, to.z);
    float err = wrapAngle(targetYaw - c.yaw);
    c.steer = clampf(-err * 2.6f, -1, 1);   // +steer = right = yaw decreasing

    // speed planning from curvature ahead
    float aLat = 21.0f * c.skill, brakeA = 17.0f;
    float allow = kMaxSpeed * c.skill;
    for (float d = 0; d < 170; d += 6) {
        float k = fabsf(track.curvatureAt(c.s + d));
        float vmax = k > 1e-4f ? sqrtf(aLat / k) : 999.0f;
        float vOk = sqrtf(vmax * vmax + 2 * brakeA * d);
        allow = fminf(allow, vOk);
    }
    // gentle rubber band so the pack stays together
    float gap = cars[player].dist - c.dist;
    if (c.ai) allow *= clampf(1.0f + gap * 0.00025f, 0.95f, 1.05f);
    allow *= slow;
    float diff = allow - v;
    c.throttle = diff > 0 ? clampf(diff * 0.35f, 0.25f, 1.0f) : 0.0f;
    c.brake = diff < -2.0f ? clampf(-diff * 0.12f, 0, 1) : 0.0f;
    c.drift = false;
    c.boosting = false;
    if (!c.ai) {  // autopilot extras for attract mode
        c.drift = fabsf(kAhead) > 1.0f / 95.0f && v > 32;
        c.boosting = c.boost > 0.25f && fabsf(track.curvatureAt(c.s + 60)) < 1.0f / 600.0f && v > 40;
    }
}

// ------------------------------------------------------------------ physics
void Game::physics(Car& c, float dt) {
    V3 f = c.fwd(), r = c.right();
    float vf = dot(c.vel, f), vl = dot(c.vel, r);
    float v = fabsf(vf);
    bool offroad = fabsf(c.lat) > track.halfW + 1.8f;

    // boost
    if (c.boosting && c.boost > 0.0f) { c.boost = fmaxf(0, c.boost - dt * 0.28f); }
    else c.boosting = false;
    float top = kMaxSpeed + (c.boosting ? kBoostSpeed : 0);
    if (offroad) top *= 0.62f;

    float accel = 0;
    if (c.throttle > 0) {
        float frac = clampf(vf / top, 0, 1.2f);
        accel = c.throttle * (16.0f * (1.0f - frac * frac) + (c.boosting ? 9.0f : 0));
        if (vf > top) accel = -6.0f;
    }
    if (c.brake > 0) {
        if (vf > 1.0f) accel -= c.brake * 30.0f;
        else accel -= c.brake * 8.0f;  // reverse
    }
    accel -= vf * (offroad ? 0.55f : 0.012f);
    vf += accel * dt;
    if (vf < -12) vf = -12;

    // steering
    float maxYaw = fminf(2.1f, (fabsf(vf) + 0.5f) / (11.0f + fabsf(vf) * 0.78f));
    if (fabsf(vf) < 0.5f) maxYaw *= fabsf(vf) * 2;
    bool drifting = c.drift && v > 22;
    float yawTarget = -c.steer * maxYaw * (drifting ? 1.55f : 1.0f) * (vf < 0 ? -1.0f : 1.0f);
    c.yawRate = approach(c.yawRate, yawTarget, drifting ? 4.0f : 9.0f, dt);
    c.yaw = wrapAngle(c.yaw + c.yawRate * dt);

    // grip: kill lateral velocity, lower while drifting
    float grip = drifting ? 1.3f : (offroad ? 4.0f : 7.5f);
    vl *= expf(-grip * dt);
    if (drifting) {
        vf -= 2.2f * dt;
        c.driftTime += dt;
        c.boost = fminf(1.0f, c.boost + dt * 0.16f);
    } else {
        c.driftTime = 0;
    }
    // speed slowly charges boost too
    c.boost = fminf(1.0f, c.boost + dt * 0.012f * (v / kMaxSpeed));

    // re-project on new heading
    f = c.fwd(); r = c.right();
    c.vel = f * vf + r * vl;
    c.pos += c.vel * dt;
    c.pos.y = 0;

    // track coordinates
    c.idx = track.nearest(c.pos, c.idx);
    V3 rel = c.pos - track.P[c.idx];
    c.lat = dot(rel, track.R[c.idx]);
    float s = track.S[c.idx] + dot(rel, track.T[c.idx]);
    s = track.wrapS(s);
    float ds = s - c.prevS;
    if (ds > track.length * 0.5f) ds -= track.length;
    if (ds < -track.length * 0.5f) ds += track.length;
    c.dist += ds;
    c.prevS = s; c.s = s;

    // walls
    float limit = track.wallOff - 1.15f;
    if (fabsf(c.lat) > limit) {
        float sd = signf(c.lat);
        V3 R = track.R[c.idx];
        c.pos -= R * (sd * (fabsf(c.lat) - limit));
        c.lat = sd * limit;
        float into = dot(c.vel, R) * sd;
        if (into > 0) {
            c.vel -= R * (sd * into * 1.4f);
            c.vel = c.vel * 0.93f;
            c.yawRate += sd * into * 0.04f;
            c.bump = fminf(1.0f, c.bump + into * 0.06f);
            if (&c == &cars[player]) { shake = fminf(1.0f, shake + into * 0.05f); audio.impacts++; }
        }
    }
    c.steerVis = approach(c.steerVis, c.steer, 10, dt);
    c.bump = fmaxf(0, c.bump - dt * 2);
}

void Game::collide() {
    for (size_t i = 0; i < cars.size(); i++)
        for (size_t j = i + 1; j < cars.size(); j++) {
            Car& a = cars[i]; Car& b = cars[j];
            V3 d = b.pos - a.pos;
            if (d.x * d.x + d.z * d.z > 49) continue;
            // two circles per car
            for (int p = -1; p <= 1; p += 2)
                for (int q = -1; q <= 1; q += 2) {
                    V3 pa = a.pos + a.fwd() * (p * 1.3f), pb = b.pos + b.fwd() * (q * 1.3f);
                    V3 dd = pb - pa; dd.y = 0;
                    float dist = len(dd), minD = 2.15f;
                    if (dist < minD && dist > 1e-4f) {
                        V3 nrm = dd * (1.0f / dist);
                        float push = (minD - dist) * 0.5f;
                        a.pos -= nrm * push; b.pos += nrm * push;
                        float rv = dot(b.vel - a.vel, nrm);
                        if (rv < 0) {
                            V3 imp = nrm * (rv * 0.6f);
                            a.vel += imp; b.vel -= imp;
                            a.yawRate += rv * 0.01f * p; b.yawRate -= rv * 0.01f * q;
                            if ((int)i == player || (int)j == player) {
                                shake = fminf(1.0f, shake + fabsf(rv) * 0.04f);
                                if (fabsf(rv) > 2.0f) audio.impacts++;
                            }
                        }
                    }
                }
        }
}

// ------------------------------------------------------------------ spray
void Game::updateSpray(float dt) {
    for (auto& p : spray) {
        p.life -= dt;
        p.v = p.v * expf(-1.6f * dt);
        p.v.y -= 1.2f * dt;
        p.p += p.v * dt;
        if (p.p.y < 0.1f) { p.p.y = 0.1f; p.v.y = 0; }
        p.size += p.grow * dt;
    }
    spray.erase(std::remove_if(spray.begin(), spray.end(), [](const Particle& p) { return p.life <= 0; }), spray.end());
    for (Car& c : cars) {
        float v = c.speed();
        if (v < 6) continue;
        if (len(c.pos - camPos) > 260) continue;
        float rate = v * 0.75f * (c.drift ? 1.8f : 1.0f) + (c.boosting ? 14 : 0);
        c.sprayAcc += rate * dt;
        V3 f = c.fwd(), r = c.right();
        while (c.sprayAcc >= 1.0f && spray.size() < 2600) {
            c.sprayAcc -= 1.0f;
            float side = frand(rngState) < 0.5f ? -1.0f : 1.0f;
            bool rear = frand(rngState) < 0.8f;
            Particle p;
            p.p = c.pos + r * (side * 0.85f) + f * (rear ? -1.7f : 1.3f) + V3(0, 0.25f, 0);
            V3 spread = r * (side * frand(rngState) * 2.5f) + V3(0, 1.0f + frand(rngState) * 2.6f, 0);
            p.v = c.vel * (0.55f + frand(rngState) * 0.25f) - f * (2.0f + frand(rngState) * 4.0f) + spread;
            p.maxLife = p.life = 0.7f + frand(rngState) * 0.9f;
            p.size = 0.35f + frand(rngState) * 0.3f;
            p.grow = 1.3f + frand(rngState) * 1.8f;
            p.alpha = clampf(v / 55.0f, 0.15f, 1.0f) * (rear ? 0.16f : 0.10f);
            spray.push_back(p);
        }
        c.sprayAcc = fminf(c.sprayAcc, 3.0f);
    }
}

// ------------------------------------------------------------------ race order
void Game::updatePlaces() {
    std::vector<int> ord(cars.size());
    for (size_t i = 0; i < cars.size(); i++) ord[i] = (int)i;
    std::sort(ord.begin(), ord.end(), [&](int a, int b) {
        const Car& A = cars[a]; const Car& B = cars[b];
        if (A.finished != B.finished) return A.finished;
        if (A.finished) return A.finishTime < B.finishTime;
        return A.dist > B.dist;
    });
    for (size_t i = 0; i < ord.size(); i++) cars[ord[i]].place = (int)i + 1;
}

// ------------------------------------------------------------------ camera
void Game::updateCamera(float dt) {
    Car& c = cars[player];
    if (state == State::Title) {
        float a = globalTime * 0.18f;
        V3 center = track.at(-28, 0) + V3(0, 0.5f, 0);
        V3 tg = track.tangentAt(0);
        float base = atan2f(tg.x, tg.z);
        camPos = center + V3(sinf(base + 2.4f + a) * 20, 3.2f + sinf(a * 0.7f) * 0.8f, cosf(base + 2.4f + a) * 20);
        camLook = center + V3(0, 0.2f, 0);
        camFov = 52;
        camYaw = c.yaw;
        return;
    }
    float v = c.speed();
    float velYaw = v > 5 ? atan2f(c.vel.x, c.vel.z) : c.yaw;
    float tgtYaw = c.yaw + wrapAngle(velYaw - c.yaw) * 0.45f;
    camYaw = camYaw + wrapAngle(tgtYaw - camYaw) * (1.0f - expf(-6.5f * dt));
    V3 fwd(sinf(camYaw), 0, cosf(camYaw));
    float back = 6.4f + v * 0.005f, up = 2.05f;
    V3 want = c.pos - fwd * back + V3(0, up, 0);
    camPos = lerp(camPos, want, 1.0f - expf(-14.0f * dt));
    camPos.y = up;
    camLook = c.pos + fwd * 7.0f + V3(0, 1.05f, 0);
    float fovT = 56.0f + v * 0.07f + (c.boosting ? 6.0f : 0.0f);
    camFov = approach(camFov, fovT, 3, dt);
    if (shake > 0) {
        float sx = (frand(rngState) - 0.5f) * shake * 0.35f, sy = (frand(rngState) - 0.5f) * shake * 0.25f;
        camPos += V3(sx, sy, 0); camLook += V3(sx, sy, 0);
        shake = fmaxf(0, shake - dt * 2.5f);
    }
}

// ------------------------------------------------------------------ main step
void Game::step(float dt, const Input& in) {
    Car& pc = cars[player];
    bool racing = state == State::Race || state == State::Finished;
    for (size_t i = 0; i < cars.size(); i++) {
        Car& c = cars[i];
        if ((int)i == player && state == State::Race && !autopilot) {
            c.steer = in.steer; c.throttle = in.throttle; c.brake = in.brake;
            c.drift = in.drift || (in.brake > 0.3f && fabsf(in.steer) > 0.5f && c.speed() > 25);
            c.boosting = in.boost && c.boost > 0.02f;
        } else if (racing) {
            driveAI(c, dt);
            if ((int)i == player && state == State::Finished) c.throttle *= 0.7f;
        } else {
            c.throttle = 0; c.brake = 1; c.steer = 0; c.drift = false; c.boosting = false;
        }
        if (racing) physics(c, dt);
        if (racing && !c.finished && c.dist >= laps * track.length) {
            c.finished = true; c.finishTime = raceTime;
        }
    }
    if (racing) collide();
    updatePlaces();

    // lap banners for the player
    int lap = pc.lap(track.length);
    if (state == State::Race && lap != pc.lastLap) {
        if (lap == laps && pc.lastLap > 0) { banner = "FINAL LAP"; bannerTime = 2.5f; }
        pc.lastLap = lap;
    }
    if (state == State::Race && pc.finished) {
        state = State::Finished; stateTime = 0;
        banner = "FINISH"; bannerTime = 3.0f;
    }
}

void Game::update(float dt, const Input& in) {
    dt = clampf(dt, 0.0f, 0.05f);
    globalTime += dt;
    stateTime += dt;
    if (bannerTime > 0) bannerTime -= dt;

    switch (state) {
        case State::Title:
            if (in.confirmPressed || in.startPressed || (autopilot && stateTime > 4.5f)) {
                resetRace(); state = State::Countdown; stateTime = 0;
            }
            break;
        case State::Countdown:
            if (stateTime >= 3.0f) { state = State::Race; stateTime = 0; banner = "GO!"; bannerTime = 1.0f; }
            break;
        case State::Race:
            if (in.startPressed) { pausedFrom = State::Race; state = State::Paused; return; }
            if (in.selectPressed) { resetRace(); state = State::Countdown; stateTime = 0; return; }
            break;
        case State::Finished:
            if (stateTime > 2.0f && (in.confirmPressed || in.startPressed)) { resetRace(); state = State::Countdown; stateTime = 0; }
            break;
        case State::Paused:
            if (in.startPressed || in.confirmPressed) { state = pausedFrom; }
            if (in.selectPressed) { resetRace(); state = State::Countdown; stateTime = 0; }
            return;
    }

    if (state == State::Race || state == State::Finished) raceTime += dt;
    const float h = 1.0f / 120.0f;
    static float acc = 0;
    acc += dt;
    while (acc >= h) { step(h, in); acc -= h; }
    updateSpray(dt);
    updateCamera(dt);

    // lightning
    nextLightning -= dt;
    if (nextLightning <= 0) {
        flash = 1.0f;
        nextLightning = 16.0f + frand(rngState) * 20.0f;
        audio.thunderPower = 0.6f + frand(rngState) * 0.4f;
        audio.thunders++;
    }
    flash = fmaxf(0, flash - dt * 3.2f);

    // audio parameters
    Car& c = cars[player];
    float v = fabsf(dot(c.vel, c.fwd()));
    int g = 1;
    while (g < 6 && v > kGearTop[g]) g++;
    float lo = kGearTop[g - 1], hi = kGearTop[g];
    float rpm = 0.28f + 0.72f * clampf((v - lo * 0.75f) / (hi - lo * 0.75f), 0, 1);
    if (state == State::Countdown || state == State::Title) rpm = 0.22f + (state == State::Countdown ? in.throttle * 0.6f : 0.0f);
    audio.rpm = rpm;
    audio.throttle = state == State::Countdown ? in.throttle : c.throttle;
    audio.speed = v / kMaxSpeed;
    audio.drift = (c.drift && v > 22) ? 1.0f : 0.0f;
    audio.boost = c.boosting ? 1.0f : 0.0f;
    audio.offroad = fabsf(c.lat) > track.halfW + 1.8f ? 1.0f : 0.0f;
    float nearest = 1e9f;
    for (size_t i = 0; i < cars.size(); i++) if ((int)i != player) nearest = fminf(nearest, len(cars[i].pos - c.pos));
    audio.nearCar = clampf(1.0f - nearest / 40.0f, 0, 1);
    audio.master = state == State::Title ? 0.5f : 1.0f;
}

void Game::render(GLuint targetFbo, int w, int h) {
    width = w; height = h;
    if (!rnd) { rnd = new Renderer(); rnd->init(*this); }
    rnd->draw(*this, targetFbo, w, h);
}
