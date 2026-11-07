// Renderer: procedural textures, painted liveries, scene passes, post-processing.
#include "render.h"
#include "shaders.h"
#include <string.h>
#include <stdio.h>
#include <algorithm>

// ------------------------------------------------------------- GL helpers
static GLuint compile(GLenum type, const std::string& src) {
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[4096]; glGetShaderInfoLog(s, sizeof log, nullptr, log); ftLog("shader compile failed: %s", log); }
    return s;
}
bool Prog::build(const std::string& vs, const std::string& fs, std::initializer_list<const char*> attribs) {
    id = glCreateProgram();
    GLuint v = compile(GL_VERTEX_SHADER, std::string(kHeader) + vs);
    GLuint f = compile(GL_FRAGMENT_SHADER, std::string(kHeader) + kSkyLib + kNoiseLib + fs);
    glAttachShader(id, v); glAttachShader(id, f);
    int i = 0;
    for (const char* a : attribs) glBindAttribLocation(id, i++, a);
    glLinkProgram(id);
    GLint ok = 0; glGetProgramiv(id, GL_LINK_STATUS, &ok);
    if (!ok) { char log[4096]; glGetProgramInfoLog(id, sizeof log, nullptr, log); ftLog("link failed: %s", log); }
    glDeleteShader(v); glDeleteShader(f);
    return ok != 0;
}
GLint Prog::U(const char* name) {
    for (auto& c : cache) if (c.first == name) return c.second;
    GLint l = glGetUniformLocation(id, name);
    cache.push_back({name, l});
    return l;
}
void Target::create(int W, int H, bool withDepth, bool lin) {
    destroy();
    w = W; h = H;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, lin ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, lin ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (withDepth) {
        glGenRenderbuffers(1, &depth);
        glBindRenderbuffer(GL_RENDERBUFFER, depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    }
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) ftLog("framebuffer incomplete %dx%d", w, h);
}
void Target::destroy() {
    if (fbo) glDeleteFramebuffers(1, &fbo);
    if (tex) glDeleteTextures(1, &tex);
    if (depth) glDeleteRenderbuffers(1, &depth);
    fbo = tex = depth = 0;
}

static GLuint makeVao(GLuint& vbo, const void* data, size_t bytes, GLenum usage,
                      std::initializer_list<std::pair<int, int>> layout, int strideFloats) {
    GLuint vao; glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, bytes, data, usage);
    int i = 0;
    for (auto& l : layout) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, l.first, GL_FLOAT, GL_FALSE, strideFloats * 4, (void*)(intptr_t)(l.second * 4));
        i++;
    }
    glBindVertexArray(0);
    return vao;
}

// ------------------------------------------------------------- procedural textures
static float tnoise(float x, float y, int period, uint32_t seed) {
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    auto H = [&](int a, int b) { a = ((a % period) + period) % period; b = ((b % period) + period) % period; return hash2(a + seed * 131, b + seed * 71); };
    return lerpf(lerpf(H(ix, iy), H(ix + 1, iy), fx), lerpf(H(ix, iy + 1), H(ix + 1, iy + 1), fx), fy);
}
static float tfbm(float x, float y, int period, uint32_t seed, int oct) {
    float s = 0, a = 0.5f, t = 0;
    for (int i = 0; i < oct; i++) { s += tnoise(x, y, period, seed + i) * a; t += a; x *= 2; y *= 2; period *= 2; a *= 0.5f; }
    return s / t;
}
static GLuint uploadTex(const std::vector<uint8_t>& px, int w, int h) {
    GLuint t; glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return t;
}
void Renderer::makeTextures() {
    const int N = 256;
    std::vector<uint8_t> px(N * N * 4);
    // asphalt aggregate
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float v = 0.5f + (tfbm(x / 32.0f, y / 32.0f, 8, 3, 4) - 0.5f) * 0.5f;
            float h = hash2(x * 7 + 1, y * 13 + 5);
            if (h < 0.10f) v += 0.25f * hash2(x, y + 99);
            else if (h < 0.22f) v -= 0.22f * hash2(x + 7, y);
            float h2 = hash2(x / 2 + 300, y / 2 + 77);
            if (h2 < 0.05f) v += 0.18f;
            uint8_t b = (uint8_t)(clampf(v, 0, 1) * 255);
            uint8_t* p = &px[(y * N + x) * 4]; p[0] = p[1] = p[2] = b; p[3] = 255;
        }
    texAsphalt = uploadTex(px, N, N);
    // grass
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float v = 0.5f + (tfbm(x / 16.0f, y / 16.0f, 16, 11, 4) - 0.5f) * 0.8f;
            v += (hash2(x * 3 + 11, y * 5 + 3) - 0.5f) * 0.35f;
            float clump = tfbm(x / 64.0f, y / 64.0f, 4, 21, 2);
            v *= 0.8f + 0.4f * clump;
            uint8_t b = (uint8_t)(clampf(v, 0, 1) * 255);
            uint8_t* p = &px[(y * N + x) * 4]; p[0] = p[1] = p[2] = b; p[3] = 255;
        }
    texGrass = uploadTex(px, N, N);
    // rock: strata + cracks, tan
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float n = tfbm(x / 32.0f, y / 32.0f, 8, 31, 5);
            float strata = 0.5f + 0.5f * sinf(y * 0.18f + n * 9.0f);
            float crack = fabsf(tfbm(x / 48.0f, y / 24.0f, 5, 41, 3) - 0.5f);
            float v = 0.62f + 0.22f * (n - 0.5f) * 2 + 0.10f * strata;
            if (crack < 0.025f) v *= 0.45f + crack * 12;
            v += (hash2(x + 5, y * 3) - 0.5f) * 0.08f;
            uint8_t* p = &px[(y * N + x) * 4];
            p[0] = (uint8_t)(clampf(v * 1.02f, 0, 1) * 255); p[1] = (uint8_t)(clampf(v * 0.98f, 0, 1) * 255); p[2] = (uint8_t)(clampf(v * 0.92f, 0, 1) * 255); p[3] = 255;
        }
    texRock = uploadTex(px, N, N);
    // crowd: one row of spectators per texture height
    {
        const int W = 128, H = 16;
        std::vector<uint8_t> c(W * H * 4);
        const V3 shirts[] = {V3(0.9f, 0.15f, 0.15f), V3(0.15f, 0.4f, 0.9f), V3(1, 0.85f, 0.1f), V3(0.95f, 0.95f, 0.95f), V3(0.2f, 0.75f, 0.3f),
                             V3(1, 0.5f, 0.1f), V3(0.6f, 0.2f, 0.7f), V3(0.1f, 0.1f, 0.12f), V3(0.2f, 0.8f, 0.9f), V3(1, 0.4f, 0.7f)};
        const V3 skins[] = {V3(0.95f, 0.78f, 0.62f), V3(0.78f, 0.56f, 0.40f), V3(0.55f, 0.37f, 0.25f), V3(0.98f, 0.85f, 0.72f)};
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int person = x / 6, px6 = x % 6;
                V3 col(0.32f, 0.34f, 0.38f);   // seats
                bool empty = hash2(person, 7) < 0.08f;
                if (!empty) {
                    V3 sh = shirts[(int)(hash2(person, 1) * 10) % 10];
                    V3 sk = skins[(int)(hash2(person, 2) * 4) % 4];
                    int headTop = 2 + (int)(hash2(person, 3) * 2);
                    if (y >= headTop && y < headTop + 4 && px6 >= 1 && px6 <= 4) col = sk;
                    if (y >= headTop && y < headTop + 1 && px6 >= 1 && px6 <= 4 && hash2(person, 4) < 0.4f) col = sh * 0.7f; // cap
                    if (y >= headTop + 4 && y < 14 && px6 <= 5) col = sh;
                    if (y >= headTop + 4 && y < headTop + 6 && (px6 == 0 || px6 == 5) && hash2(person, 5) < 0.3f) col = sk;   // arms up
                }
                uint8_t* p = &c[(y * W + x) * 4];
                p[0] = (uint8_t)(col.x * 255); p[1] = (uint8_t)(col.y * 255); p[2] = (uint8_t)(col.z * 255); p[3] = 255;
            }
        texCrowd = uploadTex(c, W, H);
    }
}

