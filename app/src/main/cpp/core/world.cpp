// Scenery generation: terrain, track furniture, buildings, nature, signage.
#include "render.h"
#include <string.h>
#include <algorithm>

// ------------------------------------------------------------- mesh helpers
void MeshBuilder::tri(V3 a, V3 b, V3 c, V3 col, float aux, float emis) {
    V3 n = norm(cross(b - a, c - a));
    triN(a, b, c, n, n, n, col, aux, emis);
}
void MeshBuilder::triN(V3 a, V3 b, V3 c, V3 na, V3 nb, V3 nc, V3 col, float aux, float emis) {
    Vtx t[3] = {{a.x, a.y, a.z, na.x, na.y, na.z, col.x, col.y, col.z, emis, aux},
                {b.x, b.y, b.z, nb.x, nb.y, nb.z, col.x, col.y, col.z, emis, aux},
                {c.x, c.y, c.z, nc.x, nc.y, nc.z, col.x, col.y, col.z, emis, aux}};
    v.insert(v.end(), t, t + 3);
}
void MeshBuilder::box(V3 mn, V3 mx, V3 col, float aux, float emis, const M4* xf) {
    V3 p[8];
    for (int i = 0; i < 8; i++) {
        p[i] = V3(i & 1 ? mx.x : mn.x, i & 2 ? mx.y : mn.y, i & 4 ? mx.z : mn.z);
        if (xf) p[i] = xformPoint(*xf, p[i]);
    }
    quad(p[0], p[4], p[6], p[2], col, aux, emis);
    quad(p[1], p[3], p[7], p[5], col, aux, emis);
    quad(p[0], p[1], p[5], p[4], col, aux, emis);
    quad(p[2], p[6], p[7], p[3], col, aux, emis);
    quad(p[0], p[2], p[3], p[1], col, aux, emis);
    quad(p[4], p[5], p[7], p[6], col, aux, emis);
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
        if (cp >= 0 && cp < 128) g[cp] = {f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], true};
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

