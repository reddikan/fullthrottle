// Procedural audio: engine, rain, tyre spray, impacts, thunder. No music.
#include "game.h"

static inline float white(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFFFF) / 8388608.0f - 1.0f; }

void Synth::render(AudioParams& p, float* out, int frames, int rate) {
    const float dt = 1.0f / rate;
    float rpmT = p.rpm, thrT = p.throttle, spdT = p.speed, drT = p.drift, boT = p.boost, nearT = p.nearCar, offT = p.offroad;
    float master = p.master;
    int imp = p.impacts.load();
    if (imp != seenImpacts) { seenImpacts = imp; impactEnv = 1.0f; }
    int th = p.thunders.load();
    if (th != seenThunder) { seenThunder = th; thunderDelay = 0.9f + (seed % 100) / 100.0f; thPow = p.thunderPower; }
    for (int i = 0; i < frames; i++) {
        rpmS += (rpmT - rpmS) * 0.0015f;
        thrS += (thrT - thrS) * 0.002f;
        spdS += (spdT - spdS) * 0.001f;
        drS += (drT - drS) * 0.002f;
        boS += (boT - boS) * 0.002f;
        nearS += (nearT - nearS) * 0.0008f;
        offS += (offT - offS) * 0.002f;

        // engine: firing frequency of a V8
        float f = 38.0f + rpmS * 175.0f;
        ph += f * dt; if (ph > 1.0) ph -= 1.0;
        lfo += f * 0.5 * dt; if (lfo > 1.0) lfo -= 1.0;
        float x = (float)ph;
        float saw = 2.0f * x - 1.0f;
        float pulse = x < 0.32f ? 1.0f : -0.45f;
        float burble = 0.75f + 0.25f * sinf((float)lfo * 6.2831853f);
        float raw = (saw * 0.55f + pulse * 0.45f) * burble + white(seed) * 0.08f;
        float cut = 0.04f + thrS * 0.10f + rpmS * 0.06f;
        engLP += (raw - engLP) * cut;
        eng += (engLP - eng) * cut;
        float engine = eng * (0.30f + thrS * 0.45f) * 0.9f;

        // opponents nearby: a detuned second engine
        ph2 += (f * 1.07f + 9.0f) * dt; if (ph2 > 1.0) ph2 -= 1.0;
        float other = ((float)ph2 * 2.0f - 1.0f) * nearS * 0.10f;
        lp4 += (other - lp4) * 0.08f;

        // rain: broadband hiss + low rumble on the roof
        float n = white(seed);
        lp1 += (n - lp1) * 0.35f;
        hp = n - lp1;
        float rain = (lp1 * 0.05f + hp * 0.06f) * p.rain;
        // tyre spray hiss rises with speed
        lp2 += (n - lp2) * (0.12f + spdS * 0.25f);
        float spray = lp2 * spdS * 0.22f;
        // drift: wet slide hiss
        lp3 += (n - lp3) * 0.5f;
        float slide = (n - lp3) * drS * 0.18f + lp2 * offS * 0.25f;
        // boost whoosh
        float whoosh = lp2 * boS * 0.25f;
        // impact thump
        float impact = 0;
        if (impactEnv > 0.001f) {
            impact = (white(seed) * 0.5f + sinf(impactEnv * 90.0f) * 0.8f) * impactEnv * impactEnv * 0.7f;
            impactEnv *= 0.9992f;
        }
        // thunder
        float thunder = 0;
        if (thunderDelay > 0) {
            thunderDelay -= dt;
            if (thunderDelay <= 0) thunderEnv = 1.0f;
        }
        if (thunderEnv > 0.0005f) {
            thLP += (n - thLP) * 0.01f;
            thLP2 += (thLP - thLP2) * 0.02f;
            float crackle = 1.0f + 0.6f * sinf(thunderEnv * 40.0f) * thunderEnv;
            thunder = thLP2 * 7.0f * thunderEnv * crackle * thPow;
            thunderEnv *= 0.99994f;
        }
        float mono = (engine + lp4 + spray + slide + whoosh + impact) * master + rain + thunder;
        float l = mono + hp * 0.02f, r = mono - hp * 0.02f;
        out[i * 2] = tanhf(l * 0.9f);
        out[i * 2 + 1] = tanhf(r * 0.9f);
    }
}
