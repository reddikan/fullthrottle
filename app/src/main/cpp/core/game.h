// Full Throttle — core game (platform independent).
#pragma once
#include <vector>
#include <string>
#include <functional>
#include <atomic>
#include "gl.h"
#include "mathx.h"

void ftLog(const char* fmt, ...);

// ---------------------------------------------------------------- vertices
struct Vtx {             // world / car vertex
    float px, py, pz;
    float nx, ny, nz;
    float r, g, b, e;    // colour + emissive
    float aux;           // material id (see Mat)
};
enum Mat {
    M_MATTE = 0, M_PAINT = 1, M_GLASS = 2, M_METAL = 3, M_GRASS = 4, M_ROCK = 5, M_CROWD = 6,
    M_FENCE = 7, M_WATER = 8, M_CONCRETE = 9, M_RUBBER = 10, M_TRIM = 11, M_LEAF = 12, M_SAND = 13, M_RIM = 14, M_GLOSS = 15
};
struct RoadVtx { float px, py, pz; float lat, s, tunnel, curv; };
struct TextVtx { float px, py, pz; float u, v; float r, g, b, a; };
struct SkidVtx { float px, py, pz; float a, u; };

struct MeshBuilder {
    std::vector<Vtx> v;
    void tri(V3 a, V3 b, V3 c, V3 col, float aux = 0, float emis = 0);
    void quad(V3 a, V3 b, V3 c, V3 d, V3 col, float aux = 0, float emis = 0) { tri(a, b, c, col, aux, emis); tri(a, c, d, col, aux, emis); }
    void triN(V3 a, V3 b, V3 c, V3 na, V3 nb, V3 nc, V3 col, float aux = 0, float emis = 0);
    void box(V3 mn, V3 mx, V3 col, float aux = 0, float emis = 0, const M4* xf = nullptr);
};

// ---------------------------------------------------------------- track
enum ZoneType { Z_STANDS, Z_CLIFF_R, Z_CLIFF_L, Z_TUNNEL, Z_HOUSES_L, Z_HOUSES_R, Z_FOREST };
struct Zone { float s0, s1; int type; };

struct Track {
    std::vector<V3> P, T, R;
    std::vector<float> S, K;
    float length = 0, ds = 2.0f;
    float halfW = 10.0f;           // asphalt half width
    float wallOff = 16.5f;         // guard-rail lateral distance
    int n = 0;
    std::vector<Zone> zones;
    std::vector<float> checkpoints;    // s positions (finish line is s=0)
    V3 lakeC; float lakeR = 0;
    V3 lighthouse;
    void build();
    int nearest(V3 p, int hint) const;
    V3 at(float s, float lat) const;
    float curvatureAt(float s) const;
    V3 tangentAt(float s) const;
    V3 rightAt(float s) const;
    float wrapS(float s) const { s = fmodf(s, length); return s < 0 ? s + length : s; }
    float distTo(V3 p) const;
    bool inZone(float s, int type) const;
    bool inTunnel(float s) const { return inZone(s, Z_TUNNEL); }
    float wallAt(float s) const { return inTunnel(s) ? halfW + 3.4f : wallOff; }
    std::vector<float> apexes;     // hairpin apex positions (for brake boards / AI)
};

// ---------------------------------------------------------------- cars
struct Livery {
    V3 c1, c2, c3, wheel, num;     // base, accent, stripe, rim, number colour
    std::string sponsor, team, driver;
    int number = 0, pattern = 0;
};

struct Car {
    // rigid body (2D in the ground plane)
    V3 pos, vel;
    float yaw = 0, yawRate = 0;
    float steerAngle = 0;          // actual front-wheel angle (rad, + = left)
    // controls
    float throttle = 0, brake = 0, steer = 0, axPrev = 0;
    bool handbrake = false, boosting = false;
    // drivetrain
    int gear = 1; float rpm = 0.2f, shiftTimer = 0; float wheelSpin = 0;
    // tyres
    float slipF = 0, slipR = 0, slideSpeed = 0, driftAmt = 0, driftHold = 0, driftCalm = 0;
    bool drifting = false; float driftDir = 1;
    bool skidOn[4] = {false, false, false, false};
    V3 skidPrev[4];
    // fx timers
    float smokeAcc = 0, sparkAcc = 0, flameTimer = 0, bump = 0;
    float rollVis = 0, rollVel = 0, pitchVis = 0, pitchVel = 0, heave = 0, heaveVel = 0;
    // race
    int idx = 0; float s = 0, lat = 0, dist = 0, prevS = 0;
    int place = 1; bool finished = false; float finishTime = 0;
    float lapStart = 0, bestLap = 0, lastLapTime = 0; int lastLap = 0;
    float draft = 0, boost = 0.4f;
    // AI
    bool ai = true; float skill = 1, laneBias = 0, avoid = 0, aggression = 0.5f, stuck = 0;
    float formationLat = 0, formationDist = 0;
    Livery liv;
    V3 fwd() const { return V3(sinf(yaw), 0, cosf(yaw)); }
    V3 right() const { return V3(-cosf(yaw), 0, sinf(yaw)); }
    V3 left() const { return V3(cosf(yaw), 0, -sinf(yaw)); }
    float speed() const { return len(vel); }
    int lap(float L) const { return dist < 0 ? 0 : (int)(dist / L) + 1; }
};