// ------------------------------------------------------------- liveries
void Renderer::paintLivery(const Livery& L, GLuint& texOut) {
    const int TW = 704, TH = 1032; const float D = 128;
    Target t; t.create(TW, TH, false);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glViewport(0, 0, TW, TH);
    glClearColor(L.c1.x, L.c1.y, L.c1.z, 1); glClear(GL_COLOR_BUFFER_BIT);
    hud.clear();
    V4 c1 = {L.c1.x, L.c1.y, L.c1.z, 1}, c2 = {L.c2.x, L.c2.y, L.c2.z, 1}, c3 = {L.c3.x, L.c3.y, L.c3.z, 1};
    V4 num = {L.num.x, L.num.y, L.num.z, 1}, blk = {0.05f, 0.05f, 0.06f, 1}, wht = {1, 1, 1, 1};
    char nb[8]; snprintf(nb, sizeof nb, "%d", L.number);

    auto SL = [&](float z, float y) { return V2{(2.6f - z) * D, (1.5f - y) * D}; };
    auto SR = [&](float z, float y) { return V2{(z + 2.6f) * D, 180 + (1.5f - y) * D}; };
    auto TOP = [&](float x, float z) { return V2{(1.1f - x) * D, 360 + (2.6f - z) * D}; };
    auto RE = [&](float x, float y) { return V2{300 + (1.1f - x) * D, 360 + (1.5f - y) * D}; };
    auto FR = [&](float x, float y) { return V2{300 + (x + 1.1f) * D, 560 + (1.5f - y) * D}; };
    auto fitText = [&](const char* s, V2 c, float size, float maxW, V4 col, int mode) {
        float w = font.measure(s) * size;
        if (w > maxW) size *= maxW / w;
        text(s, c.x, c.y + size * 0.36f, size, col, col, 1, mode);
    };

    for (int side = 0; side < 2; side++) {
        auto S = [&](float z, float y) { return side == 0 ? SL(z, y) : SR(z, y); };
        auto band = [&](float (*lo)(float, int), float (*hi)(float, int), V4 col, int pat) {
            const int NSEG = 40;
            for (int i = 0; i < NSEG; i++) {
                float z0 = -2.6f + 5.2f * i / NSEG, z1 = -2.6f + 5.2f * (i + 1) / NSEG;
                float a0 = lo(z0, pat), a1 = lo(z1, pat), b0 = hi(z0, pat), b1 = hi(z1, pat);
                if (b0 <= a0 && b1 <= a1) continue;
                quad2(S(z0, a0), S(z1, a1), S(z1, fmaxf(a1, b1)), S(z0, fmaxf(a0, b0)), col);
            }
        };
        int pat = L.pattern;
        if (pat == 0) {        // sweeping swoosh, rear-low to front-high
            band([](float z, int) { float t = (z + 2.6f) / 5.2f; return 0.30f + 0.30f * t * t; },
                 [](float z, int) { float t = (z + 2.6f) / 5.2f; return 0.52f + 0.42f * t - 0.18f * t * t; }, c2, pat);
            band([](float z, int) { float t = (z + 2.6f) / 5.2f; return 0.52f + 0.42f * t - 0.18f * t * t; },
                 [](float z, int) { float t = (z + 2.6f) / 5.2f; return 0.57f + 0.42f * t - 0.18f * t * t; }, c3, pat);
        } else if (pat == 1) { // two-tone split with pin stripe
            band([](float z, int) { return 0.0f; }, [](float z, int) { return 0.52f + 0.10f * sinf(z * 1.2f); }, c2, pat);
            band([](float z, int) { return 0.52f + 0.10f * sinf(z * 1.2f); }, [](float z, int) { return 0.56f + 0.10f * sinf(z * 1.2f); }, c3, pat);
        } else if (pat == 2) { // racing stripes along the flank + slashes on the nose
            band([](float z, int) { return 0.66f; }, [](float z, int) { return 0.76f; }, c2, pat);
            band([](float z, int) { return 0.79f; }, [](float z, int) { return 0.83f; }, c3, pat);
            for (int k = 0; k < 3; k++) {
                float z0 = 1.2f + k * 0.32f;
                quad2(S(z0, 0.25f), S(z0 + 0.16f, 0.25f), S(z0 + 0.42f, 0.95f), S(z0 + 0.26f, 0.95f), k == 1 ? c3 : c2);
            }
        } else {               // lightning zig-zag
            const float zz[] = {-2.6f, -1.6f, -1.2f, 0.2f, 0.6f, 2.6f};
            const float yy[] = {0.40f, 0.62f, 0.46f, 0.80f, 0.60f, 0.92f};
            for (int k = 0; k < 5; k++) {
                quad2(S(zz[k], yy[k]), S(zz[k + 1], yy[k + 1]), S(zz[k + 1], yy[k + 1] + 0.14f), S(zz[k], yy[k] + 0.14f), c2);
                quad2(S(zz[k], yy[k] + 0.14f), S(zz[k + 1], yy[k + 1] + 0.14f), S(zz[k + 1], yy[k + 1] + 0.18f), S(zz[k], yy[k] + 0.18f), c3);
            }
        }
        // black rocker panel
        quad2(S(-2.6f, 0.0f), S(2.6f, 0.0f), S(2.6f, 0.30f), S(-2.6f, 0.30f), blk);
        // door number on a panel
        V2 nc = S(-0.12f, 0.64f);
        if (pat == 1 || pat == 3) {
            rrect(nc.x - 0.42f * D, nc.y - 0.33f * D, 0.84f * D, 0.66f * D, 12, wht);
            text(nb, nc.x, nc.y + 0.60f * D * 0.36f, 0.60f * D, c1, c1, 1, 1);
        } else {
            text(nb, nc.x, nc.y + 0.66f * D * 0.36f, 0.66f * D, num, num, 1, 3);
        }
        // rear quarter sponsor + front fender team
        fitText(L.sponsor.c_str(), S(-1.62f, 0.62f), 0.24f * D, 1.15f * D, wht, 1);
        fitText(L.team.c_str(), S(1.58f, 0.70f), 0.13f * D, 0.9f * D, wht, 1);
        // contingency decals
        const V4 dc[3] = {{1, 1, 1, 1}, {0.9f, 0.1f, 0.1f, 1}, {1, 0.85f, 0.1f, 1}};
        const char* dt[3] = {"TIRES", "OIL", "GEAR"};
        for (int k = 0; k < 3; k++) {
            V2 a = S(0.70f + k * 0.18f, 0.36f), b = S(0.86f + k * 0.18f, 0.48f);
            float x0 = fminf(a.x, b.x), y0 = fminf(a.y, b.y);
            rect(x0, y0, fabsf(b.x - a.x), fabsf(b.y - a.y), dc[k]);
            text(dt[k], (a.x + b.x) * 0.5f, y0 + fabsf(b.y - a.y) * 0.72f, 7, k == 0 ? blk : wht, k == 0 ? blk : wht, 1, 5);
        }
        // headlight / taillight hints on the flanks
        quad2(S(2.30f, 0.60f), S(2.55f, 0.60f), S(2.55f, 0.66f), S(2.25f, 0.68f), {1, 0.95f, 0.75f, 1});
        quad2(S(-2.55f, 0.78f), S(-2.35f, 0.78f), S(-2.35f, 0.86f), S(-2.55f, 0.86f), {0.9f, 0.08f, 0.06f, 1});
    }
    // ---- top
    {
        if (L.pattern == 0 || L.pattern == 2) {
            for (int sd = -1; sd <= 1; sd += 2) {
                V2 a = TOP(sd * 0.22f, 2.6f), b = TOP(sd * 0.38f, -2.6f);
                rect(fminf(a.x, b.x), fminf(a.y, b.y), fabsf(b.x - a.x), fabsf(b.y - a.y), c2);
            }
        } else {
            V2 a = TOP(1.1f, 2.6f), b = TOP(-1.1f, 1.0f);
            rect(fminf(a.x, b.x), fminf(a.y, b.y), fabsf(b.x - a.x), fabsf(b.y - a.y), c2);
        }
        V2 rn = TOP(0, -0.38f);
        rrect(rn.x - 0.5f * D, rn.y - 0.45f * D, 1.0f * D, 0.9f * D, 14, L.pattern == 1 ? c2 : wht);
        text(nb, rn.x, rn.y + 0.78f * D * 0.36f, 0.78f * D, L.pattern == 1 ? wht : c1, L.pattern == 1 ? wht : c1, 1, 1);
        fitText(L.sponsor.c_str(), TOP(0, 1.75f), 0.30f * D, 1.6f * D, L.pattern == 1 ? c1 : wht, 1);
        fitText(L.team.c_str(), TOP(0, -2.22f), 0.15f * D, 1.5f * D, wht, 1);
    }
    // ---- rear
    {
        V2 a = RE(1.1f, 0.44f), b = RE(-1.1f, 0.10f);
        rect(fminf(a.x, b.x), fminf(a.y, b.y), fabsf(b.x - a.x), fabsf(b.y - a.y), c2);
        for (int sd = -1; sd <= 1; sd += 2) {
            V2 p0 = RE(sd * 0.20f, 0.76f), p1 = RE(sd * 0.80f, 0.90f);
            rect(fminf(p0.x, p1.x) - 2, fminf(p0.y, p1.y) - 2, fabsf(p1.x - p0.x) + 4, fabsf(p1.y - p0.y) + 4, blk);
            rect(fminf(p0.x, p1.x), fminf(p0.y, p1.y), fabsf(p1.x - p0.x), fabsf(p1.y - p0.y), {0.85f, 0.06f, 0.05f, 1});
            rect(fminf(p0.x, p1.x) + 4, fminf(p0.y, p1.y) + 4, fabsf(p1.x - p0.x) - 8, 5, {1, 0.45f, 0.35f, 1});
        }
        fitText(L.sponsor.c_str(), RE(0, 0.60f), 0.32f * D, 1.75f * D, wht, 3);
        fitText(L.team.c_str(), RE(0, 0.32f), 0.10f * D, 1.4f * D, L.pattern == 2 ? wht : blk, 5);
    }
    // ---- front
    {
        V2 a = FR(-0.75f, 0.42f), b = FR(0.75f, 0.20f);
        rect(fminf(a.x, b.x), fminf(a.y, b.y), fabsf(b.x - a.x), fabsf(b.y - a.y), blk);
        for (int sd = -1; sd <= 1; sd += 2) {
            V2 p0 = FR(sd * 0.32f, 0.52f), p1 = FR(sd * 0.84f, 0.52f), p2 = FR(sd * 0.84f, 0.66f), p3 = FR(sd * 0.40f, 0.62f);
            quad2({p0.x, p0.y + 3}, {p1.x, p1.y + 3}, {p2.x, p2.y - 3}, {p3.x, p3.y - 3}, blk);
            quad2(p0, p1, p2, p3, {1.0f, 0.95f, 0.72f, 1});
        }
        fitText(L.sponsor.c_str(), FR(0, 0.74f), 0.16f * D, 1.4f * D, wht, 1);
    }
    flushHud((float)TW, (float)TH, V3(0.03f, 0.03f, 0.04f));
    texOut = t.tex;
    glBindTexture(GL_TEXTURE_2D, texOut);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glDeleteFramebuffers(1, &t.fbo);
}

