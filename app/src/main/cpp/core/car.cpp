// Stock-car body and wheel geometry (car local space: +x = left, +y = up, +z = forward).
#include "render.h"

static const float kWheelZ[2] = {1.52f, -1.48f};
static const float kWheelR = 0.335f;

static float halfWidth(float z) {
    float w = 1.0f;
    if (z > 1.85f) { float t = (z - 1.85f) / 0.65f; w -= t * t * 0.20f; }
    if (z < -2.05f) { float t = (-2.05f - z) / 0.45f; w -= t * t * 0.10f; }
    for (float wz : kWheelZ) { float d = (z - wz) / 0.55f; w += 0.035f * expf(-d * d * 2.0f); }
    return w;
}
static float bodyTop(float z) {
    if (z > 1.0f) { float t = (z - 1.0f) / 1.5f; return 0.88f - 0.38f * powf(t, 1.7f); }
    if (z > -1.7f) return 0.89f + (1.0f - z) * 0.025f * 0.0f + (-z > 0 ? -z * 0.03f : 0);
    float t = (-1.7f - z) / 0.8f;
    return 0.95f + 0.02f * t;
}
static float roofY(float z, float top) {
    // windshield 1.0 -> 0.15, roof to -0.85, fastback rear window to -2.05
    if (z > 1.0f || z < -2.05f) return top;
    if (z > 0.20f) { float t = (1.0f - z) / 0.80f; return lerpf(top, 1.30f, powf(t, 0.7f)); }
    if (z > -0.95f) return 1.30f + 0.012f * (1.0f - fabsf(z + 0.37f) / 0.58f);
    float t = (-0.95f - z) / 1.10f;
    return lerpf(1.30f, top, powf(t, 1.4f));
}
static float bottomY(float z) {
    float y = 0.15f;
    for (float wz : kWheelZ) {
        float d = z - wz, r = 0.405f;
        if (fabsf(d) < r) y = fmaxf(y, 0.33f + sqrtf(r * r - d * d));
    }
    return y;
}

