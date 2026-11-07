// GLSL ES 3.00 shader sources.
#pragma once

static const char* kHeader = R"(#version 300 es
precision highp float;
)";

// Shared lighting / sky / fog. u_rain switches between the sunny and the storm look.
static const char* kSkyLib = R"(
uniform vec3 u_sunDir;
uniform float u_flash;
uniform float u_rain;
uniform vec3 u_tunA; uniform vec3 u_tunB;
vec3 skyBase(vec3 d) {
    float h = clamp(d.y, 0.0, 1.0);
    float sd = max(dot(d, u_sunDir), 0.0);
    vec3 sunny = mix(vec3(0.66, 0.84, 0.98), vec3(0.10, 0.38, 0.86), pow(h, 0.55));
    sunny += vec3(1.0, 0.95, 0.8) * pow(sd, 8.0) * 0.25;
    vec3 storm = mix(vec3(0.58, 0.62, 0.66), vec3(0.24, 0.29, 0.36), pow(h, 0.55));
    storm += vec3(1.0, 0.92, 0.75) * pow(sd, 5.0) * 0.22;
    return mix(sunny, storm, u_rain) + u_flash * vec3(0.45, 0.5, 0.62);
}
vec3 fogColor(vec3 d) {
    vec3 dd = normalize(vec3(d.x, max(d.y, 0.0) * 0.25 + 0.03, d.z));
    return skyBase(dd) * mix(1.0, 0.97, u_rain);
}
float fogAmount(float dist, float y) {
    float dens = mix(0.00075, 0.0017, u_rain);
    float f = 1.0 - exp(-dist * dens);
    return clamp(f * (1.0 - clamp((y - 60.0) / 600.0, 0.0, 0.4)), 0.0, 1.0);
}
// 1 inside the tunnel tube, 0 outside
float tunnelMask(vec3 p) {
    vec3 ab = u_tunB - u_tunA;
    float t = clamp(dot(p - u_tunA, ab) / dot(ab, ab), 0.0, 1.0);
    vec3 q = u_tunA + ab * t;
    float d = length((p - q).xz);
    float inside = (1.0 - smoothstep(13.0, 14.5, d)) * (1.0 - smoothstep(7.2, 7.8, p.y));
    float ends = smoothstep(0.0, 0.03, t) * smoothstep(1.0, 0.97, t);
    return inside * ends;
}
vec3 sunCol() { return mix(vec3(1.0, 0.96, 0.88) * 1.18, vec3(1.0, 0.95, 0.85) * 0.55, u_rain); }
vec3 ambient(vec3 N) {
    vec3 sky = mix(vec3(0.56, 0.68, 0.86), vec3(0.64, 0.69, 0.76), u_rain);
    vec3 gnd = mix(vec3(0.36, 0.40, 0.30), vec3(0.28, 0.29, 0.28), u_rain);
    return mix(gnd, sky, N.y * 0.5 + 0.5) * mix(0.78, 0.88, u_rain);
}
)";

static const char* kNoiseLib = R"(
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
vec2 hash22(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973)); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.xx + p3.yz) * p3.zy); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash12(i), b = hash12(i + vec2(1, 0)), c = hash12(i + vec2(0, 1)), d = hash12(i + vec2(1, 1));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
)";

