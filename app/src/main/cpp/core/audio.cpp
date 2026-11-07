// Procedural audio with an analog, gritty character.
// Engine: individual V8 firing pulses (uneven cross-plane lope) driving exhaust-pipe
// resonances, a pipe comb filter and tube-style saturation, plus intake rasp and
// off-throttle crackle. Tyres, scrapes, impacts, road rumble, crowd, rain and thunder
// are filtered noise. Everything passes through a short room echo, tape-style
// saturation and a soft high cut.
#include "game.h"
#include <string.h>

static inline float white(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFFFF) / 8388608.0f - 1.0f; }
static inline float coef(float hz, int rate) { float f = 2.0f * sinf(3.14159265f * fminf(hz, rate / 6.5f) / rate); return f; }
static inline float soft(float x) { return tanhf(x); }

void Synth::render(AudioParams& p, float* out, int frames, int rt) {
    rate = rt;
    if (!initd) {
        initd = true;
        // fixed per-cylinder character: slightly different strength and timing
        const float amp[8] = {1.0f, 0.86f, 0.95f, 0.78f, 0.97f, 0.83f, 0.92f, 0.88f};
        const float ofs[8] = {0.0f, 0.028f, -0.018f, 0.012f, 0.004f, -0.026f, 0.020f, -0.010f};
        memcpy(cylAmp, amp, sizeof amp); memcpy(cylOfs, ofs, sizeof ofs);
        memset(comb, 0, sizeof comb); memset(rvA, 0, sizeof rvA); memset(rvB, 0, sizeof rvB); memset(rvC, 0, sizeof rvC); memset(haas, 0, sizeof haas);
    }
    const float dt = 1.0f / rate;
    float rpmT = p.rpm, thrT = p.throttle, spdT = p.speed, skT = p.skid, boT = p.boost, nearT = p.nearCar;
    float offT = p.offroad, scT2 = p.scrape, tunT = p.tunnel, crT = p.crowd, rainT = p.rain;
    float master = p.master;
    int imp = p.impacts.load();
    if (imp != seenImpacts) { seenImpacts = imp; impactEnv = 1.0f; impactPow = p.impactPower; thumpPh = 0; }
    int th = p.thunders.load();
    if (th != seenThunder) { seenThunder = th; thunderDelay = 0.9f + (seed % 100) / 100.0f; thPow = p.thunderPower; }
    int sh = p.shifts.load();
    if (sh != seenShifts) { seenShifts = sh; shiftEnv = 1.0f; }
    int bf = p.backfires.load();
    if (bf != seenPops) { seenPops = bf; bfEnv = 1.0f; }
    int cp = p.checkpoints.load();
    if (cp != seenCp) { seenCp = cp; chimeEnv = 1.0f; chimePh = 0; }

    const float evPos[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    const int combLen = (int)(0.0034f * rate);           // ~1 m of exhaust pipe

    for (int i = 0; i < frames; i++) {
        rpmS += (rpmT - rpmS) * 0.0025f;
        thrS += (thrT - thrS) * 0.003f;
        spdS += (spdT - spdS) * 0.001f;
        skS += (skT - skS) * 0.004f;
        boS += (boT - boS) * 0.002f;
        nearS += (nearT - nearS) * 0.0008f;
        offS += (offT - offS) * 0.003f;
        scS += (scT2 - scS) * 0.006f;
        tunS += (tunT - tunS) * 0.0004f;
        crS += (crT - crS) * 0.0003f;
        rainS += (rainT - rainS) * 0.0005f;
        float n = white(seed), n2 = white(seed);

        // ---------------- engine
        wowPh += 0.37 * dt; if (wowPh > 1) wowPh -= 1;
        float wow = 1.0f + 0.004f * sinf((float)wowPh * 6.2831853f);
        float rpmE = (900.0f + rpmS * 7300.0f) * (1.0f - shiftEnv * 0.22f) * wow;
        crank += (double)(rpmE / 60.0f * 0.5f) * dt;          // one cycle = 2 revolutions = 8 firings
        if (crank >= 1.0) { crank -= 1.0; nextEv = 0; }
        float ph8 = (float)crank * 8.0f;
        while (nextEv < 8 && ph8 >= evPos[nextEv] + cylOfs[nextEv] * 8.0f) {
            float load = 0.30f + 0.70f * thrS;
            float a = cylAmp[nextEv] * (0.85f + 0.3f * (white(seed) * 0.5f + 0.5f)) * load;
            // lean misfires at light throttle make the idle lumpy
            if (thrS < 0.2f && (white(seed) * 0.5f + 0.5f) < 0.04f) a *= 0.3f;
            pulse += a;
            rasp += a * 0.8f;
            nextEv++;
        }
        float x = pulse * (1.0f + 0.5f * n);
        pulse *= expf(-dt / 0.0022f);
        float rs = n2 * rasp;
        rasp *= expf(-dt / 0.0065f);
        float r01 = rpmS;
        float b1 = ex1.band(x, coef(115.0f + r01 * 70.0f, rate), 0.30f);
        float b2 = ex2.band(x, coef(390.0f + r01 * 320.0f, rate), 0.45f);
        float b3 = ex3.band(x + rs * 0.6f, coef(1050.0f + r01 * 700.0f, rate), 0.65f);
        float intake = in1.band(rs, coef(2200.0f + r01 * 900.0f, rate), 0.8f) * (0.15f + 0.5f * thrS);
        float body = ex1.lp;   // low-passed pulse train: the chest thump of the V8
        float e = (b1 * 2.4f + body * 0.9f + b2 * 0.9f + b3 * 0.28f * (0.35f + thrS) + intake * 0.6f) * 0.32f;
        // exhaust pipe comb resonance
        int rp = (combPos - combLen + 4096) & 4095;
        float y = e + comb[rp] * 0.42f;
        comb[combPos] = y; combPos = (combPos + 1) & 4095;
        // tube-ish asymmetric drive
        float drive = 1.4f + thrS * 1.6f + boS * 0.6f;
        float sat = soft(y * drive) + 0.22f * soft(y * drive * 0.6f) * soft(y * drive * 0.6f);
        engDC += (sat - engDC) * 0.0005f;
        sat -= engDC;
        engLP += (sat - engLP) * (0.09f + 0.16f * thrS);
        engLP2 += (engLP - engLP2) * (0.18f + 0.2f * thrS);
        float engine = engLP2 * (0.62f + 0.30f * thrS) * (1.0f - shiftEnv * 0.45f) * (1.0f + tunS * 0.35f);
        shiftEnv *= 0.9992f;

        // off-throttle crackle and shift backfires
        popTimer -= dt;
        if (thrS < 0.12f && rpmS > 0.45f && popTimer <= 0) {
            if ((white(seed) * 0.5f + 0.5f) < 0.0009f * (rpmS - 0.3f) * 48000.0f / rate) popEnv = 0.6f + 0.4f * (white(seed) * 0.5f + 0.5f), popTimer = 0.04f;
        }
        if (bfEnv > 0.01f) { popEnv = fmaxf(popEnv, bfEnv); bfEnv = 0; }
        float pop = 0;
        if (popEnv > 0.002f) {
            pop = soft(pop1.band(n * 3.0f, coef(520.0f, rate), 0.35f) * popEnv * 4.0f) * 0.55f;
            popEnv *= expf(-dt / 0.018f);
        }

        // other cars nearby: a softer, detuned engine
        oph += (rpmE / 60.0f * 4.0f * 1.06f) * dt; if (oph > 1) oph -= 1;
        float other = oth1.band(((float)oph < 0.25f ? 1.0f : -0.3f) + n * 0.3f, coef(240.0f, rate), 0.5f) * nearS * 0.35f;

        // ---------------- tyres, scrapes, impacts
        float sqWob = sinf((float)wowPh * 6.2831853f * 23.0f) * 90.0f;
        float squeal = (sq1.band(n, coef(820.0f + sqWob + spdS * 180.0f, rate), 0.04f) * 1.0f +
                        sq2.band(n2, coef(1450.0f - sqWob, rate), 0.06f) * 0.35f) * skS;
        squeal = soft(squeal * 1.2f) * 0.20f + brown * skS * 0.8f;
        scT -= dt;
        if (scT <= 0) { scT = 0.008f + 0.02f * (white(seed) * 0.5f + 0.5f); scAM = 0.3f + 0.7f * (white(seed) * 0.5f + 0.5f); }
        float scrape = soft(sc1.band(n * scAM, coef(3100.0f, rate), 0.09f) * 4.0f) * scS * 0.35f;
        float impact = 0;
        if (impactEnv > 0.0008f) {
            thumpPh += 52.0 * dt;
            float env = impactEnv;
            float metal = im1.band(n, coef(310.0f, rate), 0.12f) * 1.6f + im2.band(n, coef(1250.0f, rate), 0.10f) * 1.0f + im3.band(n, coef(2650.0f, rate), 0.08f) * 0.6f;
            impact = soft((metal * env * 1.5f + sinf((float)thumpPh * 6.2831853f) * env * env * 1.4f + n * env * env * env * env * 0.8f) * (0.6f + impactPow * 0.8f)) * 0.75f;
            impactEnv *= expf(-dt / 0.11f);
        }

        // ---------------- road, wind, crowd, rain
        brown += (n - brown) * 0.015f;
        float roadTone = road1.band(n, coef(260.0f, rate), 0.6f);
        float road = (brown * 2.2f + roadTone * 0.5f) * spdS * (0.25f + rainS * 0.25f) + brown * offS * 3.0f;
        float wind = wind1.band(n2, coef(900.0f + spdS * 700.0f, rate), 1.2f) * spdS * spdS * 0.25f;
        float crowd = crowd1.band(n, coef(780.0f, rate), 0.9f) * crS * (0.7f + 0.3f * sinf((float)wowPh * 6.2831853f * 2.0f)) * 0.5f;
        float rain = (brown * 0.5f + (n2 - brown) * 0.05f) * rainS * 0.5f;
        float whoosh = wind1.lp * boS * 0.4f;
        float thunder = 0;
        if (thunderDelay > 0) { thunderDelay -= dt; if (thunderDelay <= 0) thunderEnv = 1.0f; }
        if (thunderEnv > 0.0005f) {
            thLP += (n - thLP) * 0.01f; thLP2 += (thLP - thLP2) * 0.02f;
            thunder = thLP2 * 7.0f * thunderEnv * (1.0f + 0.6f * sinf(thunderEnv * 40.0f) * thunderEnv) * thPow;
            thunderEnv *= expf(-dt / 2.2f);
        }
        float chime = 0;
        if (chimeEnv > 0.001f) {
            chimePh += dt;
            float fr = chimePh < 0.11 ? 880.0f : 1320.0f;
            chime = soft(sinf((float)chimePh * fr * 6.2831853f) * 1.5f) * chimeEnv * 0.14f;
            chimeEnv *= expf(-dt / 0.35f);
        }

        // ---------------- mix, room echo, tape character
        float car = (engine + pop + other + squeal + scrape + road + wind + whoosh + impact + crowd) * master;
        // small room / tunnel echo from three short combs
        float in = car * 0.5f;
        float a1 = rvA[ra], a2 = rvB[rb], a3 = rvC[rc];
        float fb = 0.45f + tunS * 0.3f;
        rvLP += ((a1 + a2 + a3) * 0.333f - rvLP) * 0.35f;
        rvA[ra] = in + rvLP * fb; rvB[rb] = in + a2 * fb * 0.9f; rvC[rc] = in + a3 * fb * 0.85f;
        ra = (ra + 1) % 2400; rb = (rb + 1) % 2900; rc = (rc + 1) % 3400;
        float wet = (a1 * 0.5f + a2 * 0.3f + a3 * 0.2f) * (0.10f + tunS * 0.30f);
        // slight stereo width on the engine
        haas[hpos] = engine * master; float late = haas[(hpos + 64 - 18) & 63]; hpos = (hpos + 1) & 63;
        float L = car + wet + rain + thunder + chime + late * 0.08f;
        float R = car - late * 0.08f + wet * 0.9f + rain + thunder + chime;
        // tape: soft saturation, gentle high cut, faint hiss
        L = soft(L * 1.15f) * 0.92f + n2 * 0.0025f;
        R = soft(R * 1.15f) * 0.92f + n * 0.0025f;
        outLP_L += (L - outLP_L) * 0.45f; outLP_R += (R - outLP_R) * 0.45f;
        out[i * 2] = outLP_L;
        out[i * 2 + 1] = outLP_R;
    }
}
