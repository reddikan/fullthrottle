// Full Throttle — simulation: tyre-model car physics, drifting, collisions, AI, race flow, camera, effects.
#include "render.h"
#include <algorithm>
#include <stdio.h>

// ------------------------------------------------------------------ tuning
static const float kMass = 1450.0f, kInertia = 2300.0f;
static const float kA = 1.52f, kB = 1.48f, kL = 3.0f, kCgH = 0.24f;
static const float kG = 9.81f;
static const float kPower = 520000.0f;       // W
static const float kDrag = 0.68f;            // N/(m/s)^2  -> ~92 m/s top speed
static const float kDownforce = 0.95f;       // N/(m/s)^2
static const float kGearTop[] = {0, 21, 35, 49, 62, 75, 95};
static const float kRollSpeed = 46.0f;       // rolling-start pace (m/s)
static const float kHalfLen = 2.5f, kHalfWid = 1.0f;

float Game::frand() { uint32_t& s = rngState; s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFFFF) / 16777216.0f; }
static inline V3 perpY(V3 r) { return V3(r.z, 0, -r.x); }           // d/dyaw of a vector
static inline float crossY(V3 r, V3 f) { return r.z * f.x - r.x * f.z; } // yaw torque of force f at r

static float pacejka(float a) {      // normalised lateral force, peak ~10 degrees
    return sinf(1.45f * atanf(10.0f * a));
}

void Game::spawn(int type, V3 p, V3 v, float life, float size, float grow, float alpha, V3 col) {
    if (parts.size() > 3200) return;
    Particle q;
    q.p = p; q.v = v; q.life = q.maxLife = life; q.size = size; q.grow = grow; q.alpha = alpha; q.col = col;
    q.rot = frand() * 6.28f; q.rotVel = (frand() - 0.5f) * 6.0f; q.type = type;
    parts.push_back(q);
}
void Game::banner(const std::string& t, const std::string& sub, float dur, int style) {
    for (auto& b : banners) if (b.style == style) b.t = fminf(b.t, 0.2f);
    Banner b; b.text = t; b.sub = sub; b.t = dur; b.dur = dur; b.style = style;
    banners.push_back(b);
}

// ------------------------------------------------------------------ setup
void Game::init() {
    track.build();
    struct L { V3 c1, c2, c3, wheel, num; const char* sponsor; const char* team; const char* driver; int number, pattern; };
    const L L16[16] = {
        {V3(0.05f, 0.18f, 0.70f), V3(1.0f, 0.86f, 0.08f), V3(0.95f, 0.95f, 1.0f), V3(1.0f, 0.86f, 0.08f), V3(1, 1, 1), "VOLT RUSH", "BLUE BOLT RACING", "YOU", 7, 0},
        {V3(0.85f, 0.07f, 0.07f), V3(1.0f, 1.0f, 1.0f), V3(0.10f, 0.10f, 0.12f), V3(0.90f, 0.90f, 0.92f), V3(1, 1, 1), "KOMET COLA", "COMET MOTORSPORT", "RED COMET", 11, 1},
        {V3(0.97f, 0.76f, 0.05f), V3(0.08f, 0.08f, 0.10f), V3(0.90f, 0.20f, 0.10f), V3(0.10f, 0.10f, 0.12f), V3(0.05f, 0.05f, 0.06f), "BIG SKY TIRES", "SKYLINE RACING", "GOLD RUSH", 24, 2},
        {V3(0.04f, 0.42f, 0.22f), V3(1.0f, 0.84f, 0.10f), V3(1.0f, 1.0f, 1.0f), V3(1.0f, 0.84f, 0.10f), V3(1, 1, 1), "RIDGELINE OIL", "RIDGE RUNNERS", "VIPER", 3, 3},
        {V3(0.07f, 0.07f, 0.08f), V3(1.0f, 0.46f, 0.04f), V3(0.70f, 0.70f, 0.72f), V3(1.0f, 0.46f, 0.04f), V3(1.0f, 0.55f, 0.1f), "TURBO PIZZA", "NIGHT SHIFT", "NIGHT OWL", 88, 0},
        {V3(0.96f, 0.96f, 0.97f), V3(0.85f, 0.08f, 0.14f), V3(0.10f, 0.20f, 0.60f), V3(0.85f, 0.08f, 0.14f), V3(0.85f, 0.08f, 0.14f), "PRISM BANK", "PRISM RACING", "CAPTAIN", 19, 1},
        {V3(0.96f, 0.42f, 0.05f), V3(0.07f, 0.12f, 0.35f), V3(1.0f, 1.0f, 1.0f), V3(0.95f, 0.95f, 0.97f), V3(1, 1, 1), "DUSK COFFEE", "SUNSET SPEED", "SUNSET", 42, 2},
        {V3(0.40f, 0.10f, 0.60f), V3(0.10f, 0.85f, 0.78f), V3(1.0f, 1.0f, 1.0f), V3(0.10f, 0.85f, 0.78f), V3(1, 1, 1), "NOVA TELECOM", "NOVA RACING", "NOVA", 5, 3},
        {V3(0.05f, 0.55f, 0.75f), V3(0.97f, 0.97f, 0.97f), V3(0.98f, 0.40f, 0.35f), V3(0.97f, 0.97f, 0.97f), V3(1, 1, 1), "CORAL FM 98", "TIDE RUNNERS", "REEF", 57, 0},
        {V3(0.95f, 0.85f, 0.75f), V3(0.55f, 0.20f, 0.08f), V3(0.10f, 0.10f, 0.12f), V3(0.55f, 0.20f, 0.08f), V3(0.55f, 0.20f, 0.08f), "HAWK PARTS", "IRON MULE", "MULE", 73, 1},
        {V3(0.85f, 0.10f, 0.45f), V3(1.0f, 1.0f, 1.0f), V3(0.20f, 0.05f, 0.30f), V3(0.97f, 0.97f, 0.97f), V3(1, 1, 1), "SUNNY JUICE", "PINK PANTHER", "PEPPER", 32, 2},
        {V3(0.12f, 0.14f, 0.20f), V3(0.20f, 0.75f, 1.0f), V3(1.0f, 1.0f, 1.0f), V3(0.20f, 0.75f, 1.0f), V3(0.20f, 0.75f, 1.0f), "VOLT ENERGY", "COBALT", "COBALT", 9, 3},
        {V3(0.98f, 0.98f, 0.98f), V3(0.05f, 0.05f, 0.06f), V3(0.95f, 0.70f, 0.05f), V3(0.10f, 0.10f, 0.12f), V3(0.05f, 0.05f, 0.06f), "KOMET ZERO", "MONOCHROME", "DUSTY", 1, 0},
        {V3(0.30f, 0.62f, 0.12f), V3(0.98f, 0.98f, 0.98f), V3(0.08f, 0.25f, 0.10f), V3(0.98f, 0.98f, 0.98f), V3(1, 1, 1), "SUNNY LIME", "GREEN MACHINE", "LIME", 66, 1},
        {V3(0.60f, 0.62f, 0.66f), V3(0.85f, 0.10f, 0.10f), V3(0.10f, 0.10f, 0.12f), V3(0.85f, 0.10f, 0.10f), V3(0.85f, 0.10f, 0.10f), "HAWK TOOLS", "SILVER HAWK", "HAWK", 14, 2},
        {V3(0.10f, 0.25f, 0.45f), V3(0.98f, 0.55f, 0.10f), V3(1.0f, 1.0f, 1.0f), V3(0.98f, 0.55f, 0.10f), V3(1, 1, 1), "TIDE OIL", "HARBOR RACING", "CAPTAIN T", 21, 3},
    };
    cars.resize(16);
    for (int i = 0; i < 16; i++) {
        Livery& l = cars[i].liv;
        l.c1 = L16[i].c1; l.c2 = L16[i].c2; l.c3 = L16[i].c3; l.wheel = L16[i].wheel; l.num = L16[i].num;
        l.sponsor = L16[i].sponsor; l.team = L16[i].team; l.driver = L16[i].driver;
        l.number = L16[i].number; l.pattern = L16[i].pattern;
    }
    player = 0;
    skid.assign(16000 * 6, SkidVtx{0, -10, 0, 0, 0});
    resetRace();
    state = State::Title; stateTime = 0;
}

