// 2D drawing helpers and the in-race HUD.
#include "render.h"
#include <string.h>
#include <stdio.h>
#include <algorithm>

// ------------------------------------------------------------- primitives
void Renderer::rect(float x, float y, float rw, float rh, V4 c) { rectGrad(x, y, rw, rh, c, c); }
void Renderer::rectGrad(float x, float y, float rw, float rh, V4 t, V4 b) {
    HudVtx a = {x, y, 0, 0, t.x, t.y, t.z, t.w, 0}, bb = {x + rw, y, 0, 0, t.x, t.y, t.z, t.w, 0};
    HudVtx cc = {x + rw, y + rh, 0, 0, b.x, b.y, b.z, b.w, 0}, d = {x, y + rh, 0, 0, b.x, b.y, b.z, b.w, 0};
    hud.insert(hud.end(), {a, bb, cc, a, cc, d});
}
static void quv(std::vector<HudVtx>& h, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, V4 c, float mode) {
    HudVtx a = {x0, y0, u0, v0, c.x, c.y, c.z, c.w, mode}, b = {x1, y0, u1, v0, c.x, c.y, c.z, c.w, mode};
    HudVtx cc = {x1, y1, u1, v1, c.x, c.y, c.z, c.w, mode}, d = {x0, y1, u0, v1, c.x, c.y, c.z, c.w, mode};
    h.insert(h.end(), {a, b, cc, a, cc, d});
}
void Renderer::circle(float cx, float cy, float r, V4 c) { quv(hud, cx - r, cy - r, cx + r, cy + r, 0, 0, 1, 1, c, 2); }
void Renderer::rrect(float x, float y, float rw, float rh, float r, V4 c) {
    r = fminf(r, fminf(rw, rh) * 0.5f);
    rect(x + r, y, rw - 2 * r, rh, c);
    rect(x, y + r, r, rh - 2 * r, c);
    rect(x + rw - r, y + r, r, rh - 2 * r, c);
    quv(hud, x, y, x + r, y + r, 0, 0, 0.5f, 0.5f, c, 2);
    quv(hud, x + rw - r, y, x + rw, y + r, 0.5f, 0, 1, 0.5f, c, 2);
    quv(hud, x, y + rh - r, x + r, y + rh, 0, 0.5f, 0.5f, 1, c, 2);
    quv(hud, x + rw - r, y + rh - r, x + rw, y + rh, 0.5f, 0.5f, 1, 1, c, 2);
}
void Renderer::line(float x0, float y0, float x1, float y1, float th, V4 c) {
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 1e-3f) return;
    float nx = -dy / l * th * 0.5f, ny = dx / l * th * 0.5f;
    quad2({x0 + nx, y0 + ny}, {x1 + nx, y1 + ny}, {x1 - nx, y1 - ny}, {x0 - nx, y0 - ny}, c);
}
void Renderer::quad2(V2 a, V2 b, V2 c, V2 d, V4 col) {
    HudVtx A = {a.x, a.y, 0, 0, col.x, col.y, col.z, col.w, 0}, B = {b.x, b.y, 0, 0, col.x, col.y, col.z, col.w, 0};
    HudVtx C = {c.x, c.y, 0, 0, col.x, col.y, col.z, col.w, 0}, D = {d.x, d.y, 0, 0, col.x, col.y, col.z, col.w, 0};
    hud.insert(hud.end(), {A, B, C, A, C, D});
}
float Renderer::text(const char* s, float x, float y, float size, V4 top, V4 bot, int align, int mode, float tracking, float rot) {
    size_t n = strlen(s);
    float wdt = (font.measure(s) + tracking * (n > 0 ? n - 1 : 0)) * size;
    float x0 = align == 1 ? x - wdt * 0.5f : (align == 2 ? x - wdt : x);
    (void)rot;
    font.layout(s, [&](float ax, float ay, float bx, float by, float u0, float v0, float u1, float v1) {
        float X0 = x0 + ax * size, X1 = x0 + bx * size, Y0 = y - by * size, Y1 = y - ay * size;
        // vertical gradient across the cap height
        float t0 = sat((Y0 - (y - size * 0.75f)) / (size * 0.75f)), t1 = sat((Y1 - (y - size * 0.75f)) / (size * 0.75f));
        V4 c0 = {lerpf(top.x, bot.x, t0), lerpf(top.y, bot.y, t0), lerpf(top.z, bot.z, t0), lerpf(top.w, bot.w, t0)};
        V4 c1 = {lerpf(top.x, bot.x, t1), lerpf(top.y, bot.y, t1), lerpf(top.z, bot.z, t1), lerpf(top.w, bot.w, t1)};
        HudVtx a = {X0, Y0, u0, v1, c0.x, c0.y, c0.z, c0.w, (float)mode}, b = {X1, Y0, u1, v1, c0.x, c0.y, c0.z, c0.w, (float)mode};
        HudVtx c = {X1, Y1, u1, v0, c1.x, c1.y, c1.z, c1.w, (float)mode}, d = {X0, Y1, u0, v0, c1.x, c1.y, c1.z, c1.w, (float)mode};
        hud.insert(hud.end(), {a, b, c, a, c, d});
    }, tracking);
    return wdt;
}
void Renderer::flushHud(float vw, float vh, V3 outline) {
    if (hud.empty()) return;
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    pHud.use();
    glUniform2f(pHud.U("u_size"), vw, vh);
    glUniform3f(pHud.U("u_outline"), outline.x, outline.y, outline.z);
    glUniform1i(pHud.U("u_font"), 0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, fontTex);
    glBindVertexArray(vaoHud);
    glBindBuffer(GL_ARRAY_BUFFER, vboHud);
    glBufferData(GL_ARRAY_BUFFER, hud.size() * sizeof(HudVtx), hud.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (int)hud.size());
    glDisable(GL_BLEND);
    hud.clear();
}