// Body: loft of cross-sections (left half), mirrored. Smooth normals.
static void buildBody(MeshBuilder& m) {
    const int NS = 54;          // sections along z
    const int NP = 9;           // points per half section
    float zs[NS];
    for (int i = 0; i < NS; i++) zs[i] = -2.50f + 5.0f * i / (NS - 1);
    std::vector<V3> G(NS * NP);
    std::vector<float> cab(NS);
    for (int i = 0; i < NS; i++) {
        float z = zs[i], w = halfWidth(z), top = bodyTop(z), roof = roofY(z, top), yb = bottomY(z);
        float g = sat((roof - top) / 0.40f);  // greenhouse factor
        cab[i] = g;
        V3 p[NP];
        p[0] = V3(w * 0.82f, yb, z);
        p[1] = V3(w * 0.97f, fmaxf(yb + 0.06f, 0.24f), z);
        p[2] = V3(w * 1.00f, fmaxf(yb + 0.10f, 0.46f), z);
        p[3] = V3(w * 1.00f, fmaxf(yb + 0.14f, 0.63f), z);
        p[4] = V3(w * 0.965f, fmaxf(yb + 0.17f, top - 0.09f), z);
        p[5] = V3(w * 0.87f, fmaxf(yb + 0.19f, top), z);
        float gb = lerpf(w * 0.60f, 0.88f, g);
        float re = lerpf(w * 0.32f, 0.72f, g);
        p[6] = V3(gb, top + lerpf(0.02f, 0.015f, g), z);
        p[7] = V3(re, lerpf(top + 0.035f, roof - 0.035f, g), z);
        p[8] = V3(0.0f, lerpf(top + 0.045f, roof, g), z);
        for (int k = 1; k < NP; k++) if (p[k].y < p[k - 1].y + 0.004f && k < 6) p[k].y = p[k - 1].y + 0.004f;
        for (int k = 0; k < NP; k++) G[i * NP + k] = p[k];
    }
    // normals: average of adjacent quad normals (outward for left side)
    auto at = [&](int i, int k) { return G[i * NP + k]; };
    std::vector<V3> N(NS * NP, V3());
    for (int i = 0; i < NS - 1; i++)
        for (int k = 0; k < NP - 1; k++) {
            V3 a = at(i, k), b = at(i, k + 1), c = at(i + 1, k + 1), d = at(i + 1, k);
            V3 nn = norm(cross(c - a, b - d));
            N[i * NP + k] += nn; N[i * NP + k + 1] += nn; N[(i + 1) * NP + k + 1] += nn; N[(i + 1) * NP + k] += nn;
        }
    for (auto& nn : N) nn = norm(nn);
    // make sure normals point outward (left half: +x on the flank)
    if (N[(NS / 2) * NP + 3].x < 0) for (auto& nn : N) nn = -nn;
    for (int i = 0; i < NS; i++) N[i * NP + 8] = norm(V3(0, N[i * NP + 8].y, N[i * NP + 8].z));

    V3 white(1, 1, 1), black(0.04f, 0.04f, 0.045f), glass(0.06f, 0.08f, 0.11f);
    for (int i = 0; i < NS - 1; i++) {
        float zc = (zs[i] + zs[i + 1]) * 0.5f;
        float g = fminf(cab[i], cab[i + 1]);
        for (int k = 0; k < NP - 1; k++) {
            float mat = M_PAINT; V3 col = white;
            if (k == 0) { mat = M_TRIM; col = black; }
            if (k == 6 && g > 0.12f) { mat = M_GLASS; col = glass; }
            if (k == 7 && g > 0.12f) {
                bool windshield = zc > 0.12f, rear = zc < -0.88f;
                if (windshield || rear) { mat = M_GLASS; col = glass; }
            }
            for (int sd = -1; sd <= 1; sd += 2) {
                auto P = [&](int ii, int kk) { V3 p = at(ii, kk); if (sd < 0) p.x = -p.x; return p; };
                auto Nn = [&](int ii, int kk) { V3 nn = N[ii * NP + kk]; if (sd < 0) nn.x = -nn.x; return nn; };
                V3 a = P(i, k), b = P(i, k + 1), c = P(i + 1, k + 1), d = P(i + 1, k);
                V3 na = Nn(i, k), nb = Nn(i, k + 1), nc = Nn(i + 1, k + 1), nd = Nn(i + 1, k);
                if (sd > 0) { m.triN(a, d, c, na, nd, nc, col, mat); m.triN(a, c, b, na, nc, nb, col, mat); }
                else { m.triN(a, b, c, na, nb, nc, col, mat); m.triN(a, c, d, na, nc, nd, col, mat); }
            }
        }
        // underside / wheel-well ceiling
        V3 a = at(i, 0), d = at(i + 1, 0);
        V3 a2 = V3(-a.x, a.y, a.z), d2 = V3(-d.x, d.y, d.z);
        m.quad(a, a2, d2, d, V3(0.03f, 0.03f, 0.035f), M_TRIM);
    }
    // nose and tail caps (fans from the centre)
    for (int e = 0; e < 2; e++) {
        int i = e ? NS - 1 : 0;
        V3 ctr(0, 0, zs[i]);
        float sumY = 0; for (int k = 0; k < NP; k++) sumY += at(i, k).y; ctr.y = sumY / NP;
        V3 nrm(0, 0, e ? 1.0f : -1.0f);
        for (int sd = -1; sd <= 1; sd += 2)
            for (int k = 0; k < NP - 1; k++) {
                V3 a = at(i, k), b = at(i, k + 1);
                if (sd < 0) { a.x = -a.x; b.x = -b.x; }
                bool flip = (e == 0) ^ (sd < 0);
                if (flip) m.triN(ctr, b, a, nrm, nrm, nrm, white, M_PAINT); else m.triN(ctr, a, b, nrm, nrm, nrm, white, M_PAINT);
            }
        {   // close the gap along the bottom edge
            V3 a = at(i, 0), b = a; b.x = -b.x;
            m.triN(ctr, a, b, nrm, nrm, nrm, white, M_PAINT); m.triN(ctr, b, a, nrm, nrm, nrm, white, M_PAINT);
        }
    }
}

static void wedge(MeshBuilder& m, V3 c, V3 h, V3 col, float mat, float emis = 0) {
    m.box(c - h, c + h, col, mat, emis);
}