void Game::resetRace() {
    // rolling-start formation, two columns; player starts 12th
    int order[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 12, 13, 14, 15};
    for (int slot = 0; slot < 16; slot++) {
        int ci = order[slot];
        Car& c = cars[ci];
        Livery liv = c.liv;
        c = Car();
        c.liv = liv;
        int row = slot / 2;
        c.formationLat = (slot % 2) ? 3.4f : -3.4f;
        c.formationDist = -row * 12.0f - (slot % 2) * 5.0f;
        float s = -210.0f + c.formationDist;
        c.pos = track.at(s, c.formationLat);
        V3 tg = track.tangentAt(s);
        c.yaw = atan2f(tg.x, tg.z);
        c.vel = tg * kRollSpeed;
        c.idx = track.nearest(c.pos, -1);
        c.s = track.wrapS(s); c.prevS = c.s; c.dist = s;
        c.ai = ci != player;
        c.skill = 0.93f + 0.06f * (1.0f - slot / 15.0f) + frand() * 0.02f;
        c.aggression = frand();
        c.laneBias = (frand() - 0.5f) * 4.0f;
        c.gear = 4;
        for (int w = 0; w < 4; w++) c.skidOn[w] = false;
    }
    cars[player].skill = autopilot ? 1.06f : 1.0f;
    parts.clear();
    banners.clear();
    raceTime = 0;
    nextCp = 0;
    timer = (track.checkpoints[0] + 220.0f) / 44.0f + 12.0f;
    camYaw = cars[player].yaw;
    camPos = cars[player].pos - cars[player].fwd() * 7 + V3(0, 2.4f, 0);
}

// ------------------------------------------------------------------ AI
void Game::formation(Car& c, float dt) {
    // keep the grid shape while rolling: leader at fixed pace, others hold their offset
    float leadDist = -210.0f + kRollSpeed * stateTime;
    float want = leadDist + c.formationDist;
    float err = want - c.dist;
    float tgtV = kRollSpeed + clampf(err * 0.8f, -8, 8);
    V3 target = track.at(c.s + 18.0f, c.formationLat);
    V3 to = target - c.pos;
    float yawErr = wrapAngle(atan2f(to.x, to.z) - c.yaw);
    c.steer = clampf(-yawErr * 3.0f, -1, 1);
    float v = dot(c.vel, c.fwd());
    c.throttle = clampf((tgtV - v) * 0.3f + 0.35f, 0, 1);
    c.brake = v > tgtV + 3 ? 0.3f : 0.0f;
    c.handbrake = false; c.boosting = false;
}

