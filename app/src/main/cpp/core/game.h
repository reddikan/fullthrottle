// Full Throttle — core game (platform independent).
#pragma once
#include <vector>
#include <string>
#include <functional>
#include <atomic>
#include "gl.h"
#include "mathx.h"

// ---------------------------------------------------------------- vertices
struct Vtx {             // world / car vertex
    float px, py, pz;
    float nx, ny, nz;
    float r, g, b, e;    // color + emissive
    float aux;           // material: 0 matte, 1 paint, 2 glass, 3 chrome/rim, 4 wet ground
};
struct RoadVtx { float px, py, pz; float lat, s, kind; };  // kind 0 road, 1 curb, 2 runoff-dirt
struct TextVtx { float px, py, pz; float u, v; float r, g, b, a; };

struct MeshBuilder {
    std::vector<Vtx> v;
    void tri(V3 a, V3 b, V3 c, V3 col, float aux = 0, float emis = 0);
    void quad(V3 a, V3 b, V3 c, V3 d, V3 col, float aux = 0, float emis = 0) { tri(a, b, c, col, aux, emis); tri(a, c, d, col, aux, emis); }
    void box(V3 mn, V3 mx, V3 col, float aux = 0, float emis = 0, const M4* xf = nullptr);
};

// ---------------------------------------------------------------- track
struct Track {
    std::vector<V3> P, T, R;      // centre, tangent, right
    std::vector<float> S, K;       // distance along, signed curvature (+ = turning right)
    float length = 0, ds = 2.0f;
    float halfW = 9.0f;            // asphalt half width
    float wallOff = 15.0f;         // barrier lateral distance
    int n = 0;
    void build();
    int nearest(V3 p, int hint) const;
    V3 at(float s, float lat) const;     // world point at distance s with lateral offset
    float curvatureAt(float s) const;
    V3 tangentAt(float s) const;
    float wrapS(float s) const { s = fmodf(s, length); return s < 0 ? s + length : s; }
    float distTo(V3 p) const;            // brute force (load time only)
};

// ---------------------------------------------------------------- cars
struct Livery { V3 c1, c2, c3; std::string sponsor; std::string tag; int number; };

struct Car {
    V3 pos, vel;
    float yaw = 0, yawRate = 0;
    float steerVis = 0;
    int idx = 0;                   // nearest track sample
    float s = 0, lat = 0;          // track coords
    float dist = 0;                // unwrapped race distance (negative before the line)
    float prevS = 0;
    float throttle = 0, brake = 0, steer = 0;
    bool drift = false, boosting = false;
    float boost = 0.35f;           // 0..1 meter
    float driftTime = 0;
    float bump = 0;                // recent impact
    bool ai = true;
    float skill = 1, laneBias = 0, avoid = 0;
    int place = 1;
    bool finished = false;
    float finishTime = 0;
    float bestLap = 0, lapStart = 0;
    int lastLap = 0;
    float sprayAcc = 0;
    Livery liv;
    V3 fwd() const { return V3(sinf(yaw), 0, cosf(yaw)); }
    V3 right() const { return V3(-cosf(yaw), 0, sinf(yaw)); }
    float speed() const { return len(vel); }
    int lap(float L) const { return dist < 0 ? 0 : (int)(dist / L) + 1; }
};

struct Input {
    float steer = 0, throttle = 0, brake = 0;
    bool drift = false, boost = false;
    bool startPressed = false, selectPressed = false, confirmPressed = false;
};

struct Particle { V3 p, v; float life, maxLife, size, grow, alpha; };

// ---------------------------------------------------------------- audio
struct AudioParams {
    std::atomic<float> rpm{0}, throttle{0}, speed{0}, drift{0}, boost{0}, offroad{0};
    std::atomic<float> nearCar{0}, master{1}, rain{1};
    std::atomic<int> impacts{0}, thunders{0};
    std::atomic<float> thunderPower{1};
};
struct Synth {
    double ph = 0, ph2 = 0, lfo = 0;
    float lp1 = 0, lp2 = 0, lp3 = 0, lp4 = 0, hp = 0, prevN = 0, eng = 0, engLP = 0;
    float rpmS = 0, thrS = 0, spdS = 0, drS = 0, boS = 0, nearS = 0, offS = 0;
    float impactEnv = 0, thunderEnv = 0, thunderDelay = 0, thLP = 0, thLP2 = 0, thPow = 1;
    int seenImpacts = 0, seenThunder = 0;
    uint32_t seed = 12345;
    void render(AudioParams& p, float* out, int frames, int rate);
};

// ---------------------------------------------------------------- game
enum class State { Title, Countdown, Race, Finished, Paused };

struct Renderer;

struct Game {
    std::function<std::vector<uint8_t>(const char*)> readAsset;
    Track track;
    std::vector<Car> cars;
    int player = 0;
    State state = State::Title, pausedFrom = State::Race;
    float stateTime = 0, raceTime = 0, globalTime = 0;
    int laps = 3;
    bool autopilot = false;       // preview / attract mode
    std::vector<Particle> spray;
    // camera
    V3 camPos, camLook; float camYaw = 0, camFov = 62, shake = 0;
    int camMode = 0;
    // weather
    float flash = 0, nextLightning = 18;
    std::string banner; float bannerTime = 0;
    AudioParams audio;
    Renderer* rnd = nullptr;
    int width = 1920, height = 1080;
    bool hideHud = false;
    float renderScale = 1.0f;     // internal resolution scale (lowered automatically if the device struggles)
    uint32_t rngState = 777;
    int finishOrder = 0;

    void init();
    void resetRace();
    void update(float dt, const Input& in);
    void render(GLuint targetFbo, int w, int h);
    void step(float dt, const Input& in);
    void driveAI(Car& c, float dt);
    void physics(Car& c, float dt);
    void collide();
    void updateCamera(float dt);
    void updateSpray(float dt);
    void updatePlaces();
};