// ---------------------------------------------------------------- world & cars
static const char* kWorldVS = R"(
in vec3 a_pos; in vec3 a_nrm; in vec4 a_col; in float a_aux;
uniform mat4 u_vp; uniform mat4 u_model;
out vec3 v_world; out vec3 v_nrm; out vec4 v_col; out float v_aux; out vec3 v_local; out vec3 v_lnrm;
void main() {
    vec4 wp = u_model * vec4(a_pos, 1.0);
    v_world = wp.xyz; v_nrm = mat3(u_model) * a_nrm; v_col = a_col; v_aux = a_aux; v_local = a_pos; v_lnrm = a_nrm;
    gl_Position = u_vp * wp;
}
)";
static const char* kWorldFS = R"(
in vec3 v_world; in vec3 v_nrm; in vec4 v_col; in float v_aux; in vec3 v_local; in vec3 v_lnrm;
uniform vec3 u_cam; uniform float u_clip; uniform float u_fade; uniform float u_time;
uniform vec3 u_rim;
uniform sampler2D u_livery; uniform sampler2D u_texA; uniform sampler2D u_texG; uniform sampler2D u_texR; uniform sampler2D u_texC;
out vec4 o;
const float D = 128.0;
vec2 liveryUV(vec3 p, vec3 n) {
    vec2 px;
    if (n.y > 0.55) px = vec2((1.1 - p.x) * D, 360.0 + (2.6 - p.z) * D);
    else if (abs(n.x) >= abs(n.z)) {
        if (n.x > 0.0) px = vec2((2.6 - p.z) * D, (1.5 - p.y) * D);
        else px = vec2((p.z + 2.6) * D, 180.0 + (1.5 - p.y) * D);
    } else if (n.z < 0.0) px = vec2(300.0 + (1.1 - p.x) * D, 360.0 + (1.5 - p.y) * D);
    else px = vec2(300.0 + (p.x + 1.1) * D, 560.0 + (1.5 - p.y) * D);
    return vec2(px.x / 704.0, 1.0 - px.y / 1032.0);
}
void main() {
    if (u_clip > 0.5 && v_world.y < -0.02) discard;
    if (u_fade < 0.999 && hash12(floor(gl_FragCoord.xy)) > u_fade) discard;
    int mat = int(v_aux + 0.5);
    vec3 N = normalize(v_nrm);
    vec3 V = normalize(u_cam - v_world);
    bool smoothBody = (mat == 1 || mat == 2);
    if (!smoothBody && !gl_FrontFacing) N = -N;
    vec3 base = v_col.rgb;
    float gloss = 0.0, spPow = 40.0, envAmt = 0.0;
    vec2 wxz = v_world.xz;
    if (mat == 1) {
        vec3 ln = normalize(v_lnrm);
        base = texture(u_livery, liveryUV(v_local, ln)).rgb;
        gloss = 1.0; spPow = 90.0; envAmt = 0.35;
    } else if (mat == 2) { gloss = 1.0; spPow = 160.0; envAmt = 0.75; }
    else if (mat == 3) { gloss = 0.7; spPow = 50.0; envAmt = 0.25; }
    else if (mat == 4) {
        float g1 = texture(u_texG, wxz * 0.11).r, g2 = texture(u_texG, wxz * 0.017).r;
        base *= 0.78 + 0.32 * g1 + 0.18 * (g2 - 0.5);
    } else if (mat == 5) {
        vec3 an = abs(N);
        vec3 tr = texture(u_texR, v_world.zy * 0.06).rgb * an.x + texture(u_texR, v_world.xz * 0.06).rgb * an.y + texture(u_texR, v_world.xy * 0.06).rgb * an.z;
        base *= tr / max(an.x + an.y + an.z, 0.001) * 1.25;
    } else if (mat == 6) {
        vec2 cu = vec2((v_world.x + v_world.z) * 0.08, v_world.y * 0.9);
        cu.y += sin(u_time * 7.0 + floor(cu.x * 2.0) * 1.7) * 0.03;
        base = texture(u_texC, cu).rgb;
    } else if (mat == 7) {
        vec2 fu = vec2((v_world.x + v_world.z) * 2.2, v_world.y * 2.2);
        float a1 = abs(fract(fu.x + fu.y) - 0.5), a2 = abs(fract(fu.x - fu.y) - 0.5);
        float wire = max(smoothstep(0.42, 0.48, a1), smoothstep(0.42, 0.48, a2));
        float dist = length(u_cam - v_world);
        wire = mix(wire, 0.55, smoothstep(25.0, 80.0, dist));
        if (wire < 0.3) discard;
        gloss = 0.3;
    } else if (mat == 8) {
        vec2 w = wxz * 0.08 + vec2(u_time * 0.05, u_time * 0.03);
        vec3 wn = normalize(vec3((vnoise(w * 3.0) - 0.5) * 0.3, 1.0, (vnoise(w * 3.0 + 7.0) - 0.5) * 0.3));
        N = wn;
        float fr = 0.04 + 0.96 * pow(1.0 - max(dot(N, V), 0.0), 5.0);
        vec3 R = reflect(-V, N);
        vec3 col = mix(base, skyBase(normalize(vec3(R.x, abs(R.y), R.z))), fr * 0.8);
        col += sunCol() * pow(max(dot(R, u_sunDir), 0.0), 220.0) * 2.0;
        float dist = length(u_cam - v_world);
        o = vec4(mix(col, fogColor((v_world - u_cam) / dist), fogAmount(dist, v_world.y)), 1.0);
        return;
    } else if (mat == 9 || mat == 13) {
        base *= 0.86 + 0.24 * texture(u_texA, wxz * 0.25 + v_world.y * 0.25).r;
    } else if (mat == 10) { gloss = 0.15; spPow = 20.0; }
    else if (mat == 11) { gloss = 0.35; spPow = 60.0; envAmt = 0.08; }
    else if (mat == 12) { base *= 0.9 + 0.2 * vnoise(v_world.xz * 0.7 + v_world.y); }
    else if (mat == 14) { base = u_rim; gloss = 1.0; spPow = 70.0; envAmt = 0.3; }
    else if (mat == 15) { gloss = 0.7; spPow = 60.0; envAmt = 0.18; }

    float tun = tunnelMask(v_world);
    float ndl = max(dot(N, u_sunDir), 0.0);
    if (mat == 12) ndl = ndl * 0.7 + 0.3 * max(dot(-N, u_sunDir), 0.0) + 0.15;  // leaves: soft wrap light
    vec3 light = ambient(N) * (1.0 - tun * 0.45) + sunCol() * ndl * (1.0 - tun * 0.92) + u_flash * 0.5;
    light += tun * vec3(0.55, 0.38, 0.22) * (0.6 + 0.4 * sin(v_world.z * 0.785 + v_world.x * 0.785));
    vec3 col = base * light;
    vec3 H = normalize(V + u_sunDir);
    col += sunCol() * pow(max(dot(N, H), 0.0), spPow) * gloss * (1.0 - tun);
    if (envAmt > 0.0) {
        float fr = pow(1.0 - max(dot(N, V), 0.0), 3.0);
        vec3 R = reflect(-V, N);
        vec3 env = skyBase(normalize(vec3(R.x, abs(R.y) + 0.05, R.z)));
        if (R.y < 0.0) env = mix(env, vec3(0.30, 0.32, 0.30), 0.7);
        env *= 1.0 - tun * 0.7;
        col = mix(col, env, clamp(fr * envAmt * 1.6 + envAmt * 0.12, 0.0, 0.9));
    }
    col = mix(col, base * 1.5, v_col.a);
    vec3 d = v_world - u_cam;
    float dist = length(d);
    col = mix(col, fogColor(d / dist), fogAmount(dist, v_world.y));
    o = vec4(col, 1.0);
}
)";