void Game::driveAI(Car& c, float dt) {
    float v = c.speed();
    float look = 10.0f + v * 0.40f;
    float kAhead = track.curvatureAt(c.s + look * 0.7f + 12.0f);
    float racing = clampf(kAhead * 1300.0f, -6.5f, 6.5f);
    float want = racing * 0.8f + c.laneBias * 0.45f + c.avoid;
    want = clampf(want, -track.halfW + 1.8f, track.halfW - 1.8f);
    // overtaking / avoidance
    float avoidTarget = 0, slow = 1.0f;
    for (const Car& o : cars) {
        if (&o == &c) continue;
        float ds = o.dist - c.dist;
        if (ds < -2 || ds > 26) continue;
        float dl = o.lat - c.lat;
        if (fabsf(dl) < 2.6f) {
            float roomL = o.lat + track.halfW, roomR = track.halfW - o.lat;
            avoidTarget = (roomR > roomL ? 1.0f : -1.0f) * 3.8f;
            if (ds < 9 && o.speed() < v) slow = fminf(slow, 0.96f - (1 - c.aggression) * 0.03f);
            if (ds < 6 && fabsf(dl) < 2.0f) slow = fminf(slow, 0.9f);
        }
    }
    c.avoid = approach(c.avoid, avoidTarget, 1.6f, dt);
    V3 target = track.at(c.s + look, want);
    V3 to = target - c.pos;
    float vh = v > 5 ? atan2f(c.vel.x, c.vel.z) : c.yaw;
    float errV = wrapAngle(atan2f(to.x, to.z) - vh);
    if (c.drifting) {
        float aLat = 1.55f * kG + kDownforce * v * v / kMass * 0.8f;
        float need = errV * 2.4f;
        c.steer = clampf(-need * v / (aLat * 1.12f), -1, 1);
    } else {
        c.steer = clampf(-errV * 2.6f - c.yawRate * 0.15f, -1, 1);
    }
    float mu = 1.55f;
    float aLat = mu * kG * 0.88f * c.skill;
    float allow = 90.0f * c.skill;
    for (float d = 0; d < 220; d += 6) {
        float k = fabsf(track.curvatureAt(c.s + d));
        float vmax = k > 1e-4f ? sqrtf(aLat / k) : 999.0f;
        // downforce lets cars carry a bit more speed in fast corners
        vmax *= 1.0f + clampf((vmax - 30) * 0.004f, 0, 0.18f);
        float vOk = sqrtf(vmax * vmax + 2 * 10.5f * d);
        allow = fminf(allow, vOk);
    }
    float gap = cars[player].dist - c.dist;
    if (c.ai) allow *= clampf(1.0f + gap * 0.00022f, 0.95f, 1.05f);
    allow *= slow;
    float diff = allow - v;
    c.throttle = diff > 0 ? clampf(diff * 0.4f, 0.2f, 1.0f) : 0.0f;
    c.brake = diff < -1.5f ? clampf(-diff * 0.15f, 0, 1) : 0.0f;
    c.handbrake = false;
    c.boosting = false;
    if (!c.ai) {
        // attract-mode driver: throws the car sideways into hairpins
        float kNear = 0, kSigned = 0; float sApex = 0;
        for (float d = 0; d < 80; d += 5) { float k = track.curvatureAt(c.s + d); if (fabsf(k) > kNear) { kNear = fabsf(k); kSigned = k; sApex = d; } }
        if (!c.drifting && kNear > 1.0f / 80.0f && sApex < 55 && sApex > 15 && v > 26) {
            c.handbrake = true; c.steer = signf(kSigned) * 0.85f; c.brake = 0; c.throttle = 0.5f;
        }
        if (c.drifting) c.throttle = fmaxf(c.throttle, 0.8f);
        c.boosting = c.boost > 0.3f && fabsf(track.curvatureAt(c.s + 80)) < 1.0f / 700.0f && v > 45;
    }
    // stuck recovery
    if (v < 3 && state == State::Race) c.stuck += dt; else c.stuck = 0;
    if (c.stuck > 2.5f) {
        c.pos = track.at(c.s, clampf(c.lat, -5, 5));
        V3 tg = track.tangentAt(c.s);
        c.yaw = atan2f(tg.x, tg.z); c.vel = tg * 8.0f; c.yawRate = 0; c.stuck = 0;
    }
}