// ------------------------------------------------------------- init
void Renderer::init(Game& g) {
    if (!font.load(g.readAsset("font.bin"))) ftLog("font load failed");
    glGenTextures(1, &fontTex);
    glBindTexture(GL_TEXTURE_2D, fontTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, font.w, font.h, 0, GL_RED, GL_UNSIGNED_BYTE, font.pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    pWorld.build(kWorldVS, kWorldFS, {"a_pos", "a_nrm", "a_col", "a_aux"});
    pRoad.build(kRoadVS, kRoadFS, {"a_pos", "a_road"});
    pText.build(kTextVS, kTextFS, {"a_pos", "a_uv", "a_col"});
    pSky.build(kFullVS, kSkyFS, {});
    pRain.build(kRainVS, kRainFS, {"a_seed"});
    pPart.build(kPartVS, kPartFS, {"a_pos", "a_uv", "a_col", "a_type"});
    pSkid.build(kSkidVS, kSkidFS, {"a_pos", "a_au"});
    pBlurRefl.build(kFullVS, kReflBlurFS, {});
    pBright.build(kFullVS, kBrightFS, {});
    pBlur.build(kFullVS, kBlurFS, {});
    pComposite.build(kFullVS, kCompositeFS, {});
    pHud.build(kHudVS, kHudFS, {"a_pos", "a_uv", "a_col", "a_mode"});
    pShadow.build(kShadowVS, kShadowFS, {"a_pos"});
    vaoHud = makeVao(vboHud, nullptr, 0, GL_STREAM_DRAW, {{2, 0}, {2, 2}, {4, 4}, {1, 8}}, 9);
    glGenVertexArrays(1, &vaoFull);

    makeTextures();
    buildWorld(g.track, font, geo);
    buildCarMesh(geo.car);
    buildWheelMesh(geo.wheel);
    ftLog("world verts %d, road %d, text %d, car %d, wheel %d", (int)geo.world.v.size(), (int)geo.road.size(), (int)geo.text.size(), (int)geo.car.v.size(), (int)geo.wheel.v.size());

    auto vtxLayout = {std::make_pair(3, 0), std::make_pair(3, 3), std::make_pair(4, 6), std::make_pair(1, 10)};
    vaoWorld = makeVao(vboWorld, geo.world.v.data(), geo.world.v.size() * sizeof(Vtx), GL_STATIC_DRAW, vtxLayout, 11);
    nWorld = (int)geo.world.v.size();
    vaoCar = makeVao(vboCar, geo.car.v.data(), geo.car.v.size() * sizeof(Vtx), GL_STATIC_DRAW, vtxLayout, 11);
    nCar = (int)geo.car.v.size();
    vaoWheel = makeVao(vboWheel, geo.wheel.v.data(), geo.wheel.v.size() * sizeof(Vtx), GL_STATIC_DRAW, vtxLayout, 11);
    nWheel = (int)geo.wheel.v.size();
    vaoRoad = makeVao(vboRoad, geo.road.data(), geo.road.size() * sizeof(RoadVtx), GL_STATIC_DRAW, {{3, 0}, {4, 3}}, 7);
    nRoad = (int)geo.road.size();
    vaoText = makeVao(vboText, geo.text.data(), geo.text.size() * sizeof(TextVtx), GL_STATIC_DRAW, {{3, 0}, {2, 3}, {4, 5}}, 9);
    nText = (int)geo.text.size();
    {
        Rng r(99);
        std::vector<float> rv;
        const int drops = 2600;
        for (int i = 0; i < drops; i++) { float sx = r.f(), sy = r.f(), sz = r.f(); rv.insert(rv.end(), {sx, sy, sz, 0.0f, sx, sy, sz, 1.0f}); }
        nRain = drops * 2;
        vaoRain = makeVao(vboRain, rv.data(), rv.size() * 4, GL_STATIC_DRAW, {{4, 0}}, 4);
    }
    vaoPart = makeVao(vboPart, nullptr, 0, GL_STREAM_DRAW, {{3, 0}, {2, 3}, {4, 5}, {1, 9}}, 10);
    vaoSkid = makeVao(vboSkid, g.skid.data(), g.skid.size() * sizeof(SkidVtx), GL_DYNAMIC_DRAW, {{3, 0}, {2, 3}}, 5);
    {
        float q[] = {-1.35f, 0, -2.9f, 1.35f, 0, -2.9f, 1.35f, 0, 2.9f, -1.35f, 0, -2.9f, 1.35f, 0, 2.9f, -1.35f, 0, 2.9f};
        vaoShadow = makeVao(vboShadow, q, sizeof q, GL_STATIC_DRAW, {{3, 0}}, 3);
    }
    livery.resize(g.cars.size());
    for (size_t i = 0; i < g.cars.size(); i++) paintLivery(g.cars[i].liv, livery[i]);

    GLint maxS = 0; glGetIntegerv(GL_MAX_SAMPLES, &maxS);
    samples = std::min(4, (int)maxS);
    ready = true;
}

void Renderer::resize(int W, int H) {
    if (W == w && H == h) return;
    w = W; h = H;
    refl.create(W / 2, H / 2, true);
    reflBlur.create(W / 2, H / 2, false);
    scene.create(W, H, false);
    bloomA.create(W / 4, H / 4, false); bloomB.create(W / 4, H / 4, false);
    bloomC.create(W / 8, H / 8, false); bloomD.create(W / 8, H / 8, false);
    if (msFbo) { glDeleteFramebuffers(1, &msFbo); glDeleteRenderbuffers(1, &msColor); glDeleteRenderbuffers(1, &msDepth); }
    glGenFramebuffers(1, &msFbo); glBindFramebuffer(GL_FRAMEBUFFER, msFbo);
    glGenRenderbuffers(1, &msColor); glBindRenderbuffer(GL_RENDERBUFFER, msColor);
    if (samples > 1) glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, W, H);
    else glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, W, H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor);
    glGenRenderbuffers(1, &msDepth); glBindRenderbuffer(GL_RENDERBUFFER, msDepth);
    if (samples > 1) glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, W, H);
    else glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, W, H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) ftLog("msaa framebuffer incomplete");
}

