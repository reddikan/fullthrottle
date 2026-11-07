// Android entry point: window/EGL, controller input, AAudio, frame loop.
#include <android_native_app_glue.h>
#include <android/log.h>
#include <android/asset_manager.h>
#include <android/window.h>
#include <aaudio/AAudio.h>
#include <EGL/egl.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>
#include "core/render.h"

void ftLog(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, "FullThrottle", fmt, ap);
    va_end(ap);
}

struct Pad {
    float axX = 0, hatX = 0, gas = 0, rtrig = 0, brake = 0, ltrig = 0;
    bool left = false, right = false, up = false, down = false;
    bool a = false, b = false, x = false, y = false, l1 = false, r1 = false, l2 = false, r2 = false;
    bool start = false, select = false;
    bool prevA = false, prevStart = false, prevSelect = false, prevB = false, prevL = false, prevR = false;
    bool tap = false, back = false;
};

struct App {
    android_app* app = nullptr;
    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLSurface surf = EGL_NO_SURFACE;
    EGLContext ctx = EGL_NO_CONTEXT;
    EGLConfig cfg = nullptr;
    int w = 0, h = 0;
    bool focused = false, resumed = false;
    Game game;
    bool gameReady = false;
    Pad pad;
    Synth synth;
    AAudioStream* stream = nullptr;
    int64_t lastNs = 0;
    float perfAcc = 0; int perfFrames = 0;
};

static int64_t nowNs() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec; }

// ------------------------------------------------------------ audio
static aaudio_data_callback_result_t audioCb(AAudioStream* s, void* user, void* data, int32_t frames) {
    App* a = (App*)user;
    a->synth.render(a->game.audio, (float*)data, frames, AAudioStream_getSampleRate(s));
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
static void startAudio(App* a) {
    if (a->stream) { AAudioStream_requestStart(a->stream); return; }
    AAudioStreamBuilder* b = nullptr;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return;
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setDataCallback(b, audioCb, a);
    if (AAudioStreamBuilder_openStream(b, &a->stream) == AAUDIO_OK) AAudioStream_requestStart(a->stream);
    else a->stream = nullptr;
    AAudioStreamBuilder_delete(b);
}
static void pauseAudio(App* a) { if (a->stream) AAudioStream_requestPause(a->stream); }

// ------------------------------------------------------------ display
static void initDisplay(App* a) {
    if (a->dpy == EGL_NO_DISPLAY) {
        a->dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        eglInitialize(a->dpy, nullptr, nullptr);
        const EGLint attr[] = {EGL_RENDERABLE_TYPE, 0x40 /*EGL_OPENGL_ES3_BIT*/, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                               EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 0, EGL_NONE};
        EGLint n = 0;
        eglChooseConfig(a->dpy, attr, &a->cfg, 1, &n);
        const EGLint ctxAttr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        a->ctx = eglCreateContext(a->dpy, a->cfg, EGL_NO_CONTEXT, ctxAttr);
    }
    a->surf = eglCreateWindowSurface(a->dpy, a->cfg, a->app->window, nullptr);
    eglMakeCurrent(a->dpy, a->surf, a->surf, a->ctx);
    eglSwapInterval(a->dpy, 1);
    eglQuerySurface(a->dpy, a->surf, EGL_WIDTH, &a->w);
    eglQuerySurface(a->dpy, a->surf, EGL_HEIGHT, &a->h);
    ftLog("surface %dx%d  GL %s", a->w, a->h, glGetString(GL_RENDERER));
    if (!a->gameReady) {
        a->game.readAsset = [a](const char* name) {
            std::vector<uint8_t> d;
            AAsset* as = AAssetManager_open(a->app->activity->assetManager, name, AASSET_MODE_BUFFER);
            if (!as) return d;
            size_t n = AAsset_getLength(as);
            d.resize(n);
            memcpy(d.data(), AAsset_getBuffer(as), n);
            AAsset_close(as);
            return d;
        };
        a->game.init();
        a->gameReady = true;
    }
    a->lastNs = nowNs();
}
static void termSurface(App* a) {
    if (a->surf != EGL_NO_SURFACE) {
        eglMakeCurrent(a->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, a->ctx);
        eglDestroySurface(a->dpy, a->surf);
        a->surf = EGL_NO_SURFACE;
    }
}

// ------------------------------------------------------------ input
static int32_t onInput(android_app* app, AInputEvent* ev) {
    App* a = (App*)app->userData;
    Pad& p = a->pad;
    int32_t type = AInputEvent_getType(ev);
    int32_t src = AInputEvent_getSource(ev);
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        if ((src & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK || (src & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD) {
            p.axX = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_X, 0);
            p.hatX = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_HAT_X, 0);
            float hatY = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_HAT_Y, 0);
            p.up = hatY < -0.5f; p.down = hatY > 0.5f;
            p.gas = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_GAS, 0);
            p.rtrig = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_RTRIGGER, 0);
            p.brake = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_BRAKE, 0);
            p.ltrig = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_LTRIGGER, 0);
            return 1;
        }
        if ((AMotionEvent_getAction(ev) & AMOTION_EVENT_ACTION_MASK) == AMOTION_EVENT_ACTION_DOWN) p.tap = true;
        return 1;
    }
    if (type == AINPUT_EVENT_TYPE_KEY) {
        bool down = AKeyEvent_getAction(ev) == AKEY_EVENT_ACTION_DOWN;
        if (AKeyEvent_getAction(ev) == AKEY_EVENT_ACTION_MULTIPLE) return 1;
        switch (AKeyEvent_getKeyCode(ev)) {
            case AKEYCODE_DPAD_LEFT: p.left = down; return 1;
            case AKEYCODE_DPAD_RIGHT: p.right = down; return 1;
            case AKEYCODE_DPAD_UP: p.up = down; return 1;
            case AKEYCODE_DPAD_DOWN: p.down = down; return 1;
            case AKEYCODE_BUTTON_A: case AKEYCODE_DPAD_CENTER: case AKEYCODE_ENTER: p.a = down; return 1;
            case AKEYCODE_BUTTON_B: p.b = down; return 1;
            case AKEYCODE_BUTTON_X: p.x = down; return 1;
            case AKEYCODE_BUTTON_Y: p.y = down; return 1;
            case AKEYCODE_BUTTON_L1: p.l1 = down; return 1;
            case AKEYCODE_BUTTON_R1: p.r1 = down; return 1;
            case AKEYCODE_BUTTON_L2: p.l2 = down; return 1;
            case AKEYCODE_BUTTON_R2: p.r2 = down; return 1;
            case AKEYCODE_BUTTON_START: p.start = down; return 1;
            case AKEYCODE_BUTTON_SELECT: p.select = down; return 1;
            case AKEYCODE_BACK:
                // In a race, Back pauses. From the title screen it exits the app.
                if (a->game.state == State::Title) return 0;
                if (down) p.back = true;
                return 1;
            default: return 0;
        }
    }
    return 0;
}