// ------------------------------------------------------------------ physics
void Game::physics(Car& c, float dt) {
    V3 f = c.fwd(), l = c.left();
    float vx = dot(c.vel, f), vy = dot(c.vel, l);
    float v = len(c.vel);
    float w = c.yawRate;
    bool isPlayer = (&c == &cars[player]);
    bool onGrass = fabsf(c.lat) > track.halfW + 1.8f;
    float surf = onGrass ? 0.80f : 1.0f;
    if (weather == Weather::Rain) surf *= 0.88f;
    float vabs = fabsf(vx);
    float beta = (vabs > 2.0f) ? atan2f(vy, vabs) : 0.0f;   // + = nose pointing right of travel

    // ---- drivetrain
    if (c.shiftTimer > 0) c.shiftTimer -= dt;
    float rpmRaw = clampf(vabs / kGearTop[c.gear], 0.0f, 1.08f);
    if (c.gear < 6 && rpmRaw > 0.96f && c.shiftTimer <= 0) {
        c.gear++; c.shiftTimer = 0.16f;
        if (isPlayer) audio.shifts++;
        if (frand() < 0.6f) { c.flameTimer = 0.18f; if (isPlayer) audio.backfires++; }
    } else if (c.gear > 1 && vabs < kGearTop[c.gear - 1] * 0.62f) { c.gear--; c.shiftTimer = 0.08f; }
    float lo = kGearTop[c.gear - 1] * 0.55f;
    c.rpm = 0.22f + 0.78f * clampf((vabs - lo) / (kGearTop[c.gear] - lo), 0, 1);
    if (c.throttle > 0.1f && vabs < 4) c.rpm = fmaxf(c.rpm, 0.35f + c.throttle * 0.4f);
    if (c.drifting) c.rpm = fminf(1.0f, c.rpm + 0.12f * c.throttle);

    float throttle = c.shiftTimer > 0.06f ? c.throttle * 0.2f : c.throttle;
    float boostF = 0;
    if (c.boosting && c.boost > 0) { boostF = kMass * 3.8f; c.boost = fmaxf(0, c.boost - dt * 0.22f); c.flameTimer = fmaxf(c.flameTimer, 0.05f); }
    else c.boosting = false;
    float Fdrive = 0, Fbrake = 0;
    if (throttle > 0 && vx > -1) Fdrive = throttle * fminf(kMass * 10.0f, kPower / fmaxf(vabs, 4.0f)) + boostF;
    if (c.brake > 0) {
        if (vx > 1.0f) Fbrake = c.brake * kMass * 13.0f;
        else Fdrive -= c.brake * kMass * 4.0f;     // reverse
    }
    if (vx < -9 && Fdrive < 0) Fdrive = 0;

    // ---- drift state machine
    // drifting only happens on purpose: hold the drift button and steer
    bool startDrift = c.handbrake && vx > 16 && fabsf(c.steer) > 0.2f;
    if (startDrift && !c.drifting) { c.drifting = true; c.driftDir = signf(c.steer); c.driftCalm = 0; c.driftHold = 0.3f; }
    c.driftHold = fmaxf(0, c.driftHold - dt);
    if (c.drifting) {
        float into = c.steer * c.driftDir;
        bool keep = c.ai || autopilot ? (into > 0.15f || c.handbrake) : c.handbrake;
        if (!keep && c.driftHold <= 0) c.driftCalm += dt; else c.driftCalm = 0;
        if (c.driftCalm > 0.12f || vx < 10 || onGrass) c.drifting = false;
    }
    c.driftAmt = approach(c.driftAmt, c.drifting ? 1.0f : sat((fabsf(beta) - 0.08f) / 0.3f) * 0.6f, c.drifting ? 5.0f : 3.0f, dt);

    if (c.drifting) {
        // ---- guided arcade drift: steering bends the path, the body holds a slide angle
        float vh = atan2f(c.vel.x, c.vel.z);
        float aLat = 1.55f * kG * surf + kDownforce * v * v / kMass * 0.8f;
        float into = c.steer * c.driftDir;
        float pathRate = -c.steer * aLat / fmaxf(v, 8.0f) * 1.12f;
        vh += pathRate * dt;
        float betaT = c.driftDir * clampf(0.30f + 0.32f * into, 0.04f, 0.62f);   // counter-steer straightens
        float yawT = vh - betaT;
        float err = wrapAngle(yawT - c.yaw);
        c.yawRate = approach(c.yawRate, err * 7.0f + pathRate, 10.0f, dt);
        c.yaw = wrapAngle(c.yaw + c.yawRate * dt);
        float spd = v;
        float scrub = 2.0f + 2.6f * fabsf(beta) + (c.driftHold > 0 ? 5.0f : 0.0f);
        spd += (Fdrive * 0.55f / kMass - scrub - Fbrake / kMass - kDrag * v * v / kMass) * dt;
        spd = fmaxf(spd, 0.0f);
        c.vel = V3(sinf(vh), 0, cosf(vh)) * spd;
        c.steerAngle = approach(c.steerAngle, clampf(beta * 0.9f - c.steer * 0.15f, -0.6f, 0.6f), 8, dt);  // visual counter-steer
        c.axPrev = 0;
        float ay = spd * pathRate;
        c.rollVel += ((clampf(ay * 0.0055f, -0.07f, 0.07f) - c.rollVis) * 120.0f - c.rollVel * 9.0f) * dt; c.rollVis += c.rollVel * dt;
        c.slideSpeed = fabsf(dot(c.vel, c.left())) + 4.0f;
    } else {
        // ---- tyre model
        bool human = !c.ai && !autopilot;
        float steerMax = human ? 0.50f / (1.0f + vabs / 13.0f) + 0.035f : 0.58f / (1.0f + vabs / 17.0f) + 0.05f;
        float target = -c.steer * steerMax;
        if (vabs > 6) target += clampf(beta, -0.5f, 0.5f) * (human ? 0.9f : 0.8f);   // counter-steer assist
        c.steerAngle = approach(c.steerAngle, clampf(target, -0.6f, 0.6f), 9.0f, dt);
        float d = c.steerAngle;
        float df = kDownforce * v * v;
        float Wf = kMass * kG * kB / kL + df * 0.46f - kMass * c.axPrev * kCgH / kL;
        float Wr = kMass * kG * kA / kL + df * 0.54f + kMass * c.axPrev * kCgH / kL;
        Wf = fmaxf(Wf, 1000.0f); Wr = fmaxf(Wr, 1000.0f);
        float muF = 1.62f * surf, muR = 1.62f * surf;
        if (c.ai) { muF *= 1.04f; muR *= 1.08f; }
        if (human) { muF *= 1.06f; muR *= 1.28f; }   // planted rear: no fishtailing
        if (c.driftHold > 0) muR *= 0.6f;
        float sv = fmaxf(vabs, 3.0f);
        float af = atan2f(vy + kA * w, sv) - d * (vx >= 0 ? 1.0f : -1.0f);
        float ar = atan2f(vy - kB * w, sv);
        float FfMax = muF * Wf, FrMax = muR * Wr;
        // longitudinal: drive on the rear, brakes split 60/40
        float FxF = -Fbrake * 0.68f * (vx > 0 ? 1.0f : 0.0f);
        float FxR = Fdrive - Fbrake * 0.32f * (vx > 0 ? 1.0f : 0.0f);
        FxF = clampf(FxF, -FfMax * 0.95f, FfMax * 0.95f);
        FxR = clampf(FxR, -FrMax * 0.95f, FrMax * 0.95f);
        float combF = sqrtf(fmaxf(0.3f, 1.0f - 0.35f * (FxF * FxF) / (FfMax * FfMax)));
        float combR = sqrtf(fmaxf(0.3f, 1.0f - (human ? 0.12f : 0.35f) * (FxR * FxR) / (FrMax * FrMax)));
        float FyF = -FfMax * pacejka(af) * combF;
        float FyR = -FrMax * pacejka(ar) * combR;
        c.slipF = fabsf(af); c.slipR = fabsf(ar);
        float wheelspin = (Fdrive > FrMax * 0.92f && vabs < 22) ? 1.0f : 0.0f;
        float Fcx = FxR + FxF * cosf(d) - FyF * sinf(d);
        float Fcy = FyF * cosf(d) + FxF * sinf(d) + FyR;
        float tq = kA * FyF * cosf(d) - kB * FyR;
        if (vabs < 5.0f) {
            float wKin = vx * tanf(d) / kL;
            w = lerpf(wKin, w, vabs / 5.0f);
            Fcy *= vabs / 5.0f;
        }
        tq -= w * (human ? 2600.0f : 1400.0f);
        if (vx > 8) tq += clampf(beta, -0.6f, 0.6f) * (human ? 26000.0f : 16000.0f);   // stability assist
        if (human && vx > 8 && fabsf(beta) > 0.10f) tq += signf(beta) * (fabsf(beta) - 0.10f) * 60000.0f;  // catch slides
        if (human && vabs > 8) {
            // yaw-rate limiter: never rotate faster than the tyres could carry the car
            float wMax = (1.9f * kG + kDownforce * v * v / kMass) / vabs * 1.05f;
            if (fabsf(w) > wMax) w = signf(w) * lerpf(fabsf(w), wMax, 0.25f);
        }
        V3 F = f * Fcx + l * Fcy;
        float dragK = kDrag * (1.0f - c.draft * 0.32f);
        if (v > 0.01f) F -= c.vel * ((dragK * v * v + (onGrass ? 4000.0f : 250.0f)) / v);
        c.axPrev = clampf(Fcx / kMass, -15, 15);
        c.vel += F * (dt / kMass);
        c.yawRate = w + tq / kInertia * dt;
        c.yaw = wrapAngle(c.yaw + c.yawRate * dt);
        float ay = Fcy / kMass, ax = Fcx / kMass;
        float rollT = clampf(ay * 0.0055f, -0.07f, 0.07f), pitchT = clampf(-ax * 0.004f, -0.035f, 0.035f);
        c.rollVel += ((rollT - c.rollVis) * 120.0f - c.rollVel * 9.0f) * dt; c.rollVis += c.rollVel * dt;
        c.pitchVel += ((pitchT - c.pitchVis) * 140.0f - c.pitchVel * 10.0f) * dt; c.pitchVis += c.pitchVel * dt;
        c.slideSpeed = fabsf(dot(c.vel, c.left())) + wheelspin * 8.0f + (c.handbrake && vabs > 6 ? 6.0f : 0.0f);
        if (Fbrake > kMass * 10.0f && vabs > 25) c.slideSpeed = fmaxf(c.slideSpeed, 4.5f);
    }
    c.pos += c.vel * dt;
    c.pos.y = 0;
    c.heaveVel += ((0 - c.heave) * 160.0f - c.heaveVel * 8.0f) * dt; c.heave += c.heaveVel * dt;
    if (onGrass && v > 10) c.heaveVel += (frand() - 0.5f) * v * 0.03f;
    c.wheelSpin = fmodf(c.wheelSpin + (vx / wheelRadius()) * dt, 2 * PI);

    // ---- track coordinates
    c.idx = track.nearest(c.pos, c.idx);
    V3 rel = c.pos - track.P[c.idx];
    c.lat = dot(rel, track.R[c.idx]);
    float s = track.wrapS(track.S[c.idx] + dot(rel, track.T[c.idx]));
    float ds = s - c.prevS;
    if (ds > track.length * 0.5f) ds -= track.length;
    if (ds < -track.length * 0.5f) ds += track.length;
    c.dist += ds; c.prevS = s; c.s = s;

    walls(c, dt);

    // ---- boost and draft
    c.boost = fminf(1.0f, c.boost + dt * ((c.drifting ? 0.18f : 0.0f) + c.draft * 0.07f + 0.006f));
    float bestDraft = 0;
    for (const Car& o : cars) {
        if (&o == &c) continue;
        V3 d2 = o.pos - c.pos;
        float along = dot(d2, f), side = fabsf(dot(d2, l));
        if (along > 4 && along < 32 && side < 1.9f && v > 30) bestDraft = fmaxf(bestDraft, 1.0f - along / 40.0f);
    }
    c.draft = approach(c.draft, bestDraft, 3, dt);
    c.flameTimer = fmaxf(0, c.flameTimer - dt);
    c.bump = fmaxf(0, c.bump - dt * 2);
}