// ---------------------------------------------------------------- road
static const char* kRoadVS = R"(
in vec3 a_pos; in vec4 a_road;
uniform mat4 u_vp;
out vec3 v_world; out vec4 v_ls;
void main() { v_world = a_pos; v_ls = a_road; gl_Position = u_vp * vec4(a_pos, 1.0); }
)";
static const char* kRoadFS = R"(
in vec3 v_world; in vec4 v_ls;
uniform vec3 u_cam; uniform float u_time; uniform float u_halfW;
uniform vec2 u_res; uniform sampler2D u_refl; uniform sampler2D u_reflSharp; uniform sampler2D u_texA;
out vec4 o;
float band(float x, float c, float w, float soft) { return 1.0 - smoothstep(w - soft, w + soft, abs(x - c)); }
vec2 ripple(vec2 p, float t) {
    vec2 cell = floor(p), f = fract(p);
    vec2 c = hash22(cell) * 0.6 + 0.2;
    float ph = fract(t * 1.3 + hash12(cell + 3.7));
    vec2 dv = f - c;
    float d = length(dv) + 1e-4;
    float w = exp(-pow((d - ph * 0.42) * 22.0, 2.0)) * (1.0 - ph);
    return dv / d * w;
}
void main() {
    vec2 wp = v_world.xz;
    float lat = v_ls.x, s = v_ls.y, curv = v_ls.w, a = abs(lat);
    float dist = length(u_cam - v_world);
    float far = smoothstep(40.0, 160.0, dist);

    // asphalt with aggregate grain
    float g1 = texture(u_texA, wp * 0.42).r, g2 = texture(u_texA, wp * 0.09).r;
    float tone = 0.40 + 0.06 * (vnoise(wp * 0.018) - 0.5) + 0.05 * (vnoise(wp * 0.11) - 0.5);
    vec3 asph = vec3(tone, tone, tone * 1.04) * (0.80 + 0.32 * mix(g1, 0.5, far * 0.7) + 0.10 * (g2 - 0.5));

    // repair patches (sealed rectangles)
    vec2 pc = vec2(floor((lat + 20.0) / 4.0), floor(s / 11.0));
    float ph = hash12(pc + 17.0);
    if (ph < 0.09) {
        vec2 lp = vec2(fract((lat + 20.0) / 4.0), fract(s / 11.0));
        vec2 inset = hash22(pc) * 0.15 + 0.05;
        float inP = step(inset.x, lp.x) * step(lp.x, 1.0 - inset.y) * step(inset.y, lp.y) * step(lp.y, 1.0 - inset.x);
        asph *= mix(1.0, ph < 0.045 ? 0.82 : 1.08, inP);
        float edge = inP * (1.0 - step(inset.x + 0.02, lp.x) * step(lp.x, 0.98 - inset.y) * step(inset.y + 0.012, lp.y) * step(lp.y, 0.988 - inset.x));
        asph *= 1.0 - edge * 0.45 * (1.0 - far);
    }
    // cracks
    float cn = vnoise(wp * 0.35 + 4.0);
    float crack = (1.0 - smoothstep(0.006, 0.02, abs(cn - 0.5))) * smoothstep(0.62, 0.7, vnoise(wp * 0.04));
    asph *= 1.0 - crack * 0.5 * (1.0 - far);

    // worn tyre lanes: two darker bands per lane + darker racing line
    float wear = 0.0;
    for (int k = -1; k <= 1; k++) {
        float lc = float(k) * 6.67;
        wear += band(lat, lc - 0.95, 0.42, 0.25) + band(lat, lc + 0.95, 0.42, 0.25);
    }
    float rl = clamp(curv * 1300.0, -6.5, 6.5);
    wear += 0.8 * (band(lat, rl - 0.95, 0.5, 0.35) + band(lat, rl + 0.95, 0.5, 0.35));
    wear *= 0.65 + 0.35 * vnoise(vec2(lat * 3.0, s * 0.08));
    asph *= 1.0 - clamp(wear, 0.0, 1.2) * 0.14;

    // permanent rubber marks: braking zones and corners
    float kAbs = abs(curv);
    float markMask = smoothstep(0.003, 0.012, kAbs);
    float marks = 0.0;
    for (int i = 0; i < 7; i++) {
        float fi = float(i);
        float seg = floor(s / 34.0 + fi * 0.37);
        float on = step(0.45 - markMask * 0.35, hash12(vec2(seg, fi)));
        float c = (hash12(vec2(fi, 3.0)) - 0.5) * 13.0 + rl * 0.6;
        float wob = sin(s * (0.03 + fi * 0.011) + fi * 2.1) * (0.6 + 2.2 * markMask) + curv * 220.0 * sin(s * 0.02 + fi);
        float m = band(lat, c + wob, 0.12 + 0.05 * hash12(vec2(fi, 9.0)), 0.06);
        float fade = smoothstep(0.0, 0.25, fract(s / 34.0 + fi * 0.37)) * smoothstep(1.0, 0.7, fract(s / 34.0 + fi * 0.37));
        marks = max(marks, m * on * fade * (0.35 + 0.65 * markMask) * (0.6 + 0.4 * g1));
    }
    asph = mix(asph, vec3(0.07, 0.07, 0.075), marks * 0.75 * (1.0 - far * 0.5));

    // markings: thick dashes + solid edge lines
    float edge = step(u_halfW - 0.75, a) * step(a, u_halfW - 0.25);
    float dash = band(a, 3.33, 0.20, 0.02) * step(fract(s / 15.0), 0.5);
    float mark = max(edge, dash) * (0.82 + 0.18 * g1) * (1.0 - marks * 0.5);
    vec3 col = mix(asph, vec3(0.93, 0.93, 0.91), mark);
    if (s < 2.4) {
        float ck = mod(floor((lat + 20.0) / 1.2) + floor(s / 1.2), 2.0);
        col = mix(vec3(0.07), vec3(0.94), ck);
        mark = 1.0;
    }

    float tun = v_ls.z;
    vec3 N = vec3(0.0, 1.0, 0.0);
    vec3 V = normalize(u_cam - v_world);
    vec3 light = ambient(N) * (1.0 - tun * 0.5) + sunCol() * max(u_sunDir.y, 0.0) * (1.0 - tun * 0.92);
    light += tun * vec3(0.6, 0.42, 0.24) * (0.55 + 0.45 * cos(s * 0.785));
    vec3 lit = col * light;
    // dry sheen at grazing angles
    vec3 Hd = normalize(V + u_sunDir);
    lit += sunCol() * pow(max(Hd.y, 0.0), 30.0) * 0.06 * (1.0 - u_rain) * (1.0 - tun);

    if (u_rain > 0.5) {
        float n1 = vnoise(wp * 0.07);
        float puddle = smoothstep(0.58, 0.78, n1 * 0.75 + vnoise(wp * 0.21 + 3.0) * 0.45);
        puddle = max(puddle, smoothstep(u_halfW - 2.6, u_halfW - 0.3, a) * 0.75);
        float rf = 1.0 - smoothstep(8.0, 45.0, dist);
        vec2 rp = (ripple(wp * 1.8, u_time) + ripple(wp * 2.7 + 5.3, u_time * 1.17) * 0.7) * rf * 0.5;
        vec2 bump = (vec2(g1, g2) - 0.5) * 0.10 * (1.0 - puddle);
        vec3 Nw = normalize(vec3(rp.x * 0.45 + bump.x, 1.0, rp.y * 0.45 + bump.y));
        float fres = 0.03 + 0.97 * pow(1.0 - max(dot(Nw, V), 0.0), 5.0);
        float wet = mix(0.55, 1.0, puddle) * (1.0 - mark * 0.35);
        vec2 suv = gl_FragCoord.xy / u_res + Nw.xz * vec2(0.03, 0.05) * (1.0 - puddle * 0.6);
        vec3 refl = mix(texture(u_refl, suv).rgb, texture(u_reflSharp, suv).rgb, puddle * 0.8);
        refl = refl / (1.0 + max(dot(refl, vec3(0.33)) - 0.55, 0.0) * 1.2);
        float ramt = max(clamp(fres * 1.25, 0.0, 1.0) * wet, puddle * 0.42);
        lit = lit * 0.55 * (1.0 - ramt * 0.85) + refl * ramt;
        vec3 H = normalize(V + u_sunDir);
        float nh = max(dot(Nw, H), 0.0);
        lit += vec3(1.0, 0.94, 0.82) * (pow(nh, 180.0) * 1.1 + pow(nh, 22.0) * 0.05) * wet;
        lit += u_flash * 0.25 * ramt;
    }
    vec3 d = v_world - u_cam;
    lit = mix(lit, fogColor(d / dist), fogAmount(dist, 0.0));
    o = vec4(lit, 1.0);
}
)";

