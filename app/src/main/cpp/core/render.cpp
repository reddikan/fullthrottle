// Renderer: reflection pass, main pass (MSAA), bloom, composite, HUD.
#include "render.h"
#include "shaders.h"
#include <string.h>
#include <stdio.h>
#include <algorithm>

void ftLog(const char* fmt, ...);

// ------------------------------------------------------------- GL helpers
static GLuint compile(GLenum type, const std::string& src) {
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096]; glGetShaderInfoLog(s, sizeof log, nullptr, log);
        ftLog("shader compile failed: %s", log);
    }
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
                      std::initializer_list<std::pair<int, int>> layout /* (components, offsetFloats) */, int strideFloats) {
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

    buildWorld(g.track, font, geo);
    buildCars(font, g.cars, geo);
    ftLog("world verts %d, road %d, text %d, car %d", (int)geo.world.v.size(), (int)geo.road.size(), (int)geo.text.size(), (int)geo.car.v.size());

    auto vtxLayout = {std::make_pair(3, 0), std::make_pair(3, 3), std::make_pair(4, 6), std::make_pair(1, 10)};
    vaoWorld = makeVao(vboWorld, geo.world.v.data(), geo.world.v.size() * sizeof(Vtx), GL_STATIC_DRAW, vtxLayout, 11);
    nWorld = (int)geo.world.v.size();
    vaoCar = makeVao(vboCar, geo.car.v.data(), geo.car.v.size() * sizeof(Vtx), GL_STATIC_DRAW, vtxLayout, 11);
    nCar = (int)geo.car.v.size();
    vaoRoad = makeVao(vboRoad, geo.road.data(), geo.road.size() * sizeof(RoadVtx), GL_STATIC_DRAW, {{3, 0}, {3, 3}}, 6);
    nRoad = (int)geo.road.size();
    auto textLayout = {std::make_pair(3, 0), std::make_pair(2, 3), std::make_pair(4, 5)};
    vaoText = makeVao(vboText, geo.text.data(), geo.text.size() * sizeof(TextVtx), GL_STATIC_DRAW, textLayout, 9);
    nText = (int)geo.text.size();
    vaoCarText = makeVao(vboCarText, geo.carText.data(), geo.carText.size() * sizeof(TextVtx), GL_STATIC_DRAW, textLayout, 9);
    // rain: line segments, each with two vertices sharing a seed
    {
        Rng r(99);
        std::vector<float> rv;
        const int drops = 2600;
        for (int i = 0; i < drops; i++) {
            float sx = r.f(), sy = r.f(), sz = r.f();
            rv.insert(rv.end(), {sx, sy, sz, 0.0f, sx, sy, sz, 1.0f});
        }
        nRain = drops * 2;
        vaoRain = makeVao(vboRain, rv.data(), rv.size() * 4, GL_STATIC_DRAW, {{4, 0}}, 4);
    }
    vaoSpray = makeVao(vboSpray, nullptr, 0, GL_STREAM_DRAW, {{3, 0}, {3, 3}}, 6);
    vaoHud = makeVao(vboHud, nullptr, 0, GL_STREAM_DRAW, {{2, 0}, {2, 2}, {4, 4}, {1, 8}}, 9);
    {
        float q[] = {-1.25f, 0, -2.8f, 1.25f, 0, -2.8f, 1.25f, 0, 2.8f, -1.25f, 0, -2.8f, 1.25f, 0, 2.8f, -1.25f, 0, 2.8f};
        vaoShadow = makeVao(vboShadow, q, sizeof q, GL_STATIC_DRAW, {{3, 0}}, 3);
    }
    glGenVertexArrays(1, &vaoFull);

    pWorld.build(kWorldVS, kWorldFS, {"a_pos", "a_nrm", "a_col", "a_aux"});
    pRoad.build(kRoadVS, kRoadFS, {"a_pos", "a_road"});
    pText.build(kTextVS, kTextFS, {"a_pos", "a_uv", "a_col"});
    pSky.build(kFullVS, kSkyFS, {});
    pRain.build(kRainVS, kRainFS, {"a_seed"});
    pSpray.build(kSprayVS, kSprayFS, {"a_pos", "a_uva"});
    pBlurRefl.build(kFullVS, kReflBlurFS, {});
    pBright.build(kFullVS, kBrightFS, {});
    pBlur.build(kFullVS, kBlurFS, {});
    pComposite.build(kFullVS, kCompositeFS, {});
    pHud.build(kHudVS, kHudFS, {"a_pos", "a_uv", "a_col", "a_mode"});
    pShadow.build(kShadowVS, kShadowFS, {"a_pos"});

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
static const V3 kSun = norm(V3(0.35f, 0.42f, 0.84f));
static const float kFog = 0.0017f;

static void setCommon(Prog& p, V3 cam, float flash, const M4& vp) {
    p.use();
    glUniformMatrix4fv(p.U("u_vp"), 1, GL_FALSE, vp.m);
    glUniform3f(p.U("u_cam"), cam.x, cam.y, cam.z);
    glUniform3f(p.U("u_sunDir"), kSun.x, kSun.y, kSun.z);
    glUniform1f(p.U("u_flash"), flash);
    glUniform1f(p.U("u_fogD"), kFog);
}

static float carFade(const Game& g, int i) {
    if (i == g.player || g.state == State::Title) return 1.0f;
    V3 d = g.cars[i].pos - g.camPos; d.y = 0;
    return smooth(2.8f, 6.5f, len(d));
}
static M4 carModel(const Car& c) {
    float roll = clampf(c.yawRate * c.speed() * 0.0035f, -0.06f, 0.06f);
    float pitch = clampf(-c.throttle * 0.012f + c.brake * 0.02f, -0.03f, 0.03f);
    return translate(c.pos + V3(0, 0.02f + c.bump * 0.05f, 0)) * rotY(c.yaw) * rotZ(-roll) * rotX(pitch);
}

void Renderer::drawScene(Game& g, const M4& vp, V3 cam, bool reflection) {
    M4 I = M4::ident();
    float flash = g.flash > 0 ? g.flash * (0.6f + 0.4f * sinf(g.globalTime * 60)) : 0;

    // sky
    {
        V3 f = norm(g.camLook - g.camPos), r = norm(cross(f, V3(0, 1, 0))), u = cross(r, f);
        if (reflection) { f.y = -f.y; r.y = -r.y; u.y = -u.y; }
        float ty = tanf(g.camFov * PI / 360.0f), tx = ty * (float)w / (float)h;
        glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
        pSky.use();
        glUniform3f(pSky.U("u_fwd"), f.x, f.y, f.z);
        glUniform3f(pSky.U("u_right"), r.x, r.y, r.z);
        glUniform3f(pSky.U("u_up"), u.x, u.y, u.z);
        glUniform2f(pSky.U("u_tan"), tx, ty);
        glUniform1f(pSky.U("u_time"), g.globalTime);
        glUniform3f(pSky.U("u_sunDir"), kSun.x, kSun.y, kSun.z);
        glUniform1f(pSky.U("u_flash"), flash);
        glBindVertexArray(vaoFull);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
    }

    // static world
    setCommon(pWorld, cam, flash, vp);
    glUniform1f(pWorld.U("u_clip"), reflection ? 1.0f : 0.0f);
    glUniform1f(pWorld.U("u_fade"), 1.0f);
    glUniformMatrix4fv(pWorld.U("u_model"), 1, GL_FALSE, I.m);
    glBindVertexArray(vaoWorld);
    glDrawArrays(GL_TRIANGLES, 0, nWorld);

    // road
    if (!reflection) {
        setCommon(pRoad, cam, flash, vp);
        glUniform1f(pRoad.U("u_time"), g.globalTime);
        glUniform1f(pRoad.U("u_halfW"), g.track.halfW);
        glUniform1f(pRoad.U("u_trackLen"), g.track.length);
        glUniform2f(pRoad.U("u_res"), (float)w, (float)h);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, reflBlur.tex);
        glActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, refl.tex);
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(pRoad.U("u_refl"), 0);
        glUniform1i(pRoad.U("u_reflSharp"), 1);
        glBindVertexArray(vaoRoad);
        glDrawArrays(GL_TRIANGLES, 0, nRoad);

        // car contact shadows
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
        pShadow.use();
        glUniformMatrix4fv(pShadow.U("u_vp"), 1, GL_FALSE, vp.m);
        glBindVertexArray(vaoShadow);
        for (auto& c : g.cars) {
            M4 m = translate(c.pos) * rotY(c.yaw);
            glUniformMatrix4fv(pShadow.U("u_model"), 1, GL_FALSE, m.m);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
        glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    }

    // cars
    setCommon(pWorld, cam, flash, vp);
    glUniform1f(pWorld.U("u_clip"), reflection ? 1.0f : 0.0f);
    glBindVertexArray(vaoCar);
    for (size_t ci = 0; ci < g.cars.size(); ci++) {
        const Car& c = g.cars[ci];
        if (len(c.pos - g.camPos) > 900) continue;
        float fade = reflection ? 1.0f : carFade(g, (int)ci);
        if (fade <= 0.02f) continue;
        glUniform1f(pWorld.U("u_fade"), fade);
        M4 m = carModel(c);
        glUniformMatrix4fv(pWorld.U("u_model"), 1, GL_FALSE, m.m);
        glUniform3f(pWorld.U("u_p1"), c.liv.c1.x, c.liv.c1.y, c.liv.c1.z);
        glUniform3f(pWorld.U("u_p2"), c.liv.c2.x, c.liv.c2.y, c.liv.c2.z);
        glUniform3f(pWorld.U("u_p3"), c.liv.c3.x, c.liv.c3.y, c.liv.c3.z);
        glDrawArrays(GL_TRIANGLES, 0, nCar);
    }
    glUniform1f(pWorld.U("u_fade"), 1.0f);

    // text: signs and decals
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    setCommon(pText, cam, flash, vp);
    glUniform1f(pText.U("u_clip"), reflection ? 1.0f : 0.0f);
    glUniform1i(pText.U("u_font"), 0);
    glBindTexture(GL_TEXTURE_2D, fontTex);
    glUniform1f(pText.U("u_outline"), 0.0f);
    glUniform1f(pText.U("u_fade"), 1.0f);
    glUniformMatrix4fv(pText.U("u_model"), 1, GL_FALSE, I.m);
    glBindVertexArray(vaoText);
    glDrawArrays(GL_TRIANGLES, 0, nText);
    glUniform1f(pText.U("u_outline"), 0.09f);
    glBindVertexArray(vaoCarText);
    for (size_t i = 0; i < g.cars.size(); i++) {
        const Car& c = g.cars[i];
        if (len(c.pos - g.camPos) > 250) continue;
        float fade = reflection ? 1.0f : carFade(g, (int)i);
        if (fade <= 0.02f) continue;
        glUniform1f(pText.U("u_fade"), fade);
        M4 m = carModel(c);
        glUniformMatrix4fv(pText.U("u_model"), 1, GL_FALSE, m.m);
        glDrawArrays(GL_TRIANGLES, geo.carTextStart[i], geo.carTextCount[i]);
    }

    if (!reflection) {
        // spray
        glDepthMask(GL_FALSE);
        if (!g.spray.empty()) {
            V3 f = norm(g.camLook - g.camPos), r = norm(cross(f, V3(0, 1, 0))), u = cross(r, f);
            sprayBuf.clear();
            for (const Particle& p : g.spray) {
                float a = p.alpha * sat(p.life / p.maxLife * 1.5f) * sat((p.maxLife - p.life) * 8.0f);
                V3 R = r * p.size, U = u * p.size;
                V3 c[4] = {p.p - R - U, p.p + R - U, p.p + R + U, p.p - R + U};
                float uv[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
                int idx[6] = {0, 1, 2, 0, 2, 3};
                for (int k : idx) sprayBuf.insert(sprayBuf.end(), {c[k].x, c[k].y, c[k].z, uv[k][0], uv[k][1], a});
            }
            setCommon(pSpray, cam, flash, vp);
            glUniform3f(pSpray.U("u_tint"), 0.66f, 0.69f, 0.72f);
            glBindVertexArray(vaoSpray);
            glBindBuffer(GL_ARRAY_BUFFER, vboSpray);
            glBufferData(GL_ARRAY_BUFFER, sprayBuf.size() * 4, sprayBuf.data(), GL_STREAM_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, (int)sprayBuf.size() / 6);
        }
        // rain
        setCommon(pRain, cam, flash, vp);
        glUniform1f(pRain.U("u_time"), g.globalTime);
        glUniform3f(pRain.U("u_box"), 50, 28, 50);
        const Car& pc = g.cars[g.player];
        V3 rel = V3(1.5f, -24.0f, 0.8f) - pc.vel;
        glUniform3f(pRain.U("u_rel"), rel.x, rel.y, rel.z);
        glBindVertexArray(vaoRain);
        glDrawArrays(GL_LINES, 0, nRain);
        glDepthMask(GL_TRUE);
    }
    glDisable(GL_BLEND);
}

static void fullscreen(Prog& p, GLuint vao) { (void)p; glBindVertexArray(vao); glDrawArrays(GL_TRIANGLES, 0, 3); }

void Renderer::draw(Game& g, GLuint target, int outW, int outH) {
    int W = (int)(outW * g.renderScale), H = (int)(outH * g.renderScale);
    resize(W, H);
    float aspect = (float)W / (float)H;
    M4 proj = perspective(g.camFov * PI / 180.0f, aspect, 0.3f, 3200.0f);
    M4 view = lookAt(g.camPos, g.camLook, V3(0, 1, 0));
    M4 vp = proj * view;
    glDisable(GL_CULL_FACE);

    // 1. reflection
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
    fullscreen(pBlurRefl, vaoFull);
    glEnable(GL_DEPTH_TEST);

    // 2. main scene (MSAA)
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo);
    glViewport(0, 0, W, H);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDepthFunc(GL_LEQUAL);
    drawScene(g, vp, g.camPos, false);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, scene.fbo);
    glBlitFramebuffer(0, 0, W, H, 0, 0, W, H, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    // 3. bloom
    glDisable(GL_DEPTH_TEST);
    auto pass = [&](Target& dst, Prog& p, GLuint src) {
        glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
        glViewport(0, 0, dst.w, dst.h);
        p.use();
        glBindTexture(GL_TEXTURE_2D, src);
        glUniform1i(p.U("u_tex"), 0);
        fullscreen(p, vaoFull);
    };
    pBright.use(); glUniform2f(pBright.U("u_texel"), 1.0f / W, 1.0f / H);
    pass(bloomA, pBright, scene.tex);
    pBlur.use(); glUniform2f(pBlur.U("u_dir"), 1.0f / bloomA.w, 0); pass(bloomB, pBlur, bloomA.tex);
    pBlur.use(); glUniform2f(pBlur.U("u_dir"), 0, 1.0f / bloomA.h); pass(bloomA, pBlur, bloomB.tex);
    pBlur.use(); glUniform2f(pBlur.U("u_dir"), 0, 0); pass(bloomC, pBlur, bloomA.tex);
    pBlur.use(); glUniform2f(pBlur.U("u_dir"), 1.5f / bloomC.w, 0); pass(bloomD, pBlur, bloomC.tex);
    pBlur.use(); glUniform2f(pBlur.U("u_dir"), 0, 1.5f / bloomC.h); pass(bloomC, pBlur, bloomD.tex);

    // 4. composite
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
    glUniform2f(pComposite.U("u_res"), (float)W, (float)H);
    glUniform1f(pComposite.U("u_time"), g.globalTime);
    glUniform1f(pComposite.U("u_flashC"), g.flash * 0.5f);
    fullscreen(pComposite, vaoFull);

    // 5. HUD
    drawHud(g);
    glEnable(GL_DEPTH_TEST);
}