// rail/wall contacts using the four body corners
void Game::walls(Car& c, float dt) {
    V3 f = c.fwd(), l = c.left();
    float wall = track.wallAt(c.s) - 0.15f;
    V3 R = track.R[c.idx], T = track.T[c.idx];
    for (int k = 0; k < 4; k++) {
        float sx = (k & 1) ? 1.0f : -1.0f, sz = (k & 2) ? 1.0f : -1.0f;
        V3 r = f * (sz * kHalfLen * 0.96f) + l * (sx * kHalfWid);
        V3 p = c.pos + r;
        float lat = dot(p - track.P[c.idx], R);
        float pen = fabsf(lat) - wall;
        if (pen <= 0) continue;
        float sd = signf(lat);
        V3 n = R * (-sd);
        c.pos += n * pen;
        V3 vp = c.vel + perpY(r) * c.yawRate;
        float vn = dot(vp, n);
        if (vn < 0) {
            float crn = crossY(r, n);
            float j = -(1.0f + 0.25f) * vn / (1.0f / kMass + crn * crn / kInertia);
            c.vel += n * (j / kMass);
            c.yawRate += crn * j / kInertia;
            // friction along the rail
            float vt = dot(vp, T);
            float crt = crossY(r, T);
            float jt = clampf(-vt / (1.0f / kMass + crt * crt / kInertia), -0.3f * j, 0.3f * j);
            c.vel += T * (jt / kMass);
            c.yawRate += crt * jt / kInertia;
            float hit = -vn;
            c.bump = fminf(1.0f, c.bump + hit * 0.05f);
            c.rollVel += sd * hit * 0.02f;
            if (&c == &cars[player]) {
                shake = fminf(1.0f, shake + hit * 0.05f);
                if (hit > 2.0f) { audio.impactPower = clampf(hit / 15.0f, 0.2f, 1.0f); audio.impacts++; }
            }
            int nsp = (int)clampf(hit * 2.0f, 2, 30);
            for (int q = 0; q < nsp; q++)
                spawn(P_SPARK, p + V3(0, 0.4f + frand() * 0.4f, 0), c.vel * 0.6f + n * (2 + frand() * 4) + V3((frand() - 0.5f) * 6, 2 + frand() * 4, (frand() - 0.5f) * 6),
                      0.25f + frand() * 0.35f, 0.05f, 0, 1, V3(1.0f, 0.75f, 0.3f));
        }
        // scraping
        float slide = fabsf(dot(c.vel, T));
        if (slide > 6) {
            c.sparkAcc += dt * slide * 2.5f;
            while (c.sparkAcc > 1) {
                c.sparkAcc -= 1;
                spawn(P_SPARK, p + V3(0, 0.35f, 0), c.vel * 0.7f + V3((frand() - 0.5f) * 5, 1 + frand() * 3, (frand() - 0.5f) * 5),
                      0.2f + frand() * 0.3f, 0.05f, 0, 1, V3(1.0f, 0.7f, 0.25f));
            }
            if (&c == &cars[player]) audio.scrape = fminf(1.0f, slide / 30.0f);
        }
    }
}