struct Input {
    float steer = 0, throttle = 0, brake = 0;
    bool handbrake = false, boost = false;
    bool startPressed = false, selectPressed = false, confirmPressed = false;
    bool leftPressed = false, rightPressed = false;
};

enum PType { P_SMOKE, P_SPARK, P_FLAME, P_SPRAY, P_CONFETTI, P_DUST, P_DEBRIS };
struct Particle {
    V3 p, v; float life, maxLife, size, grow, alpha; V3 col; float rot, rotVel; int type;
};

// ---------------------------------------------------------------- audio
struct AudioParams {
    std::atomic<float> rpm{0}, throttle{0}, speed{0}, skid{0}, boost{0}, offroad{0}, scrape{0};
    std::atomic<float> nearCar{0}, master{1}, rain{0}, tunnel{0}, crowd{0};
    std::atomic<int> impacts{0}, thunders{0}, shifts{0}, backfires{0}, checkpoints{0};
    std::atomic<float> impactPower{1}, thunderPower{1};
};
struct Synth {
    double ph = 0, ph2 = 0, lfo = 0, sq = 0, cph = 0;
    float lp1 = 0, lp2 = 0, lp3 = 0, lp4 = 0, hp = 0, eng = 0, engLP = 0, sqLP = 0, scLP = 0, crowdLP = 0;
    float rpmS = 0, thrS = 0, spdS = 0, skS = 0, boS = 0, nearS = 0, offS = 0, scS = 0, tunS = 0, crS = 0;
    float impactEnv = 0, impactPow = 1, thunderEnv = 0, thunderDelay = 0, thLP = 0, thLP2 = 0, thPow = 1;
    float shiftEnv = 0, popEnv = 0, chimeEnv = 0; double chimePh = 0;
    int seenImpacts = 0, seenThunder = 0, seenShifts = 0, seenPops = 0, seenCp = 0;
    uint32_t seed = 12345;
    void render(AudioParams& p, float* out, int frames, int rate);
};

// ---------------------------------------------------------------- game
enum class State { Title, Rolling, Race, Finished, TimeUp, Paused };
enum class Weather { Sunny, Rain };

struct Banner { std::string text, sub; float t = 0, dur = 2; int style = 0; };

struct Renderer;

struct Game {
    std::function<std::vector<uint8_t>(const char*)> readAsset;
    Track track;
    std::vector<Car> cars;
    int player = 0;
    State state = State::Title, pausedFrom = State::Race;
    Weather weather = Weather::Sunny;
    float stateTime = 0, raceTime = 0, globalTime = 0;
    int laps = 3;
    bool autopilot = false;
    // arcade timer
    float timer = 40;
    int nextCp = 0;
    std::vector<Particle> parts;
    // skid marks (ring buffer, 6 vertices per segment)
    std::vector<SkidVtx> skid;
    int skidHead = 0, skidCount = 0;
    long long skidWritten = 0;
    // camera
    V3 camPos, camLook; float camYaw = 0, camFov = 60, shake = 0;
    float flash = 0, nextLightning = 18;
    std::vector<Banner> banners;
    AudioParams audio;
    Renderer* rnd = nullptr;
    int width = 1920, height = 1080;
    bool hideHud = false;
    float renderScale = 1.0f;
    uint32_t rngState = 777;
    int menuSel = 0;

    void init();
    void resetRace();
    void update(float dt, const Input& in);
    void render(GLuint targetFbo, int w, int h);
    void step(float dt, const Input& in);
    void driveAI(Car& c, float dt);
    void formation(Car& c, float dt);
    void physics(Car& c, float dt);
    void collide();
    void walls(Car& c, float dt);
    void tyreFx(Car& c, float dt);
    void addSkid(V3 a, V3 b, V3 side, float w, float alpha);
    void updateCamera(float dt);
    void updateParticles(float dt);
    void updatePlaces();
    void banner(const std::string& t, const std::string& sub, float dur, int style);
    void spawn(int type, V3 p, V3 v, float life, float size, float grow, float alpha, V3 col);
    float frand();
};

// car geometry helpers (car.cpp)
void wheelLayout(int w, V3* pos, float* sideSign, bool* front);
float wheelRadius();