void planeText(std::vector<TextVtx>& out, const Font& f, const char* s, V3 origin, V3 right, V3 up,
               float size, V3 col, int align, float tracking) {
    size_t n = strlen(s);
    float w = f.measure(s) + tracking * (n > 0 ? n - 1 : 0);
    float x0 = align == 1 ? -w * 0.5f : (align == 2 ? -w : 0.0f);
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

// ------------------------------------------------------------- shapes
static void icosa(MeshBuilder& m, V3 c, V3 s, V3 col, Rng& rng, float jitter, float aux = M_LEAF) {
    const float t = 1.618034f;
    V3 v[12] = {V3(-1, t, 0), V3(1, t, 0), V3(-1, -t, 0), V3(1, -t, 0), V3(0, -1, t), V3(0, 1, t),
                V3(0, -1, -t), V3(0, 1, -t), V3(t, 0, -1), V3(t, 0, 1), V3(-t, 0, -1), V3(-t, 0, 1)};
    static const int f[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
                                 {11, 10, 2}, {10, 7, 6}, {7, 1, 8}, {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8},
                                 {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    for (auto& p : v) { p = norm(p) * (1.0f + rng.range(-jitter, jitter)); p = c + mul(p, s); }
    for (auto& tr : f) m.tri(v[tr[0]], v[tr[1]], v[tr[2]], col * rng.range(0.92f, 1.06f), aux);
}
static void prism(MeshBuilder& m, V3 base, float r0, float r1, float h, int sides, V3 col, float aux = M_MATTE, V3 lean = V3(), float rot = 0) {
    for (int i = 0; i < sides; i++) {
        float a0 = rot + 2 * PI * i / sides, a1 = rot + 2 * PI * (i + 1) / sides;
        V3 b0 = base + V3(cosf(a0) * r0, 0, sinf(a0) * r0), b1 = base + V3(cosf(a1) * r0, 0, sinf(a1) * r0);
        V3 top = base + V3(0, h, 0) + lean;
        V3 t0 = top + V3(cosf(a0) * r1, 0, sinf(a0) * r1), t1 = top + V3(cosf(a1) * r1, 0, sinf(a1) * r1);
        if (r1 > 0.001f) m.quad(b0, t0, t1, b1, col, aux); else m.tri(b0, top, b1, col, aux);
    }
}
static void disc(MeshBuilder& m, V3 c, float r, int sides, V3 col, float aux, bool up = true) {
    for (int i = 0; i < sides; i++) {
        float a0 = 2 * PI * i / sides, a1 = 2 * PI * (i + 1) / sides;
        V3 p0 = c + V3(cosf(a0) * r, 0, sinf(a0) * r), p1 = c + V3(cosf(a1) * r, 0, sinf(a1) * r);
        if (up) m.tri(c, p1, p0, col, aux); else m.tri(c, p0, p1, col, aux);
    }
}
// oriented box: centre, half extents along (right, up, fwd) axes
static void obox(MeshBuilder& m, V3 c, V3 r, V3 u, V3 f, float hx, float hy, float hz, V3 col, float aux = 0, float emis = 0) {
    M4 xf = M4::ident();
    xf.m[0] = r.x; xf.m[1] = r.y; xf.m[2] = r.z;
    xf.m[4] = u.x; xf.m[5] = u.y; xf.m[6] = u.z;
    xf.m[8] = f.x; xf.m[9] = f.y; xf.m[10] = f.z;
    xf.m[12] = c.x; xf.m[13] = c.y; xf.m[14] = c.z;
    m.box(V3(-hx, -hy, -hz), V3(hx, hy, hz), col, aux, emis, &xf);
}

static void pineTree(MeshBuilder& m, V3 p, Rng& rng, float sc) {
    prism(m, p, 0.35f * sc, 0.28f * sc, 2.0f * sc, 5, V3(0.36f, 0.24f, 0.15f));
    V3 g = lerp(V3(0.08f, 0.36f, 0.13f), V3(0.14f, 0.48f, 0.17f), rng.f());
    int tiers = 2 + (rng.next() % 2);
    float y = 1.4f * sc, r = 3.4f * sc, rot = rng.f() * 6.28f;
    for (int i = 0; i < tiers; i++) {
        float h = 5.0f * sc * (1.0f - i * 0.12f);
        prism(m, p + V3(0, y, 0), r, 0, h, 7, g * (1.0f + i * 0.05f), M_LEAF, V3(), rot);
        disc(m, p + V3(0, y, 0), r, 7, g * 0.6f, M_LEAF, false);
        y += h * 0.5f; r *= 0.74f;
    }
}
static void blobTree(MeshBuilder& m, V3 p, Rng& rng, float sc) {
    float th = rng.range(2.0f, 3.2f) * sc;
    prism(m, p, 0.32f * sc, 0.22f * sc, th + 1.0f, 5, V3(0.38f, 0.26f, 0.16f));
    V3 g = lerp(V3(0.16f, 0.50f, 0.12f), V3(0.30f, 0.66f, 0.16f), rng.f());
    int lumps = 1 + (rng.next() % 3);
    for (int i = 0; i < lumps; i++) {
        V3 c = p + V3(rng.range(-1.0f, 1.0f) * sc, th + rng.range(1.6f, 2.6f) * sc, rng.range(-1.0f, 1.0f) * sc);
        float r = rng.range(2.0f, 3.0f) * sc;
        icosa(m, c, V3(r, r * 0.85f, r), g, rng, 0.12f);
    }
}
static void palmTree(MeshBuilder& m, V3 p, Rng& rng) {
    float h = rng.range(7.0f, 10.0f);
    V3 bend = norm(V3(rng.range(-1, 1), 0, rng.range(-1, 1))) * rng.range(0.8f, 2.0f);
    V3 cur = p; float r = 0.30f;
    for (int i = 0; i < 5; i++) {
        float t = (i + 1) / 5.0f;
        V3 next = p + V3(0, h * t, 0) + bend * (t * t);
        V3 d = next - cur;
        prism(m, cur, r, r * 0.9f, d.y, 5, V3(0.50f, 0.40f, 0.28f), M_MATTE, V3(d.x, 0, d.z));
        cur = next; r *= 0.92f;
    }
    V3 g = lerp(V3(0.20f, 0.52f, 0.14f), V3(0.32f, 0.62f, 0.18f), rng.f());
    for (int i = 0; i < 7; i++) {
        float a = 2 * PI * i / 7 + rng.range(-0.2f, 0.2f);
        V3 dir(cosf(a), 0, sinf(a)), side(-sinf(a), 0, cosf(a));
        float L = rng.range(3.4f, 4.6f);
        V3 mid = cur + dir * (L * 0.5f) + V3(0, 0.6f, 0), tip = cur + dir * L + V3(0, -1.6f, 0);
        m.tri(cur, mid + side * 0.7f, mid, g, M_LEAF); m.tri(cur, mid, mid - side * 0.7f, g, M_LEAF);
        m.tri(mid, mid + side * 0.7f, tip, g, M_LEAF); m.tri(mid, tip, mid - side * 0.7f, g, M_LEAF);
    }
}

// ------------------------------------------------------------- terrain
struct TerrainCtx {
    const Track* t;
    float tunnelMidS = 0;
};
static int coarseNearest(const Track& t, V3 p, float* dOut) {
    float best = 1e18f; int bi = 0;
    for (int i = 0; i < t.n; i += 3) { V3 d = p - t.P[i]; float dd = d.x * d.x + d.z * d.z; if (dd < best) { best = dd; bi = i; } }
    for (int j = -3; j <= 3; j++) { int i = (bi + j + t.n) % t.n; V3 d = p - t.P[i]; float dd = d.x * d.x + d.z * d.z; if (dd < best) { best = dd; bi = i; } }
    *dOut = sqrtf(best);
    return bi;
}
static float cliffHeight(float s) { return 19.0f + 9.0f * fbm(s * 0.01f, 3.3f, 3); }

static float heightAt(const Track& t, float x, float z, float* sandOut = nullptr) {
    float d; int i = coarseNearest(t, V3(x, 0, z), &d);
    float s = t.S[i];
    V3 rel = V3(x, 0, z) - t.P[i];
    float lat = dot(rel, t.R[i]);
    float sand = 0;
    float flat = t.wallOff + 16.0f;
    float h;
    if (d < flat) h = -0.3f;
    else {
        float tt = smooth(flat, flat + 90.0f, d);
        float n = fbm(x * 0.0040f + 11.3f, z * 0.0040f + 7.1f, 4);
        h = powf(n, 1.4f) * 70.0f * tt;
        h += tt * (fbm(x * 0.025f, z * 0.025f, 2) - 0.5f) * 5.0f;
        float far = smooth(380.0f, 800.0f, d);
        h += far * powf(fbm(x * 0.0018f + 40, z * 0.0018f - 13, 4), 1.5f) * 260.0f;
        // embankment that hugs the circuit
        h += smooth(flat, flat + 30, d) * (1.0f - smooth(flat + 35, flat + 80, d)) * 3.0f * vnoise(x * 0.02f, z * 0.02f);
        h -= 0.3f * (1.0f - smooth(flat, flat + 30, d));
    }
    // cliff side: the hill sits at the cliff top
    if (t.inZone(s, Z_CLIFF_R) && lat > 0 && d > t.wallOff + 10) h = fmaxf(h, cliffHeight(s) + (d - t.wallOff) * 0.25f);
    // tunnel: hill above
    if (t.inTunnel(s) || t.inTunnel(s - 40) || t.inTunnel(s + 40)) {
        float inT = (t.inTunnel(s) ? 1.0f : 0.6f);
        if (d > 27.0f) h = fmaxf(h, inT * fminf(9.0f + (d - 27.0f) * 1.3f, 42.0f) * (1.0f - smooth(120.0f, 260.0f, d)));
    }
    // lake basin
    float dl = len(V3(x, 0, z) - V3(t.lakeC.x, 0, t.lakeC.z));
    if (dl < t.lakeR + 14) { h = fminf(h, lerpf(-2.5f, -0.06f, smooth(t.lakeR - 4, t.lakeR + 12, dl))); sand = 1 - smooth(t.lakeR, t.lakeR + 8, dl); }
    // sea to the north
    if (z > 1215) {
        float k = smooth(1215.0f, 1265.0f, z);
        h = lerpf(h, -5.0f, k);
        sand = fmaxf(sand, 1 - smooth(1250.0f, 1290.0f, z)) * smooth(1205.0f, 1225.0f, z);
    }
    if (sandOut) *sandOut = sand;
    return h;
}

static V3 groundColor(float x, float z, float h, float slope, float sand) {
    float n = fbm(x * 0.012f, z * 0.012f, 3);
    V3 c = lerp(V3(0.28f, 0.56f, 0.17f), V3(0.40f, 0.68f, 0.21f), n);
    c = lerp(c, V3(0.24f, 0.50f, 0.14f), sat(slope * 1.5f - 0.35f));
    c = lerp(c, V3(0.52f, 0.54f, 0.52f), smooth(150.0f, 260.0f, h + n * 40.0f));
    c = lerp(c, V3(0.86f, 0.80f, 0.62f), sand);
    return c;
}

// ------------------------------------------------------------- sponsors
struct Sponsor { const char* name; V3 bg, fg, accent; };
static const Sponsor kSponsors[] = {
    {"KOMET COLA", V3(0.85f, 0.10f, 0.10f), V3(1, 1, 1), V3(1.0f, 0.85f, 0.1f)},
    {"BIG SKY TIRES", V3(0.98f, 0.80f, 0.08f), V3(0.08f, 0.08f, 0.1f), V3(0.1f, 0.1f, 0.12f)},
    {"RIDGELINE OIL", V3(0.06f, 0.40f, 0.22f), V3(1, 1, 1), V3(1.0f, 0.8f, 0.1f)},
    {"DUSK COFFEE", V3(0.30f, 0.18f, 0.12f), V3(1.0f, 0.86f, 0.6f), V3(0.9f, 0.4f, 0.1f)},
    {"VOLT ENERGY", V3(0.08f, 0.26f, 0.80f), V3(1, 1, 1), V3(0.3f, 0.9f, 1.0f)},
    {"PRISM BANK", V3(0.95f, 0.95f, 0.95f), V3(0.85f, 0.10f, 0.20f), V3(0.1f, 0.2f, 0.6f)},
    {"TURBO PIZZA", V3(0.95f, 0.45f, 0.05f), V3(1, 1, 1), V3(0.8f, 0.05f, 0.05f)},
    {"CORAL FM 98", V3(0.10f, 0.20f, 0.45f), V3(1, 1, 1), V3(1.0f, 0.45f, 0.4f)},
    {"HAWK PARTS", V3(0.12f, 0.12f, 0.14f), V3(1.0f, 0.80f, 0.10f), V3(1.0f, 0.8f, 0.1f)},
    {"SUNNY JUICE", V3(1.0f, 0.88f, 0.20f), V3(0.85f, 0.25f, 0.05f), V3(0.2f, 0.6f, 0.1f)},
};
static const int kNumSponsors = sizeof(kSponsors) / sizeof(kSponsors[0]);

// billboard on two legs; base at ground point, faces `face` (unit, horizontal)
static void billboard(MeshBuilder& m, std::vector<TextVtx>& text, const Font& font, V3 base, V3 face, float W, float H, float y0, const Sponsor& S) {
    V3 rr(face.z, 0, -face.x);  // viewer's right
    V3 up(0, 1, 0);
    V3 c = base + up * (y0 + H * 0.5f);
    obox(m, c - face * 0.12f, rr, up, face, W * 0.5f + 0.15f, H * 0.5f + 0.15f, 0.1f, V3(0.52f, 0.54f, 0.58f), M_METAL);
    // front panel
    V3 a = base + up * y0 - rr * (W * 0.5f), b = base + up * y0 + rr * (W * 0.5f);
    V3 f = face * 0.01f;
    m.quad(a + f, b + f, b + up * H + f, a + up * H + f, S.bg, M_MATTE, 0.12f);
    // accent stripe
    m.quad(a + f * 2, b + f * 2, b + up * (H * 0.16f) + f * 2, a + up * (H * 0.16f) + f * 2, S.accent, M_MATTE, 0.1f);
    for (int q = -1; q <= 1; q += 2)
        obox(m, base + rr * (q * W * 0.32f) + up * (y0 * 0.5f) - face * 0.12f, rr, up, face, 0.18f, y0 * 0.5f, 0.18f, V3(0.30f, 0.31f, 0.34f), M_METAL);
    float ts = H * 0.55f;
    float tw = font.measure(S.name) * ts;
    if (tw > W * 0.88f) ts *= W * 0.88f / tw;
    planeText(text, font, S.name, c - up * (ts * 0.30f) + up * (H * 0.06f) + face * 0.06f, rr, up, ts, S.fg, 1, 0.0f);
}

static void hazardBanner(MeshBuilder& m, V3 a, V3 b, float y0, float h, V3 face) {
    // yellow / blue diagonal stripes between a and b (ground points), from y0 to y0+h
    V3 dir = b - a; float L = len(dir); dir = dir * (1.0f / L);
    float w = 1.2f;
    int n = (int)(L / w);
    for (int i = 0; i < n; i++) {
        float x0 = i * w, x1 = x0 + w;
        V3 col = (i % 2) ? V3(0.10f, 0.25f, 0.75f) : V3(1.0f, 0.82f, 0.08f);
        V3 p0 = a + dir * x0 + V3(0, y0, 0), p1 = a + dir * x1 + V3(0, y0, 0);
        V3 q0 = a + dir * fminf(L, x0 + w * 0.8f) + V3(0, y0 + h, 0), q1 = a + dir * fminf(L, x1 + w * 0.8f) + V3(0, y0 + h, 0);
        V3 nrm = cross(p1 - p0, q0 - p0);
        if (dot(nrm, face) > 0) m.quad(p0 + face * 0.02f, p1 + face * 0.02f, q1 + face * 0.02f, q0 + face * 0.02f, col, M_MATTE, 0.1f);
        else m.quad(p0 + face * 0.02f, q0 + face * 0.02f, q1 + face * 0.02f, p1 + face * 0.02f, col, M_MATTE, 0.1f);
    }
}

// gantry spanning the track at distance s
static void gantry(MeshBuilder& m, std::vector<TextVtx>& text, const Font& font, const Track& t, float s, int kind) {
    V3 c = t.at(s, 0), rr = t.rightAt(s), fw = t.tangentAt(s), up(0, 1, 0);
    float span = t.wallOff + 1.5f;
    V3 face = -fw;           // faces oncoming cars
    V3 viewR(face.z, 0, -face.x);
    V3 dark(0.16f, 0.17f, 0.20f);
    for (int q = -1; q <= 1; q += 2) {
        V3 pc = c + rr * (q * span);
        obox(m, pc + up * 5.0f, rr, up, fw, 0.5f, 5.0f, 0.5f, kind == 0 ? V3(0.85f, 0.86f, 0.9f) : dark, M_METAL);
        obox(m, pc + up * 0.4f, rr, up, fw, 0.9f, 0.4f, 0.9f, V3(0.6f, 0.6f, 0.62f), M_CONCRETE);
    }
    float y0 = 7.2f, y1 = 9.8f;
    obox(m, c + up * ((y0 + y1) * 0.5f), rr, up, fw, span + 0.6f, (y1 - y0) * 0.5f, 0.55f, dark, M_METAL);
    if (kind == 0) {
        // checkered band on both faces
        for (int side = -1; side <= 1; side += 2) {
            V3 f = face * (side * 0.57f);
            V3 nf = face * (float)side;
            for (int i = 0; i < 40; i++)
                for (int j = 0; j < 2; j++) {
                    float x0 = -span + i * (2 * span / 40), x1 = x0 + 2 * span / 40;
                    float ya = y1 - 0.9f + j * 0.45f, yb = ya + 0.45f;
                    V3 col = ((i + j) % 2) ? V3(0.05f, 0.05f, 0.06f) : V3(0.95f, 0.95f, 0.95f);
                    V3 p0 = c + rr * x0 + up * ya + f, p1 = c + rr * x1 + up * ya + f, p2 = c + rr * x1 + up * yb + f, p3 = c + rr * x0 + up * yb + f;
                    if (dot(cross(p1 - p0, p3 - p0), nf) > 0) m.quad(p0, p1, p2, p3, col, M_MATTE, 0.1f); else m.quad(p0, p3, p2, p1, col, M_MATTE, 0.1f);
                }
            V3 vr = side < 0 ? -viewR : viewR;
            planeText(text, font, "FULL THROTTLE", c + up * (y0 + 0.35f) + face * (side * 0.6f), vr, up, 1.35f, V3(1, 1, 1), 1, 0.04f);
        }
        // start lights
        for (int i = 0; i < 5; i++) {
            V3 lp = c + rr * ((i - 2) * 1.2f) + up * (y1 + 0.7f);
            obox(m, lp, rr, up, fw, 0.5f, 0.6f, 0.3f, V3(0.05f, 0.05f, 0.06f));
            obox(m, lp + face * 0.31f, rr, up, fw, 0.3f, 0.3f, 0.02f, V3(0.2f, 1.0f, 0.3f), M_MATTE, 1.0f);
        }
    } else {
        for (int side = -1; side <= 1; side += 2) {
            V3 nf = face * (float)side;
            V3 a = c - rr * span + face * (side * 0.57f), b = c + rr * span + face * (side * 0.57f);
            hazardBanner(m, a, b, y0 + 0.05f, y1 - y0 - 0.1f, nf);
            // black name panel
            V3 pc = c + up * ((y0 + y1) * 0.5f) + face * (side * 0.6f);
            V3 vr = side < 0 ? -viewR : viewR;
            V3 p0 = pc - vr * 6.0f - up * 1.0f, p1 = pc + vr * 6.0f - up * 1.0f;
            m.quad(p0, p1, p1 + up * 2.0f, p0 + up * 2.0f, V3(0.04f, 0.04f, 0.05f), M_MATTE);
            if (dot(cross(p1 - p0, up), nf) < 0) m.quad(p0, p0 + up * 2.0f, p1 + up * 2.0f, p1, V3(0.04f, 0.04f, 0.05f));
            planeText(text, font, "CHECKPOINT", pc - up * 0.55f + face * (side * 0.05f), vr, up, 1.55f, V3(1, 1, 1), 1, 0.06f);
        }
    }
}

// inflatable arch over the track
static void inflatableArch(MeshBuilder& m, std::vector<TextVtx>& text, const Font& font, const Track& t, float s) {
    V3 c = t.at(s, 0), rr = t.rightAt(s), fw = t.tangentAt(s), up(0, 1, 0);
    float R = t.wallOff + 1.0f, tube = 1.25f;
    const int seg = 28, ring = 10;
    for (int i = 0; i < seg; i++) {
        float a0 = PI * i / seg, a1 = PI * (i + 1) / seg;
        V3 c0 = c + rr * (cosf(a0) * R) + up * (sinf(a0) * R * 0.72f);
        V3 c1 = c + rr * (cosf(a1) * R) + up * (sinf(a1) * R * 0.72f);
        V3 d0 = norm(rr * cosf(a0) + up * (sinf(a0) * 0.72f)), d1 = norm(rr * cosf(a1) + up * (sinf(a1) * 0.72f));
        V3 col = (i % 7 == 3) ? V3(1.0f, 0.82f, 0.1f) : V3(0.08f, 0.30f, 0.85f);
        for (int j = 0; j < ring; j++) {
            float b0 = 2 * PI * j / ring, b1 = 2 * PI * (j + 1) / ring;
            V3 n00 = d0 * cosf(b0) + fw * sinf(b0), n01 = d0 * cosf(b1) + fw * sinf(b1);
            V3 n10 = d1 * cosf(b0) + fw * sinf(b0), n11 = d1 * cosf(b1) + fw * sinf(b1);
            m.triN(c0 + n00 * tube, c1 + n10 * tube, c1 + n11 * tube, n00, n10, n11, col, M_GLOSS);
            m.triN(c0 + n00 * tube, c1 + n11 * tube, c0 + n01 * tube, n00, n11, n01, col, M_GLOSS);
        }
    }
    V3 face = -fw, vr(face.z, 0, -face.x);
    V3 top = c + up * (R * 0.72f);
    V3 pa = top - vr * 7.5f - up * 1.0f + face * 1.3f, pb = top + vr * 7.5f - up * 1.0f + face * 1.3f;
    m.quad(pa, pb, pb + up * 2.2f, pa + up * 2.2f, V3(1, 1, 1), M_MATTE, 0.1f);
    planeText(text, font, "KOMET COLA", top - up * 0.55f + face * 1.35f, vr, up, 1.7f, V3(0.85f, 0.1f, 0.1f), 1);
}

static void lighthouse(MeshBuilder& m, V3 p) {
    Rng rng(5);
    for (int i = 0; i < 6; i++) icosa(m, p + V3(rng.range(-6, 6), -1.0f, rng.range(-6, 6)), V3(4, 2.2f, 4), V3(0.52f, 0.48f, 0.44f), rng, 0.25f, M_ROCK);
    float H = 26, r0 = 2.8f, r1 = 1.9f;
    int bands = 7;
    for (int b = 0; b < bands; b++) {
        float y0 = H * b / bands, y1 = H * (b + 1) / bands;
        float ra = lerpf(r0, r1, (float)b / bands), rb = lerpf(r0, r1, (float)(b + 1) / bands);
        V3 col = (b % 2) ? V3(0.95f, 0.95f, 0.95f) : V3(0.85f, 0.12f, 0.10f);
        prism(m, p + V3(0, y0, 0), ra, rb, y1 - y0, 16, col, M_GLOSS);
    }
    disc(m, p + V3(0, H, 0), 2.7f, 16, V3(0.15f, 0.15f, 0.17f), M_METAL);
    prism(m, p + V3(0, H, 0), 2.7f, 2.7f, 0.25f, 16, V3(0.15f, 0.15f, 0.17f), M_METAL);
    prism(m, p + V3(0, H + 0.25f, 0), 1.5f, 1.5f, 2.4f, 10, V3(1.0f, 0.95f, 0.7f), M_GLASS);
    prism(m, p + V3(0, H + 2.65f, 0), 1.9f, 0.0f, 2.2f, 10, V3(0.85f, 0.12f, 0.10f), M_GLOSS);
}

static void house(MeshBuilder& m, V3 p, V3 face, Rng& rng, bool tall) {
    V3 rr(face.z, 0, -face.x), up(0, 1, 0);
    float W = rng.range(9, 14), D = rng.range(8, 11), H = tall ? rng.range(12, 18) : rng.range(4.5f, 6.5f);
    const V3 walls[] = {V3(0.93f, 0.88f, 0.76f), V3(0.98f, 0.96f, 0.92f), V3(0.96f, 0.80f, 0.68f), V3(0.80f, 0.88f, 0.92f), V3(0.98f, 0.90f, 0.60f)};
    V3 wc = walls[rng.next() % 5];
    obox(m, p + up * (H * 0.5f), rr, up, face, W * 0.5f, H * 0.5f, D * 0.5f, wc, M_CONCRETE);
    // roof
    V3 roofc = tall ? V3(0.55f, 0.56f, 0.6f) : (rng.f() < 0.5f ? V3(0.72f, 0.33f, 0.22f) : V3(0.35f, 0.38f, 0.45f));
    if (tall) obox(m, p + up * (H + 0.3f), rr, up, face, W * 0.5f + 0.3f, 0.3f, D * 0.5f + 0.3f, roofc, M_CONCRETE);
    else {
        V3 a = p + up * H - rr * (W * 0.5f + 0.5f) + face * (D * 0.5f + 0.6f), b = p + up * H + rr * (W * 0.5f + 0.5f) + face * (D * 0.5f + 0.6f);
        V3 c = b - face * (D + 1.2f), d = a - face * (D + 1.2f);
        V3 ridgeA = p + up * (H + 2.8f) - rr * (W * 0.5f + 0.5f), ridgeB = p + up * (H + 2.8f) + rr * (W * 0.5f + 0.5f);
        m.quad(a, b, ridgeB, ridgeA, roofc); m.quad(d, ridgeA, ridgeB, c, roofc);
        m.tri(a, ridgeA, d, wc * 0.9f); m.tri(b, c, ridgeB, wc * 0.9f);
    }
    // windows
    int floors = tall ? (int)(H / 3.2f) : 1;
    int cols = (int)(W / 2.6f);
    for (int f = 0; f < floors; f++)
        for (int c = 0; c < cols; c++) {
            float x = -W * 0.5f + (c + 0.5f) * (W / cols);
            float y = tall ? 1.4f + f * 3.2f : H * 0.35f;
            V3 wp = p + rr * x + up * y + face * (D * 0.5f + 0.02f);
            V3 a = wp - rr * 0.6f, b = wp + rr * 0.6f;
            m.quad(a - rr * 0.12f, b + rr * 0.12f, b + rr * 0.12f + up * 1.7f, a - rr * 0.12f + up * 1.7f, V3(1, 1, 1));
            m.quad(a + face * 0.01f + up * 0.1f, b + face * 0.01f + up * 0.1f, b + face * 0.01f + up * 1.6f, a + face * 0.01f + up * 1.6f, V3(0.25f, 0.45f, 0.62f), M_GLASS);
            if (tall) obox(m, wp + up * -0.1f + face * 0.6f, rr, up, face, 1.0f, 0.06f, 0.6f, V3(0.9f, 0.9f, 0.9f), M_CONCRETE);
        }
}

// ------------------------------------------------------------- world
void buildWorld(const Track& t, const Font& font, WorldGeo& geo) {
    MeshBuilder& m = geo.world;
    Rng rng(4242);
    const int n = t.n;
    const float hw = t.halfW;
    V3 up(0, 1, 0);

    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (auto& p : t.P) { minX = fminf(minX, p.x); maxX = fmaxf(maxX, p.x); minZ = fminf(minZ, p.z); maxZ = fmaxf(maxZ, p.z); }
    geo.mapMin = {minX, minZ}; geo.mapMax = {maxX, maxZ};
    for (int i = 0; i < n; i += 4) geo.minimap.push_back({t.P[i].x, t.P[i].z});

    // ---------------- terrain (smooth shaded)
    {
        const float margin = 1300.0f, cell = 22.0f;
        float tx0 = minX - margin, tz0 = minZ - margin;
        int gx = (int)((maxX - minX + 2 * margin) / cell), gz = (int)((maxZ - minZ + 2 * margin) / cell);
        int W = gx + 1;
        std::vector<float> H((gx + 1) * (gz + 1)), SA((gx + 1) * (gz + 1));
        for (int j = 0; j <= gz; j++)
            for (int i = 0; i <= gx; i++) {
                float x = tx0 + i * cell, z = tz0 + j * cell;
                float sand;
                // finer sampling is only needed near the circuit; far away use the cheap version
                H[j * W + i] = heightAt(t, x, z, &sand);
                SA[j * W + i] = sand;
            }
        auto P = [&](int i, int j) { return V3(tx0 + i * cell, H[j * W + i], tz0 + j * cell); };
        auto N = [&](int i, int j) {
            int i0 = std::max(0, i - 1), i1 = std::min(gx, i + 1), j0 = std::max(0, j - 1), j1 = std::min(gz, j + 1);
            float dx = (H[j * W + i1] - H[j * W + i0]) / ((i1 - i0) * cell);
            float dz = (H[j1 * W + i] - H[j0 * W + i]) / ((j1 - j0) * cell);
            return norm(V3(-dx, 1, -dz));
        };
        auto C = [&](int i, int j) {
            V3 p = P(i, j), nn = N(i, j);
            return groundColor(p.x, p.z, p.y, 1 - nn.y, SA[j * W + i]);
        };
        for (int j = 0; j < gz; j++)
            for (int i = 0; i < gx; i++) {
                V3 a = P(i, j), b = P(i + 1, j), c = P(i + 1, j + 1), d = P(i, j + 1);
                V3 na = N(i, j), nb = N(i + 1, j), nc = N(i + 1, j + 1), nd = N(i, j + 1);
                V3 ca = C(i, j), cb = C(i + 1, j), cc = C(i + 1, j + 1), cd = C(i, j + 1);
                // per-vertex colours: emit with individual colours
                Vtx tv[6] = {
                    {a.x, a.y, a.z, na.x, na.y, na.z, ca.x, ca.y, ca.z, 0, M_GRASS}, {d.x, d.y, d.z, nd.x, nd.y, nd.z, cd.x, cd.y, cd.z, 0, M_GRASS},
                    {c.x, c.y, c.z, nc.x, nc.y, nc.z, cc.x, cc.y, cc.z, 0, M_GRASS}, {a.x, a.y, a.z, na.x, na.y, na.z, ca.x, ca.y, ca.z, 0, M_GRASS},
                    {c.x, c.y, c.z, nc.x, nc.y, nc.z, cc.x, cc.y, cc.z, 0, M_GRASS}, {b.x, b.y, b.z, nb.x, nb.y, nb.z, cb.x, cb.y, cb.z, 0, M_GRASS}};
                m.v.insert(m.v.end(), tv, tv + 6);
            }
    }

    // ---------------- distant mountains (grey rocky peaks)
    {
        V3 ctr((minX + maxX) * 0.5f, 0, (minZ + maxZ) * 0.5f);
        for (int i = 0; i < 46; i++) {
            float a = 2 * PI * i / 46 + rng.range(-0.05f, 0.05f);
            if (sinf(a) > 0.55f) continue;  // keep the sea view open to the north
            float r = rng.range(2100, 2700);
            V3 base = ctr + V3(cosf(a) * r, -5, sinf(a) * r);
            float H = rng.range(160, 330), R = rng.range(200, 320);
            int sides = 7;
            V3 peak = base + V3(rng.range(-40, 40), H, rng.range(-40, 40));
            V3 rim[7], mid[7];
            for (int k = 0; k < sides; k++) {
                float b = 2 * PI * k / sides + rng.range(-0.2f, 0.2f);
                rim[k] = base + V3(cosf(b) * R * rng.range(0.8f, 1.2f), 0, sinf(b) * R * rng.range(0.8f, 1.2f));
                mid[k] = lerp(rim[k], peak, rng.range(0.45f, 0.6f)) + V3(rng.range(-20, 20), rng.range(-15, 15), rng.range(-20, 20));
            }
            for (int k = 0; k < sides; k++) {
                int k2 = (k + 1) % sides;
                V3 rock(0.56f, 0.60f, 0.66f), snow(0.92f, 0.94f, 0.97f);
                m.quad(rim[k], mid[k], mid[k2], rim[k2], rock * rng.range(0.85f, 1.1f), M_ROCK);
                m.tri(mid[k], peak, mid[k2], H > 290 ? snow : rock * rng.range(0.95f, 1.15f), H > 290 ? M_CONCRETE : M_ROCK);
            }
        }
    }

    // ---------------- sea + lake water
    {
        V3 a(minX - 3000, -1.0f, 1238), b(maxX + 3000, -1.0f, 1238), c(maxX + 3000, -1.0f, 5000), d(minX - 3000, -1.0f, 5000);
        m.quad(a, d, c, b, V3(0.12f, 0.55f, 0.72f), M_WATER);
        disc(m, V3(t.lakeC.x, -0.30f, t.lakeC.z), t.lakeR + 6, 40, V3(0.10f, 0.70f, 0.78f), M_WATER);
    }

    // ---------------- runoff, curbs, guard rails, walls
    auto side = [&](int i, float lat, float y) { V3 p = t.P[i] + t.R[i] * lat; p.y = y; return p; };
    std::vector<float> curbAmt(n, 0);
    for (int i = 0; i < n; i++) {
        float k = 0;
        for (int j = -12; j <= 12; j++) k = fmaxf(k, fabsf(t.K[(i + j + n) % n]));
        curbAmt[i] = k > 1.0f / 320.0f ? 1.0f : 0.0f;
    }
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float s = t.S[i];
        bool tunnel = t.inTunnel(s);
        float wall = t.wallAt(s), wallJ = t.wallAt(t.S[j]);
        bool curb = curbAmt[i] > 0.5f && !tunnel;
        for (int sd = -1; sd <= 1; sd += 2) {
            float inner = hw + (curb ? 1.7f : 0.0f);
            V3 con = tunnel ? V3(0.55f, 0.55f, 0.57f) : V3(0.80f, 0.80f, 0.72f);
            V3 a = side(i, sd * inner, 0.0f), b = side(j, sd * inner, 0.0f);
            V3 c = side(j, sd * wallJ, 0.0f), d = side(i, sd * wall, 0.0f);
            if (sd > 0) m.quad(a, b, c, d, con, M_CONCRETE); else m.quad(a, d, c, b, con, M_CONCRETE);
            if (curb) {
                bool red = ((i / 2) % 2) == 0;
                V3 cc = red ? V3(0.86f, 0.08f, 0.08f) : V3(0.96f, 0.96f, 0.96f);
                V3 a0 = side(i, sd * hw, 0.0f), b0 = side(j, sd * hw, 0.0f);
                V3 a1 = side(i, sd * (hw + 0.25f), 0.07f), b1 = side(j, sd * (hw + 0.25f), 0.07f);
                V3 a2 = side(i, sd * (hw + 1.45f), 0.07f), b2 = side(j, sd * (hw + 1.45f), 0.07f);
                V3 a3 = side(i, sd * (hw + 1.7f), 0.0f), b3 = side(j, sd * (hw + 1.7f), 0.0f);
                if (sd > 0) { m.quad(a0, b0, b1, a1, cc, M_GLOSS); m.quad(a1, b1, b2, a2, cc, M_GLOSS); m.quad(a2, b2, b3, a3, cc * 0.8f, M_GLOSS); }
                else { m.quad(a0, a1, b1, b0, cc, M_GLOSS); m.quad(a1, a2, b2, b1, cc, M_GLOSS); m.quad(a2, a3, b3, b2, cc * 0.8f, M_GLOSS); }
            }
        }
    }
    // guard rails / walls / fences
    for (int i = 0; i < n; i += 2) {
        int j = (i + 2) % n;
        float s = t.S[i];
        bool tunnel = t.inTunnel(s), stands = t.inZone(s, Z_STANDS);
        if (tunnel) continue;
        for (int sd = -1; sd <= 1; sd += 2) {
            float lat = sd * t.wallOff;
            V3 back = t.R[i] * (sd * 0.3f);
            if (stands) {
                // concrete wall + catch fence
                V3 a0 = side(i, lat, 0), b0 = side(j, lat, 0), a1 = side(i, lat, 1.1f), b1 = side(j, lat, 1.1f);
                V3 wc(0.88f, 0.89f, 0.9f);
                if (sd > 0) m.quad(a0, a1, b1, b0, wc, M_CONCRETE); else m.quad(a0, b0, b1, a1, wc, M_CONCRETE);
                m.quad(a1, b1, b1 + back, a1 + back, wc * 0.9f, M_CONCRETE);
                V3 f0 = side(i, lat + sd * 0.15f, 1.1f), f1 = side(j, lat + sd * 0.15f, 1.1f);
                m.quad(f0, f1, f1 + up * 4.6f, f0 + up * 4.6f, V3(0.25f, 0.27f, 0.3f), M_FENCE);
                if (i % 6 == 0) {
                    M4 xf = translate(f0);
                    m.box(V3(-0.09f, 0, -0.09f), V3(0.09f, 5.2f, 0.09f), V3(0.2f, 0.21f, 0.24f), M_METAL, 0, &xf);
                    // overhang
                    obox(m, f0 + up * 5.1f - t.R[i] * (sd * 0.6f), t.R[i], up, t.T[i], 0.7f, 0.05f, 0.05f, V3(0.2f, 0.21f, 0.24f), M_METAL);
                }
            } else {
                // armco: two rails
                for (int r = 0; r < 2; r++) {
                    float y0 = 0.42f + r * 0.36f, y1 = y0 + 0.30f;
                    V3 a0 = side(i, lat, y0), b0 = side(j, lat, y0), a1 = side(i, lat, y1), b1 = side(j, lat, y1);
                    V3 am = side(i, lat - sd * 0.07f, (y0 + y1) * 0.5f), bm = side(j, lat - sd * 0.07f, (y0 + y1) * 0.5f);
                    V3 rc(0.66f, 0.69f, 0.72f);
                    if (sd > 0) { m.quad(a0, am, bm, b0, rc, M_METAL); m.quad(am, a1, b1, bm, rc * 1.08f, M_METAL); }
                    else { m.quad(a0, b0, bm, am, rc, M_METAL); m.quad(am, bm, b1, a1, rc * 1.08f, M_METAL); }
                    m.quad(a0 + back, a1 + back, b1 + back, b0 + back, rc * 0.7f, M_METAL);
                }
                if (i % 4 == 0) {
                    V3 pp = side(i, lat + sd * 0.25f, 0);
                    obox(m, pp + up * 0.55f, t.R[i], up, t.T[i], 0.08f, 0.55f, 0.08f, V3(0.45f, 0.47f, 0.5f), M_METAL);
                }
            }
        }
    }

    // ---------------- grandstands, light towers, control tower
    for (float s0 = 0; s0 < t.length; s0 += 6) {
        if (!t.inZone(s0, Z_STANDS) || !t.inZone(s0 + 6, Z_STANDS)) continue;
        int i = (int)(s0 / t.ds) % n, j = (int)((s0 + 6) / t.ds) % n;
        for (int sd = -1; sd <= 1; sd += 2) {
            for (int row = 0; row < 9; row++) {
                float lat = sd * (t.wallOff + 4.0f + row * 1.7f), lat2 = lat + sd * 1.7f;
                float y = 1.0f + row * 1.1f;
                V3 a = side(i, lat, y), b = side(j, lat, y), c = side(j, lat2, y), d = side(i, lat2, y);
                V3 a0 = side(i, lat, y - 1.1f), b0 = side(j, lat, y - 1.1f);
                // riser with crowd
                if (sd > 0) { m.quad(a0, a, b, b0, V3(1, 1, 1), M_CROWD); m.quad(a, d, c, b, V3(0.6f, 0.62f, 0.66f), M_CONCRETE); }
                else { m.quad(a0, b0, b, a, V3(1, 1, 1), M_CROWD); m.quad(a, b, c, d, V3(0.6f, 0.62f, 0.66f), M_CONCRETE); }
            }
            // back wall
            float latB = sd * (t.wallOff + 4.0f + 9 * 1.7f);
            V3 ba = side(i, latB, 0), bb = side(j, latB, 0);
            m.quad(ba, bb, bb + up * 11.0f, ba + up * 11.0f, V3(0.55f, 0.57f, 0.62f), M_CONCRETE);
            // roof canopy
            V3 r0 = side(i, sd * (t.wallOff + 3.0f), 14.5f), r1 = side(j, sd * (t.wallOff + 3.0f), 14.5f);
            V3 r2 = side(j, latB + sd * 1.0f, 16.5f), r3 = side(i, latB + sd * 1.0f, 16.5f);
            V3 rc = ((int)(s0 / 18) % 2) ? V3(0.80f, 0.12f, 0.12f) : V3(0.10f, 0.30f, 0.75f);
            m.quad(r0, r1, r2, r3, rc, M_GLOSS); m.quad(r0, r3, r2, r1, rc * 0.6f, M_GLOSS);
            if (((int)s0) % 24 == 0) {
                V3 col = side(i, latB, 0);
                obox(m, col + up * 8.25f, t.R[i], up, t.T[i], 0.25f, 8.25f, 0.25f, V3(0.75f, 0.76f, 0.8f), M_METAL);
            }
            // light towers
            if (((int)s0) % 96 == 0) {
                V3 lp = side(i, latB + sd * 3.0f, 0);
                obox(m, lp + up * 15.0f, t.R[i], up, t.T[i], 0.35f, 15.0f, 0.35f, V3(0.55f, 0.56f, 0.6f), M_METAL);
                obox(m, lp + up * 30.5f - t.R[i] * (sd * 0.4f), t.R[i], up, t.T[i], 0.3f, 1.4f, 2.4f, V3(0.2f, 0.2f, 0.22f), M_METAL);
                obox(m, lp + up * 30.5f - t.R[i] * (sd * 0.72f), t.R[i], up, t.T[i], 0.04f, 1.2f, 2.2f, V3(1.0f, 0.98f, 0.9f), M_MATTE, 0.9f);
            }
        }
    }
    {   // control tower + pit building at the start line (left side)
        V3 c = side(0, -(t.wallOff + 40), 0);
        V3 rr = t.R[0], fw = t.T[0];
        obox(m, c + up * 7.0f + fw * 30.0f, rr, up, fw, 9.0f, 7.0f, 30.0f, V3(0.92f, 0.93f, 0.95f), M_CONCRETE);
        for (int f = 0; f < 3; f++)
            obox(m, c + up * (2.6f + f * 4.2f) + fw * 30.0f + rr * 9.02f, rr, up, fw, 0.02f, 1.2f, 29.0f, V3(0.25f, 0.5f, 0.7f), M_GLASS);
        obox(m, c + up * 18.0f - fw * 6.0f, rr, up, fw, 5.0f, 18.0f, 5.0f, V3(0.95f, 0.95f, 0.97f), M_CONCRETE);
        obox(m, c + up * 37.0f - fw * 6.0f, rr, up, fw, 6.0f, 1.2f, 6.0f, V3(0.85f, 0.12f, 0.12f), M_GLOSS);
        obox(m, c + up * 33.0f - fw * 6.0f, rr, up, fw, 5.6f, 2.8f, 5.6f, V3(0.2f, 0.45f, 0.65f), M_GLASS);
    }

    // ---------------- cliff wall
    {
        float sPrev = -1; V3 pb, pm, pt; bool have = false;
        for (float s = 0; s < t.length; s += 5) {
            if (!t.inZone(s, Z_CLIFF_R)) { have = false; continue; }
            V3 R = t.rightAt(s);
            float h = cliffHeight(s);
            float j1 = vnoise(s * 0.11f, 1.0f) * 2.5f, j2 = vnoise(s * 0.07f, 9.0f) * 3.0f;
            V3 b = t.at(s, t.wallOff + 2.0f);
            V3 mpt = t.at(s, t.wallOff + 3.5f + j1) + up * (h * 0.45f);
            V3 tp = t.at(s, t.wallOff + 6.0f + j2) + up * h;
            if (have) {
                V3 rock(0.74f, 0.62f, 0.46f);
                float v = 0.88f + 0.2f * vnoise(s * 0.2f, 4.0f);
                m.quad(pb, b, mpt, pm, rock * v, M_ROCK);
                m.quad(pm, mpt, tp, pt, rock * (v * 1.05f), M_ROCK);
                m.quad(pt, tp, tp + R * 8.0f + up * 1.0f, pt + R * 8.0f + up * 1.0f, V3(0.36f, 0.62f, 0.2f), M_GRASS);
            }
            pb = b; pm = mpt; pt = tp; have = true; sPrev = s;
        }
        (void)sPrev;
    }

    // ---------------- tunnel
    {
        float s0 = -1, s1 = -1;
        for (auto& z : t.zones) if (z.type == Z_TUNNEL) { s0 = z.s0; s1 = z.s1; }
        float wl = t.halfW + 3.4f, H = 7.5f;
        for (float s = s0; s < s1; s += 4) {
            int i = (int)(s / t.ds) % n, j = (int)(fminf(s + 4, s1) / t.ds) % n;
            for (int sd = -1; sd <= 1; sd += 2) {
                float lat = sd * wl;
                const float bands[5] = {0, 0.9f, 2.6f, 4.2f, H};
                const V3 bc[4] = {V3(0.45f, 0.45f, 0.47f), V3(0.92f, 0.86f, 0.70f), V3(0.80f, 0.74f, 0.60f), V3(0.65f, 0.66f, 0.68f)};
                for (int b = 0; b < 4; b++) {
                    V3 a0 = side(i, lat, bands[b]), b0 = side(j, lat, bands[b]), a1 = side(i, lat, bands[b + 1]), b1 = side(j, lat, bands[b + 1]);
                    if (sd > 0) m.quad(a0, a1, b1, b0, bc[b] * 0.85f, M_CONCRETE); else m.quad(a0, b0, b1, a1, bc[b] * 0.85f, M_CONCRETE);
                }
            }
            V3 c0 = side(i, -wl, H), c1 = side(j, -wl, H), c2 = side(j, wl, H), c3 = side(i, wl, H);
            m.quad(c0, c3, c2, c1, V3(0.30f, 0.30f, 0.32f), M_CONCRETE);
            m.quad(c0, c1, c2, c3, V3(0.30f, 0.30f, 0.32f), M_CONCRETE);
            // light strips (orange) every 8 m
            if (((int)s) % 8 == 0)
                for (int sd = -1; sd <= 1; sd += 2) {
                    V3 lp = side(i, sd * (t.halfW - 2.0f), H - 0.15f);
                    obox(m, lp, t.R[i], up, t.T[i], 0.6f, 0.08f, 1.6f, V3(1.0f, 0.62f, 0.22f), M_MATTE, 1.0f);
                }
        }
        // portals
        for (int e = 0; e < 2; e++) {
            float s = e ? s1 : s0;
            V3 c = t.at(s, 0), rr = t.rightAt(s), fw = t.tangentAt(s);
            V3 out = e ? fw : -fw;
            obox(m, c + up * (H + 3.0f) + out * 0.8f, rr, up, fw, wl + 8.0f, 3.0f, 0.8f, V3(0.70f, 0.70f, 0.70f), M_CONCRETE);
            for (int sd = -1; sd <= 1; sd += 2)
                obox(m, c + rr * (sd * (wl + 4.5f)) + up * (H * 0.5f) + out * 0.8f, rr, up, fw, 4.0f, H * 0.5f, 0.8f, V3(0.70f, 0.70f, 0.70f), M_CONCRETE);
            V3 band = c + up * (H + 0.6f) + out * 1.62f;
            m.quad(band - rr * wl, band + rr * wl, band + rr * wl + up * 0.6f, band - rr * wl + up * 0.6f, V3(1.0f, 0.75f, 0.1f), M_MATTE, 0.2f);
        }
    }

    // ---------------- lighthouse + sea-side palms
    lighthouse(m, t.lighthouse);

    // ---------------- gantries, arch, brake boards, billboards
    std::vector<V3> blocked;   // positions to keep trees away from
    gantry(m, geo.text, font, t, 0.0f, 0);
    blocked.push_back(t.at(0, -(t.wallOff + 2))); blocked.push_back(t.at(0, t.wallOff + 2));
    for (float cps : t.checkpoints) { gantry(m, geo.text, font, t, cps, 1); blocked.push_back(t.at(cps, -(t.wallOff + 2))); blocked.push_back(t.at(cps, t.wallOff + 2)); }
    inflatableArch(m, geo.text, font, t, 300.0f);

    // hairpin apexes
    {
        std::vector<std::pair<float, float>> peaks;
        for (int i = 0; i < n; i++) {
            float k = fabsf(t.K[i]);
            if (k < 1.0f / 95.0f) continue;
            bool mx = true;
            for (int j = -40; j <= 40; j++) if (fabsf(t.K[(i + j + n) % n]) > k) { mx = false; break; }
            if (mx) peaks.push_back({t.S[i], t.K[i]});
        }
        for (auto& pk : peaks) {
            const_cast<Track&>(t).apexes.push_back(pk.first);
            float outside = pk.second > 0 ? -1.0f : 1.0f;
            for (int b = 1; b <= 3; b++) {
                float s = pk.first - 70.0f - b * 60.0f;
                V3 fw = t.tangentAt(s);
                V3 base = t.at(s, outside * (t.wallOff - 0.6f));
                if (t.inTunnel(s)) continue;
                V3 face = -fw, vr(face.z, 0, -face.x);
                obox(m, base + up * 0.9f, vr, up, face, 0.08f, 0.9f, 0.08f, V3(0.3f, 0.3f, 0.32f), M_METAL);
                V3 c = base + up * 2.4f;
                m.quad(c - vr * 0.9f - up * 0.7f + face * 0.05f, c + vr * 0.9f - up * 0.7f + face * 0.05f, c + vr * 0.9f + up * 0.7f + face * 0.05f, c - vr * 0.9f + up * 0.7f + face * 0.05f, V3(1, 1, 1), M_MATTE, 0.15f);
                obox(m, c, vr, up, face, 0.95f, 0.75f, 0.04f, V3(0.08f, 0.08f, 0.1f));
                char buf[8]; snprintf(buf, sizeof buf, "%d", b * 100);
                planeText(geo.text, font, buf, c - up * 0.35f + face * 0.1f, vr, up, 1.0f, V3(0.05f, 0.05f, 0.06f), 1);
            }
        }
    }
    // billboards: outside of corners and along straights
    int sp = 0;
    for (float s = 140; s < t.length - 60; s += 150) {
        if (t.inTunnel(s) || t.inTunnel(s + 30)) continue;
        int i = (int)(s / t.ds) % n;
        float k = 0; for (int j = 0; j < 30; j++) k += t.K[(i + j) % n];
        float sd = (sp % 2) ? 1.0f : -1.0f;
        if (fabsf(k) > 0.03f) sd = k > 0 ? -1.0f : 1.0f;
        if (t.inZone(s, Z_STANDS)) continue;
        if (t.inZone(s, Z_CLIFF_R) && sd > 0) sd = -1;
        const Sponsor& S = kSponsors[sp % kNumSponsors]; sp++;
        V3 rr = t.R[i], fw = t.T[i];
        V3 base = t.P[i] + rr * (sd * (t.wallOff + 5.0f + rng.range(0, 4)));
        base.y = 0;
        float yaw = rng.range(0.35f, 0.75f);
        V3 face = norm(-fw * cosf(yaw) - rr * (sd * sinf(yaw)));
        bool big = (sp % 3) == 0;
        billboard(m, geo.text, font, base, face, big ? 18.0f : 12.0f, big ? 4.5f : 3.2f, big ? 3.0f : 1.6f, S);
        blocked.push_back(base);
    }
    // small sponsor boards on the guard rail in corners
    for (int i = 0; i < n; i += 9) {
        if (fabsf(t.K[i]) < 1.0f / 260.0f || t.inTunnel(t.S[i]) || t.inZone(t.S[i], Z_STANDS)) continue;
        float sd = t.K[i] > 0 ? -1.0f : 1.0f;
        const Sponsor& S = kSponsors[(i / 9) % kNumSponsors];
        V3 rr = t.R[i], fw = t.T[i];
        V3 face = -rr * sd;
        V3 c = t.P[i] + rr * (sd * (t.wallOff + 0.35f)) + up * 1.55f;
        V3 vr(face.z, 0, -face.x);
        obox(m, c, vr, up, face, 2.6f, 0.45f, 0.04f, S.bg, M_MATTE, 0.1f);
        planeText(geo.text, font, S.name, c - up * 0.2f + face * 0.06f, vr, up, 0.62f, S.fg, 1);
        (void)fw;
    }

    // ---------------- houses
    for (float s = 0; s < t.length; s += 38) {
        for (int sd = -1; sd <= 1; sd += 2) {
            if (!t.inZone(s, sd < 0 ? Z_HOUSES_L : Z_HOUSES_R)) continue;
            if (rng.f() < 0.3f) continue;
            V3 rr = t.rightAt(s), fw = t.tangentAt(s);
            V3 p = t.at(s, sd * (t.wallOff + rng.range(22, 45)));
            if (t.distTo(p) < t.wallOff + 15) continue;
            p.y = heightAt(t, p.x, p.z);
            if (p.y > 6) continue;
            house(m, p, norm(-rr * (float)sd + fw * rng.range(-0.3f, 0.3f)), rng, rng.f() < 0.2f);
            blocked.push_back(p);
        }
    }

    // ---------------- trees
    auto clear = [&](V3 p, float r) {
        for (auto& b : blocked) if (len(V3(p.x - b.x, 0, p.z - b.z)) < r) return false;
        if (len(V3(p.x - t.lakeC.x, 0, p.z - t.lakeC.z)) < t.lakeR + 10) return false;
        if (len(V3(p.x - t.lighthouse.x, 0, p.z - t.lighthouse.z)) < 14) return false;
        return true;
    };
    int placed = 0, tries = 0;
    while (placed < 1500 && tries < 40000) {
        tries++;
        float x = rng.range(minX - 500, maxX + 500), z = rng.range(minZ - 500, maxZ + 300);
        float d; int ni = coarseNearest(t, V3(x, 0, z), &d);
        float s = t.S[ni];
        if (d < t.wallOff + 7.0f) continue;
        bool forest = t.inZone(s, Z_FOREST);
        float density = d < 140 ? 1.0f : (d < 400 ? 0.45f : 0.12f);
        if (forest) density = 1.0f;
        if (t.inZone(s, Z_STANDS) && d < 80) continue;
        if (rng.f() > density) continue;
        float sand;
        float h = heightAt(t, x, z, &sand);
        if (h > 120 || h < -0.5f) continue;
        V3 p(x, h - 0.2f, z);
        if (!clear(p, 16)) continue;
        if (sand > 0.3f || z > 1150) palmTree(m, p, rng);
        else if (forest || rng.f() < 0.45f) pineTree(m, p, rng, rng.range(1.0f, 1.9f));
        else blobTree(m, p, rng, rng.range(0.9f, 1.5f));
        placed++;
    }
    // rows of trees right behind the rails
    for (int i = 0; i < n; i += 9) {
        float s = t.S[i];
        if (t.inZone(s, Z_STANDS) || t.inTunnel(s)) continue;
        for (int sd = -1; sd <= 1; sd += 2) {
            if (t.inZone(s, Z_CLIFF_R) && sd > 0) continue;
            if (rng.f() < 0.5f) continue;
            V3 p = t.P[i] + t.R[i] * (sd * (t.wallOff + rng.range(6.0f, 16.0f))) + t.T[i] * rng.range(-5.f, 5.f);
            if (t.distTo(p) < t.wallOff + 5.0f || !clear(p, 9)) continue;
            float sand;
            p.y = heightAt(t, p.x, p.z, &sand) - 0.2f;
            if (p.y < -0.5f) continue;
            if (sand > 0.3f || p.z > 1150) palmTree(m, p, rng);
            else if (rng.f() < 0.5f) pineTree(m, p, rng, rng.range(1.1f, 1.8f));
            else blobTree(m, p, rng, rng.range(1.0f, 1.5f));
        }
    }

    // ---------------- road surface
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float s0 = t.S[i], s1 = (j == 0) ? t.length : t.S[j];
        float tun = t.inTunnel(s0) ? 1.0f : 0.0f;
        float kc = t.K[i], kc2 = t.K[j];
        const int cols = 4;
        for (int c = 0; c < cols; c++) {
            float l0 = -hw + 2 * hw * c / cols, l1 = -hw + 2 * hw * (c + 1) / cols;
            V3 a = side(i, l0, 0), b = side(i, l1, 0), cc = side(j, l1, 0), d = side(j, l0, 0);
            RoadVtx A = {a.x, a.y, a.z, l0, s0, tun, kc}, B = {b.x, b.y, b.z, l1, s0, tun, kc};
            RoadVtx C = {cc.x, cc.y, cc.z, l1, s1, tun, kc2}, D = {d.x, d.y, d.z, l0, s1, tun, kc2};
            geo.road.push_back(A); geo.road.push_back(D); geo.road.push_back(C);
            geo.road.push_back(A); geo.road.push_back(C); geo.road.push_back(B);
        }
    }
}