static void buildAero(MeshBuilder& m) {
    V3 black(0.05f, 0.05f, 0.055f), carbon(0.09f, 0.09f, 0.10f);
    // rear spoiler blade (slightly raked) + end plates
    for (int i = 0; i < 2; i++) {
        float y0 = 0.98f, y1 = 1.20f;
        V3 a(-0.95f, y0, -2.36f), b(0.95f, y0, -2.36f), c(0.95f, y1, -2.44f), d(-0.95f, y1, -2.44f);
        V3 off(0, 0, i ? -0.025f : 0.0f);
        if (i) m.quad(a + off, b + off, c + off, d + off, carbon, M_TRIM); else m.quad(b, a, d, c, carbon, M_TRIM);
    }
    for (int sd = -1; sd <= 1; sd += 2) wedge(m, V3(sd * 0.955f, 1.08f, -2.36f), V3(0.012f, 0.13f, 0.14f), carbon, M_TRIM);
    // front splitter
    wedge(m, V3(0, 0.13f, 2.45f), V3(0.92f, 0.025f, 0.16f), black, M_TRIM);
    // rear diffuser with fins
    wedge(m, V3(0, 0.20f, -2.52f), V3(0.86f, 0.07f, 0.06f), black, M_TRIM);
    for (int f = -3; f <= 3; f++) wedge(m, V3(f * 0.24f, 0.17f, -2.56f), V3(0.012f, 0.06f, 0.07f), carbon, M_TRIM);
    // side skirts between the wheels
    for (int sd = -1; sd <= 1; sd += 2) wedge(m, V3(sd * 0.985f, 0.19f, 0.02f), V3(0.035f, 0.05f, 1.02f), black, M_TRIM);
    // mirrors
    for (int sd = -1; sd <= 1; sd += 2) {
        wedge(m, V3(sd * 0.93f, 1.00f, 0.78f), V3(0.07f, 0.045f, 0.06f), black, M_TRIM);
        wedge(m, V3(sd * 0.87f, 0.96f, 0.80f), V3(0.04f, 0.015f, 0.02f), black, M_TRIM);
    }
    // roof fins on the rear window
    for (int sd = -1; sd <= 1; sd += 2) {
        V3 a(sd * 0.56f, 1.30f, -0.90f), b(sd * 0.56f, 1.05f, -1.80f), c(sd * 0.56f, 1.12f, -1.80f), d(sd * 0.56f, 1.33f, -0.95f);
        m.quad(a, b, c, d, black, M_TRIM); m.quad(a, d, c, b, black, M_TRIM);
    }
    // window net on the driver's (left) window
    {
        V3 a(0.83f, 0.93f, 0.55f), b(0.83f, 0.93f, -0.35f), c(0.72f, 1.22f, -0.35f), d(0.74f, 1.18f, 0.25f);
        V3 y(1.0f, 0.85f, 0.10f);
        for (int k = 0; k < 4; k++) {
            float t0 = k / 4.0f, t1 = t0 + 0.12f;
            m.quad(lerp(a, d, t0), lerp(b, c, t0), lerp(b, c, t1), lerp(a, d, t1), y, M_MATTE, 0.2f);
        }
    }
    // tail-light bars (emissive) on the tail panel
    float rz = -2.505f;
    V3 red(1.0f, 0.10f, 0.06f);
    for (int sd = -1; sd <= 1; sd += 2) {
        float x0 = sd * 0.22f, x1 = sd * 0.78f;
        V3 a(x0, 0.80f, rz), b(x1, 0.80f, rz), c(x1, 0.86f, rz), d(x0, 0.86f, rz);
        m.quad(a, b, c, d, red, M_MATTE, 1.0f); m.quad(a, d, c, b, red, M_MATTE, 1.0f);
    }
    // exhaust tips
    for (int sd = -1; sd <= 1; sd += 2) wedge(m, V3(sd * 0.40f, 0.24f, -2.55f), V3(0.07f, 0.05f, 0.05f), V3(0.55f, 0.55f, 0.58f), M_METAL);
}

void buildCarMesh(MeshBuilder& m) {
    buildBody(m);
    buildAero(m);
}