// ------------------------------------------------------------- scene
static V3 sunDir(const Game& g) { return g.weather == Weather::Rain ? norm(V3(0.35f, 0.42f, 0.84f)) : norm(V3(-0.45f, 0.62f, 0.64f)); }

void Renderer::drawScene(Game& g, const M4& vp, V3 cam, bool reflection) {
    M4 I = M4::ident();
    float flash = g.flash > 0 ? g.flash * (0.6f + 0.4f * sinf(g.globalTime * 60)) : 0;
    float rain = g.weather == Weather::Rain ? 1.0f : 0.0f;
    V3 sun = sunDir(g);
    float tS0 = 0, tS1 = 0;
    for (auto& z : g.track.zones) if (z.type == Z_TUNNEL) { tS0 = z.s0; tS1 = z.s1; }
    V3 tA = g.track.at(tS0, 0), tB = g.track.at(tS1, 0);
    auto common = [&](Prog& p) {
        p.use();
        glUniformMatrix4fv(p.U("u_vp"), 1, GL_FALSE, vp.m);
        glUniform3f(p.U("u_cam"), cam.x, cam.y, cam.z);
        glUniform3f(p.U("u_sunDir"), sun.x, sun.y, sun.z);
        glUniform1f(p.U("u_flash"), flash);
        glUniform1f(p.U("u_rain"), rain);
        glUniform3f(p.U("u_tunA"), tA.x, tA.y, tA.z);
        glUniform3f(p.U("u_tunB"), tB.x, tB.y, tB.z);
    };

    // sky
    {
        V3 f = norm(g.camLook - g.camPos), r = norm(cross(f, V3(0, 1, 0))), u = cross(r, f);
        if (reflection) { f.y = -f.y; r.y = -r.y; u.y = -u.y; }
        float ty = tanf(g.camFov * PI / 360.0f), tx = ty * (float)w / (float)h;
        glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
        common(pSky);
        glUniform3f(pSky.U("u_fwd"), f.x, f.y, f.z);
        glUniform3f(pSky.U("u_right"), r.x, r.y, r.z);
        glUniform3f(pSky.U("u_up"), u.x, u.y, u.z);
        glUniform2f(pSky.U("u_tan"), tx, ty);
        glUniform1f(pSky.U("u_time"), g.globalTime);
        glBindVertexArray(vaoFull);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
    }

    // static world
    common(pWorld);
    glUniform1f(pWorld.U("u_clip"), reflection ? 1.0f : 0.0f);
    glUniform1f(pWorld.U("u_fade"), 1.0f);
    glUniform1f(pWorld.U("u_time"), g.globalTime);
    glUniformMatrix4fv(pWorld.U("u_model"), 1, GL_FALSE, I.m);
    glActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, texAsphalt);
    glActiveTexture(GL_TEXTURE0 + 2); glBindTexture(GL_TEXTURE_2D, texGrass);
    glActiveTexture(GL_TEXTURE0 + 3); glBindTexture(GL_TEXTURE_2D, texRock);
    glActiveTexture(GL_TEXTURE0 + 4); glBindTexture(GL_TEXTURE_2D, texCrowd);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(pWorld.U("u_livery"), 0);
    glUniform1i(pWorld.U("u_texA"), 1);
    glUniform1i(pWorld.U("u_texG"), 2);
    glUniform1i(pWorld.U("u_texR"), 3);
    glUniform1i(pWorld.U("u_texC"), 4);
    glBindVertexArray(vaoWorld);
    glDrawArrays(GL_TRIANGLES, 0, nWorld);

    if (!reflection) {
        // road
        common(pRoad);
        glUniform1f(pRoad.U("u_time"), g.globalTime);
        glUniform1f(pRoad.U("u_halfW"), g.track.halfW);
        glUniform2f(pRoad.U("u_res"), (float)w, (float)h);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, reflBlur.tex);
        glActiveTexture(GL_TEXTURE0 + 5); glBindTexture(GL_TEXTURE_2D, refl.tex);
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(pRoad.U("u_refl"), 0);
        glUniform1i(pRoad.U("u_reflSharp"), 5);
        glUniform1i(pRoad.U("u_texA"), 1);
        glBindVertexArray(vaoRoad);
        glDrawArrays(GL_TRIANGLES, 0, nRoad);

        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
        // skid marks (ring buffer upload of new segments)
        {
            int segs = (int)(g.skid.size() / 6);
            long long fresh = g.skidWritten - skidUploaded;
            glBindBuffer(GL_ARRAY_BUFFER, vboSkid);
            if (fresh >= segs) glBufferSubData(GL_ARRAY_BUFFER, 0, g.skid.size() * sizeof(SkidVtx), g.skid.data());
            else if (fresh > 0) {
                int start = (int)(((g.skidHead - fresh) % segs + segs) % segs);
                int n1 = (int)std::min<long long>(fresh, segs - start);
                glBufferSubData(GL_ARRAY_BUFFER, start * 6 * sizeof(SkidVtx), n1 * 6 * sizeof(SkidVtx), &g.skid[start * 6]);
                if (fresh > n1) glBufferSubData(GL_ARRAY_BUFFER, 0, (fresh - n1) * 6 * sizeof(SkidVtx), &g.skid[0]);
            }
            skidUploaded = g.skidWritten;
            if (g.skidCount > 0) {
                common(pSkid);
                glBindVertexArray(vaoSkid);
                glDrawArrays(GL_TRIANGLES, 0, g.skidCount * 6);
            }
        }
        // contact shadows
        common(pShadow);
        glBindVertexArray(vaoShadow);
        for (auto& c : g.cars) {
            M4 m = translate(c.pos + V3(-sun.x, 0, -sun.z) * 0.25f) * rotY(c.yaw);
            glUniformMatrix4fv(pShadow.U("u_model"), 1, GL_FALSE, m.m);
            glUniform1f(pShadow.U("u_alpha"), g.track.inTunnel(c.s) ? 0.35f : 0.62f);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
        glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    }

    // cars
    common(pWorld);
    glUniform1f(pWorld.U("u_clip"), reflection ? 1.0f : 0.0f);
    for (size_t ci = 0; ci < g.cars.size(); ci++) {
        const Car& c = g.cars[ci];
        float dcam = len(c.pos - g.camPos);
        if (dcam > 900) continue;
        float fade = 1.0f;
        if (!reflection && (int)ci != g.player && g.state != State::Title) { V3 d = c.pos - g.camPos; d.y = 0; fade = smooth(2.8f, 6.0f, len(d)); }
        if (fade <= 0.02f) continue;
        glUniform1f(pWorld.U("u_fade"), fade);
        M4 m = translate(c.pos + V3(0, 0.02f + c.heave * 0.5f, 0)) * rotY(c.yaw) * rotZ(-c.rollVis) * rotX(c.pitchVis);
        glUniformMatrix4fv(pWorld.U("u_model"), 1, GL_FALSE, m.m);
        glBindTexture(GL_TEXTURE_2D, livery[ci]);
        glBindVertexArray(vaoCar);
        glDrawArrays(GL_TRIANGLES, 0, nCar);
        if (dcam < 300) {
            glUniform3f(pWorld.U("u_rim"), c.liv.wheel.x, c.liv.wheel.y, c.liv.wheel.z);
            M4 base = translate(c.pos + V3(0, 0.0f, 0)) * rotY(c.yaw);
            glBindVertexArray(vaoWheel);
            for (int wi = 0; wi < 4; wi++) {
                V3 wp; float sg; bool front;
                wheelLayout(wi, &wp, &sg, &front);
                M4 wm = base * translate(wp) * rotY(front ? c.steerAngle : 0.0f) * scaleM(V3(sg, 1, 1)) * rotX(c.wheelSpin);
                glUniformMatrix4fv(pWorld.U("u_model"), 1, GL_FALSE, wm.m);
                glDrawArrays(GL_TRIANGLES, 0, nWheel);
            }
        }
    }
    glUniform1f(pWorld.U("u_fade"), 1.0f);

    // sign text
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    common(pText);
    glUniform1f(pText.U("u_clip"), reflection ? 1.0f : 0.0f);
    glUniform1i(pText.U("u_font"), 0);
    glBindTexture(GL_TEXTURE_2D, fontTex);
    glUniformMatrix4fv(pText.U("u_model"), 1, GL_FALSE, I.m);
    glBindVertexArray(vaoText);
    glDrawArrays(GL_TRIANGLES, 0, nText);

    if (!reflection) {
        drawParticles(g, vp, cam);
        if (g.weather == Weather::Rain) {
            glDepthMask(GL_FALSE);
            common(pRain);
            glUniform1f(pRain.U("u_time"), g.globalTime);
            glUniform3f(pRain.U("u_box"), 50, 28, 50);
            V3 rel = V3(1.5f, -24.0f, 0.8f) - g.cars[g.player].vel;
            glUniform3f(pRain.U("u_rel"), rel.x, rel.y, rel.z);
            glBindVertexArray(vaoRain);
            glDrawArrays(GL_LINES, 0, nRain);
            glDepthMask(GL_TRUE);
        }
    }
    glDisable(GL_BLEND);
    (void)common;
}

