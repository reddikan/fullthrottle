// Track, scenery and car geometry generation.
#include "render.h"
#include <string.h>

// ------------------------------------------------------------- mesh helpers
void MeshBuilder::tri(V3 a, V3 b, V3 c, V3 col, float aux, float emis) {
    V3 n = norm(cross(b - a, c - a));
    Vtx t[3] = {{a.x, a.y, a.z, n.x, n.y, n.z, col.x, col.y, col.z, emis, aux},
                {b.x, b.y, b.z, n.x, n.y, n.z, col.x, col.y, col.z, emis, aux},
                {c.x, c.y, c.z, n.x, n.y, n.z, col.x, col.y, col.z, emis, aux}};
    v.insert(v.end(), t, t + 3);
}
void MeshBuilder::box(V3 mn, V3 mx, V3 col, float aux, float emis, const M4* xf) {
    V3 p[8];
    for (int i = 0; i < 8; i++) {
        p[i] = V3(i & 1 ? mx.x : mn.x, i & 2 ? mx.y : mn.y, i & 4 ? mx.z : mn.z);
        if (xf) p[i] = xformPoint(*xf, p[i]);
    }
    // CCW when viewed from outside
    quad(p[0], p[4], p[6], p[2], col, aux, emis);  // -x
    quad(p[1], p[3], p[7], p[5], col, aux, emis);  // +x
    quad(p[0], p[1], p[5], p[4], col, aux, emis);  // -y
    quad(p[2], p[6], p[7], p[3], col, aux, emis);  // +y
    quad(p[0], p[2], p[3], p[1], col, aux, emis);  // -z
    quad(p[4], p[5], p[7], p[6], col, aux, emis);  // +z
}

// ------------------------------------------------------------- font
bool Font::load(const std::vector<uint8_t>& d) {
    if (d.size() < 16) return false;
    int32_t hdr[3]; memcpy(hdr, d.data(), 12); memcpy(&lineH, d.data() + 12, 4);
    w = hdr[0]; h = hdr[1]; int n = hdr[2];
    size_t off = 16;
    for (int i = 0; i < n; i++) {
        int32_t cp; float f[9];
        memcpy(&cp, d.data() + off, 4); memcpy(f, d.data() + off + 4, 36); off += 40;
        if (cp >= 0 && cp < 128) { g[cp] = {f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], true}; }
    }
    if (d.size() < off + (size_t)w * h) return false;
    pixels.assign(d.begin() + off, d.begin() + off + (size_t)w * h);
    return true;
}
float Font::measure(const char* s) const {
    float x = 0;
    for (const char* p = s; *p; ++p) { unsigned c = (unsigned char)*p; if (c < 128 && g[c].valid) x += g[c].adv; }
    return x;
}

// ------------------------------------------------------------- track
static V3 catmull(V3 p0, V3 p1, V3 p2, V3 p3, float t) {
    float t2 = t * t, t3 = t2 * t;
    return (p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) * 0.5f;
}

