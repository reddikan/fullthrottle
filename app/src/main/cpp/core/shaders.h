// GLSL ES 3.00 shader sources.
#pragma once

static const char* kHeader = R"(#version 300 es
precision highp float;
)";

// Sky / fog colour shared by most shaders.
static const char* kSkyLib = R"(
uniform vec3 u_sunDir;
uniform float u_flash;
vec3 skyBase(vec3 d) {
    float h = clamp(d.y, 0.0, 1.0);
    vec3 hor = vec3(0.58, 0.62, 0.66);
    vec3 zen = vec3(0.24, 0.29, 0.36);
    vec3 c = mix(hor, zen, pow(h, 0.55));
    float sd = max(dot(d, u_sunDir), 0.0);
    c += vec3(1.0, 0.92, 0.75) * pow(sd, 5.0) * 0.22;
    return c + u_flash * vec3(0.45, 0.5, 0.62);
}
vec3 fogColor(vec3 d) {
    vec3 dd = normalize(vec3(d.x, max(d.y, 0.0) * 0.25 + 0.03, d.z));
    return skyBase(dd) * 0.97;
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
out vec3 v_world; out vec3 v_nrm; out vec4 v_col; out float v_aux; out vec3 v_local;
void main() {
    vec4 wp = u_model * vec4(a_pos, 1.0);
    v_world = wp.xyz; v_nrm = mat3(u_model) * a_nrm; v_col = a_col; v_aux = a_aux; v_local = a_pos;
    gl_Position = u_vp * wp;
}
)";
static const char* kWorldFS = R"(
in vec3 v_world; in vec3 v_nrm; in vec4 v_col; in float v_aux; in vec3 v_local;
uniform vec3 u_cam; uniform float u_clip; uniform float u_fogD;
uniform vec3 u_p1; uniform vec3 u_p2; uniform vec3 u_p3;
out vec4 o;
vec3 livery(vec3 p, vec3 N) {
    vec3 c = u_p1;
    if (N.y > 0.55 && abs(abs(p.x) - 0.26) < 0.10) c = u_p2;
    float band = p.y - (0.52 + 0.07 * p.z);
    if (abs(N.x) > 0.45 && band > -0.045 && band < 0.065) c = u_p2;
    if (abs(N.x) > 0.45 && p.z > 1.4 && p.y > 0.5) c = mix(c, u_p2, 0.0);
    if (N.z < -0.6) c = u_p1 * 0.9;
    if (p.y < 0.33) c = vec3(0.05, 0.05, 0.055);
    return c;
}
void main() {
    if (u_clip > 0.5 && v_world.y < -0.02) discard;
    vec3 N = normalize(v_nrm);
    if (!gl_FrontFacing) N = -N;
    vec3 base = v_col.rgb;
    float aux = v_aux;
    if (aux > 0.5 && aux < 1.5) base = livery(v_local, normalize(v_nrm) * (gl_FrontFacing ? 1.0 : -1.0));
    vec3 V = normalize(u_cam - v_world);
    float ndl = max(dot(N, u_sunDir), 0.0);
    vec3 amb = mix(vec3(0.28, 0.29, 0.28), vec3(0.64, 0.69, 0.76), N.y * 0.5 + 0.5);
    vec3 col = base * (amb * 0.88 + vec3(1.0, 0.95, 0.85) * ndl * 0.5 + u_flash * 0.5);
    float gloss = aux < 0.5 ? 0.0 : (aux < 1.5 ? 1.0 : (aux < 2.5 ? 1.0 : (aux < 3.5 ? 0.6 : 0.18)));
    float sp = aux < 3.5 ? 110.0 : 24.0;
    vec3 H = normalize(V + u_sunDir);
    col += vec3(1.0, 0.95, 0.85) * pow(max(dot(N, H), 0.0), sp) * gloss * 1.1;
    float fr = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    vec3 R = reflect(-V, N);
    vec3 env = skyBase(normalize(vec3(R.x, abs(R.y) + 0.05, R.z)));
    if (R.y < 0.0) env = mix(env, vec3(0.14, 0.15, 0.16), 0.65);
    float envAmt = aux > 1.5 && aux < 2.5 ? 0.85 : 0.55;
    col = mix(col, env, clamp(fr * gloss * envAmt + gloss * 0.06, 0.0, 1.0));
    col = mix(col, base * 1.5, v_col.a);
    vec3 d = v_world - u_cam;
    float dist = length(d);
    float f = 1.0 - exp(-dist * u_fogD);
    f *= 1.0 - clamp((v_world.y - 60.0) / 500.0, 0.0, 0.35);
    col = mix(col, fogColor(d / dist), clamp(f, 0.0, 1.0));
    o = vec4(col, 1.0);
}
)";