// ---------------------------------------------------------------- skid marks
static const char* kSkidVS = R"(
in vec3 a_pos; in vec2 a_au;
uniform mat4 u_vp;
out vec2 v_au; out vec3 v_world;
void main() { v_au = a_au; v_world = a_pos; gl_Position = u_vp * vec4(a_pos, 1.0); }
)";
static const char* kSkidFS = R"(
in vec2 v_au; in vec3 v_world;
uniform vec3 u_cam;
out vec4 o;
void main() {
    float u = v_au.y;
    float edge = smoothstep(0.0, 0.18, u) * smoothstep(1.0, 0.82, u);
    float tread = 0.72 + 0.28 * sin(u * 31.4);
    float a = v_au.x * edge * tread * (0.8 + 0.2 * vnoise(v_world.xz * 2.0));
    float dist = length(u_cam - v_world);
    a *= 1.0 - fogAmount(dist, 0.0);
    o = vec4(0.05, 0.05, 0.055, a * 0.85);
}
)";

// ---------------------------------------------------------------- text (world signs)
static const char* kTextVS = R"(
in vec3 a_pos; in vec2 a_uv; in vec4 a_col;
uniform mat4 u_vp; uniform mat4 u_model;
out vec3 v_world; out vec2 v_uv; out vec4 v_col;
void main() { vec4 wp = u_model * vec4(a_pos, 1.0); v_world = wp.xyz; v_uv = a_uv; v_col = a_col; gl_Position = u_vp * wp; }
)";
static const char* kTextFS = R"(
in vec3 v_world; in vec2 v_uv; in vec4 v_col;
uniform sampler2D u_font; uniform vec3 u_cam; uniform float u_clip;
out vec4 o;
void main() {
    if (u_clip > 0.5 && v_world.y < -0.02) discard;
    float d = texture(u_font, v_uv).r;
    float w = max(fwidth(d) * 0.7, 0.004);
    float fill = smoothstep(0.5 - w, 0.5 + w, d);
    if (fill < 0.02) discard;
    float tun = tunnelMask(v_world);
    vec3 col = v_col.rgb * (0.95 - tun * 0.4 + u_flash * 0.5);
    vec3 dd = v_world - u_cam; float dist = length(dd);
    col = mix(col, fogColor(dd / dist), fogAmount(dist, v_world.y));
    o = vec4(col, fill * v_col.a);
}
)";