void Track::build() {
    // Hand-laid circuit (metres). Start/finish straight runs +x along z=0.
    const float cp[][2] = {
        {-160, 0}, {60, 0}, {280, 0}, {420, 40}, {500, 160}, {480, 300}, {380, 380},
        {250, 400}, {170, 480}, {190, 620}, {110, 740}, {-40, 760}, {-170, 680},
        {-190, 540}, {-110, 430}, {-160, 320}, {-320, 300}, {-430, 210}, {-420, 80}, {-320, 10}};
    int nc = sizeof(cp) / sizeof(cp[0]);
    std::vector<V3> dense;
    for (int i = 0; i < nc; i++) {
        V3 p0(cp[(i + nc - 1) % nc][0], 0, cp[(i + nc - 1) % nc][1]);
        V3 p1(cp[i][0], 0, cp[i][1]);
        V3 p2(cp[(i + 1) % nc][0], 0, cp[(i + 1) % nc][1]);
        V3 p3(cp[(i + 2) % nc][0], 0, cp[(i + 2) % nc][1]);
        for (int k = 0; k < 200; k++) dense.push_back(catmull(p0, p1, p2, p3, k / 200.0f));
    }
    // arc-length resample, starting at the start line (x=0 on the first straight)
    int startIdx = 0; float best = 1e9f;
    for (size_t i = 0; i < dense.size() / 4; i++) {
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
            a = lerp(a, b, t);
            seg = len(b - a);
            acc = 0;
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
    for (int i = 0; i < n; i++) {  // smooth
        float s = 0;
        for (int j = -6; j <= 6; j++) s += k0[(i + j + n) % n];
        K[i] = s / 13;
    }
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
    V3 c = lerp(P[i], P[j], f);
    V3 r = norm(lerp(R[i], R[j], f));
    return c + r * lat;
}
V3 Track::tangentAt(float s) const { s = wrapS(s); int i = (int)(s / ds) % n; return T[i]; }
float Track::curvatureAt(float s) const { s = wrapS(s); int i = (int)(s / ds) % n; return K[i]; }
float Track::distTo(V3 p) const {
    float best = 1e18f;
    for (int i = 0; i < n; i += 3) { V3 d = p - P[i]; float dd = d.x * d.x + d.z * d.z; if (dd < best) best = dd; }
    return sqrtf(best);
}

// ------------------------------------------------------------- terrain
float terrainHeight(float x, float z, float d, float halfW) {
    float flat = halfW + 10.0f;
    if (d < flat) return -0.06f;
    float t = smooth(flat, flat + 70.0f, d);
    float n = fbm(x * 0.0042f + 11.3f, z * 0.0042f + 7.1f, 4);
    float hills = powf(n, 1.5f) * 85.0f * t;
    float m = fbm(x * 0.0021f + 40.0f, z * 0.0021f - 13.0f, 4);
    float mountains = smooth(320.0f, 720.0f, d) * powf(m, 1.6f) * 330.0f;
    float bumps = t * (fbm(x * 0.03f, z * 0.03f, 2) - 0.5f) * 4.0f;
    // gentle embankment right next to the barrier
    float bank = smooth(flat, flat + 8.0f, d) * (1.0f - smooth(flat + 20.0f, flat + 60.0f, d)) * 2.5f * vnoise(x * 0.02f, z * 0.02f);
    return -0.06f + hills + mountains + bumps + bank;
}

static V3 grassColor(float x, float z, float h, float slope) {
    float n = fbm(x * 0.02f, z * 0.02f, 3);
    V3 a(0.27f, 0.50f, 0.15f), b(0.36f, 0.60f, 0.19f);
    V3 c = lerp(a, b, n);
    c = lerp(c, V3(0.20f, 0.38f, 0.13f), sat(slope * 1.4f - 0.3f));
    c = lerp(c, V3(0.40f, 0.43f, 0.38f), smooth(110.0f, 220.0f, h + n * 30.0f));
    return c;
}

// ------------------------------------------------------------- scenery pieces
static void icosa(MeshBuilder& m, V3 c, V3 s, V3 col, Rng& rng, float jitter) {
    const float t = 1.618034f;
    V3 v[12] = {V3(-1, t, 0), V3(1, t, 0), V3(-1, -t, 0), V3(1, -t, 0), V3(0, -1, t), V3(0, 1, t),
                V3(0, -1, -t), V3(0, 1, -t), V3(t, 0, -1), V3(t, 0, 1), V3(-t, 0, -1), V3(-t, 0, 1)};
    static const int f[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
                                 {11, 10, 2}, {10, 7, 6}, {7, 1, 8}, {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8},
                                 {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    for (auto& p : v) { p = norm(p) * (1.0f + rng.range(-jitter, jitter)); p = c + mul(p, s); }
    for (auto& tr : f) {
        V3 cc = col * rng.range(0.88f, 1.1f);
        m.tri(v[tr[0]], v[tr[1]], v[tr[2]], cc);
    }
}
static void prism(MeshBuilder& m, V3 base, float r0, float r1, float h, int sides, V3 col, V3 lean = V3()) {
    for (int i = 0; i < sides; i++) {
        float a0 = 2 * PI * i / sides, a1 = 2 * PI * (i + 1) / sides;
        V3 b0 = base + V3(cosf(a0) * r0, 0, sinf(a0) * r0), b1 = base + V3(cosf(a1) * r0, 0, sinf(a1) * r0);
        V3 top = base + V3(0, h, 0) + lean;
        V3 t0 = top + V3(cosf(a0) * r1, 0, sinf(a0) * r1), t1 = top + V3(cosf(a1) * r1, 0, sinf(a1) * r1);
        m.quad(b0, t0, t1, b1, col);
    }
}
static void blobTree(MeshBuilder& m, V3 p, Rng& rng) {
    float sc = rng.range(0.8f, 1.35f);
    float th = rng.range(2.5f, 4.0f) * sc;
    prism(m, p, 0.35f * sc, 0.25f * sc, th + 1.0f, 5, V3(0.33f, 0.22f, 0.13f));
    V3 g = lerp(V3(0.20f, 0.45f, 0.12f), V3(0.38f, 0.62f, 0.18f), rng.f());
    int lumps = 2 + (rng.next() % 2);
    for (int i = 0; i < lumps; i++) {
        V3 c = p + V3(rng.range(-1.2f, 1.2f) * sc, th + rng.range(1.0f, 2.6f) * sc, rng.range(-1.2f, 1.2f) * sc);
        float r = rng.range(1.8f, 2.8f) * sc;
        icosa(m, c, V3(r, r * 0.9f, r), g, rng, 0.18f);
    }
}
static void umbrellaTree(MeshBuilder& m, V3 p, Rng& rng) {
    float sc = rng.range(0.9f, 1.4f);
    float th = rng.range(6.0f, 9.0f) * sc;
    V3 lean(rng.range(-0.8f, 0.8f), 0, rng.range(-0.8f, 0.8f));
    prism(m, p, 0.3f * sc, 0.2f * sc, th, 5, V3(0.30f, 0.21f, 0.14f), lean);
    V3 g = lerp(V3(0.18f, 0.40f, 0.12f), V3(0.30f, 0.55f, 0.16f), rng.f());
    icosa(m, p + lean + V3(0, th + 0.6f * sc, 0), V3(4.2f * sc, 1.3f * sc, 4.2f * sc), g, rng, 0.12f);
}
static void palmTree(MeshBuilder& m, V3 p, Rng& rng) {
    float h = rng.range(7.0f, 11.0f);
    V3 bend(rng.range(-1, 1), 0, rng.range(-1, 1));
    bend = norm(bend) * rng.range(0.8f, 2.0f);
    V3 cur = p; float r = 0.32f;
    for (int i = 0; i < 5; i++) {
        float t = (i + 1) / 5.0f;
        V3 next = p + V3(0, h * t, 0) + bend * (t * t);
        V3 d = next - cur;
        prism(m, cur, r, r * 0.9f, d.y, 5, V3(0.42f, 0.33f, 0.22f), V3(d.x, 0, d.z));
        cur = next; r *= 0.92f;
    }
    V3 g = lerp(V3(0.20f, 0.46f, 0.14f), V3(0.30f, 0.58f, 0.18f), rng.f());
    int fr = 7;
    for (int i = 0; i < fr; i++) {
        float a = 2 * PI * i / fr + rng.range(-0.2f, 0.2f);
        V3 dir(cosf(a), 0, sinf(a)), side(-sinf(a), 0, cosf(a));
        float L = rng.range(3.2f, 4.5f);
        V3 mid = cur + dir * (L * 0.5f) + V3(0, 0.6f, 0);
        V3 tip = cur + dir * L + V3(0, -1.6f, 0);
        V3 c = g * rng.range(0.85f, 1.1f);
        m.tri(cur, mid + side * 0.7f, mid, c); m.tri(cur, mid, mid - side * 0.7f, c);
        m.tri(mid, mid + side * 0.7f, tip, c); m.tri(mid, tip, mid - side * 0.7f, c);
        // back faces so fronds are visible from below
        m.tri(cur, mid, mid + side * 0.7f, c * 0.8f); m.tri(cur, mid - side * 0.7f, mid, c * 0.8f);
        m.tri(mid, tip, mid + side * 0.7f, c * 0.8f); m.tri(mid, mid - side * 0.7f, tip, c * 0.8f);
    }
}

// Text placed on a plane. origin = left baseline (align 0) or centre (align 1). size = em height in metres.
static void planeText(std::vector<TextVtx>& out, const Font& f, const char* s, V3 origin, V3 right, V3 up,
                      float size, V3 col, int align = 1, float tracking = 0.0f) {
    float w = f.measure(s) + tracking * (strlen(s) - 1);
    float x0 = align == 1 ? -w * 0.5f : 0.0f;
    f.layout(s, [&](float ax, float ay, float bx, float by, float u0, float v0, float u1, float v1) {
        V3 p00 = origin + right * ((x0 + ax) * size) + up * (ay * size);
        V3 p10 = origin + right * ((x0 + bx) * size) + up * (ay * size);
        V3 p11 = origin + right * ((x0 + bx) * size) + up * (by * size);
        V3 p01 = origin + right * ((x0 + ax) * size) + up * (by * size);
        TextVtx a = {p00.x, p00.y, p00.z, u0, v0, col.x, col.y, col.z, 1};
        TextVtx b = {p10.x, p10.y, p10.z, u1, v0, col.x, col.y, col.z, 1};
        TextVtx c = {p11.x, p11.y, p11.z, u1, v1, col.x, col.y, col.z, 1};
        TextVtx d = {p01.x, p01.y, p01.z, u0, v1, col.x, col.y, col.z, 1};
        out.push_back(a); out.push_back(b); out.push_back(c);
        out.push_back(a); out.push_back(c); out.push_back(d);
    }, tracking);
}

struct Sponsor { const char* name; V3 bg, fg; };
static const Sponsor kSponsors[] = {
    {"KOMET COLA", V3(0.80f, 0.10f, 0.10f), V3(1, 1, 1)},
    {"BIG SKY TIRES", V3(0.98f, 0.80f, 0.10f), V3(0.08f, 0.08f, 0.1f)},
    {"RIDGELINE OIL", V3(0.10f, 0.45f, 0.25f), V3(1, 1, 1)},
    {"DUSK COFFEE", V3(0.32f, 0.20f, 0.14f), V3(1.0f, 0.85f, 0.6f)},
    {"VOLT ENERGY", V3(0.10f, 0.30f, 0.85f), V3(1, 1, 1)},
    {"PRISM BANK", V3(0.95f, 0.95f, 0.95f), V3(0.85f, 0.12f, 0.2f)},
};

// ------------------------------------------------------------- world
void buildWorld(const Track& t, const Font& font, WorldGeo& geo) {
    MeshBuilder& m = geo.world;
    Rng rng(4242);
    const int n = t.n;
    const float hw = t.halfW, wall = t.wallOff;

    // bounds
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (auto& p : t.P) { minX = fminf(minX, p.x); maxX = fmaxf(maxX, p.x); minZ = fminf(minZ, p.z); maxZ = fmaxf(maxZ, p.z); }
    geo.mapMin = {minX, minZ}; geo.mapMax = {maxX, maxZ};
    for (int i = 0; i < n; i += 4) geo.minimap.push_back({t.P[i].x, t.P[i].z});

    // ---------------- terrain (flat shaded grid)
    const float margin = 900.0f, cell = 16.0f;
    float tx0 = minX - margin, tz0 = minZ - margin;
    int gx = (int)((maxX - minX + 2 * margin) / cell), gz = (int)((maxZ - minZ + 2 * margin) / cell);
    std::vector<float> H((gx + 1) * (gz + 1));
    for (int j = 0; j <= gz; j++)
        for (int i = 0; i <= gx; i++) {
            float x = tx0 + i * cell, z = tz0 + j * cell;
            float d = t.distTo(V3(x, 0, z));
            H[j * (gx + 1) + i] = terrainHeight(x, z, d, hw);
        }
    auto HP = [&](int i, int j) { return V3(tx0 + i * cell, H[j * (gx + 1) + i], tz0 + j * cell); };
    for (int j = 0; j < gz; j++)
        for (int i = 0; i < gx; i++) {
            V3 a = HP(i, j), b = HP(i + 1, j), c = HP(i + 1, j + 1), d = HP(i, j + 1);
            if (a.y < 0 && b.y < 0 && c.y < 0 && d.y < 0) {  // flat: single colour quad
                V3 col = grassColor(a.x, a.z, 0, 0);
                m.quad(a, d, c, b, col, 4);
                continue;
            }
            V3 n1 = norm(cross(d - a, c - a));
            V3 cen = (a + b + c) * (1.0f / 3);
            m.tri(a, d, c, grassColor(cen.x, cen.z, cen.y, 1 - n1.y), 4);
            V3 n2 = norm(cross(c - a, b - a));
            cen = (a + c + d) * (1.0f / 3);
            m.tri(a, c, b, grassColor(cen.x + 3, cen.z, cen.y, 1 - n2.y), 4);
        }

    // ---------------- runoff, curbs, barriers
    auto side = [&](int i, float lat, float y) { V3 p = t.P[i] + t.R[i] * lat; p.y = y; return p; };
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float k = t.K[i];
        bool corner = fabsf(k) > 1.0f / 260.0f;
        for (int sd = -1; sd <= 1; sd += 2) {
            // runoff surface between asphalt edge and wall
            bool outside = (k > 0 && sd < 0) || (k < 0 && sd > 0);
            float dirt = corner && outside ? 1.0f : 0.0f;
            V3 grass = grassColor(t.P[i].x, t.P[i].z, 0, 0) * 0.95f;
            V3 col = lerp(grass, V3(0.58f, 0.34f, 0.22f), dirt);
            float inner = hw + (corner ? 1.6f : 0.0f);
            V3 a = side(i, sd * inner, 0.0f), b = side(j, sd * inner, 0.0f);
            V3 c = side(j, sd * wall, 0.0f), d = side(i, sd * wall, 0.0f);
            if (sd > 0) m.quad(a, b, c, d, col, 4); else m.quad(a, d, c, b, col, 4);
            // concrete strip next to asphalt on straights
            if (!corner) {
                V3 e = side(i, sd * (hw + 0.9f), 0.01f), f = side(j, sd * (hw + 0.9f), 0.01f);
                V3 a2 = side(i, sd * hw, 0.01f), b2 = side(j, sd * hw, 0.01f);
                V3 cc(0.42f, 0.43f, 0.44f);
                if (sd > 0) m.quad(a2, b2, f, e, cc, 4); else m.quad(a2, e, f, b2, cc, 4);
            }
            // curbs
            if (corner) {
                bool red = ((i / 2) % 2) == 0;
                V3 cc = red ? V3(0.80f, 0.10f, 0.08f) : V3(0.92f, 0.92f, 0.92f);
                V3 a2 = side(i, sd * hw, 0.03f), b2 = side(j, sd * hw, 0.03f);
                V3 e = side(i, sd * (hw + 1.6f), 0.10f), f = side(j, sd * (hw + 1.6f), 0.10f);
                if (sd > 0) m.quad(a2, b2, f, e, cc, 4); else m.quad(a2, e, f, b2, cc, 4);
            }
        }
    }
    // barriers every 2 samples
    for (int i = 0; i < n; i += 2) {
        int j = (i + 2) % n;
        for (int sd = -1; sd <= 1; sd += 2) {
            float lat = sd * wall;
            V3 a0 = side(i, lat, 0.0f), b0 = side(j, lat, 0.0f);
            V3 a1 = side(i, lat, 0.45f), b1 = side(j, lat, 0.45f);
            V3 a2 = side(i, lat, 1.0f), b2 = side(j, lat, 1.0f);
            V3 back = t.R[i] * (sd * 0.35f);
            V3 white(0.82f, 0.84f, 0.86f), blue(0.10f, 0.36f, 0.86f);
            if (sd > 0) {  // wall on right: inner face looks -R
                m.quad(a0, a1, b1, b0, white, 0); m.quad(a1, a2, b2, b1, blue, 3);
                m.quad(a2, a2 + back, b2 + back, b2, white * 0.9f, 0);
                m.quad(a0 + back, b0 + back, b2 + back, a2 + back, white * 0.75f, 0);
            } else {
                m.quad(a0, b0, b1, a1, white, 0); m.quad(a1, b1, b2, a2, blue, 3);
                m.quad(a2, b2, b2 + back, a2 + back, white * 0.9f, 0);
                m.quad(a0 + back, a2 + back, b2 + back, b0 + back, white * 0.75f, 0);
            }
        }
    }

    // ---------------- chevrons on outside of tight corners
    for (int i = 0; i < n; i += 9) {
        float k = t.K[i];
        if (fabsf(k) < 1.0f / 170.0f) continue;
        float sd = k > 0 ? -1.0f : 1.0f;  // outside
        V3 base = t.P[i] + t.R[i] * (sd * (wall - 0.2f));
        V3 fn = -t.T[i];                  // faces oncoming cars
        V3 rr = t.R[i];                   // viewer's right
        V3 c0 = base + V3(0, 1.0f, 0);
        V3 a = c0 - rr * 0.9f, b = c0 + rr * 0.9f, c = b + V3(0, 1.3f, 0), d = a + V3(0, 1.3f, 0);
        m.quad(a, b, c, d, V3(0.98f, 0.78f, 0.05f), 0, 0.15f);
        m.quad(b + fn * -0.08f, a + fn * -0.08f, d + fn * -0.08f, c + fn * -0.08f, V3(0.2f, 0.2f, 0.2f));
        // arrow pointing toward the turn (viewer right for right turns)
        float dir = k > 0 ? 1.0f : -1.0f;
        V3 o = c0 + V3(0, 0.65f, 0) + fn * 0.02f;
        for (int q = -1; q <= 0; q++) {
            V3 ofs = rr * (dir * q * 0.45f + dir * 0.2f);
            V3 tip = o + ofs + rr * (dir * 0.25f);
            V3 tA = o + ofs - rr * (dir * 0.15f) + V3(0, 0.45f, 0);
            V3 tB = o + ofs - rr * (dir * 0.15f) - V3(0, 0.45f, 0);
            V3 w = rr * (dir * 0.2f);
            V3 blk(0.05f, 0.05f, 0.05f);
            if (dir > 0) { m.quad(tA, tA + w, tip + w, tip, blk, 0, 0.0f); m.quad(tip, tip + w, tB + w, tB, blk); }
            else { m.quad(tA, tip, tip + w, tA + w, blk); m.quad(tip, tB, tB + w, tip + w, blk); }
        }
    }

    // ---------------- billboards
    int sp = 0;
    for (float s = 120; s < t.length - 80; s += 210) {
        int i = (int)(s / t.ds);
        float sd = (sp % 2) ? 1.0f : -1.0f;
        if (fabsf(t.K[i]) > 1.0f / 170.0f) sd = t.K[i] > 0 ? -1.0f : 1.0f;
        const Sponsor& S = kSponsors[sp % 6]; sp++;
        V3 rr = t.R[i], fwd = t.T[i];
        V3 base = t.P[i] + rr * (sd * (wall + 3.0f)) + fwd * 4.0f;
        // angle the board toward oncoming traffic
        V3 face = norm(-fwd * 1.0f - rr * (sd * 0.55f));
        V3 bright(face.z, 0, -face.x);  // right-hand side for a viewer looking at the board
        float W = 13.0f, Hh = 3.2f, y0 = 1.6f;
        V3 a = base - bright * (W / 2) + V3(0, y0, 0), b = base + bright * (W / 2) + V3(0, y0, 0);
        V3 c = b + V3(0, Hh, 0), d = a + V3(0, Hh, 0);
        // front (facing `face`) — winding CCW seen from face side
        V3 nrm = norm(cross(b - a, d - a));
        if (dot(nrm, face) > 0) m.quad(a, b, c, d, S.bg, 0, 0.12f); else m.quad(a, d, c, b, S.bg, 0, 0.12f);
        V3 off = -face * 0.15f;
        if (dot(nrm, face) > 0) m.quad(b + off, a + off, d + off, c + off, V3(0.25f, 0.25f, 0.27f)); else m.quad(a + off, b + off, c + off, d + off, V3(0.25f, 0.25f, 0.27f));
        // frame + posts
        for (int q = -1; q <= 1; q += 2) {
            M4 xf = translate(base + bright * (q * W * 0.33f));
            m.box(V3(-0.18f, 0, -0.18f), V3(0.18f, y0, 0.18f), V3(0.3f, 0.3f, 0.32f), 0, 0, &xf);
        }
        V3 lo = V3(0, y0 - 0.15f, 0);
        m.quad(a + lo + face * 0.02f, b + lo + face * 0.02f, b + face * 0.02f, a + face * 0.02f, V3(0.9f, 0.9f, 0.9f));
        float tsize = 2.1f;
        float tw = font.measure(S.name) * tsize;
        if (tw > W * 0.9f) tsize *= W * 0.9f / tw;
        planeText(geo.text, font, S.name, base + V3(0, y0 + Hh * 0.5f - tsize * 0.36f, 0) + face * 0.07f, bright, V3(0, 1, 0), tsize, S.fg);
    }

    // ---------------- start gantry
    {
        V3 rr = t.R[0], fwd = t.T[0];
        V3 base = t.P[0];
        for (int q = -1; q <= 1; q += 2) {
            V3 pc = base + rr * (q * (wall + 1.0f));
            M4 xf = translate(pc);
            m.box(V3(-0.6f, 0, -0.6f), V3(0.6f, 9.5f, 0.6f), V3(0.2f, 0.2f, 0.23f), 0, 0, &xf);
        }
        V3 a = base - rr * (wall + 1.6f), b = base + rr * (wall + 1.6f);
        V3 up(0, 1, 0);
        float y0 = 7.0f, y1 = 9.6f;
        V3 face = -fwd;  // faces oncoming
        V3 fa = a + up * y0 + face * 0.6f, fb = b + up * y0 + face * 0.6f;
        V3 ba = a + up * y0 - face * 0.6f, bb = b + up * y0 - face * 0.6f;
        V3 col(0.12f, 0.12f, 0.15f);
        // front face
        V3 nrm = norm(cross(fb - fa, up));
        if (dot(nrm, face) > 0) m.quad(fa, fb, fb + up * (y1 - y0), fa + up * (y1 - y0), col); else m.quad(fa, fa + up * (y1 - y0), fb + up * (y1 - y0), fb, col);
        if (dot(nrm, face) > 0) m.quad(bb, ba, ba + up * (y1 - y0), bb + up * (y1 - y0), col); else m.quad(bb, bb + up * (y1 - y0), ba + up * (y1 - y0), ba, col);
        m.quad(fa + up * (y1 - y0), fb + up * (y1 - y0), bb + up * (y1 - y0), ba + up * (y1 - y0), col * 0.8f);
        m.quad(fa, ba, bb, fb, col * 0.6f);
        // red light strip
        V3 strip = face * 0.62f;
        V3 vr = norm(fb - fa);
        V3 la = fa + up * 0.15f + face * 0.02f, lb = fb + up * 0.15f + face * 0.02f;
        if (dot(nrm, face) > 0) m.quad(la, lb, lb + up * 0.25f, la + up * 0.25f, V3(1.0f, 0.2f, 0.15f), 0, 1.0f);
        else m.quad(la, la + up * 0.25f, lb + up * 0.25f, lb, V3(1.0f, 0.2f, 0.15f), 0, 1.0f);
        (void)strip;
        V3 viewerRight(face.z, 0, -face.x);
        (void)vr;
        planeText(geo.text, font, "FULL THROTTLE", base + up * (y0 + 0.55f) + face * 0.65f, viewerRight, up, 1.7f, V3(1, 1, 1), 1, 0.04f);
        V3 backRight = -viewerRight;
        planeText(geo.text, font, "FULL THROTTLE", base + up * (y0 + 0.55f) - face * 0.65f, backRight, up, 1.7f, V3(1.0f, 0.85f, 0.2f), 1, 0.04f);
    }

    // ---------------- grandstand along the main straight (left side)
    {
        float s0 = 30, s1 = 150;
        for (float s = s0; s < s1; s += 4) {
            int i = (int)(s / t.ds), j = (int)((s + 4) / t.ds);
            for (int row = 0; row < 7; row++) {
                float lat = -(wall + 4.0f + row * 1.6f), lat2 = lat - 1.6f;
                float y = 0.6f + row * 0.9f;
                V3 a = side(i, lat, y), b = side(j, lat, y), c = side(j, lat2, y), d = side(i, lat2, y);
                m.quad(a, d, c, b, V3(0.55f, 0.56f, 0.6f));
                V3 a0 = side(i, lat, y - 0.9f), b0 = side(j, lat, y - 0.9f);
                m.quad(a0, a, b, b0, V3(0.45f, 0.46f, 0.5f));
                // crowd
                for (int q = 0; q < 5; q++) {
                    V3 p = side(i, lat - 0.8f, y) + t.T[i] * (q * 0.8f + rng.range(0, 0.3f));
                    V3 cc = lerp(V3(0.35f, 0.36f, 0.4f), V3(rng.range(0.2f, 1), rng.range(0.2f, 1), rng.range(0.2f, 1)), 0.55f);
                    M4 xf = translate(p);
                    m.box(V3(-0.22f, 0, -0.2f), V3(0.22f, 0.75f, 0.2f), cc, 0, 0, &xf);
                }
            }
            // roof
            V3 r0 = side(i, -(wall + 3.0f), 8.6f), r1 = side(j, -(wall + 3.0f), 8.6f);
            V3 r2 = side(j, -(wall + 17.0f), 10.5f), r3 = side(i, -(wall + 17.0f), 10.5f);
            m.quad(r0, r3, r2, r1, V3(0.85f, 0.86f, 0.9f));
            m.quad(r0, r1, r2, r3, V3(0.3f, 0.3f, 0.33f));
            if (((int)s) % 20 == 0) {
                M4 xf = translate(side(i, -(wall + 3.2f), 0));
                m.box(V3(-0.15f, 0, -0.15f), V3(0.15f, 8.6f, 0.15f), V3(0.3f, 0.3f, 0.33f), 0, 0, &xf);
            }
        }
    }

    // ---------------- trees
    int placed = 0, tries = 0;
    while (placed < 1100 && tries < 20000) {
        tries++;
        float x = rng.range(minX - 420, maxX + 420), z = rng.range(minZ - 420, maxZ + 420);
        float d = t.distTo(V3(x, 0, z));
        if (d < wall + 6.0f) continue;
        // keep the grandstand and gantry clear
        V3 rel = V3(x, 0, z) - t.P[0];
        if (fabsf(rel.x - 90) < 80 && rel.z < -(wall) && rel.z > -(wall + 30)) continue;
        float density = d < 160 ? 1.0f : (d < 420 ? 0.35f : 0.12f);
        if (rng.f() > density) continue;
        float h = terrainHeight(x, z, d, hw);
        if (h > 140) continue;
        V3 p(x, h - 0.2f, z);
        float r = rng.f();
        if (r < 0.5f) blobTree(m, p, rng);
        else if (r < 0.8f) umbrellaTree(m, p, rng);
        else palmTree(m, p, rng);
        placed++;
    }
    // clusters of trees right behind the walls for that close parallax
    for (int i = 0; i < n; i += 11) {
        for (int sd = -1; sd <= 1; sd += 2) {
            if (rng.f() < 0.45f) continue;
            V3 p = t.P[i] + t.R[i] * (sd * (wall + rng.range(5.0f, 14.0f))) + t.T[i] * rng.range(-6.f, 6.f);
            if (t.distTo(p) < wall + 4.0f) continue;
            V3 rel = p - t.P[0];
            if (fabsf(rel.x - 90) < 80 && rel.z < 0 && rel.z > -(wall + 30)) continue;
            p.y = terrainHeight(p.x, p.z, t.distTo(p), hw) - 0.2f;
            float r = rng.f();
            if (r < 0.45f) blobTree(m, p, rng); else if (r < 0.75f) umbrellaTree(m, p, rng); else palmTree(m, p, rng);
        }
    }

    // ---------------- road surface (shaded in the road shader)
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float s0 = t.S[i], s1 = (j == 0) ? t.length : t.S[j];
        const int cols = 4;
        for (int c = 0; c < cols; c++) {
            float l0 = -hw + 2 * hw * c / cols, l1 = -hw + 2 * hw * (c + 1) / cols;
            V3 a = side(i, l0, 0), b = side(i, l1, 0), cc = side(j, l1, 0), d = side(j, l0, 0);
            RoadVtx A = {a.x, a.y, a.z, l0, s0, 0}, B = {b.x, b.y, b.z, l1, s0, 0};
            RoadVtx C = {cc.x, cc.y, cc.z, l1, s1, 0}, D = {d.x, d.y, d.z, l0, s1, 0};
            // CCW from above: right is -x when facing +z, so order a(left) -> d -> cc -> b
            geo.road.push_back(A); geo.road.push_back(D); geo.road.push_back(C);
            geo.road.push_back(A); geo.road.push_back(C); geo.road.push_back(B);
        }
    }
}

// ------------------------------------------------------------- cars
static void sectionPts(float w, float top, V3* out, float z) {
    out[0] = V3(w * 0.90f, 0.22f, z);
    out[1] = V3(w, 0.46f, z);
    out[2] = V3(w * 0.97f, 0.74f, z);
    out[3] = V3(w * 0.78f, top, z);
    out[4] = V3(-w * 0.78f, top, z);
    out[5] = V3(-w * 0.97f, 0.74f, z);
    out[6] = V3(-w, 0.46f, z);
    out[7] = V3(-w * 0.90f, 0.22f, z);
}

static void buildCarMesh(MeshBuilder& m) {
    struct Sec { float z, w, top; };
    const Sec secs[] = {{-2.45f, 0.86f, 0.80f}, {-2.30f, 0.99f, 0.94f}, {-1.6f, 1.0f, 0.95f}, {-0.6f, 1.0f, 0.92f},
                        {0.8f, 1.0f, 0.89f}, {1.7f, 0.98f, 0.82f}, {2.25f, 0.90f, 0.66f}, {2.48f, 0.72f, 0.48f}};
    const int ns = sizeof(secs) / sizeof(secs[0]);
    V3 P[8][8];
    for (int i = 0; i < ns; i++) sectionPts(secs[i].w, secs[i].top, P[i], secs[i].z);
    V3 paint(1, 1, 1);
    for (int i = 0; i < ns - 1; i++)
        for (int k = 0; k < 7; k++) {
            V3 a = P[i][k], b = P[i][k + 1], c = P[i + 1][k + 1], d = P[i + 1][k];
            m.quad(a, b, c, d, paint, 1);
        }
    // bottom
    for (int i = 0; i < ns - 1; i++) m.quad(P[i][7], P[i][0], P[i + 1][0], P[i + 1][7], V3(0.05f, 0.05f, 0.05f));
    // caps
    for (int k = 1; k < 7; k++) m.tri(P[0][0], P[0][k + 1], P[0][k], paint, 1);
    m.tri(P[0][0], P[0][7], P[0][0], paint, 1);
    for (int k = 1; k < 7; k++) m.tri(P[ns - 1][0], P[ns - 1][k], P[ns - 1][k + 1], paint, 1);

    // cabin
    struct CSec { float z, bw, beltY, rw, roofY; };
    const CSec cs[] = {{1.05f, 0.90f, 0.88f, 0.84f, 0.92f}, {0.05f, 0.88f, 0.90f, 0.66f, 1.34f},
                       {-1.05f, 0.88f, 0.93f, 0.66f, 1.34f}, {-1.95f, 0.90f, 0.95f, 0.80f, 1.00f}};
    V3 C[4][4];
    for (int i = 0; i < 4; i++) {
        C[i][0] = V3(cs[i].bw, cs[i].beltY, cs[i].z);
        C[i][1] = V3(cs[i].rw, cs[i].roofY, cs[i].z);
        C[i][2] = V3(-cs[i].rw, cs[i].roofY, cs[i].z);
        C[i][3] = V3(-cs[i].bw, cs[i].beltY, cs[i].z);
    }
    V3 glass(0.05f, 0.07f, 0.09f);
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 3; k++) {
            V3 a = C[i][k], b = C[i][k + 1], c = C[i + 1][k + 1], d = C[i + 1][k];
            bool roof = (k == 1 && i == 1);
            // order so normals face outward (cabin runs front->back so flip)
            if (roof) m.quad(a, d, c, b, paint, 1);
            else m.quad(a, d, c, b, glass, 2);
        }
    // pillars: paint a thin strip at the windshield edges
    // front & rear caps of cabin
    m.quad(C[0][0], C[0][1], C[0][2], C[0][3], glass, 2);
    m.quad(C[3][3], C[3][2], C[3][1], C[3][0], glass, 2);

    // spoiler
    m.box(V3(-0.98f, 1.02f, -2.40f), V3(0.98f, 1.08f, -2.12f), V3(0.08f, 0.08f, 0.09f), 3);
    m.box(V3(-0.70f, 0.92f, -2.30f), V3(-0.62f, 1.03f, -2.22f), V3(0.08f, 0.08f, 0.09f));
    m.box(V3(0.62f, 0.92f, -2.30f), V3(0.70f, 1.03f, -2.22f), V3(0.08f, 0.08f, 0.09f));
    // rear fascia
    float rz = -2.462f;
    V3 red(1.0f, 0.10f, 0.06f);
    for (int sd = -1; sd <= 1; sd += 2) {
        float x0 = sd * 0.26f, x1 = sd * 0.74f;
        V3 a(x0, 0.66f, rz), b(x1, 0.66f, rz), c(x1, 0.75f, rz), d(x0, 0.75f, rz);
        if (sd > 0) m.quad(b, a, d, c, red, 0, 1.0f); else m.quad(a, b, c, d, red, 0, 1.0f);
        float xa = x1 - sd * 0.09f;
        m.quad(V3(xa, 0.56f, rz), V3(x1, 0.56f, rz), V3(x1, 0.66f, rz), V3(xa, 0.66f, rz), red, 0, 1.0f);
    }
    m.quad(V3(0.80f, 0.22f, rz), V3(-0.80f, 0.22f, rz), V3(-0.80f, 0.34f, rz), V3(0.80f, 0.34f, rz), V3(0.04f, 0.04f, 0.045f));
    // exhausts
    m.box(V3(-0.48f, 0.26f, -2.52f), V3(-0.36f, 0.34f, -2.40f), V3(0.6f, 0.6f, 0.62f), 3);
    m.box(V3(0.36f, 0.26f, -2.52f), V3(0.48f, 0.34f, -2.40f), V3(0.6f, 0.6f, 0.62f), 3);
    // headlights and grille
    float fz = 2.47f;
    m.quad(V3(-0.62f, 0.48f, fz), V3(-0.25f, 0.48f, fz), V3(-0.25f, 0.58f, fz - 0.05f), V3(-0.62f, 0.58f, fz - 0.05f), V3(0.95f, 0.95f, 0.85f), 0, 0.8f);
    m.quad(V3(0.25f, 0.48f, fz), V3(0.62f, 0.48f, fz), V3(0.62f, 0.58f, fz - 0.05f), V3(0.25f, 0.58f, fz - 0.05f), V3(0.95f, 0.95f, 0.85f), 0, 0.8f);
    m.quad(V3(-0.6f, 0.26f, fz + 0.01f), V3(0.6f, 0.26f, fz + 0.01f), V3(0.6f, 0.40f, fz + 0.01f), V3(-0.6f, 0.40f, fz + 0.01f), V3(0.04f, 0.04f, 0.045f));

    // wheels
    const int sides = 10;
    for (int w = 0; w < 4; w++) {
        float sx = (w & 1) ? 1.0f : -1.0f;
        float wz = (w & 2) ? 1.52f : -1.48f;
        float cx = sx * 0.84f, r = 0.36f, half = 0.16f;
        for (int i = 0; i < sides; i++) {
            float a0 = 2 * PI * i / sides, a1 = 2 * PI * (i + 1) / sides;
            V3 o0(0, r + sinf(a0) * r, wz + cosf(a0) * r), o1(0, r + sinf(a1) * r, wz + cosf(a1) * r);
            V3 in0 = o0 + V3(cx - half, 0, 0), in1 = o1 + V3(cx - half, 0, 0);
            V3 out0 = o0 + V3(cx + half, 0, 0), out1 = o1 + V3(cx + half, 0, 0);
            if (sx > 0) m.quad(in0, in1, out1, out0, V3(0.05f, 0.05f, 0.055f));
            else m.quad(in0, out0, out1, in1, V3(0.05f, 0.05f, 0.055f));
            V3 hub(cx + sx * half, r, wz);
            V3 rim0 = hub + (o0 - V3(0, r, wz)) * 0.62f, rim1 = hub + (o1 - V3(0, r, wz)) * 0.62f;
            V3 tire0 = V3(cx + sx * half, o0.y, o0.z), tire1 = V3(cx + sx * half, o1.y, o1.z);
            if (sx > 0) { m.tri(hub, rim1, rim0, V3(0.62f, 0.62f, 0.66f), 3); m.quad(rim0, rim1, tire1, tire0, V3(0.07f, 0.07f, 0.075f)); }
            else { m.tri(hub, rim0, rim1, V3(0.62f, 0.62f, 0.66f), 3); m.quad(rim0, tire0, tire1, rim1, V3(0.07f, 0.07f, 0.075f)); }
        }
    }
}