// ---------------------------------------------------------------- road
static const char* kRoadVS = R"(
in vec3 a_pos; in vec3 a_road;
uniform mat4 u_vp;
out vec3 v_world; out vec2 v_ls;
void main() { v_world = a_pos; v_ls = a_road.xy; gl_Position = u_vp * vec4(a_pos, 1.0); }
)";
static const char* kRoadFS = R"(
in vec3 v_world; in vec2 v_ls;
uniform vec3 u_cam; uniform float u_fogD; uniform float u_time; uniform float u_halfW; uniform float u_trackLen;
uniform vec2 u_res; uniform sampler2D u_refl; uniform sampler2D u_reflSharp;
out vec4 o;
vec2 ripple(vec2 p, float t) {
    vec2 cell = floor(p), f = fract(p);
    vec2 c = hash22(cell) * 0.6 + 0.2;
    float ph = fract(t * 1.3 + hash12(cell + 3.7));
    vec2 dv = f - c;
    float d = length(dv) + 1e-4;
    float r = ph * 0.42;
    float w = exp(-pow((d - r) * 22.0, 2.0)) * (1.0 - ph);
    return dv / d * w;
}
void main() {
    vec2 wp = v_world.xz;
    float lat = v_ls.x, s = v_ls.y, a = abs(lat);
    float n1 = vnoise(wp * 0.07), n2 = vnoise(wp * 0.55), n3 = vnoise(wp * 2.7);
    vec3 asphalt = vec3(0.105, 0.11, 0.12) * (0.82 + 0.28 * n2 + 0.12 * n3);
    // darker rubbered racing groove
    asphalt *= 1.0 - 0.18 * smoothstep(4.0, 0.0, abs(lat - sin(s * 0.01) * 2.0));
    float edge = step(u_halfW - 0.75, a) * step(a, u_halfW - 0.38);
    float dash = step(abs(a - 3.0), 0.11) * step(fract(s / 12.0), 0.45);
    float mark = max(edge, dash);
    vec3 col = mix(asphalt, vec3(0.78, 0.78, 0.76), mark * 0.9);
    if (s < 3.0) {
        float ck = mod(floor(lat / 1.0) + floor(s / 1.0), 2.0);
        col = mix(vec3(0.06), vec3(0.85), ck);
        mark = 1.0;
    }
    float dist = length(u_cam - v_world);
    float puddle = smoothstep(0.58, 0.78, n1 * 0.75 + vnoise(wp * 0.21 + 3.0) * 0.45);
    puddle = max(puddle, smoothstep(u_halfW - 2.6, u_halfW - 0.3, a) * 0.75);
    float rf = 1.0 - smoothstep(8.0, 45.0, dist);
    vec2 rp = (ripple(wp * 1.1, u_time) + ripple(wp * 1.7 + 5.3, u_time * 1.17) * 0.7) * rf * 0.6;
    vec2 bump = (vec2(n3, n2) - 0.5) * 0.10 * (1.0 - puddle);
    vec3 N = normalize(vec3(rp.x * 0.45 + bump.x, 1.0, rp.y * 0.45 + bump.y));
    vec3 V = normalize(u_cam - v_world);
    float fres = 0.03 + 0.97 * pow(1.0 - max(dot(N, V), 0.0), 5.0);
    float wet = mix(0.55, 1.0, puddle) * (1.0 - mark * 0.35);
    vec2 suv = gl_FragCoord.xy / u_res + N.xz * vec2(0.03, 0.05) * (1.0 - puddle * 0.6);
    vec3 rb = texture(u_refl, suv).rgb;
    vec3 rs = texture(u_reflSharp, suv).rgb;
    vec3 refl = mix(rb, rs, puddle * 0.8);
    refl = refl / (1.0 + max(dot(refl, vec3(0.33)) - 0.55, 0.0) * 1.2);
    float ramt = clamp(fres * 1.25, 0.0, 1.0) * wet;
    ramt = max(ramt, puddle * 0.42);
    col = col * (1.0 - ramt * 0.85) + refl * ramt;
    vec3 H = normalize(V + u_sunDir);
    float nh = max(dot(N, H), 0.0);
    float sp = pow(nh, 180.0) * 1.1 + pow(nh, 22.0) * 0.05;
    col += vec3(1.0, 0.94, 0.82) * sp * wet;
    col += u_flash * 0.25 * ramt;
    vec3 d = v_world - u_cam;
    float f = 1.0 - exp(-dist * u_fogD);
    col = mix(col, fogColor(d / dist), clamp(f, 0.0, 1.0));
    o = vec4(col, 1.0);
}
)";