static Input readInput(App* a) {
    Pad& p = a->pad;
    Input in;
    float sx = p.axX;
    if (fabsf(sx) < 0.12f) sx = 0; else sx = (sx - signf(sx) * 0.12f) / 0.88f;
    if (fabsf(p.hatX) > 0.5f) sx = p.hatX;
    if (p.left) sx = -1;
    if (p.right) sx = 1;
    in.steer = clampf(sx, -1, 1);
    // A = gas, B / L2 = brake & reverse, R1 / L1 / R2 = drift (hold), X / Y = boost
    float thr = p.a ? 1.0f : 0.0f;
    if (p.up) thr = 1.0f;
    float brk = fmaxf(p.ltrig, p.brake);
    if (p.b || p.l2 || p.down) brk = 1.0f;
    in.throttle = clampf(thr, 0, 1);
    in.brake = clampf(brk, 0, 1);
    in.handbrake = p.r1 || p.l1 || p.r2 || p.rtrig > 0.4f || p.gas > 0.4f;
    in.boost = p.x || p.y;
    in.confirmPressed = (p.a && !p.prevA) || p.tap;
    in.startPressed = (p.start && !p.prevStart) || p.back;
    in.selectPressed = p.select && !p.prevSelect;
    bool lNow = p.left || p.hatX < -0.5f || p.axX < -0.6f, rNow = p.right || p.hatX > 0.5f || p.axX > 0.6f;
    in.leftPressed = lNow && !p.prevL; in.rightPressed = rNow && !p.prevR;
    p.prevL = lNow; p.prevR = rNow;
    p.prevA = p.a; p.prevStart = p.start; p.prevSelect = p.select;
    p.tap = false; p.back = false;
    return in;
}

// ------------------------------------------------------------ lifecycle
static void onCmd(android_app* app, int32_t cmd) {
    App* a = (App*)app->userData;
    switch (cmd) {
        case APP_CMD_INIT_WINDOW: if (app->window) initDisplay(a); break;
        case APP_CMD_TERM_WINDOW: termSurface(a); break;
        case APP_CMD_GAINED_FOCUS: a->focused = true; a->lastNs = nowNs(); startAudio(a); break;
        case APP_CMD_LOST_FOCUS:
            a->focused = false;
            pauseAudio(a);
            if (a->game.state == State::Race) { a->game.pausedFrom = State::Race; a->game.state = State::Paused; }
            break;
        case APP_CMD_RESUME: a->resumed = true; break;
        case APP_CMD_PAUSE: a->resumed = false; pauseAudio(a); break;
        default: break;
    }
}

void android_main(android_app* app) {
    static App a;
    a.app = app;
    app->userData = &a;
    app->onAppCmd = onCmd;
    app->onInputEvent = onInput;
    ANativeActivity_setWindowFlags(app->activity, AWINDOW_FLAG_KEEP_SCREEN_ON | AWINDOW_FLAG_FULLSCREEN, 0);

    while (true) {
        int events; android_poll_source* source;
        bool active = a.focused && a.surf != EGL_NO_SURFACE;
        while (ALooper_pollAll(active ? 0 : -1, nullptr, &events, (void**)&source) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) {
                if (a.stream) { AAudioStream_requestStop(a.stream); AAudioStream_close(a.stream); a.stream = nullptr; }
                termSurface(&a);
                return;
            }
            active = a.focused && a.surf != EGL_NO_SURFACE;
        }
        if (!active || !a.gameReady) continue;

        int64_t t = nowNs();
        float dt = (t - a.lastNs) / 1e9f;
        a.lastNs = t;
        a.game.update(dt, readInput(&a));
        eglQuerySurface(a.dpy, a.surf, EGL_WIDTH, &a.w);
        eglQuerySurface(a.dpy, a.surf, EGL_HEIGHT, &a.h);
        a.game.render(0, a.w, a.h);
        eglSwapBuffers(a.dpy, a.surf);

        // If the device can't hold ~60 fps, lower the internal resolution a step.
        a.perfAcc += dt; a.perfFrames++;
        if (a.perfAcc > 3.0f) {
            float avg = a.perfAcc / a.perfFrames;
            if (avg > 0.0185f && a.game.renderScale > 0.7f) { a.game.renderScale -= 0.1f; ftLog("render scale -> %.2f", a.game.renderScale); }
            a.perfAcc = 0; a.perfFrames = 0;
        }
    }
}