void Renderer::drawParticles(Game& g, const M4& vp, V3 cam) {
    if (g.parts.empty()) return;
    V3 f = norm(g.camLook - g.camPos), r = norm(cross(f, V3(0, 1, 0))), u = cross(r, f);
    partBuf.clear(); partAdd.clear();
    auto put = [&](std::vector<float>& b, V3 p, float uu, float vv, V3 c, float a, float type) {
        b.insert(b.end(), {p.x, p.y, p.z, uu, vv, c.x, c.y, c.z, a, type});
    };
    for (const Particle& p : g.parts) {
        float lifeK = p.life / p.maxLife;
        if (p.type == P_SPARK) {
            V3 dir = p.v; float sp = len(dir);
            if (sp < 0.1f) continue;
            dir = dir * (1.0f / sp);
            float L = clampf(sp * 0.03f, 0.08f, 0.7f);
            V3 view = norm(p.p - cam);
            V3 side = norm(cross(dir, view)) * 0.035f;
            V3 a = p.p, b = p.p - dir * L;
            float al = sat(lifeK * 2.0f);
            put(partAdd, a - side, 0, -1, p.col, al, 1); put(partAdd, b - side, 1, -1, p.col, al, 1); put(partAdd, b + side, 1, 1, p.col, al, 1);
            put(partAdd, a - side, 0, -1, p.col, al, 1); put(partAdd, b + side, 1, 1, p.col, al, 1); put(partAdd, a + side, 0, 1, p.col, al, 1);
            continue;
        }
        if (p.type == P_CONFETTI) {
            float c = cosf(p.rot), s = sinf(p.rot);
            V3 ax = V3(c, 0.4f * s, s) * p.size, az = V3(-s * 0.6f, 0.3f * c, c * 0.6f) * p.size;
            if (p.p.y <= 0.03f) { ax = V3(c, 0, s) * p.size; az = V3(-s, 0, c) * (p.size * 0.6f); }
            V3 q[4] = {p.p - ax - az, p.p + ax - az, p.p + ax + az, p.p - ax + az};
            int idx[6] = {0, 1, 2, 0, 2, 3};
            for (int k : idx) put(partBuf, q[k], 0, 0, p.col, 1, 4);
            continue;
        }
        float a;
        if (p.type == P_FLAME) a = sat(lifeK * 1.5f);
        else a = p.alpha * sat(lifeK * 1.5f) * sat((1 - lifeK) * 8.0f);
        float c = cosf(p.rot), s = sinf(p.rot);
        V3 R = (r * c + u * s) * p.size, U = (u * c - r * s) * p.size;
        V3 q[4] = {p.p - R - U, p.p + R - U, p.p + R + U, p.p - R + U};
        float uv[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        int idx[6] = {0, 1, 2, 0, 2, 3};
        std::vector<float>& b = p.type == P_FLAME ? partAdd : partBuf;
        for (int k : idx) put(b, q[k], uv[k][0], uv[k][1], p.col, a, p.type == P_FLAME ? 2.0f : 0.0f);
    }
    float rain = g.weather == Weather::Rain ? 1.0f : 0.0f;
    V3 sun = sunDir(g);
    pPart.use();
    glUniformMatrix4fv(pPart.U("u_vp"), 1, GL_FALSE, vp.m);
    glUniform3f(pPart.U("u_cam"), cam.x, cam.y, cam.z);
    glUniform3f(pPart.U("u_sunDir"), sun.x, sun.y, sun.z);
    glUniform1f(pPart.U("u_rain"), rain);
    glUniform1f(pPart.U("u_flash"), g.flash);
    glDepthMask(GL_FALSE);
    glBindVertexArray(vaoPart);
    glBindBuffer(GL_ARRAY_BUFFER, vboPart);
    if (!partBuf.empty()) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBufferData(GL_ARRAY_BUFFER, partBuf.size() * 4, partBuf.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (int)partBuf.size() / 10);
    }
    if (!partAdd.empty()) {
        glBlendFunc(GL_ONE, GL_ONE);
        glBufferData(GL_ARRAY_BUFFER, partAdd.size() * 4, partAdd.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (int)partAdd.size() / 10);
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
}

static void fullscreen(GLuint vao) { glBindVertexArray(vao); glDrawArrays(GL_TRIANGLES, 0, 3); }

void Renderer::draw(Game& g, GLuint target, int outW, int outH) {
    int W = (int)(outW * g.renderScale), H = (int)(outH * g.renderScale);
    resize(W, H);
    float aspect = (float)W / (float)H;
    M4 proj = perspective(g.camFov * PI / 180.0f, aspect, 0.3f, 4200.0f);
    M4 view = lookAt(g.camPos, g.camLook, V3(0, 1, 0));
    M4 vp = proj * view;
    bool rain = g.weather == Weather::Rain;
    glDisable(GL_CULL_FACE);

    if (rain) {
        glBindFramebuffer(GL_FRAMEBUFFER, refl.fbo);
        glViewport(0, 0, refl.w, refl.h);
        glClearColor(0.3f, 0.32f, 0.35f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        M4 vpR = proj * view * scaleM(V3(1, -1, 1));
        V3 camR(g.camPos.x, -g.camPos.y, g.camPos.z);
        drawScene(g, vpR, camR, true);
        glDisable(GL_DEPTH_TEST);
        glBindFramebuffer(GL_FRAMEBUFFER, reflBlur.fbo);
        glViewport(0, 0, reflBlur.w, reflBlur.h);
        pBlurRefl.use();
        glBindTexture(GL_TEXTURE_2D, refl.tex);
        glUniform1i(pBlurRefl.U("u_tex"), 0);
        glUniform2f(pBlurRefl.U("u_texel"), 1.0f / refl.w, 1.0f / refl.h);
        fullscreen(vaoFull);
        glEnable(GL_DEPTH_TEST);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, msFbo);
    glViewport(0, 0, W, H);
    glClearColor(0.5f, 0.7f, 0.9f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDepthFunc(GL_LEQUAL);
    drawScene(g, vp, g.camPos, false);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, scene.fbo);
    glBlitFramebuffer(0, 0, W, H, 0, 0, W, H, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glDisable(GL_DEPTH_TEST);
    auto pass = [&](Target& dst, Prog& p, GLuint src) {
        glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
        glViewport(0, 0, dst.w, dst.h);
        p.use();
        glBindTexture(GL_TEXTURE_2D, src);
        glUniform1i(p.U("u_tex"), 0);
        fullscreen(vaoFull);
    };
    pBright.use(); glUniform2f(pBright.U("u_texel"), 1.0f / W, 1.0f / H); glUniform1f(pBright.U("u_thresh"), rain ? 0.62f : 0.80f);
    pass(bloomA, pBright, scene.tex);
    pBlur.use(); glUniform2f(pBlur.U("u_dir"), 1.0f / bloomA.w, 0); pass(bloomB, pBlur, bloomA.tex);
    pBlur.use(); glUniform2f(pBlur.U("u_dir"), 0, 1.0f / bloomA.h); pass(bloomA, pBlur, bloomB.tex);
    pBlur.use(); glUniform2f(pBlur.U("u_dir"), 0, 0); pass(bloomC, pBlur, bloomA.tex);
    pBlur.use(); glUniform2f(pBlur.U("u_dir"), 1.5f / bloomC.w, 0); pass(bloomD, pBlur, bloomC.tex);
    pBlur.use(); glUniform2f(pBlur.U("u_dir"), 0, 1.5f / bloomC.h); pass(bloomC, pBlur, bloomD.tex);

    glBindFramebuffer(GL_FRAMEBUFFER, target);
    glViewport(0, 0, outW, outH);
    pComposite.use();
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, scene.tex);
    glActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, bloomA.tex);
    glActiveTexture(GL_TEXTURE0 + 2); glBindTexture(GL_TEXTURE_2D, bloomC.tex);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(pComposite.U("u_scene"), 0);
    glUniform1i(pComposite.U("u_bloomA"), 1);
    glUniform1i(pComposite.U("u_bloomB"), 2);
    glUniform2f(pComposite.U("u_res"), (float)outW, (float)outH);
    glUniform1f(pComposite.U("u_time"), g.globalTime);
    glUniform1f(pComposite.U("u_flashC"), g.flash * 0.5f);
    glUniform1f(pComposite.U("u_bloom"), rain ? 1.0f : 0.45f);
    glUniform1f(pComposite.U("u_sat"), rain ? 1.10f : 1.16f);
    float spd = g.cars[g.player].speed();
    glUniform1f(pComposite.U("u_speedFx"), g.state == State::Title ? 0.0f : clampf((spd - 55.0f) / 30.0f, 0, 1) + (g.cars[g.player].boosting ? 0.6f : 0.0f));
    fullscreen(vaoFull);

    drawHud(g);
    glEnable(GL_DEPTH_TEST);
}