// ---------------------------------------------------------------- text (world & car decals)
static const char* kTextVS = R"(
in vec3 a_pos; in vec2 a_uv; in vec4 a_col;
uniform mat4 u_vp; uniform mat4 u_model;
out vec3 v_world; out vec2 v_uv; out vec4 v_col;
void main() { vec4 wp = u_model * vec4(a_pos, 1.0); v_world = wp.xyz; v_uv = a_uv; v_col = a_col; gl_Position = u_vp * wp; }
)";
static const char* kTextFS = R"(
in vec3 v_world; in vec2 v_uv; in vec4 v_col;
uniform sampler2D u_font; uniform vec3 u_cam; uniform float u_fogD; uniform float u_clip; uniform float u_outline;
out vec4 o;
void main() {
    if (u_clip > 0.5 && v_world.y < -0.02) discard;
    float d = texture(u_font, v_uv).r;
    float w = max(fwidth(d) * 0.7, 0.004);
    float fill = smoothstep(0.5 - w, 0.5 + w, d);
    float edge = u_outline > 0.0 ? smoothstep(0.5 - u_outline - w, 0.5 - u_outline + w, d) : fill;
    if (edge < 0.02) discard;
    vec3 col = mix(vec3(0.03), v_col.rgb, u_outline > 0.0 ? fill : 1.0);
    col *= 0.85 + u_flash * 0.5;
    vec3 dd = v_world - u_cam; float dist = length(dd);
    float f = 1.0 - exp(-dist * u_fogD);
    col = mix(col, fogColor(dd / dist), clamp(f, 0.0, 1.0));
    o = vec4(col, edge * v_col.a);
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
    if (d.y > -0.05) {
        float dy = max(d.y, 0.0);
        vec2 p = d.xz / (dy + 0.10) * 1.4 + vec2(u_time * 0.012, u_time * 0.005);
        float n = fbm(p * 0.8);
        float n2 = fbm(p * 2.1 + 7.0);
        float cov = smoothstep(0.30, 0.68, n * 0.78 + n2 * 0.32);
        float thick = smoothstep(0.38, 0.85, n * 0.9 + n2 * 0.2);
        float sd = max(dot(d, u_sunDir), 0.0);
        vec3 dark = vec3(0.15, 0.17, 0.21), lite = vec3(0.70, 0.72, 0.76);
        vec3 cl = mix(lite, dark, thick);
        cl += vec3(1.0, 0.88, 0.68) * pow(sd, 6.0) * (1.0 - thick) * 0.7;
        cl += u_flash * vec3(0.6, 0.65, 0.8) * (1.0 - thick * 0.5);
        float fade = smoothstep(-0.02, 0.22, d.y);
        c = mix(c, cl, cov * fade * 0.96);
        float sun = smoothstep(0.9990, 0.9996, sd);
        c += vec3(1.0, 0.96, 0.86) * sun * 1.6 * (1.0 - cov * 0.8);
        c += vec3(1.0, 0.9, 0.72) * pow(sd, 48.0) * (1.0 - cov * 0.55) * 0.55;
    }
    o = vec4(c, 1.0);
}
)";