// ---------------------------------------------------------------- sky
static const char* kFullVS = R"(
out vec2 v_uv; out vec2 v_ndc;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    v_uv = p; v_ndc = p * 2.0 - 1.0;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
static const char* kSkyFS = R"(
in vec2 v_ndc;
uniform vec3 u_fwd; uniform vec3 u_right; uniform vec3 u_up; uniform vec2 u_tan; uniform float u_time;
out vec4 o;
float fbm(vec2 p) { float s = 0.0, a = 0.5; for (int i = 0; i < 5; i++) { s += vnoise(p) * a; p = p * 2.02 + vec2(1.7, 9.2); a *= 0.5; } return s; }
void main() {
    vec3 d = normalize(u_fwd + u_right * v_ndc.x * u_tan.x + u_up * v_ndc.y * u_tan.y);
    vec3 c = skyBase(d);
    float sd = max(dot(d, u_sunDir), 0.0);
    if (d.y > -0.05) {
        float dy = max(d.y, 0.0);
        vec2 p = d.xz / (dy + 0.12) * 1.3 + vec2(u_time * 0.010, u_time * 0.004);
        if (u_rain < 0.5) {
            // puffy cumulus: bright tops, soft blue-grey bases
            float n = fbm(p * 0.55);
            float dens = smoothstep(0.50, 0.74, n);
            vec2 toSun = normalize(u_sunDir.xz + 1e-4) * 0.12;
            float n2 = fbm(p * 0.55 + toSun);
            float shade = clamp(0.62 + (n - n2) * 4.5, 0.25, 1.0);
            vec3 cl = mix(vec3(0.62, 0.70, 0.82), vec3(1.0, 1.0, 1.0), shade);
            cl += vec3(1.0, 0.95, 0.85) * pow(sd, 10.0) * 0.25;
            float fade = smoothstep(-0.02, 0.16, d.y) * (1.0 - smoothstep(0.75, 1.0, dy) * 0.4);
            c = mix(c, cl, dens * fade * 0.95);
            c += vec3(1.0, 0.98, 0.9) * smoothstep(0.9993, 0.9998, sd) * 2.0 * (1.0 - dens);
            c += vec3(1.0, 0.95, 0.8) * pow(sd, 60.0) * 0.6;
        } else {
            float n = fbm(p * 0.8), n2 = fbm(p * 2.1 + 7.0);
            float cov = smoothstep(0.30, 0.68, n * 0.78 + n2 * 0.32);
            float thick = smoothstep(0.38, 0.85, n * 0.9 + n2 * 0.2);
            vec3 cl = mix(vec3(0.70, 0.72, 0.76), vec3(0.15, 0.17, 0.21), thick);
            cl += vec3(1.0, 0.88, 0.68) * pow(sd, 6.0) * (1.0 - thick) * 0.7;
            cl += u_flash * vec3(0.6, 0.65, 0.8) * (1.0 - thick * 0.5);
            c = mix(c, cl, cov * smoothstep(-0.02, 0.22, d.y) * 0.96);
            c += vec3(1.0, 0.96, 0.86) * smoothstep(0.9990, 0.9996, sd) * 1.6 * (1.0 - cov * 0.8);
            c += vec3(1.0, 0.9, 0.72) * pow(sd, 48.0) * (1.0 - cov * 0.55) * 0.55;
        }
    }
    o = vec4(c, 1.0);
}
)";

