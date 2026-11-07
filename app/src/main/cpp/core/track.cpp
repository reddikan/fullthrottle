// Circuit layout and queries.
#include "game.h"

static V3 catmull(V3 p0, V3 p1, V3 p2, V3 p3, float t) {
    float t2 = t * t, t3 = t2 * t;
    return (p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) * 0.5f;
}

void Track::build() {
    // "Cliffside Coast": main straight (+x) -> lake hairpin -> esses -> cliff sweeper ->
    // tunnel (north) -> lighthouse hairpin by the sea -> fast west run -> final corner.
    const float cp[][2] = {
        {-300, 0}, {0, 0}, {300, 0}, {520, 0},
        {650, 35}, {705, 145}, {660, 255}, {540, 272},
        {425, 238}, {320, 300}, {215, 252}, {105, 300}, {20, 385},
        {-75, 480}, {-150, 610}, {-190, 760}, {-200, 880}, {-196, 1000},
        {-215, 1105}, {-300, 1165}, {-400, 1140}, {-452, 1040},
        {-472, 880}, {-520, 700}, {-560, 520}, {-545, 345}, {-490, 170}, {-425, 50}};
    int nc = sizeof(cp) / sizeof(cp[0]);
    std::vector<V3> dense;
    for (int i = 0; i < nc; i++) {
        V3 p0(cp[(i + nc - 1) % nc][0], 0, cp[(i + nc - 1) % nc][1]);
        V3 p1(cp[i][0], 0, cp[i][1]);
        V3 p2(cp[(i + 1) % nc][0], 0, cp[(i + 1) % nc][1]);
        V3 p3(cp[(i + 2) % nc][0], 0, cp[(i + 2) % nc][1]);
        for (int k = 0; k < 200; k++) dense.push_back(catmull(p0, p1, p2, p3, k / 200.0f));
    }
    // rotate so the ring starts at the start line (x = 0 on the main straight)
    int startIdx = 0; float best = 1e9f;
    for (size_t i = 0; i < dense.size() / 6; i++) {
        float d = fabsf(dense[i].x) + fabsf(dense[i].z);
        if (d < best) { best = d; startIdx = (int)i; }
    }
    std::vector<V3> ring;
    for (size_t i = 0; i < dense.size(); i++) ring.push_back(dense[(startIdx + i) % dense.size()]);
    P.clear();
    P.push_back(ring[0]);
    float acc = 0;
    for (size_t i = 1; i <= ring.size(); i++) {
        V3 a = ring[i - 1], b = ring[i % ring.size()];
        float seg = len(b - a);
        while (acc + seg >= ds) {
            float t = (ds - acc) / seg;
            a = lerp(a, b, t); seg = len(b - a); acc = 0;
            P.push_back(a);
        }
        acc += seg;
    }
    if (len(P.back() - P[0]) < ds * 0.5f) P.pop_back();
    n = (int)P.size();
    length = n * ds;
    T.resize(n); R.resize(n); S.resize(n); K.resize(n);
    std::vector<float> ang(n);
    for (int i = 0; i < n; i++) {
        T[i] = norm(P[(i + 1) % n] - P[(i + n - 1) % n]);
        R[i] = V3(-T[i].z, 0, T[i].x);
        S[i] = i * ds;
        ang[i] = atan2f(T[i].x, T[i].z);
    }
    std::vector<float> k0(n);
    for (int i = 0; i < n; i++) k0[i] = -wrapAngle(ang[(i + 2) % n] - ang[(i + n - 2) % n]) / (4 * ds);
    for (int i = 0; i < n; i++) {
        float s = 0;
        for (int j = -6; j <= 6; j++) s += k0[(i + j + n) % n];
        K[i] = s / 13;
    }

    auto sAt = [&](float x, float z) { return S[nearest(V3(x, 0, z), -1)]; };
    zones.clear();
    float sStandA = sAt(-250, 0), sStandB = sAt(430, 0);
    zones.push_back({sStandA, length, Z_STANDS});
    zones.push_back({0, sStandB, Z_STANDS});
    zones.push_back({sAt(10, 395), sAt(-178, 700), Z_CLIFF_R});
    zones.push_back({sAt(-197, 800), sAt(-197, 975), Z_TUNNEL});
    zones.push_back({sAt(-478, 860), sAt(-552, 440), Z_HOUSES_L});
    zones.push_back({sAt(-505, 640), sAt(-540, 330), Z_HOUSES_R});
    zones.push_back({sAt(-140, 590), sAt(-215, 1100), Z_FOREST});
    checkpoints = {sAt(20, 385), sAt(-452, 1040)};

    lakeC = V3(598, -0.35f, 140);
    lakeR = distTo(lakeC) - wallOff - 9.0f;
    lighthouse = V3(-315, 0, 1232);
    ftLog("track %.0f m, %d samples, lake r=%.0f, checkpoints %.0f %.0f", length, n, lakeR, checkpoints[0], checkpoints[1]);
}

bool Track::inZone(float s, int type) const {
    s = wrapS(s);
    for (auto& z : zones) if (z.type == type && s >= z.s0 && s <= z.s1) return true;
    return false;
}
int Track::nearest(V3 p, int hint) const {
    int bestI = 0; float best = 1e18f;
    if (hint < 0) {
        for (int i = 0; i < n; i++) { V3 d = p - P[i]; float dd = d.x * d.x + d.z * d.z; if (dd < best) { best = dd; bestI = i; } }
        return bestI;
    }
    for (int j = -40; j <= 40; j++) {
        int i = (hint + j + n) % n;
        V3 d = p - P[i]; float dd = d.x * d.x + d.z * d.z;
        if (dd < best) { best = dd; bestI = i; }
    }
    return bestI;
}
V3 Track::at(float s, float lat) const {
    s = wrapS(s);
    int i = (int)(s / ds) % n; float f = (s - i * ds) / ds;
    int j = (i + 1) % n;
    return lerp(P[i], P[j], f) + norm(lerp(R[i], R[j], f)) * lat;
}
V3 Track::tangentAt(float s) const { s = wrapS(s); int i = (int)(s / ds) % n; return norm(lerp(T[i], T[(i + 1) % n], (s - i * ds) / ds)); }
V3 Track::rightAt(float s) const { V3 t = tangentAt(s); return V3(-t.z, 0, t.x); }
float Track::curvatureAt(float s) const { s = wrapS(s); int i = (int)(s / ds) % n; return K[i]; }
float Track::distTo(V3 p) const {
    float best = 1e18f;
    for (int i = 0; i < n; i += 3) { V3 d = p - P[i]; float dd = d.x * d.x + d.z * d.z; if (dd < best) best = dd; }
    return sqrtf(best);
}
