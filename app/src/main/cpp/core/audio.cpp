// Procedural audio: engine, gear shifts, backfires, tyre squeal, scraping, impacts, crowd, rain, thunder.
#include "game.h"

static inline float white(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFFFF) / 8388608.0f - 1.0f; }

void Synth::render(AudioParams& p, float* out, int frames, int rate) {
    const float dt = 1.0f / rate;
    float rpmT = p.rpm, thrT = p.throttle, spdT = p.speed, skT = p.skid, boT = p.boost, nearT = p.nearCar;
    float offT = p.offroad, scT = p.scrape, tunT = p.tunnel, crT = p.crowd;
    float master = p.master, rainAmt = p.rain;
    int imp = p.impacts.load();
    if (imp != seenImpacts) { seenImpacts = imp; impactEnv = 1.0f; impactPow = p.impactPower; }
    int th = p.thunders.load();
    if (th != seenThunder) { seenThunder = th; thunderDelay = 0.9f + (seed % 100) / 100.0f; thPow = p.thunderPower; }
    int sh = p.shifts.load();
    if (sh != seenShifts) { seenShifts = sh; shiftEnv = 1.0f; }
    int bf = p.backfires.load();
    if (bf != seenPops) { seenPops = bf; popEnv = 1.0f; }
    int cp = p.checkpoints.load();
    if (cp != seenCp) { seenCp = cp; chimeEnv = 1.0f; chimePh = 0; }
    for (int i = 0; i < frames; i++) {
        rpmS += (rpmT - rpmS) * 0.002f;
        thrS += (thrT - thrS) * 0.002f;
        spdS += (spdT - spdS) * 0.001f;
        skS += (skT - skS) * 0.003f;
        boS += (boT - boS) * 0.002f;
        nearS += (nearT - nearS) * 0.0008f;
        offS += (offT - offS) * 0.002f;
        scS += (scT - scS) * 0.004f;
        tunS += (tunT - tunS) * 0.0005f;
        crS += (crT - crS) * 0.0003f;
        float n = white(seed);

        // engine: V8 firing frequency with burble; dips on gear changes
        float dip = shiftEnv * 0.25f;
        float f = 42.0f + (rpmS - dip) * 185.0f;
        ph += f * dt; if (ph > 1.0) ph -= 1.0;
        lfo += f * 0.5 * dt; if (lfo > 1.0) lfo -= 1.0;
        float x = (float)ph;
        float saw = 2.0f * x - 1.0f, pulse = x < 0.3f ? 1.0f : -0.42f;
        float burble = 0.72f + 0.28f * sinf((float)lfo * 6.2831853f);
        float raw = (saw * 0.5f + pulse * 0.5f) * burble + n * 0.07f;
        float cut = 0.05f + thrS * 0.11f + rpmS * 0.06f;
        engLP += (raw - engLP) * cut;
        eng += (engLP - eng) * cut;
        float engine = eng * (0.32f + thrS * 0.48f) * (1.0f - shiftEnv * 0.5f) * (1.0f + tunS * 0.5f);
        shiftEnv *= 0.9993f;

        // backfire pops
        float pop = 0;
        if (popEnv > 0.01f) { pop = n * popEnv * popEnv * 0.9f * (sinf(popEnv * 300.0f) > 0 ? 1.0f : 0.3f); popEnv *= 0.9990f; }

        // other cars
        ph2 += (f * 1.07f + 9.0f) * dt; if (ph2 > 1.0) ph2 -= 1.0;
        lp4 += (((float)ph2 * 2.0f - 1.0f) * nearS * 0.10f - lp4) * 0.08f;

        // tyre squeal: band-limited noise with a resonant tone
        sq += (900.0 + 250.0 * sinf((float)lfo * 3.0f) + spdS * 300.0) * dt; if (sq > 1.0) sq -= 1.0;
        lp3 += (n - lp3) * 0.45f;
        float hiss = n - lp3;
        float tone = sinf((float)sq * 6.2831853f) * 0.6f + sinf((float)sq * 12.566f) * 0.2f;
        sqLP += (tone * (0.7f + 0.3f * hiss) - sqLP) * 0.5f;
        float squeal = (sqLP * 0.22f + hiss * 0.10f) * skS;

        // metal scraping
        scLP += (n - scLP) * 0.25f;
        float scrape = (n - scLP) * scS * 0.25f * (0.6f + 0.4f * sinf((float)ph * 40.0f));

        // road noise, wind, rain
        lp1 += (n - lp1) * 0.35f;
        hp = n - lp1;
        lp2 += (n - lp2) * (0.10f + spdS * 0.22f);
        float road = lp2 * spdS * (0.10f + rainAmt * 0.14f) + lp2 * offS * 0.25f;
        float wind = hp * spdS * spdS * 0.03f;
        float rain = (lp1 * 0.05f + hp * 0.06f) * rainAmt;
        float whoosh = lp2 * boS * 0.25f;

        // crowd roar near the grandstands
        crowdLP += (n - crowdLP) * 0.06f;
        cph += 2.3 * dt; if (cph > 1) cph -= 1;
        float crowd = crowdLP * crS * (0.6f + 0.4f * sinf((float)cph * 6.28f)) * 0.35f;

        // impacts
        float impact = 0;
        if (impactEnv > 0.001f) {
            impact = (n * 0.6f + sinf(impactEnv * 70.0f) * 0.9f) * impactEnv * impactEnv * 0.8f * (0.4f + impactPow * 0.6f);
            impactEnv *= 0.9993f;
        }
        // checkpoint chime
        float chime = 0;
        if (chimeEnv > 0.001f) {
            chimePh += dt;
            float fr = chimePh < 0.12 ? 880.0f : 1320.0f;
            chime = sinf((float)chimePh * fr * 6.2831853f) * chimeEnv * 0.18f;
            chimeEnv *= 0.99985f;
        }
        // thunder
        float thunder = 0;
        if (thunderDelay > 0) { thunderDelay -= dt; if (thunderDelay <= 0) thunderEnv = 1.0f; }
        if (thunderEnv > 0.0005f) {
            thLP += (n - thLP) * 0.01f;
            thLP2 += (thLP - thLP2) * 0.02f;
            thunder = thLP2 * 7.0f * thunderEnv * (1.0f + 0.6f * sinf(thunderEnv * 40.0f) * thunderEnv) * thPow;
            thunderEnv *= 0.99994f;
        }
        float mono = (engine + pop + lp4 + squeal + scrape + road + wind + whoosh + impact + crowd) * master + rain + thunder + chime;
        out[i * 2] = tanhf((mono + hp * 0.015f) * 0.9f);
        out[i * 2 + 1] = tanhf((mono - hp * 0.015f) * 0.9f);
    }
}
