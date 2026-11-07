// Headless preview renderer: runs the real game code on Mesa (EGL surfaceless),
// drives the player car with the autopilot and writes frames/audio for a video.
// Usage: preview W H fps seconds outPrefix [firstFrameTime] [stillsOnly]
#include "../app/src/main/cpp/core/render.h"
#include <EGL_min.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

void ftLog(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr);
}

static std::vector<uint8_t> readFile(const char* name) {
    std::string p = std::string(ASSET_DIR) + "/" + name;
    FILE* f = fopen(p.c_str(), "rb");
    std::vector<uint8_t> d;
    if (!f) { ftLog("missing asset %s", p.c_str()); return d; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    d.resize(n); fread(d.data(), 1, n, f); fclose(f);
    return d;
}

int main(int argc, char** argv) {
    int W = argc > 1 ? atoi(argv[1]) : 1280, H = argc > 2 ? atoi(argv[2]) : 720;
    int fps = argc > 3 ? atoi(argv[3]) : 30;
    float secs = argc > 4 ? atof(argv[4]) : 10;
    const char* out = argc > 5 ? argv[5] : "out";
    float first = argc > 6 ? atof(argv[6]) : 0;
    int stills = argc > 7 ? atoi(argv[7]) : 0;

    EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    EGLint maj, min;
    if (!eglInitialize(dpy, &maj, &min)) { ftLog("eglInitialize failed"); return 1; }
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
    EGLConfig cfg; EGLint ncfg = 0;
    eglChooseConfig(dpy, cfgAttr, &cfg, 1, &ncfg);
    EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, ncfg ? cfg : nullptr, EGL_NO_CONTEXT, ctxAttr);
    if (!ctx || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) { ftLog("context failed"); return 1; }
    ftLog("GL: %s / %s", glGetString(GL_RENDERER), glGetString(GL_VERSION));

    Game game;
    game.readAsset = readFile;
    game.autopilot = true;
    game.init();
    if (getenv("NOHUD")) game.hideHud = true;
    if (getenv("RAIN")) game.weather = Weather::Rain;

    Target fin; fin.create(W, H, false);
    std::vector<uint8_t> px(W * H * 4);
    std::string audioPath = std::string(out) + ".f32";
    FILE* af = stills ? nullptr : fopen(audioPath.c_str(), "wb");
    FILE* vf = nullptr;
    if (!stills) {
        std::string vpath = std::string(out) + ".rgba";
        vf = fopen(vpath.c_str(), "wb");
    }
    Synth synth;
    const int rate = 48000;
    std::vector<float> abuf(rate / fps * 2 + 16);
    Input in;
    int total = (int)(secs * fps);
    float dt = 1.0f / fps;
    for (int f = 0; f < total; f++) {
        float t = f * dt;
        // 2x substeps per video frame for smooth physics
        game.update(dt * 0.5f, in);
        game.update(dt * 0.5f, in);
        if (t + 1e-4f < first) continue;
        if (af) {
            int n = rate / fps;
            synth.render(game.audio, abuf.data(), n, rate);
            fwrite(abuf.data(), sizeof(float), n * 2, af);
        }
        if (stills && (f % stills) != 0) continue;
        game.render(fin.fbo, W, H);
        glBindFramebuffer(GL_FRAMEBUFFER, fin.fbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        if (vf) {
            // flip vertically while writing
            for (int y = H - 1; y >= 0; y--) fwrite(&px[(size_t)y * W * 4], 1, W * 4, vf);
        } else {
            char name[512]; snprintf(name, sizeof name, "%s_%05.1f.rgba", out, t);
            FILE* sf = fopen(name, "wb");
            for (int y = H - 1; y >= 0; y--) fwrite(&px[(size_t)y * W * 4], 1, W * 4, sf);
            fclose(sf);
        }
        if (f % 30 == 0) {
            const Car& c = game.cars[game.player];
            ftLog("t=%.1f state=%d place=%d lap=%d speed=%.0fmph lat=%.1f dist=%.0f", t, (int)game.state, c.place,
                  c.lap(game.track.length), c.speed() * 2.237f, c.lat, c.dist);
        }
    }
    if (vf) fclose(vf);
    if (af) fclose(af);
    ftLog("track length %.0f m", game.track.length);
    return 0;
}