// ------------------------------------------------------------- HUD
static float VH = 1080, VW = 1920;

void Renderer::hudRect(float x, float y, float rw, float rh, V4 c) {
    HudVtx a = {x, y, 0, 0, c.x, c.y, c.z, c.w, 0}, b = {x + rw, y, 0, 0, c.x, c.y, c.z, c.w, 0};
    HudVtx cc = {x + rw, y + rh, 0, 0, c.x, c.y, c.z, c.w, 0}, d = {x, y + rh, 0, 0, c.x, c.y, c.z, c.w, 0};
    hud.insert(hud.end(), {a, b, cc, a, cc, d});
}
static void hudQuadUV(std::vector<HudVtx>& hud, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, V4 c, float mode) {
    HudVtx a = {x0, y0, u0, v0, c.x, c.y, c.z, c.w, mode}, b = {x1, y0, u1, v0, c.x, c.y, c.z, c.w, mode};
    HudVtx cc = {x1, y1, u1, v1, c.x, c.y, c.z, c.w, mode}, d = {x0, y1, u0, v1, c.x, c.y, c.z, c.w, mode};
    hud.insert(hud.end(), {a, b, cc, a, cc, d});
}
void Renderer::hudLine(float x0, float y0, float x1, float y1, float th, V4 c) {
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 1e-3f) return;
    float nx = -dy / l * th * 0.5f, ny = dx / l * th * 0.5f;
    HudVtx a = {x0 + nx, y0 + ny, 0, 0, c.x, c.y, c.z, c.w, 0}, b = {x1 + nx, y1 + ny, 0, 0, c.x, c.y, c.z, c.w, 0};
    HudVtx cc = {x1 - nx, y1 - ny, 0, 0, c.x, c.y, c.z, c.w, 0}, d = {x0 - nx, y0 - ny, 0, 0, c.x, c.y, c.z, c.w, 0};
    hud.insert(hud.end(), {a, b, cc, a, cc, d});
}
float Renderer::hudText(const char* s, float x, float y, float size, V4 c, int align, float tracking) {
    float wdt = (font.measure(s) + tracking * (strlen(s) > 0 ? strlen(s) - 1 : 0)) * size;
    float x0 = align == 1 ? x - wdt * 0.5f : (align == 2 ? x - wdt : x);
    font.layout(s, [&](float ax, float ay, float bx, float by, float u0, float v0, float u1, float v1) {
        hudQuadUV(hud, x0 + ax * size, y - by * size, x0 + bx * size, y - ay * size, u0, v1, u1, v0, c, 1);
    }, tracking);
    return wdt;
}