// Unit wheel centred at origin, axle along x, outer face at +x. Rim colour via M_RIM.
void buildWheelMesh(MeshBuilder& m) {
    const int S = 20;
    const float R = kWheelR, w = 0.155f;
    V3 tyre(0.07f, 0.07f, 0.075f), side(0.10f, 0.10f, 0.11f), letter(0.85f, 0.85f, 0.85f);
    // profile (x, r) from inner rim around tread to outer rim
    const float prof[][2] = {{-w + 0.01f, 0.24f}, {-w, 0.29f}, {-w + 0.03f, R}, {w - 0.03f, R}, {w, 0.29f}, {w - 0.01f, 0.24f}};
    const int NPf = 6;
    for (int i = 0; i < S; i++) {
        float a0 = 2 * PI * i / S, a1 = 2 * PI * (i + 1) / S;
        for (int k = 0; k < NPf - 1; k++) {
            auto P = [&](float a, int kk) { return V3(prof[kk][0], sinf(a) * prof[kk][1], cosf(a) * prof[kk][1]); };
            auto Nn = [&](float a, int kk) {
                float nx = kk <= 1 ? -1.0f : (kk >= 4 ? 1.0f : 0.0f);
                V3 radial(0, sinf(a), cosf(a));
                return norm(V3(nx, 0, 0) + radial * (kk == 2 || kk == 3 ? 1.0f : 0.35f));
            };
            V3 col = (k == 2) ? tyre : side;
            if (k == 3 && (i % 5 == 1)) col = letter;   // sidewall lettering
            m.triN(P(a0, k), P(a1, k), P(a1, k + 1), Nn(a0, k), Nn(a1, k), Nn(a1, k + 1), col, M_RUBBER);
            m.triN(P(a0, k), P(a1, k + 1), P(a0, k + 1), Nn(a0, k), Nn(a1, k + 1), Nn(a0, k + 1), col, M_RUBBER);
        }
    }
    // dark recess and rim
    float fx = w - 0.03f;
    for (int i = 0; i < S; i++) {
        float a0 = 2 * PI * i / S, a1 = 2 * PI * (i + 1) / S;
        V3 c(fx - 0.06f, 0, 0);
        m.tri(c, V3(fx - 0.06f, sinf(a1) * 0.24f, cosf(a1) * 0.24f), V3(fx - 0.06f, sinf(a0) * 0.24f, cosf(a0) * 0.24f), V3(0.03f, 0.03f, 0.03f), M_TRIM);
        // rim lip ring
        V3 o0(fx, sinf(a0) * 0.245f, cosf(a0) * 0.245f), o1(fx, sinf(a1) * 0.245f, cosf(a1) * 0.245f);
        V3 i0(fx - 0.01f, sinf(a0) * 0.205f, cosf(a0) * 0.205f), i1(fx - 0.01f, sinf(a1) * 0.205f, cosf(a1) * 0.205f);
        m.quad(i0, i1, o1, o0, V3(1, 1, 1), M_RIM);
    }
    // five spokes
    for (int k = 0; k < 5; k++) {
        float a = 2 * PI * k / 5;
        V3 dir(0, sinf(a), cosf(a)), perp(0, cosf(a), -sinf(a));
        V3 h0(fx - 0.005f, 0, 0);
        V3 p0 = h0 + dir * 0.05f - perp * 0.045f, p1 = h0 + dir * 0.05f + perp * 0.045f;
        V3 p2 = h0 + dir * 0.215f + perp * 0.03f - V3(0.03f, 0, 0), p3 = h0 + dir * 0.215f - perp * 0.03f - V3(0.03f, 0, 0);
        m.quad(p0, p1, p2, p3, V3(1, 1, 1), M_RIM);
    }
    // hub
    for (int i = 0; i < 10; i++) {
        float a0 = 2 * PI * i / 10, a1 = 2 * PI * (i + 1) / 10;
        V3 c(fx + 0.01f, 0, 0);
        m.tri(c, V3(fx, sinf(a1) * 0.06f, cosf(a1) * 0.06f), V3(fx, sinf(a0) * 0.06f, cosf(a0) * 0.06f), V3(0.8f, 0.8f, 0.82f), M_METAL);
    }
}

void wheelLayout(int w, V3* pos, float* sideSign, bool* front) {
    float sx = (w & 1) ? -1.0f : 1.0f;
    *front = (w & 2) != 0;
    *pos = V3(sx * 0.845f, kWheelR, *front ? kWheelZ[0] : kWheelZ[1]);
    *sideSign = sx;
}
float wheelRadius() { return kWheelR; }