// ---------------------------------------------------------------- rain & spray
static const char* kRainVS = R"(
in vec4 a_seed;
uniform mat4 u_vp; uniform vec3 u_cam; uniform float u_time; uniform vec3 u_box; uniform vec3 u_rel;
out float v_a;
void main() {
    vec3 fall = vec3(1.5, -24.0, 0.8);
    vec3 p = mod(a_seed.xyz * u_box + fall * u_time - u_cam, u_box) - u_box * 0.5;
    vec3 wp = u_cam + p;
    float streak = a_seed.w;
    wp -= u_rel * 0.028 * streak;
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
static const char* kSprayVS = R"(
in vec3 a_pos; in vec3 a_uva;
uniform mat4 u_vp;
out vec3 v_uva; out vec3 v_world;
void main() { v_uva = a_uva; v_world = a_pos; gl_Position = u_vp * vec4(a_pos, 1.0); }
)";
static const char* kSprayFS = R"(
in vec3 v_uva; in vec3 v_world;
uniform vec3 u_cam; uniform float u_fogD; uniform vec3 u_tint;
out vec4 o;
void main() {
    float r = length(v_uva.xy);
    float a = pow(clamp(1.0 - r, 0.0, 1.0), 1.6) * v_uva.z;
    float n = vnoise(v_uva.xy * 3.0 + v_world.xz * 0.6);
    a *= 0.65 + 0.7 * n;
    if (a < 0.003) discard;
    float dist = length(v_world - u_cam);
    a *= smoothstep(0.8, 3.0, dist);
    vec3 col = u_tint * (0.95 + u_flash * 0.5);
    float f = 1.0 - exp(-dist * u_fogD);
    col = mix(col, fogColor((v_world - u_cam) / dist), clamp(f, 0.0, 1.0));
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
uniform sampler2D u_tex; uniform vec2 u_texel;
out vec4 o;
void main() {
    vec3 c = vec3(0.0);
    c += texture(u_tex, v_uv + u_texel * vec2(-1.0, -1.0)).rgb;
    c += texture(u_tex, v_uv + u_texel * vec2(1.0, -1.0)).rgb;
    c += texture(u_tex, v_uv + u_texel * vec2(-1.0, 1.0)).rgb;
    c += texture(u_tex, v_uv + u_texel * vec2(1.0, 1.0)).rgb;
    c *= 0.25;
    float l = max(max(c.r, c.g), c.b);
    float k = smoothstep(0.62, 1.0, l);
    o = vec4(c * k, 1.0);
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
uniform vec2 u_res; uniform float u_time; uniform float u_flashC;
out vec4 o;
float h(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
void main() {
    vec3 c = texture(u_scene, v_uv).rgb;
    vec3 b = texture(u_bloomA, v_uv).rgb * 0.55 + texture(u_bloomB, v_uv).rgb * 0.75;
    c += b;
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(l), c, 1.12);
    c *= vec3(0.98, 1.0, 1.035);
    c = (c - 0.45) * 1.08 + 0.45;
    vec2 q = v_uv - 0.5;
    c *= 1.0 - dot(q, q) * 0.6;
    c += (h(v_uv * u_res + fract(u_time) * 100.0) - 0.5) * 0.022;
    c += u_flashC * 0.12;
    o = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)";

// ---------------------------------------------------------------- shadow & HUD
static const char* kShadowVS = R"(
in vec3 a_pos;
uniform mat4 u_vp; uniform mat4 u_model;
out vec2 v_uv;
void main() { v_uv = a_pos.xz * vec2(1.0 / 1.25, 1.0 / 2.8); gl_Position = u_vp * u_model * vec4(a_pos.x, 0.03, a_pos.z, 1.0); }
)";
static const char* kShadowFS = R"(
in vec2 v_uv;
out vec4 o;
void main() { float r = length(v_uv); float a = smoothstep(1.0, 0.45, r) * 0.6; o = vec4(0.0, 0.0, 0.0, a); }
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
uniform sampler2D u_font;
out vec4 o;
void main() {
    if (v_mode < 0.5) { o = v_col; return; }
    if (v_mode > 1.5) { float r = length(v_uv * 2.0 - 1.0); float a = smoothstep(1.0, 0.8, r); o = vec4(v_col.rgb, v_col.a * a); return; }
    float d = texture(u_font, v_uv).r;
    float w = max(fwidth(d) * 0.7, 0.01);
    float fill = smoothstep(0.5 - w, 0.5 + w, d);
    float edge = smoothstep(0.34 - w, 0.34 + w, d);
    vec3 col = mix(vec3(0.0), v_col.rgb, fill);
    o = vec4(col, v_col.a * max(fill, edge * 0.75));
}
)";