// ---------------------------------------------------------------- rain streaks & particles
static const char* kRainVS = R"(
in vec4 a_seed;
uniform mat4 u_vp; uniform vec3 u_cam; uniform float u_time; uniform vec3 u_box; uniform vec3 u_rel;
out float v_a;
void main() {
    vec3 fall = vec3(1.5, -24.0, 0.8);
    vec3 p = mod(a_seed.xyz * u_box + fall * u_time - u_cam, u_box) - u_box * 0.5;
    vec3 wp = u_cam + p - u_rel * 0.028 * a_seed.w;
    float dist = length(p);
    v_a = smoothstep(0.6, 2.0, dist) * (1.0 - smoothstep(14.0, 30.0, dist));
    gl_Position = u_vp * vec4(wp, 1.0);
}
)";
static const char* kRainFS = R"(
in float v_a;
out vec4 o;
void main() { o = vec4(0.78, 0.82, 0.88, v_a * (0.30 + u_flash * 0.4)); }
)";
// particles: pos, uv, rgba, type
static const char* kPartVS = R"(
in vec3 a_pos; in vec2 a_uv; in vec4 a_col; in float a_type;
uniform mat4 u_vp;
out vec2 v_uv; out vec4 v_col; out float v_type; out vec3 v_world;
void main() { v_uv = a_uv; v_col = a_col; v_type = a_type; v_world = a_pos; gl_Position = u_vp * vec4(a_pos, 1.0); }
)";
static const char* kPartFS = R"(
in vec2 v_uv; in vec4 v_col; in float v_type; in vec3 v_world;
uniform vec3 u_cam;
out vec4 o;
void main() {
    int t = int(v_type + 0.5);
    float dist = length(v_world - u_cam);
    if (t == 1 || t == 2) {          // sparks / flames (additive)
        float r = t == 1 ? abs(v_uv.y) : length(v_uv);
        float a = t == 1 ? (1.0 - smoothstep(0.2, 1.0, r)) : pow(clamp(1.0 - r, 0.0, 1.0), 1.5);
        vec3 col = t == 1 ? mix(v_col.rgb, vec3(1.0, 1.0, 0.85), 1.0 - r) : mix(v_col.rgb, vec3(1.0, 0.95, 0.6), pow(1.0 - r, 3.0));
        o = vec4(col * a * v_col.a * 1.6, 1.0);
        return;
    }
    if (t == 4) {                    // confetti: solid little cards
        vec3 col = v_col.rgb * (0.85 + 0.25 * sunCol().r);
        col = mix(col, fogColor((v_world - u_cam) / dist), fogAmount(dist, v_world.y));
        o = vec4(col, 1.0);
        return;
    }
    float r = length(v_uv);
    float a = pow(clamp(1.0 - r, 0.0, 1.0), 1.4) * v_col.a;
    float n = vnoise(v_uv * 2.5 + v_world.xz * 0.4);
    a *= 0.6 + 0.8 * n;
    a *= smoothstep(0.6, 2.5, dist);
    if (a < 0.003) discard;
    vec3 col = v_col.rgb * (ambient(vec3(0, 1, 0)) * 0.9 + sunCol() * 0.45 * (0.7 + 0.3 * n));
    col = mix(col, fogColor((v_world - u_cam) / dist), fogAmount(dist, v_world.y));
    o = vec4(col, a);
}
)";