void Game::collide() {
    for (size_t i = 0; i < cars.size(); i++)
        for (size_t j = i + 1; j < cars.size(); j++) {
            Car& A = cars[i]; Car& B = cars[j];
            V3 d = B.pos - A.pos;
            if (d.x * d.x + d.z * d.z > 36) continue;
            V3 axA[2] = {A.fwd(), A.left()}, axB[2] = {B.fwd(), B.left()};
            float hA[2] = {kHalfLen, kHalfWid}, hB[2] = {kHalfLen, kHalfWid};
            float best = 1e9f; V3 bestN; int bestFrom = 0;
            V3 axes[4] = {axA[0], axA[1], axB[0], axB[1]};
            bool sep = false;
            for (int k = 0; k < 4; k++) {
                V3 ax = axes[k];
                float ra = hA[0] * fabsf(dot(axA[0], ax)) + hA[1] * fabsf(dot(axA[1], ax));
                float rb = hB[0] * fabsf(dot(axB[0], ax)) + hB[1] * fabsf(dot(axB[1], ax));
                float dist = dot(d, ax);
                float ov = ra + rb - fabsf(dist);
                if (ov <= 0) { sep = true; break; }
                if (ov < best) { best = ov; bestN = dist < 0 ? -ax : ax; bestFrom = k < 2 ? 0 : 1; }
            }
            if (sep) continue;
            V3 n = bestN;  // from A to B
            // contact point: deepest corner of the other box
            V3 cpt;
            if (bestFrom == 0) {
                float mn = 1e9f;
                for (int k = 0; k < 4; k++) {
                    V3 cr = B.pos + axB[0] * (((k & 1) ? 1 : -1) * hB[0]) + axB[1] * (((k & 2) ? 1 : -1) * hB[1]);
                    float pr = dot(cr, n); if (pr < mn) { mn = pr; cpt = cr; }
                }
            } else {
                float mx = -1e9f;
                for (int k = 0; k < 4; k++) {
                    V3 cr = A.pos + axA[0] * (((k & 1) ? 1 : -1) * hA[0]) + axA[1] * (((k & 2) ? 1 : -1) * hA[1]);
                    float pr = dot(cr, n); if (pr > mx) { mx = pr; cpt = cr; }
                }
            }
            A.pos -= n * (best * 0.5f); B.pos += n * (best * 0.5f);
            V3 rA = cpt - A.pos, rB = cpt - B.pos;
            V3 vA = A.vel + perpY(rA) * A.yawRate, vB = B.vel + perpY(rB) * B.yawRate;
            V3 vr = vB - vA;
            float vn = dot(vr, n);
            if (vn >= 0) continue;
            float cA = crossY(rA, n), cB = crossY(rB, n);
            float jn = -(1.0f + 0.3f) * vn / (2.0f / kMass + cA * cA / kInertia + cB * cB / kInertia);
            A.vel -= n * (jn / kMass); A.yawRate -= cA * jn / kInertia;
            B.vel += n * (jn / kMass); B.yawRate += cB * jn / kInertia;
            V3 t(-n.z, 0, n.x);
            float vt = dot(vr, t);
            float tA = crossY(rA, t), tB = crossY(rB, t);
            float jt = clampf(-vt / (2.0f / kMass + tA * tA / kInertia + tB * tB / kInertia), -0.35f * jn, 0.35f * jn);
            A.vel -= t * (jt / kMass); A.yawRate -= tA * jt / kInertia;
            B.vel += t * (jt / kMass); B.yawRate += tB * jt / kInertia;
            float hit = -vn;
            A.rollVel -= dot(n, A.left()) * hit * 0.02f; B.rollVel += dot(n, B.left()) * hit * 0.02f;
            A.bump = fminf(1, A.bump + hit * 0.04f); B.bump = fminf(1, B.bump + hit * 0.04f);
            // hits knock AI cars loose for a moment
            if (A.ai && hit > 4) A.driftHold = 0.25f;
            if (B.ai && hit > 4) B.driftHold = 0.25f;
            int nsp = (int)clampf(hit * 3.0f + fabsf(vt) * 0.5f, 3, 45);
            V3 vm = (A.vel + B.vel) * 0.5f;
            for (int q = 0; q < nsp; q++)
                spawn(P_SPARK, cpt + V3(0, 0.45f + frand() * 0.5f, 0), vm * 0.8f + t * ((frand() - 0.5f) * 10) + V3(0, 1.5f + frand() * 4, 0),
                      0.25f + frand() * 0.4f, 0.05f, 0, 1, V3(1.0f, 0.75f, 0.3f));
            if (hit > 5) for (int q = 0; q < 4; q++)
                spawn(P_SMOKE, cpt + V3(0, 0.6f, 0), vm * 0.5f + V3(0, 1, 0), 0.8f, 0.6f, 2.5f, 0.25f, V3(0.75f, 0.75f, 0.75f));
            if ((int)i == player || (int)j == player) {
                shake = fminf(1.0f, shake + hit * 0.06f);
                if (hit > 1.5f) { audio.impactPower = clampf(hit / 12.0f, 0.2f, 1.0f); audio.impacts++; }
                if (fabsf(vt) > 3) audio.scrape = fminf(1.0f, fabsf(vt) / 20.0f);
            }
        }
}

// ------------------------------------------------------------------ skid marks, smoke
void Game::addSkid(V3 a, V3 b, V3 side, float w, float alpha) {
    V3 h = side * (w * 0.5f);
    float y = 0.014f;
    V3 a0 = a - h, a1 = a + h, b0 = b - h, b1 = b + h;
    a0.y = a1.y = b0.y = b1.y = y;
    SkidVtx q[6] = {{a0.x, a0.y, a0.z, alpha, 0}, {b0.x, b0.y, b0.z, alpha, 0}, {b1.x, b1.y, b1.z, alpha, 1},
                    {a0.x, a0.y, a0.z, alpha, 0}, {b1.x, b1.y, b1.z, alpha, 1}, {a1.x, a1.y, a1.z, alpha, 1}};
    int base = skidHead * 6;
    for (int k = 0; k < 6; k++) skid[base + k] = q[k];
    skidHead = (skidHead + 1) % (int)(skid.size() / 6);
    skidCount = std::min(skidCount + 1, (int)(skid.size() / 6));
    skidWritten++;
}

