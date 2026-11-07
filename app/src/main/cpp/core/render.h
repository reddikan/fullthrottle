#pragma once
#include "game.h"
#include <initializer_list>

struct Glyph { float u0, v0, u1, v1, xoff, yoff, w, h, adv; bool valid; };
struct Font {
    int w = 0, h = 0;
    float lineH = 1;
    Glyph g[128] = {};
    std::vector<uint8_t> pixels;
    bool load(const std::vector<uint8_t>& data);
    float measure(const char* s) const;
    // emit(x0,y0,x1,y1,u0,v0,u1,v1) in em units, y up, origin at baseline-left.
    template <class F> void layout(const char* s, F emit, float tracking = 0.0f) const {
        float x = 0;
        for (const char* p = s; *p; ++p) {
            unsigned c = (unsigned char)*p;
            if (c >= 128) continue;
            const Glyph& gl = g[c];
            if (gl.valid && gl.w > 0) emit(x + gl.xoff, gl.yoff - gl.h, x + gl.xoff + gl.w, gl.yoff, gl.u0, gl.v1, gl.u1, gl.v0);
            x += gl.adv + tracking;
        }
    }
};
void planeText(std::vector<TextVtx>& out, const Font& f, const char* s, V3 origin, V3 right, V3 up,
               float size, V3 col, int align = 1, float tracking = 0.0f);

struct WorldGeo {
    MeshBuilder world;
    std::vector<RoadVtx> road;
    std::vector<TextVtx> text;
    MeshBuilder car, wheel;
    std::vector<V2> minimap;
    V2 mapMin, mapMax;
};
void buildWorld(const Track& t, const Font& font, WorldGeo& geo);
void buildCarMesh(MeshBuilder& m);
void buildWheelMesh(MeshBuilder& m);

struct Prog {
    GLuint id = 0;
    std::vector<std::pair<std::string, GLint>> cache;
    bool build(const std::string& vs, const std::string& fs, std::initializer_list<const char*> attribs);
    void use() const { glUseProgram(id); }
    GLint U(const char* name);
};

struct Target {
    GLuint fbo = 0, tex = 0, depth = 0; int w = 0, h = 0;
    void create(int w, int h, bool withDepth, bool linear = true);
    void destroy();
};

struct HudVtx { float x, y, u, v, r, g, b, a, mode; };

struct Renderer {
    Font font;
    GLuint fontTex = 0;
    GLuint texAsphalt = 0, texGrass = 0, texRock = 0, texCrowd = 0;
    std::vector<GLuint> livery;
    WorldGeo geo;
    GLuint vaoWorld = 0, vboWorld = 0; int nWorld = 0;
    GLuint vaoRoad = 0, vboRoad = 0; int nRoad = 0;
    GLuint vaoText = 0, vboText = 0; int nText = 0;
    GLuint vaoCar = 0, vboCar = 0; int nCar = 0;
    GLuint vaoWheel = 0, vboWheel = 0; int nWheel = 0;
    GLuint vaoRain = 0, vboRain = 0; int nRain = 0;
    GLuint vaoPart = 0, vboPart = 0;
    GLuint vaoSkid = 0, vboSkid = 0; long long skidUploaded = 0;
    GLuint vaoHud = 0, vboHud = 0;
    GLuint vaoFull = 0;
    GLuint vaoShadow = 0, vboShadow = 0;
    Prog pWorld, pRoad, pText, pSky, pRain, pPart, pSkid, pBlurRefl, pBright, pBlur, pComposite, pHud, pShadow;
    Target refl, reflBlur, scene, bloomA, bloomB, bloomC, bloomD;
    GLuint msFbo = 0, msColor = 0, msDepth = 0; int samples = 4;
    int w = 0, h = 0;
    std::vector<HudVtx> hud;
    std::vector<float> partBuf, partAdd;
    bool ready = false;
    float VW = 1920, VH = 1080;

    void init(Game& g);
    void resize(int w, int h);
    void draw(Game& g, GLuint target, int w, int h);
    void makeTextures();
    void paintLivery(const Livery& l, GLuint& tex);
    void drawScene(Game& g, const M4& vp, V3 cam, bool reflection);
    void drawParticles(Game& g, const M4& vp, V3 cam);
    void drawHud(Game& g);
    void flushHud(float vw, float vh, V3 outline);
    // 2D helpers (HUD + livery canvas)
    void rect(float x, float y, float w, float h, V4 c);
    void rectGrad(float x, float y, float w, float h, V4 top, V4 bot);
    void rrect(float x, float y, float w, float h, float r, V4 c);
    void circle(float cx, float cy, float r, V4 c);
    void line(float x0, float y0, float x1, float y1, float th, V4 c);
    void quad2(V2 a, V2 b, V2 c, V2 d, V4 col);
    float text(const char* s, float x, float y, float size, V4 top, V4 bot, int align = 0, int mode = 1, float tracking = 0, float rot = 0);
    float text(const char* s, float x, float y, float size, V4 c, int align = 0, int mode = 1, float tracking = 0) { return text(s, x, y, size, c, c, align, mode, tracking); }
};