// ------------------------------------------------------------- HUD
static const V4 kOrangeT = {1.0f, 0.86f, 0.30f, 1}, kOrangeB = {1.0f, 0.46f, 0.05f, 1};
static const V4 kWhite = {1, 1, 1, 1}, kPanel = {0.04f, 0.07f, 0.16f, 0.78f}, kPanelEdge = {0.35f, 0.45f, 0.65f, 0.55f};
static const V4 kGold = {1.0f, 0.80f, 0.20f, 1};

static void fmtTime(char* buf, size_t n, float t) {
    if (t < 0) t = 0;
    int m = (int)(t / 60); float s = t - m * 60;
    snprintf(buf, n, "%d:%05.2f", m, s);
}

void Renderer::drawHud(Game& g) {
    hud.clear();
    if (g.hideHud) return;
    VH = 1080; VW = 1080.0f * (float)w / (float)h;
    const Car& pc = g.cars[g.player];
    char buf[64];
    float L = g.track.length;

    auto panel = [&](float x, float y, float pw, float ph, float r) {
        rrect(x - 2, y - 2, pw + 4, ph + 4, r + 2, kPanelEdge);
        rrect(x, y, pw, ph, r, kPanel);
    };

    if (g.state == State::Title) {
        float a = sat(g.stateTime * 1.5f);
        rectGrad(0, VH * 0.18f, VW, 230, {0, 0, 0, 0}, {0, 0, 0, 0.0f});
        text("FULL THROTTLE", VW * 0.5f, VH * 0.33f, 150, {1, 0.88f, 0.3f, a}, {1, 0.45f, 0.05f, a}, 1, 3, 0.01f);
        text("CLIFFSIDE COAST", VW * 0.5f, VH * 0.33f + 52, 34, {1, 1, 1, a}, {1, 1, 1, a}, 1, 1, 0.12f);
        float py = VH * 0.66f;
        panel(VW * 0.5f - 230, py, 460, 76, 14);
        text("WEATHER", VW * 0.5f - 200, py + 47, 24, {0.7f, 0.8f, 1, a}, {0.7f, 0.8f, 1, a}, 0, 1, 0.1f);
        const char* wname = g.weather == Weather::Sunny ? "SUNNY" : "RAIN";
        text("<", VW * 0.5f + 40, py + 50, 34, kGold, kGold, 1, 1);
        text(wname, VW * 0.5f + 120, py + 52, 40, kOrangeT, kOrangeB, 1, 1);
        text(">", VW * 0.5f + 200, py + 50, 34, kGold, kGold, 1, 1);
        float blink = 0.45f + 0.55f * (0.5f + 0.5f * sinf(g.globalTime * 4));
        text("PRESS A TO RACE", VW * 0.5f, VH - 120, 40, {1, 1, 1, blink}, {1, 1, 1, blink}, 1, 1, 0.08f);
        text("A GAS    B BRAKE    HOLD R1 + STEER TO DRIFT    X BOOST    START PAUSE", VW * 0.5f, VH - 60, 20, {0.85f, 0.9f, 1, 0.9f}, {0.85f, 0.9f, 1, 0.9f}, 1, 1, 0.06f);
        flushHud(VW, VH, V3(0.03f, 0.06f, 0.18f));
        return;
    }

    // ---------------- top-left: lap, times, standings
    {
        float x = 18;
        text("LAP", x + 2, 30, 17, kWhite, kWhite, 0, 1, 0.1f);
        int lap = std::max(1, std::min(g.laps, pc.lap(L)));
        snprintf(buf, sizeof buf, "%d/%d", lap, g.laps);
        text(buf, x, 76, 48, kWhite, {0.85f, 0.9f, 1, 1}, 0, 1);
        float lapT = g.state == State::Rolling ? 0 : g.raceTime - pc.lapStart;
        char tb[32]; fmtTime(tb, sizeof tb, lapT);
        snprintf(buf, sizeof buf, "TIME %s", tb);
        text(buf, x + 2, 100, 17, kGold, kGold, 0, 1);
        if (pc.bestLap > 0) { fmtTime(tb, sizeof tb, pc.bestLap); snprintf(buf, sizeof buf, "BEST %s", tb); }
        else snprintf(buf, sizeof buf, "BEST --:--.--");
        text(buf, x + 2, 120, 17, kWhite, kWhite, 0, 1);

        std::vector<int> ord(g.cars.size());
        for (size_t i = 0; i < g.cars.size(); i++) ord[g.cars[i].place - 1] = (int)i;
        float y = 134, rh = 27;
        for (size_t i = 0; i < ord.size(); i++) {
            const Car& c = g.cars[ord[i]];
            bool me = ord[i] == g.player;
            if (me) { rrect(x - 2, y - 2, 236, rh + 1, 6, kGold); rrect(x, y, 232, rh - 3, 5, {0.12f, 0.10f, 0.05f, 0.92f}); }
            else rrect(x, y, 232, rh - 3, 5, kPanel);
            snprintf(buf, sizeof buf, "%d", (int)i + 1);
            text(buf, x + 26, y + 19, 18, kGold, kOrangeB, 2, 1);
            circle(x + 42, y + 12, 9.5f, {0.02f, 0.02f, 0.03f, 1});
            circle(x + 42, y + 12, 8, {c.liv.c1.x, c.liv.c1.y, c.liv.c1.z, 1});
            rect(x + 36, y + 9, 12, 4, {c.liv.c2.x, c.liv.c2.y, c.liv.c2.z, 1});
            text(c.liv.driver.c_str(), x + 58, y + 14, 14, kWhite, kWhite, 0, 1);
            snprintf(buf, sizeof buf, "#%d", c.liv.number);
            text(buf, x + 58, y + 23, 9, {0.7f, 0.75f, 0.85f, 1}, {0.7f, 0.75f, 0.85f, 1}, 0, 5);
            if (i > 0) {
                const Car& lead = g.cars[ord[0]];
                float gap = (lead.dist - c.dist) / fmaxf(25.0f, c.speed());
                snprintf(buf, sizeof buf, "+%.1f", gap);
                text(buf, x + 224, y + 18, 13, {0.75f, 0.82f, 0.95f, 1}, {0.75f, 0.82f, 0.95f, 1}, 2, 5);
            }
            y += rh;
        }
    }

    // ---------------- top-centre: arcade timer + arc speedometer
    {
        float cx = VW * 0.5f;
        int secs = (int)ceilf(g.timer);
        snprintf(buf, sizeof buf, "%d", secs);
        V4 tt = kOrangeT, tb = kOrangeB;
        if (g.timer < 10 && g.state == State::Race && fmodf(g.globalTime, 0.5f) < 0.25f) { tt = {1, 0.4f, 0.3f, 1}; tb = {0.9f, 0.05f, 0.05f, 1}; }
        text(buf, cx, 84, 78, tt, tb, 1, 3);
        float gcx = cx, gcy = 262, R = 162;
        const float a0 = 158 * PI / 180, a1 = 22 * PI / 180;
        const int segs = 60;
        for (int i = 0; i < segs; i++) {
            float t0 = (float)i / segs, t1 = (float)(i + 1) / segs;
            float A0 = lerpf(a0, a1, t0), A1 = lerpf(a0, a1, t1);
            V4 col = t0 < 0.55f ? V4{lerpf(0.15f, 1.0f, t0 / 0.55f), lerpf(0.85f, 0.88f, t0 / 0.55f), lerpf(0.55f, 0.15f, t0 / 0.55f), 1}
                                : V4{1.0f, lerpf(0.88f, 0.12f, (t0 - 0.55f) / 0.45f), lerpf(0.15f, 0.12f, (t0 - 0.55f) / 0.45f), 1};
            line(gcx + cosf(A0) * R, gcy - sinf(A0) * R, gcx + cosf(A1) * R, gcy - sinf(A1) * R, 6, {0.02f, 0.04f, 0.1f, 0.85f});
            line(gcx + cosf(A0) * R, gcy - sinf(A0) * R, gcx + cosf(A1) * R, gcy - sinf(A1) * R, 3.5f, col);
        }
        for (int k = 0; k <= 10; k++) {
            float A = lerpf(a0, a1, k / 10.0f);
            float ca = cosf(A), sa = sinf(A);
            line(gcx + ca * (R - 4), gcy - sa * (R - 4), gcx + ca * (R + 9), gcy - sa * (R + 9), 3, kWhite);
            snprintf(buf, sizeof buf, "%d", k);
            text(buf, gcx + ca * (R + 22), gcy - sa * (R + 22) + 6, 15, kWhite, kWhite, 1, 1);
        }
        float rpmT = clampf(pc.rpm, 0, 1.0f);
        float A = lerpf(a0, a1, rpmT);
        line(gcx + cosf(A) * (R - 14), gcy - sinf(A) * (R - 14), gcx + cosf(A) * (R + 12), gcy - sinf(A) * (R + 12), 6, {0.02f, 0.02f, 0.05f, 1});
        line(gcx + cosf(A) * (R - 12), gcy - sinf(A) * (R - 12), gcx + cosf(A) * (R + 10), gcy - sinf(A) * (R + 10), 3, kGold);
        float mph = pc.speed() * 2.23694f;
        snprintf(buf, sizeof buf, "%d", (int)(mph + 0.5f));
        float sw = text(buf, cx + 12, 176, 56, kOrangeT, kOrangeB, 2, 1);
        (void)sw;
        text("MPH", cx + 20, 174, 18, kWhite, kWhite, 0, 1);
    }

    // ---------------- top-right: position + traffic radar
    {
        float rx = VW - 18;
        text("POSITION", rx, 28, 17, kWhite, kWhite, 2, 1, 0.1f);
        snprintf(buf, sizeof buf, "/%d", (int)g.cars.size());
        float w1 = text(buf, rx, 84, 26, kWhite, kWhite, 2, 1);
        snprintf(buf, sizeof buf, "%d", pc.place);
        text(buf, rx - w1 - 4, 88, 70, kOrangeT, kOrangeB, 2, 3);
        float px = VW - 118, py = 102, pw = 100, ph = 200;
        panel(px, py, pw, ph, 12);
        text("TRAFFIC", px + pw * 0.5f, py + 15, 10, {0.7f, 0.8f, 1, 1}, {0.7f, 0.8f, 1, 1}, 1, 5, 0.12f);
        for (int k = -1; k <= 1; k += 2) line(px + pw * 0.5f + k * 26, py + 22, px + pw * 0.5f + k * 26, py + ph - 8, 1.5f, {1, 1, 1, 0.18f});
        float cyP = py + ph * 0.62f;
        for (size_t i = 0; i < g.cars.size(); i++) {
            const Car& c = g.cars[i];
            float dd = c.dist - pc.dist, dl = c.lat - pc.lat;
            if (dd < -50 || dd > 70 || fabsf(dl) > 14) continue;
            float cx = px + pw * 0.5f + dl * 3.0f, cy = cyP - dd * 1.6f;
            if (cy < py + 24 || cy > py + ph - 10) continue;
            bool me = (int)i == g.player;
            if (me) rrect(cx - 8, cy - 13, 16, 26, 4, kWhite);
            rrect(cx - 6, cy - 11, 12, 22, 3, {c.liv.c1.x, c.liv.c1.y, c.liv.c1.z, 1});
            rect(cx - 4, cy - 6, 8, 5, {0.05f, 0.05f, 0.08f, 0.9f});
        }
    }

    // ---------------- bottom-left: minimap
    {
        float mx = 18, my = VH - 178, mw = 180, mh = 160;
        panel(mx, my, mw, mh, 14);
        V2 mn = geo.mapMin, mxp = geo.mapMax;
        float sc = fminf((mw - 30) / (mxp.x - mn.x), (mh - 30) / (mxp.y - mn.y));
        float ox = mx + mw * 0.5f - (mn.x + mxp.x) * 0.5f * sc, oy = my + mh * 0.5f + (mn.y + mxp.y) * 0.5f * sc;
        auto& mm = geo.minimap;
        for (size_t i = 0; i < mm.size(); i++) {
            V2 a = mm[i], b = mm[(i + 1) % mm.size()];
            line(ox + a.x * sc, oy - a.y * sc, ox + b.x * sc, oy - b.y * sc, 7, {0.05f, 0.08f, 0.18f, 1});
        }
        for (size_t i = 0; i < mm.size(); i++) {
            V2 a = mm[i], b = mm[(i + 1) % mm.size()];
            line(ox + a.x * sc, oy - a.y * sc, ox + b.x * sc, oy - b.y * sc, 3.5f, {0.95f, 0.97f, 1, 1});
        }
        V3 sl = g.track.at(0, 0);
        circle(ox + sl.x * sc, oy - sl.z * sc, 4, {1, 0.2f, 0.2f, 1});
        for (size_t i = 0; i < g.cars.size(); i++) {
            if ((int)i == g.player) continue;
            const Car& c = g.cars[i];
            circle(ox + c.pos.x * sc, oy - c.pos.z * sc, 4.5f, {0.02f, 0.02f, 0.05f, 1});
            circle(ox + c.pos.x * sc, oy - c.pos.z * sc, 3.5f, {c.liv.c1.x, c.liv.c1.y, c.liv.c1.z, 1});
        }
        circle(ox + pc.pos.x * sc, oy - pc.pos.z * sc, 7, {0.02f, 0.02f, 0.05f, 1});
        circle(ox + pc.pos.x * sc, oy - pc.pos.z * sc, 5.5f, kGold);
    }

    // ---------------- bottom-right: gear, shifter, boost, draft
    {
        float gx = VW - 160, gy = VH - 96;
        rrect(gx - 3, gy - 3, 66, 78, 9, {1, 1, 1, 0.9f});
        rrect(gx, gy, 60, 72, 7, {0.04f, 0.07f, 0.16f, 0.95f});
        snprintf(buf, sizeof buf, "%d", pc.gear);
        text(buf, gx + 30, gy + 52, 46, kWhite, kWhite, 1, 1);
        text("GEAR", gx + 30, gy + 67, 10, {0.7f, 0.8f, 1, 1}, {0.7f, 0.8f, 1, 1}, 1, 5, 0.1f);
        float sx = VW - 84, sy = VH - 92;
        rrect(sx, sy, 62, 70, 8, kPanel);
        V4 lc = {1, 1, 1, 0.7f};
        for (int k = 0; k < 3; k++) line(sx + 14 + k * 17, sy + 12, sx + 14 + k * 17, sy + 58, 2.5f, lc);
        line(sx + 14, sy + 35, sx + 48, sy + 35, 2.5f, lc);
        int gcol = (pc.gear - 1) / 2, grow = (pc.gear - 1) % 2;
        circle(sx + 14 + gcol * 17, grow ? sy + 58 : sy + 12, 6, kGold);
        // boost bar
        float bx = VW - 340, by = VH - 40;
        text("BOOST", bx, by - 8, 13, {0.7f, 0.8f, 1, 1}, {0.7f, 0.8f, 1, 1}, 0, 1, 0.1f);
        int segs = 12;
        for (int i = 0; i < segs; i++) {
            float f = (i + 0.5f) / segs;
            bool on = f <= pc.boost;
            V4 c = on ? (pc.boosting ? V4{0.35f, 0.9f, 1.0f, 1} : V4{lerpf(0.3f, 1.0f, f), lerpf(0.9f, 0.5f, f), 0.15f, 1}) : V4{1, 1, 1, 0.15f};
            rrect(bx + i * 14, by, 11, 16, 3, c);
        }
        if (pc.draft > 0.35f && g.state == State::Race) {
            float a = sat((pc.draft - 0.35f) * 4);
            rrect(VW - 160, VH - 140, 140, 34, 8, {0.04f, 0.07f, 0.16f, 0.85f * a});
            text("DRAFT", VW - 90, VH - 115, 24, {0.5f, 0.95f, 1.0f, a}, {0.2f, 0.65f, 1.0f, a}, 1, 1, 0.08f);
        }
    }

    // ---------------- centre banners
    for (const Banner& b : g.banners) {
        float age = b.dur - b.t;
        float pop = 1.0f + 0.45f * sat(1.0f - age / 0.16f);
        float a = sat(b.t / 0.3f);
        V4 t = {kOrangeT.x, kOrangeT.y, kOrangeT.z, a}, bo = {kOrangeB.x, kOrangeB.y, kOrangeB.z, a};
        float size = 96, y = VH * 0.36f;
        if (b.style == 1) {
            size = 200; y = VH * 0.56f;
            if (b.text == "GO!") { t = {0.85f, 1.0f, 0.35f, a}; bo = {0.20f, 0.78f, 0.10f, a}; }
            else { t = {1, 1, 1, a}; bo = {1.0f, 0.8f, 0.2f, a}; size = 150; }
        } else if (b.style == 2) continue;
        else if (b.style == 4) { size = 86; y = VH * 0.32f; }
        else if (b.style == 3) { size = 90; y = VH * 0.42f; }
        else if (b.style == 5) { size = 130; y = VH * 0.45f; t = {1, 0.5f, 0.4f, a}; bo = {0.9f, 0.05f, 0.05f, a}; }
        text(b.text.c_str(), VW * 0.5f, y, size * pop, t, bo, 1, 3, 0.01f);
        if (!b.sub.empty()) text(b.sub.c_str(), VW * 0.5f, y + 36, 26, {1, 1, 1, a}, {1, 1, 1, a}, 1, 1, 0.04f);
    }

    // ---------------- end panels
    if ((g.state == State::Finished || g.state == State::TimeUp) && g.stateTime > 1.8f) {
        float a = sat((g.stateTime - 1.8f) * 2);
        float pw = 620, ph = 280, px = VW * 0.5f - pw * 0.5f, py = VH * 0.5f - 60;
        rrect(px - 3, py - 3, pw + 6, ph + 6, 18, {kPanelEdge.x, kPanelEdge.y, kPanelEdge.z, a});
        rrect(px, py, pw, ph, 16, {kPanel.x, kPanel.y, kPanel.z, 0.92f * a});
        if (g.state == State::Finished) {
            snprintf(buf, sizeof buf, "P%d OF %d", pc.place, (int)g.cars.size());
            text(buf, VW * 0.5f, py + 90, 72, {kOrangeT.x, kOrangeT.y, kOrangeT.z, a}, {kOrangeB.x, kOrangeB.y, kOrangeB.z, a}, 1, 3);
            char tb[32]; fmtTime(tb, sizeof tb, pc.finishTime);
            snprintf(buf, sizeof buf, "RACE TIME  %s", tb);
            text(buf, VW * 0.5f, py + 140, 30, {1, 1, 1, a}, {1, 1, 1, a}, 1, 1);
            fmtTime(tb, sizeof tb, pc.bestLap);
            snprintf(buf, sizeof buf, "BEST LAP  %s", tb);
            text(buf, VW * 0.5f, py + 180, 26, {kGold.x, kGold.y, kGold.z, a}, {kGold.x, kGold.y, kGold.z, a}, 1, 1);
        } else {
            text("TIME UP", VW * 0.5f, py + 100, 80, {1, 0.5f, 0.4f, a}, {0.9f, 0.05f, 0.05f, a}, 1, 3);
            snprintf(buf, sizeof buf, "YOU REACHED P%d", pc.place);
            text(buf, VW * 0.5f, py + 160, 30, {1, 1, 1, a}, {1, 1, 1, a}, 1, 1);
        }
        float blink = 0.5f + 0.5f * sinf(g.globalTime * 4);
        text("PRESS A TO RACE AGAIN", VW * 0.5f, py + ph - 30, 26, {1, 1, 1, a * (0.5f + 0.5f * blink)}, {1, 1, 1, a * (0.5f + 0.5f * blink)}, 1, 1, 0.08f);
    }
    if (g.state == State::Paused) {
        rect(0, 0, VW, VH, {0, 0, 0, 0.5f});
        text("PAUSED", VW * 0.5f, VH * 0.45f, 120, kOrangeT, kOrangeB, 1, 3);
        text("START  RESUME        SELECT  RESTART", VW * 0.5f, VH * 0.45f + 70, 28, kWhite, kWhite, 1, 1, 0.05f);
    }
    flushHud(VW, VH, V3(0.03f, 0.06f, 0.18f));
}