// ---------------------------------------------------------------- post
static const char* kReflBlurFS = R"(
in vec2 v_uv;
uniform sampler2D u_tex; uniform vec2 u_texel;
out vec4 o;
void main() {
    vec3 c = vec3(0.0); float ws = 0.0;
    for (int i = -7; i <= 7; i++) {
        float fi = float(i);
        float w = exp(-fi * fi / 24.0);
        c += texture(u_tex, v_uv + vec2(fi * u_texel.x * 0.35, fi * u_texel.y * 2.2)).rgb * w;
        ws += w;
    }
    o = vec4(c / ws, 1.0);
}
)";
static const char* kBrightFS = R"(
in vec2 v_uv;
uniform sampler2D u_tex; uniform vec2 u_texel; uniform float u_thresh;
out vec4 o;
void main() {
    vec3 c = vec3(0.0);
    c += texture(u_tex, v_uv + u_texel * vec2(-1.0, -1.0)).rgb;
    c += texture(u_tex, v_uv + u_texel * vec2(1.0, -1.0)).rgb;
    c += texture(u_tex, v_uv + u_texel * vec2(-1.0, 1.0)).rgb;
    c += texture(u_tex, v_uv + u_texel * vec2(1.0, 1.0)).rgb;
    c *= 0.25;
    float l = max(max(c.r, c.g), c.b);
    o = vec4(c * smoothstep(u_thresh, 1.0, l), 1.0);
}
)";
static const char* kBlurFS = R"(
in vec2 v_uv;
uniform sampler2D u_tex; uniform vec2 u_dir;
out vec4 o;
void main() {
    vec3 c = texture(u_tex, v_uv).rgb * 0.227027;
    c += texture(u_tex, v_uv + u_dir * 1.3846).rgb * 0.316216;
    c += texture(u_tex, v_uv - u_dir * 1.3846).rgb * 0.316216;
    c += texture(u_tex, v_uv + u_dir * 3.2308).rgb * 0.070270;
    c += texture(u_tex, v_uv - u_dir * 3.2308).rgb * 0.070270;
    o = vec4(c, 1.0);
}
)";
static const char* kCompositeFS = R"(
in vec2 v_uv;
uniform sampler2D u_scene; uniform sampler2D u_bloomA; uniform sampler2D u_bloomB;
uniform vec2 u_res; uniform float u_time; uniform float u_flashC; uniform float u_bloom; uniform float u_sat; uniform float u_speedFx;
out vec4 o;
float h(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
void main() {
    vec2 uv = v_uv;
    vec3 c = texture(u_scene, uv).rgb;
    // subtle radial speed blur at the screen edges
    if (u_speedFx > 0.01) {
        vec2 dir = (uv - vec2(0.5, 0.55));
        float k = u_speedFx * smoothstep(0.25, 0.7, length(dir)) * 0.012;
        c = (c + texture(u_scene, uv - dir * k).rgb + texture(u_scene, uv - dir * k * 2.0).rgb) / 3.0;
    }
    vec3 b = texture(u_bloomA, uv).rgb * 0.55 + texture(u_bloomB, uv).rgb * 0.75;
    c += b * u_bloom;
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(l), c, u_sat);
    c = (c - 0.45) * 1.06 + 0.45;
    vec2 q = uv - 0.5;
    c *= 1.0 - dot(q, q) * 0.45;
    c += (h(uv * u_res + fract(u_time) * 100.0) - 0.5) * 0.012;
    c += u_flashC * 0.12;
    o = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)";