void Game::tyreFx(Car& c, float dt) {
    float v = c.speed();
    M4 xf = translate(c.pos) * rotY(c.yaw);
    V3 l = c.left(), f = c.fwd();
    bool rain = weather == Weather::Rain;
    bool close = len(c.pos - camPos) < 220;
    for (int w = 0; w < 4; w++) {
        V3 lp; float sgn; bool front;
        wheelLayout(w, &lp, &sgn, &front);
        V3 p = xformPoint(xf, V3(lp.x, 0, lp.z));
        V3 r = p - c.pos;
        V3 vw = c.vel + perpY(r) * c.yawRate;
        float lat = fabsf(dot(vw, l));
        float slip = lat;
        if (!front) slip = c.slideSpeed + lat * 0.3f;
        if (c.brake > 0.8f && v > 25 && front) slip = fmaxf(slip, 6.0f);
        float latPos = fabsf(dot(p - track.P[c.idx], track.R[c.idx]));
        bool onTarmac = latPos < track.halfW + 1.6f;
        bool skidding = slip > fmaxf(3.8f, 0.11f * v) && v > 4 && onTarmac;
        if (skidding && c.skidOn[w]) {
            float a = clampf((slip - 3.0f) / 9.0f, 0.25f, 0.9f) * (rain ? 0.5f : 1.0f);
            if (len(p - c.skidPrev[w]) > 0.35f) { addSkid(c.skidPrev[w], p, l, 0.30f, a); c.skidPrev[w] = p; }
        } else c.skidPrev[w] = p;
        c.skidOn[w] = skidding;
        if (!close) continue;
        // tyre smoke (dry) / spray (wet)
        if (skidding && !front && !rain) {
            c.smokeAcc += dt * (slip - 3.0f) * 1.6f;
            while (c.smokeAcc > 1) {
                c.smokeAcc -= 1;
                spawn(P_SMOKE, p + V3(0, 0.3f, 0), c.vel * 0.25f + V3((frand() - 0.5f) * 2.0f, 0.6f + frand(), (frand() - 0.5f) * 2.0f),
                      1.2f + frand() * 1.2f, 0.5f, 1.9f + frand() * 1.2f, 0.17f, V3(0.80f, 0.80f, 0.82f));
            }
        }
        if (!onTarmac && v > 8 && frand() < dt * v * 1.5f)
            spawn(P_DUST, p + V3(0, 0.2f, 0), c.vel * 0.3f + V3((frand() - 0.5f) * 3, 1 + frand() * 2, (frand() - 0.5f) * 3),
                  1.0f + frand(), 0.5f, 2.2f, 0.35f, V3(0.75f, 0.70f, 0.55f));
        if (rain && v > 8 && frand() < dt * v * (front ? 0.5f : 1.1f))
            spawn(P_SPRAY, p + V3(0, 0.25f, 0), c.vel * 0.6f - f * (2 + frand() * 3) + V3((frand() - 0.5f) * 2, 1 + frand() * 2.4f, (frand() - 0.5f) * 2),
                  0.7f + frand() * 0.8f, 0.35f, 1.6f + frand() * 1.5f, clampf(v / 55, 0.15f, 1.0f) * 0.15f, V3(0.66f, 0.69f, 0.72f));
    }
    // exhaust flames
    if (c.flameTimer > 0 && close) {
        for (int sd = -1; sd <= 1; sd += 2) {
            V3 ex = xformPoint(xf, V3(sd * 0.40f, 0.24f, -2.6f));
            for (int q = 0; q < 2; q++)
                spawn(P_FLAME, ex, c.vel - f * (5 + frand() * 6) + V3((frand() - 0.5f), frand(), (frand() - 0.5f)), 0.07f + frand() * 0.07f,
                      0.22f + frand() * 0.15f, 1.5f, 1.0f, V3(1.0f, 0.55f, 0.15f));
        }
    }
}

void Game::updateParticles(float dt) {
    for (auto& p : parts) {
        p.life -= dt;
        switch (p.type) {
            case P_SPARK: p.v.y -= 14.0f * dt; p.v = p.v * expf(-0.8f * dt); if (p.p.y < 0.02f && p.v.y < 0) { p.v.y *= -0.35f; p.v = p.v * 0.7f; } break;
            case P_SMOKE: case P_DUST: p.v = p.v * expf(-1.5f * dt); p.v.y += 0.35f * dt; p.size += p.grow * dt; p.grow *= expf(-0.6f * dt); break;
            case P_SPRAY: p.v = p.v * expf(-1.6f * dt); p.v.y -= 1.2f * dt; p.size += p.grow * dt; if (p.p.y < 0.1f) p.v.y = 0; break;
            case P_FLAME: p.size += p.grow * dt; break;
            case P_CONFETTI:
                if (p.p.y > 0.02f) {
                    p.v.y = -1.2f; p.v.x += sinf(globalTime * 3 + p.rot) * dt * 2; p.rot += p.rotVel * dt;
                } else { p.v = V3(); p.p.y = 0.02f; }
                break;
            case P_DEBRIS: p.v.y -= 12 * dt; if (p.p.y < 0.03f) { p.p.y = 0.03f; p.v = p.v * 0.5f; p.v.y = fabsf(p.v.y) * 0.2f; } p.rot += p.rotVel * dt; break;
        }
        p.p += p.v * dt;
    }
    parts.erase(std::remove_if(parts.begin(), parts.end(), [](const Particle& p) { return p.life <= 0; }), parts.end());
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
        // slow orbit around the player car as it rolls in formation
        float a = globalTime * 0.25f;
        V3 fw = c.fwd(), rr = c.right();
        camPos = c.pos + fw * (cosf(a) * 8.5f) + rr * (sinf(a) * 8.5f) + V3(0, 1.6f + 0.4f * sinf(a * 0.7f), 0);
        camLook = c.pos + V3(0, 0.7f, 0);
        camFov = 50;
        camYaw = c.yaw;
        return;
    }
    float v = c.speed();
    float velYaw = v > 6 ? atan2f(c.vel.x, c.vel.z) : c.yaw;
    float tgtYaw = c.yaw + wrapAngle(velYaw - c.yaw) * 0.78f;
    camYaw = camYaw + wrapAngle(tgtYaw - camYaw) * (1.0f - expf(-5.5f * dt));
    V3 fwd(sinf(camYaw), 0, cosf(camYaw));
    float back = 6.6f + v * 0.006f, up = 2.35f;
    V3 want = c.pos - fwd * back + V3(0, up, 0);
    camPos = lerp(camPos, want, 1.0f - expf(-16.0f * dt));
    camPos.y = up;
    camLook = c.pos + fwd * 5.0f + V3(0, 0.95f, 0);
    float fovT = 60.0f + v * 0.075f + (c.boosting ? 6.0f : 0.0f);
    camFov = approach(camFov, fovT, 3, dt);
    if (shake > 0) {
        float sx = (frand() - 0.5f) * shake * 0.4f, sy = (frand() - 0.5f) * shake * 0.3f;
        camPos += V3(sx, sy, 0); camLook += V3(sx * 0.5f, sy * 0.5f, 0);
        shake = fmaxf(0, shake - dt * 2.5f);
    }
}

// ------------------------------------------------------------------ main step
void Game::step(float dt, const Input& in) {
    bool racing = state == State::Race || state == State::Finished || state == State::TimeUp;
    bool rolling = state == State::Rolling || state == State::Title;
    for (size_t i = 0; i < cars.size(); i++) {
        Car& c = cars[i];
        if (rolling) formation(c, dt);
        else if ((int)i == player && state == State::Race && !autopilot) {
            float rate = fabsf(in.steer) < fabsf(c.steer) ? 12.0f : 6.5f;   // return to centre faster than turn-in
            c.steer = approach(c.steer, in.steer, rate, dt);
            c.throttle = in.throttle; c.brake = in.brake;
            c.handbrake = in.handbrake;
            c.boosting = in.boost && c.boost > 0.02f;
        } else if (racing) {
            driveAI(c, dt);
            if ((int)i == player && state != State::Race) { c.throttle *= (state == State::TimeUp ? 0.0f : 0.7f); if (state == State::TimeUp) c.brake = 0.3f; }
        }
        physics(c, dt);
        if (racing && !c.finished && c.dist >= laps * track.length) { c.finished = true; c.finishTime = raceTime; }
    }
    collide();
}