void buildCars(const Font& font, const std::vector<Car>& cars, WorldGeo& geo) {
    geo.car.v.clear();
    buildCarMesh(geo.car);
    geo.carText.clear(); geo.carTextStart.clear(); geo.carTextCount.clear();
    for (const Car& c : cars) {
        int start = (int)geo.carText.size();
        char num[8]; snprintf(num, sizeof num, "%d", c.liv.number);
        V3 fg = c.liv.c3;
        // rear sponsor (seen from the chase camera)
        float sz = 0.30f;
        float tw = font.measure(c.liv.sponsor.c_str()) * sz;
        if (tw > 1.36f) sz *= 1.36f / tw;
        planeText(geo.carText, font, c.liv.sponsor.c_str(), V3(0, 0.42f, -2.475f), V3(-1, 0, 0), V3(0, 1, 0), sz, fg);
        // roof number
        planeText(geo.carText, font, num, V3(0, 1.345f, -0.75f), V3(-1, 0, 0), V3(0, 0, 1), 0.85f, fg);
        // door numbers
        planeText(geo.carText, font, num, V3(1.005f, 0.46f, -0.2f), V3(0, 0, -1), V3(0, 1, 0), 0.42f, fg);
        planeText(geo.carText, font, num, V3(-1.005f, 0.46f, -0.2f), V3(0, 0, 1), V3(0, 1, 0), 0.42f, fg);
        geo.carTextStart.push_back(start);
        geo.carTextCount.push_back((int)geo.carText.size() - start);
    }
}