// ---------------------------------------------------------------- shadow & HUD
static const char* kShadowVS = R"(
in vec3 a_pos;
uniform mat4 u_vp; uniform mat4 u_model;
out vec2 v_uv;
void main() { v_uv = a_pos.xz * vec2(1.0 / 1.35, 1.0 / 2.9); gl_Position = u_vp * u_model * vec4(a_pos.x, 0.02, a_pos.z, 1.0); }
)";
static const char* kShadowFS = R"(
in vec2 v_uv;
uniform float u_alpha;
out vec4 o;
void main() {
    vec2 q = abs(v_uv);
    float r = length(max(q - vec2(0.55, 0.75), 0.0)) / 0.4;
    float a = (1.0 - smoothstep(0.0, 1.0, r)) * u_alpha;
    o = vec4(0.02, 0.03, 0.05, a);
}
)";
static const char* kHudVS = R"(
in vec2 a_pos; in vec2 a_uv; in vec4 a_col; in float a_mode;
uniform vec2 u_size;
out vec2 v_uv; out vec4 v_col; out float v_mode;
void main() {
    v_uv = a_uv; v_col = a_col; v_mode = a_mode;
    gl_Position = vec4(a_pos.x / u_size.x * 2.0 - 1.0, 1.0 - a_pos.y / u_size.y * 2.0, 0.0, 1.0);
}
)";
static const char* kHudFS = R"(
in vec2 v_uv; in vec4 v_col; in float v_mode;
uniform sampler2D u_font; uniform vec3 u_outline;
out vec4 o;
void main() {
    int m = int(v_mode + 0.5);
    if (m == 0) { o = v_col; return; }
    if (m == 2) { float r = length(v_uv * 2.0 - 1.0); float a = 1.0 - smoothstep(1.0 - fwidth(r) * 1.5, 1.0, r); o = vec4(v_col.rgb, v_col.a * a); return; }
    float d = texture(u_font, v_uv).r;
    float w = max(fwidth(d) * 0.7, 0.01);
    float fill = smoothstep(0.5 - w, 0.5 + w, d);
    if (m == 5) { o = vec4(v_col.rgb, v_col.a * fill); return; }
    float ow = m == 3 ? 0.26 : 0.17;
    float edge = smoothstep(0.5 - ow - w, 0.5 - ow + w, d);
    vec3 col = mix(u_outline, v_col.rgb, fill);
    o = vec4(col, v_col.a * edge);
}
)";