static void fmtTime(char* buf, size_t n, float t) {
    if (t < 0) t = 0;
    int m = (int)(t / 60); float s = t - m * 60;
    snprintf(buf, n, "%d:%05.2f", m, s);
}

void Renderer::drawHud(Game& g) {
    hud.clear();
    if (g.hideHud) return;
    VH = 1080; VW = 1080.0f * (float)w / (float)h;
    const V4 white = {1, 1, 1, 1}, yellow = {1.0f, 0.82f, 0.15f, 1}, panel = {0.02f, 0.03f, 0.05f, 0.55f};
    const Car& pc = g.cars[g.player];
    char buf[64];
    float L = g.track.length;

    if (g.state == State::Title) {
        float t = g.stateTime;
        float a = sat(t * 1.5f);
        hudRect(0, VH * 0.62f, VW, 170, {0, 0, 0, 0.45f * a});
        hudText("FULL THROTTLE", VW * 0.5f, VH * 0.62f + 105, 110, {1, 1, 1, a}, 1, 0.02f);
        hudText("RAIN DAY  -  LOOK TEST", VW * 0.5f, VH * 0.62f + 150, 30, {1.0f, 0.82f, 0.15f, a}, 1, 0.12f);
        float blink = 0.5f + 0.5f * sinf(g.globalTime * 4);
        hudText("PRESS A TO RACE", VW * 0.5f, VH - 90, 34, {1, 1, 1, a * (0.4f + 0.6f * blink)}, 1, 0.08f);
    } else {
        // standings (left)
        std::vector<int> ord(g.cars.size());
        for (size_t i = 0; i < g.cars.size(); i++) ord[g.cars[i].place - 1] = (int)i;
        float x = 28, y = 30;
        hudRect(x, y, 300, 92, panel);
        snprintf(buf, sizeof buf, "%d", pc.place);
        float pw = hudText(buf, x + 18, y + 80, 86, yellow);
        snprintf(buf, sizeof buf, "/%d", (int)g.cars.size());
        hudText(buf, x + 24 + pw, y + 78, 40, white);
        int lap = std::max(1, std::min(g.laps, pc.lap(L)));
        snprintf(buf, sizeof buf, "LAP %d/%d", lap, g.laps);
        hudText(buf, x + 282, y + 40, 30, white, 2);
        fmtTime(buf, sizeof buf, g.raceTime);
        hudText(buf, x + 282, y + 78, 26, {0.8f, 0.85f, 0.9f, 1}, 2);
        y += 104;
        for (size_t i = 0; i < ord.size(); i++) {
            const Car& c = g.cars[ord[i]];
            bool me = ord[i] == g.player;
            hudRect(x, y, 230, 34, me ? V4{1.0f, 0.82f, 0.15f, 0.85f} : V4{0.02f, 0.03f, 0.05f, 0.5f});
            V4 tc = me ? V4{0.05f, 0.05f, 0.05f, 1} : white;
            snprintf(buf, sizeof buf, "%d", (int)i + 1);
            hudText(buf, x + 30, y + 26, 24, tc, 2);
            hudRect(x + 40, y + 6, 8, 22, {c.liv.c1.x, c.liv.c1.y, c.liv.c1.z, 1});
            hudRect(x + 48, y + 6, 4, 22, {c.liv.c2.x, c.liv.c2.y, c.liv.c2.z, 1});
            hudText(c.liv.tag.c_str(), x + 62, y + 26, 24, tc);
            if (i > 0 && !me) {
                const Car& lead = g.cars[ord[0]];
                float gap = (lead.dist - c.dist) / fmaxf(20.0f, lead.speed());
                snprintf(buf, sizeof buf, "+%.1f", gap);
                hudText(buf, x + 220, y + 25, 20, {0.75f, 0.8f, 0.85f, 1}, 2);
            }
            y += 38;
        }

        // minimap (bottom-left)
        {
            float mx = 28, my = VH - 250, mw = 230, mh = 220;
            hudRect(mx, my, mw, mh, panel);
            V2 mn = g.rnd->geo.mapMin, mxp = g.rnd->geo.mapMax;
            float sc = fminf((mw - 40) / (mxp.x - mn.x), (mh - 40) / (mxp.y - mn.y));
            float ox = mx + mw * 0.5f - (mn.x + mxp.x) * 0.5f * sc, oy = my + mh * 0.5f + (mn.y + mxp.y) * 0.5f * sc;
            auto& mm = g.rnd->geo.minimap;
            for (size_t i = 0; i < mm.size(); i++) {
                V2 a = mm[i], b = mm[(i + 1) % mm.size()];
                hudLine(ox + a.x * sc, oy - a.y * sc, ox + b.x * sc, oy - b.y * sc, 7, {0.85f, 0.88f, 0.92f, 0.9f});
            }
            for (size_t i = 0; i < g.cars.size(); i++) {
                const Car& c = g.cars[i];
                float px = ox + c.pos.x * sc, py = oy - c.pos.z * sc;
                float r = (int)i == g.player ? 9.0f : 6.5f;
                V4 col = (int)i == g.player ? yellow : V4{c.liv.c1.x, c.liv.c1.y, c.liv.c1.z, 1};
                hudQuadUV(hud, px - r - 2, py - r - 2, px + r + 2, py + r + 2, 0, 0, 1, 1, {0, 0, 0, 0.8f}, 2);
                hudQuadUV(hud, px - r, py - r, px + r, py + r, 0, 0, 1, 1, col, 2);
            }
        }

        // speed + boost (bottom-right)
        {
            float mph = pc.speed() * 2.23694f;
            float bx = VW - 330, by = VH - 170;
            hudRect(bx, by, 300, 140, panel);
            snprintf(buf, sizeof buf, "%d", (int)(mph + 0.5f));
            hudText(buf, bx + 210, by + 92, 84, white, 2);
            hudText("MPH", bx + 218, by + 90, 28, {0.8f, 0.85f, 0.9f, 1});
            int segs = 14;
            float sw = 262.0f / segs;
            for (int i = 0; i < segs; i++) {
                float f = (i + 0.5f) / segs;
                bool on = f <= pc.boost;
                V4 c = on ? (f < 0.5f ? V4{0.2f, 0.9f, 0.35f, 1} : (f < 0.8f ? V4{0.95f, 0.85f, 0.15f, 1} : V4{1.0f, 0.35f, 0.15f, 1}))
                          : V4{1, 1, 1, 0.15f};
                if (pc.boosting && on) c = {0.35f, 0.85f, 1.0f, 1};
                hudRect(bx + 19 + i * sw, by + 108, sw - 4, 16, c);
            }
            hudText("BOOST", bx + 19, by + 102, 18, {0.8f, 0.85f, 0.9f, 1}, 0, 0.1f);
        }

        // centre messages
        if (g.state == State::Countdown) {
            int n = 3 - (int)g.stateTime;
            float f = g.stateTime - (int)g.stateTime;
            snprintf(buf, sizeof buf, "%d", n);
            float s = 220 * (1.25f - 0.25f * f);
            hudText(buf, VW * 0.5f, VH * 0.42f + s * 0.35f, s, {1, 0.82f, 0.15f, 1 - f * 0.6f}, 1);
        }
        if (g.bannerTime > 0 && !g.banner.empty()) {
            float a = sat(g.bannerTime * 2);
            hudText(g.banner.c_str(), VW * 0.5f, VH * 0.36f, 110, {1, 0.82f, 0.15f, a}, 1, 0.03f);
        }
        if (g.state == State::Finished && g.stateTime > 1.5f) {
            float a = sat((g.stateTime - 1.5f) * 2);
            hudRect(VW * 0.5f - 330, VH * 0.5f - 60, 660, 210, {0, 0, 0, 0.6f * a});
            snprintf(buf, sizeof buf, "YOU FINISHED P%d", pc.place);
            hudText(buf, VW * 0.5f, VH * 0.5f + 30, 64, {1, 0.82f, 0.15f, a}, 1);
            fmtTime(buf, sizeof buf, pc.finishTime);
            hudText(buf, VW * 0.5f, VH * 0.5f + 80, 34, {1, 1, 1, a}, 1);
            hudText("PRESS A TO RACE AGAIN", VW * 0.5f, VH * 0.5f + 125, 26, {1, 1, 1, a * 0.8f}, 1, 0.08f);
        }
        if (g.state == State::Paused) {
            hudRect(0, 0, VW, VH, {0, 0, 0, 0.5f});
            hudText("PAUSED", VW * 0.5f, VH * 0.45f, 110, white, 1);
            hudText("START: RESUME     SELECT: RESTART", VW * 0.5f, VH * 0.45f + 70, 30, {1, 1, 1, 0.8f}, 1, 0.05f);
        }
    }

    if (hud.empty()) return;
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    pHud.use();
    glUniform2f(pHud.U("u_size"), VW, VH);
    glUniform1i(pHud.U("u_font"), 0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, fontTex);
    glBindVertexArray(vaoHud);
    glBindBuffer(GL_ARRAY_BUFFER, vboHud);
    glBufferData(GL_ARRAY_BUFFER, hud.size() * sizeof(HudVtx), hud.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (int)hud.size());
    glDisable(GL_BLEND);
}