void Game::update(float dt, const Input& in) {
    dt = clampf(dt, 0.0f, 0.05f);
    globalTime += dt;
    stateTime += dt;
    for (auto& b : banners) b.t -= dt;
    banners.erase(std::remove_if(banners.begin(), banners.end(), [](const Banner& b) { return b.t <= 0; }), banners.end());
    audio.scrape = fmaxf(0.0f, audio.scrape - dt * 3);

    switch (state) {
        case State::Title:
            if (in.leftPressed || in.rightPressed) weather = weather == Weather::Sunny ? Weather::Rain : Weather::Sunny;
            if (in.confirmPressed || in.startPressed || (autopilot && globalTime > 4.0f)) {
                resetRace(); state = State::Rolling; stateTime = 0;
                banner("ROLLING START!!", "Cliffside Coast", 3.2f, 0);
            }
            break;
        case State::Rolling:
            if (stateTime >= 3.6f) {
                state = State::Race; stateTime = 0;
                banner("GO!", "", 1.2f, 1);
                for (auto& c : cars) { c.lapStart = 0; }
            }
            break;
        case State::Race:
            if (in.startPressed) { pausedFrom = State::Race; state = State::Paused; return; }
            if (in.selectPressed) { resetRace(); state = State::Rolling; stateTime = 0; banner("ROLLING START!!", "Cliffside Coast", 3.2f, 0); return; }
            break;
        case State::Finished: case State::TimeUp:
            if (stateTime > 2.5f && (in.confirmPressed || in.startPressed)) { resetRace(); state = State::Rolling; stateTime = 0; banner("ROLLING START!!", "Cliffside Coast", 3.2f, 0); }
            break;
        case State::Paused:
            if (in.startPressed || in.confirmPressed) state = pausedFrom;
            if (in.selectPressed) { resetRace(); state = State::Rolling; stateTime = 0; }
            return;
    }

    if (state == State::Race || state == State::Finished || state == State::TimeUp) raceTime += dt;
    const float h = 1.0f / 120.0f;
    static float acc = 0;
    acc += dt;
    if (state == State::Title) acc = 0;
    while (acc >= h) { step(h, in); acc -= h; }
    for (auto& c : cars) tyreFx(c, dt);
    updatePlaces();
    updateParticles(dt);
    updateCamera(dt);

    Car& pc = cars[player];
    // laps, checkpoints, arcade timer
    if (state == State::Race) {
        int lap = pc.lap(track.length);
        if (lap != pc.lastLap) {
            if (pc.lastLap > 0) {
                float lt = raceTime - pc.lapStart;
                pc.lastLapTime = lt;
                if (pc.bestLap <= 0 || lt < pc.bestLap) pc.bestLap = lt;
                pc.lapStart = raceTime;
                if (lap == laps) banner("FINAL LAP", "", 2.2f, 3);
            }
            pc.lastLap = lap;
        }
        // checkpoint sequence per lap: cp0, cp1, finish line
        float L = track.length;
        int lapIdx = (int)floorf(fmaxf(pc.dist, 0) / L);
        std::vector<float> cps = {track.checkpoints[0], track.checkpoints[1], L};
        int total = lapIdx * 3;
        float within = pc.dist - lapIdx * L;
        for (float c : cps) if (pc.dist >= 0 && within >= c) total++;
        if (total > nextCp) {
            int k = nextCp % 3;
            float seg = (k == 2) ? cps[0] : cps[k + 1] - cps[k];
            float bonus = fmaxf(8.0f, floorf(seg / 44.0f + 4.0f));
            nextCp = total;
            if (pc.dist < laps * L - 1) {
                timer += bonus;
                char sub[32]; snprintf(sub, sizeof sub, "checkpoint +%ds", (int)bonus);
                banner("EXTRA TIME", sub, 2.0f, 4);
                audio.checkpoints++;
            }
        }
        timer -= dt;
        if (timer <= 0 && !pc.finished) { timer = 0; state = State::TimeUp; stateTime = 0; banner("TIME UP", "", 3.0f, 5); }
        if (pc.finished) { state = State::Finished; stateTime = 0; banner("FINISH", "", 3.0f, 1); }
    }

    // weather
    if (weather == Weather::Rain) {
        nextLightning -= dt;
        if (nextLightning <= 0) { flash = 1.0f; nextLightning = 16.0f + frand() * 20.0f; audio.thunderPower = 0.6f + frand() * 0.4f; audio.thunders++; }
    }
    flash = fmaxf(0, flash - dt * 3.2f);

    // audio parameters
    float v = fabsf(dot(pc.vel, pc.fwd()));
    audio.rpm = pc.rpm;
    audio.throttle = pc.throttle;
    audio.speed = v / 90.0f;
    audio.skid = (weather == Weather::Sunny) ? clampf((pc.slideSpeed - 3.5f) / 10.0f, 0, 1) : 0.0f;
    audio.boost = pc.boosting ? 1.0f : 0.0f;
    audio.offroad = fabsf(pc.lat) > track.halfW + 1.8f ? 1.0f : 0.0f;
    audio.rain = weather == Weather::Rain ? 1.0f : 0.0f;
    audio.tunnel = track.inTunnel(pc.s) ? 1.0f : 0.0f;
    audio.crowd = track.inZone(pc.s, Z_STANDS) ? 1.0f : 0.0f;
    float nearest = 1e9f;
    for (size_t i = 0; i < cars.size(); i++) if ((int)i != player) nearest = fminf(nearest, len(cars[i].pos - pc.pos));
    audio.nearCar = clampf(1.0f - nearest / 40.0f, 0, 1);
    audio.master = state == State::Title ? 0.5f : 1.0f;
}

void Game::render(GLuint targetFbo, int w, int h) {
    width = w; height = h;
    if (!rnd) { rnd = new Renderer(); rnd->init(*this); }
    rnd->draw(*this, targetFbo, w, h);
}
