// GLM
// Created by inigo quilez - iq/2014
//   https://www.youtube.com/c/InigoQuilez
//   https://iquilezles.org/
//
// Ported from https://www.shadertoy.com/view/4ts3z2 (Pirates of the Caribbean)
// for Shader Workshop (glm)
//
// A simple and cheap 2D shader to accompany the Pirates of the Caribean music.
//
// mainImage: straightforward Shadertoy port (texture2D via the host).
// mainFrame: same image, inlined sw_texture2D (toggle Use Frame Shader to compare).
// Hardcoding wrap/filter (e.g. sw_texture2D_linear_repeat) can squeeze a bit more.

#include "glm_shader_workshop_common.h"

using namespace glm;

// texture() → texture2D helper (host PLT — used by mainImage)
static inline vec4 tex(GLuint ch, vec2 uv)
{
	pgl_vec4 v = texture2D(ch, uv.x, uv.y);
	return vec4(v.x, v.y, v.z, v.w);
}

// Inlined sample for mainFrame; wrap/filter come from the live glTexture (channel UI).
static inline vec4 tex(const glTexture* t, vec2 uv)
{
	assert(t);
	pgl_vec4 v = sw_texture2D(t, uv.x, uv.y);
	return vec4(v.x, v.y, v.z, v.w);
}

static float fbm(vec2 p)
{
	return 0.5000f * tex(iChannel1, p * 1.00f).x +
	       0.2500f * tex(iChannel1, p * 2.02f).x +
	       0.1250f * tex(iChannel1, p * 4.03f).x +
	       0.0625f * tex(iChannel1, p * 8.04f).x;
}

static float fbm(vec2 p, const glTexture* t1)
{
	return 0.5000f * tex(t1, p * 1.00f).x +
	       0.2500f * tex(t1, p * 2.02f).x +
	       0.1250f * tex(t1, p * 4.03f).x +
	       0.0625f * tex(t1, p * 8.04f).x;
}

// main instrument
static float instrument(float freq, float time)
{
	float ph = 1.0f;
	ph *= sin(6.283185f * freq * time * 2.0f);
	ph *= 0.5f + 0.5f * max(0.0f, 5.0f - 0.01f * freq);
	ph *= exp(-time * freq * 0.2f);

	float y = 0.0f;
	y += 0.70f * sin(1.00f * 6.283185f * freq * time + ph) * exp2(-0.7f * 0.007f * freq * time);
	y += 0.20f * sin(2.01f * 6.283185f * freq * time + ph) * exp2(-0.7f * 0.011f * freq * time);
	y += 0.20f * sin(3.01f * 6.283185f * freq * time + ph) * exp2(-0.7f * 0.015f * freq * time);
	y += 0.16f * sin(4.01f * 6.283185f * freq * time + ph) * exp2(-0.7f * 0.018f * freq * time);
	y += 0.13f * sin(5.01f * 6.283185f * freq * time + ph) * exp2(-0.7f * 0.021f * freq * time);
	y += 0.10f * sin(6.01f * 6.283185f * freq * time + ph) * exp2(-0.7f * 0.027f * freq * time);
	y += 0.09f * sin(8.01f * 6.283185f * freq * time + ph) * exp2(-0.7f * 0.030f * freq * time);
	y += 0.07f * sin(9.01f * 6.283185f * freq * time + ph) * exp2(-0.7f * 0.033f * freq * time);

	y += 0.35f * y * y * y;
	y += 0.10f * y * y * y;

	y *= 1.0f + 1.5f * exp(-8.0f * time);
	y *= clamp(time / 0.004f, 0.0f, 1.0f);

	y *= 2.5f - 1.5f * clamp(log2(freq) / 10.0f, 0.0f, 1.0f);
	return y;
}

// music data
static float doChannel1(float soundTime);
static float doChannel2(float soundTime);

#define D(a) b += float(a); if (t > b) x = b;

#define tint 0.144f

static float doChannel1(float t)
{
	float x = 0.0f;
	float y = 0.0f;
	float b = 0.0f;
	t /= tint;

	// F2
	x = t; b = 0.0f;
	D(36) D(2) D(2) D(20) D(2) D(16) D(6) D(2) D(226)
	y += instrument(174.0f, tint * (t - x));

	// G2
	x = t; b = 0.0f;
	D(53) D(208)
	y += instrument(195.0f, tint * (t - x));

	// A2
	x = t; b = 0.0f;
	D(34) D(2) D(2) D(2) D(1) D(7) D(2) D(2) D(2) D(1) D(3) D(8) D(2) D(8) D(2) D(4) D(2) D(2) D(2) D(1)
	D(31) D(2) D(4) D(138) D(46) D(2)
	y += instrument(220.0f, tint * (t - x));

	// A#2
	x = t; b = 0.0f;
	D(42) D(2) D(2) D(14) D(2) D(2) D(1) D(25) D(2) D(16) D(2) D(2)
	y += instrument(233.0f, tint * (t - x));

	// B2
	x = t; b = 0.0f;
	D(125)
	y += instrument(246.0f, tint * (t - x));

	// C3
	x = t; b = 0.0f;
	D(35) D(6) D(7) D(2) D(3) D(1) D(5) D(7) D(2) D(2) D(1) D(1) D(2) D(3) D(6) D(199) D(2) D(2) D(2) D(1)
	y += instrument(261.0f, tint * (t - x));

	// C#3
	x = t; b = 0.0f;
	D(120) D(2) D(4) D(132) D(1) D(5) D(42) D(2)
	y += instrument(277.0f, tint * (t - x));

	// D3
	x = t; b = 0.0f;
	D(0) D(2) D(1) D(2) D(1) D(2) D(1) D(1) D(1) D(1) D(2) D(1) D(2) D(1) D(2) D(1) D(1) D(1) D(1) D(2)
	D(1) D(2) D(1) D(2) D(1) D(3) D(2) D(2) D(2) D(2) D(2) D(1) D(5) D(3) D(5) D(2) D(2) D(12) D(2) D(6)
	D(2) D(2) D(2) D(2) D(2) D(1) D(1) D(2) D(5) D(3) D(2) D(2) D(2) D(3) D(3) D(6) D(1) D(136) D(9) D(2)
	D(2) D(2) D(1) D(17) D(2) D(2) D(2) D(1) D(11)
	y += instrument(293.0f, tint * (t - x));

	// E3
	x = t; b = 0.0f;
	D(41) D(7) D(2) D(15) D(7) D(2) D(27) D(6) D(13) D(2) D(4) D(132) D(1) D(23) D(2) D(2) D(2) D(18) D(4)
	y += instrument(329.0f, tint * (t - x));

	// F3
	x = t; b = 0.0f;
	D(42) D(2) D(2) D(20) D(2) D(2) D(19) D(11) D(2) D(6) D(2) D(4) D(5) D(5) D(8) D(2) D(2) D(20) D(2) D(16)
	D(6) D(2) D(82) D(4) D(2) D(2) D(2) D(2) D(1) D(12) D(5) D(2) D(2) D(2) D(1) D(7)
	y += instrument(349.0f, tint * (t - x));

	// G3
	x = t; b = 0.0f;
	D(47) D(24) D(19) D(2) D(2) D(2) D(2) D(3) D(11) D(37) D(120) D(13) D(2) D(2) D(2) D(18)
	y += instrument(391.0f, tint * (t - x));

	// A3
	x = t; b = 0.0f;
	D(95) D(5) D(2) D(12) D(16) D(2) D(2) D(2) D(1) D(7) D(2) D(2) D(2) D(1) D(3) D(8) D(2) D(8) D(2) D(4)
	D(2) D(2) D(2) D(1) D(31) D(2) D(4) D(2) D(2) D(12) D(1) D(1) D(30) D(2) D(2) D(3) D(12) D(5) D(2) D(2)
	D(3)
	y += instrument(440.0f, tint * (t - x));

	// A#3
	x = t; b = 0.0f;
	D(96) D(2) D(40) D(2) D(2) D(14) D(2) D(2) D(1) D(25) D(2) D(16) D(2) D(2) D(24) D(18) D(1) D(1) D(24) D(24)
	y += instrument(466.0f, tint * (t - x));

	// C4
	x = t; b = 0.0f;
	D(131) D(6) D(7) D(2) D(3) D(1) D(5) D(7) D(2) D(2) D(1) D(1) D(2) D(3) D(6) D(47) D(2)
	y += instrument(523.0f, tint * (t - x));

	// C#4
	x = t; b = 0.0f;
	D(216) D(2) D(3)
	y += instrument(554.0f, tint * (t - x));

	// D4
	x = t; b = 0.0f;
	D(132) D(2) D(2) D(2) D(2) D(2) D(1) D(5) D(3) D(5) D(2) D(2) D(12) D(2) D(6) D(2) D(2) D(2) D(2) D(2)
	D(1) D(1) D(2) D(5) D(3) D(2) D(2) D(2) D(3) D(3) D(6) D(2) D(2) D(4) D(4) D(2) D(5) D(7) D(5)
	y += instrument(587.0f, tint * (t - x));

	// E4
	x = t; b = 0.0f;
	D(137) D(7) D(2) D(15) D(7) D(2) D(27) D(6) D(13) D(2) D(8)
	y += instrument(659.0f, tint * (t - x));

	// F4
	x = t; b = 0.0f;
	D(138) D(2) D(2) D(20) D(2) D(2) D(19) D(11) D(2) D(6) D(2) D(4) D(5) D(13) D(2) D(1) D(4) D(3)
	y += instrument(698.0f, tint * (t - x));

	// G4
	x = t; b = 0.0f;
	D(143) D(24) D(19) D(2) D(2) D(2) D(2) D(3) D(11) D(24) D(14) D(4)
	y += instrument(783.0f, tint * (t - x));

	// A4
	x = t; b = 0.0f;
	D(191) D(5) D(2) D(12) D(24)
	y += instrument(880.0f, tint * (t - x));

	// A#4
	x = t; b = 0.0f;
	D(192) D(2) D(52)
	y += instrument(932.0f, tint * (t - x));

	// C5
	x = t; b = 0.0f;
	y += instrument(1046.0f, tint * (t - x));
	return y;
}

static float doChannel2(float t)
{
	float x = 0.0f;
	float y = 0.0f;
	float b = 0.0f;
	t /= tint;

	// D0
	x = t; b = 0.0f;
	D(24) D(6) D(3)
	y += instrument(36.0f, tint * (t - x));

	// F0
	x = t; b = 0.0f;
	D(66) D(2) D(1) D(2) D(91) D(2) D(1) D(2)
	y += instrument(43.0f, tint * (t - x));

	// G0
	x = t; b = 0.0f;
	D(96) D(2) D(1) D(2) D(91) D(2) D(1) D(2) D(49) D(2) D(1) D(2) D(1) D(2) D(1) D(2)
	y += instrument(48.0f, tint * (t - x));

	// A0
	x = t; b = 0.0f;
	D(48) D(2) D(1) D(2) D(22) D(2) D(43) D(2) D(1) D(2) D(1) D(2) D(1) D(2) D(13) D(2) D(1) D(2) D(22) D(2)
	D(43) D(2) D(1) D(2) D(13) D(2) D(1) D(2) D(1) D(2) D(1) D(2) D(13) D(2) D(1) D(2) D(1) D(2) D(1) D(2)
	D(37) D(2) D(1) D(2)
	y += instrument(55.0f, tint * (t - x));

	// A#0
	x = t; b = 0.0f;
	D(42) D(2) D(1) D(2) D(13) D(2) D(1) D(2) D(25) D(2) D(1) D(2) D(13) D(2) D(1) D(2) D(25) D(2) D(1) D(2)
	D(13) D(2) D(1) D(2) D(25) D(2) D(1) D(2) D(13) D(2) D(1) D(2) D(23)
	y += instrument(58.0f, tint * (t - x));

	// C1
	x = t; b = 0.0f;
	D(41) D(31) D(2) D(63) D(31) D(2) D(56) D(2) D(2) D(52) D(2) D(1) D(2)
	y += instrument(65.0f, tint * (t - x));

	// D1
	x = t; b = 0.0f;
	D(24) D(6) D(3) D(3) D(2) D(1) D(15) D(2) D(1) D(2) D(19) D(2) D(1) D(2) D(1) D(2) D(1) D(2) D(13) D(2)
	D(1) D(2) D(7) D(2) D(1) D(2) D(13) D(2) D(1) D(15) D(2) D(1) D(2) D(19) D(2) D(1) D(2) D(1) D(2) D(1)
	D(2) D(13) D(2) D(1) D(2) D(7) D(2) D(1) D(2) D(7) D(2) D(46) D(2) D(1) D(2) D(1) D(2) D(1) D(1) D(1)
	D(13) D(2) D(1) D(2) D(1) D(2) D(1) D(1) D(1) D(7)
	y += instrument(73.0f, tint * (t - x));

	// F1
	x = t; b = 0.0f;
	D(66) D(2) D(1) D(2) D(91) D(2) D(1) D(2) D(121) D(2) D(1) D(1) D(1)
	y += instrument(87.0f, tint * (t - x));

	// G1
	x = t; b = 0.0f;
	D(96) D(2) D(1) D(2) D(91) D(2) D(1) D(2) D(49) D(2) D(1) D(2) D(1) D(2) D(1) D(2)
	y += instrument(97.0f, tint * (t - x));

	// A1
	x = t; b = 0.0f;
	D(48) D(2) D(1) D(2) D(22) D(2) D(43) D(2) D(1) D(2) D(1) D(2) D(1) D(2) D(13) D(2) D(1) D(2) D(22) D(2)
	D(43) D(2) D(1) D(2) D(13) D(2) D(1) D(2) D(1) D(2) D(1) D(2) D(13) D(2) D(1) D(2) D(1) D(2) D(1) D(2)
	D(37) D(2) D(1) D(2)
	y += instrument(110.0f, tint * (t - x));

	// A#1
	x = t; b = 0.0f;
	D(42) D(2) D(1) D(2) D(13) D(2) D(1) D(2) D(25) D(2) D(1) D(2) D(13) D(2) D(1) D(2) D(25) D(2) D(1) D(2)
	D(13) D(2) D(1) D(2) D(25) D(2) D(1) D(2) D(13) D(2) D(1) D(2) D(23)
	y += instrument(116.0f, tint * (t - x));

	// C2
	x = t; b = 0.0f;
	D(41) D(31) D(2) D(63) D(31) D(2) D(56) D(2) D(2) D(52) D(2) D(1) D(2)
	y += instrument(130.0f, tint * (t - x));

	// D2
	x = t; b = 0.0f;
	D(36) D(2) D(1) D(15) D(2) D(1) D(2) D(19) D(2) D(1) D(2) D(1) D(2) D(1) D(2) D(13) D(2) D(1) D(2) D(7)
	D(2) D(1) D(2) D(13) D(2) D(1) D(15) D(2) D(1) D(2) D(19) D(2) D(1) D(2) D(1) D(2) D(1) D(2) D(13) D(2)
	D(1) D(2) D(7) D(2) D(1) D(2) D(7) D(2) D(46) D(2) D(1) D(2) D(1) D(2) D(1) D(1) D(1) D(13) D(2) D(1)
	D(2) D(1) D(2) D(1) D(1) D(1) D(7)
	y += instrument(146.0f, tint * (t - x));

	// F2
	x = t; b = 0.0f;
	D(288) D(2) D(1) D(1) D(1)
	y += instrument(174.0f, tint * (t - x));
	return y;
}

#undef D

extern "C" {

// TEX_STARS / TEX_GRAY_NOISE_MEDIUM
// Shadertoy: iChannel0 water/organic, iChannel1 noise for fbm/stars
void set_channels(texture_settings* ts)
{
	ts[0].ch = TEX_STARS;
	SET_DFLT_TEX_PARAMS(ts[0]);
	ts[0].filter = TS_LINEAR;
	ts[0].wrap = TS_REPEAT;

	ts[1].ch = TEX_GRAY_NOISE_MEDIUM;
	SET_DFLT_TEX_PARAMS(ts[1]);
	ts[1].filter = TS_LINEAR;
	ts[1].wrap = TS_REPEAT;
}

static void shade(vec4* fragColor, vec2 fragCoord, const glTexture* tex0, const glTexture* tex1)
{
	float time = mod(iTime, 60.0f);
	vec2 p = (2.0f * fragCoord - iResolution.xy()) / iResolution.y;
	vec2 i = p;

	// camera
	p += vec2(1.0f, 3.0f) * 0.001f * 2.0f * cos(iTime * 5.0f + vec2(0.0f, 1.5f));
	p += vec2(1.0f, 3.0f) * 0.001f * 1.0f * cos(iTime * 9.0f + vec2(1.0f, 4.5f));
	float an = 0.3f * sin(0.1f * time);
	float co = cos(an);
	float si = sin(an);
	// GLSL mat2(co,-si,si,co)*p  (column-major ctor)
	p = (mat2(co, -si, si, co) * p) * 0.85f;

	// water
	vec2 q = vec2(p.x, 1.0f) / p.y;
	q.y -= 0.9f * time;
	vec4 t0 = tex(tex0, 0.1f * q * vec2(1.0f, 2.0f) - vec2(0.0f, 0.007f * iTime));
	vec2 off = vec2(t0.x, t0.y);
	q += 0.4f * (-1.0f + 2.0f * off);
	vec3 tw = tex(tex0, 0.05f * q * vec2(1.0f, 4.0f) + vec2(0.0f, 0.01f * iTime)).zyx();
	vec3 col = 0.2f * sqrt(tw);
	float re = 1.0f - smoothstep(0.0f, 0.7f, abs(p.x - 0.6f) - abs(p.y) * 0.5f + 0.2f);
	col += 1.0f * vec3(1.0f, 0.9f, 0.73f) * re * 0.2f * (0.1f + 0.9f * off.y) * 5.0f * (1.0f - col.x);
	float re2 = 1.0f - smoothstep(0.0f, 2.0f, abs(p.x - 0.6f) - abs(p.y) * 0.85f);
	col += 0.7f * re2 * smoothstep(0.35f, 1.0f, tex(tex1, 0.075f * q * vec2(1.0f, 4.0f)).x);

	// sky
	vec3 sky = vec3(0.0f, 0.05f, 0.1f) * 1.4f;
	// stars
	sky += 0.5f * smoothstep(0.95f, 1.00f, tex(tex1, 0.25f * p).x);
	sky += 0.5f * smoothstep(0.85f, 1.0f, tex(tex1, 0.25f * p).x);
	sky += 0.2f * pow(1.0f - max(0.0f, p.y), 2.0f);
	// clouds
	float f = fbm(0.002f * vec2(p.x, 1.0f) / p.y, tex1);
	vec3 cloud = vec3(0.3f, 0.4f, 0.5f) * 0.7f * (1.0f - 0.85f * smoothstep(0.4f, 1.0f, f));
	sky = mix(sky, cloud, 0.95f * smoothstep(0.4f, 0.6f, f));
	sky = mix(sky, vec3(0.33f, 0.34f, 0.35f), pow(1.0f - max(0.0f, p.y), 2.0f));
	col = mix(col, sky, smoothstep(0.0f, 0.1f, p.y));

	// horizon
	col += 0.1f * pow(clamp(1.0f - abs(p.y), 0.0f, 1.0f), 9.0f);

	// moon
	float d = length(p - vec2(0.6f, 0.5f));
	vec3 moon = vec3(0.98f, 0.97f, 0.95f) * (1.0f - 0.1f * smoothstep(0.2f, 0.5f, f));
	col += 0.8f * moon * exp(-4.0f * d) * vec3(1.1f, 1.0f, 0.8f);
	col += 0.2f * moon * exp(-2.0f * d);
	moon *= 0.85f + 0.15f * smoothstep(0.25f, 0.7f, fbm(0.05f * p + 0.3f, tex1));
	col = mix(col, moon, 1.0f - smoothstep(0.2f, 0.22f, d));

	// postprocess
	col = pow(1.4f * col, vec3(1.5f, 1.2f, 1.0f));
	col *= clamp(1.0f - 0.3f * length(i), 0.0f, 1.0f);

	// fade
	col *= smoothstep(3.0f, 6.0f, time);
	col *= 1.0f - smoothstep(44.0f, 50.0f, time);

	*fragColor = vec4(col, 1.0f);
}

void mainImage(vec4* fragColor, vec2 fragCoord)
{
	float time = mod(iTime, 60.0f);
	vec2 p = (2.0f * fragCoord - iResolution.xy()) / iResolution.y;
	vec2 i = p;

	// camera
	p += vec2(1.0f, 3.0f) * 0.001f * 2.0f * cos(iTime * 5.0f + vec2(0.0f, 1.5f));
	p += vec2(1.0f, 3.0f) * 0.001f * 1.0f * cos(iTime * 9.0f + vec2(1.0f, 4.5f));
	float an = 0.3f * sin(0.1f * time);
	float co = cos(an);
	float si = sin(an);
	// GLSL mat2(co,-si,si,co)*p  (column-major ctor)
	p = (mat2(co, -si, si, co) * p) * 0.85f;

	// water
	vec2 q = vec2(p.x, 1.0f) / p.y;
	q.y -= 0.9f * time;
	vec4 t0 = tex(iChannel0, 0.1f * q * vec2(1.0f, 2.0f) - vec2(0.0f, 0.007f * iTime));
	vec2 off = vec2(t0.x, t0.y);
	q += 0.4f * (-1.0f + 2.0f * off);
	vec3 tw = tex(iChannel0, 0.05f * q * vec2(1.0f, 4.0f) + vec2(0.0f, 0.01f * iTime)).zyx();
	vec3 col = 0.2f * sqrt(tw);
	float re = 1.0f - smoothstep(0.0f, 0.7f, abs(p.x - 0.6f) - abs(p.y) * 0.5f + 0.2f);
	col += 1.0f * vec3(1.0f, 0.9f, 0.73f) * re * 0.2f * (0.1f + 0.9f * off.y) * 5.0f * (1.0f - col.x);
	float re2 = 1.0f - smoothstep(0.0f, 2.0f, abs(p.x - 0.6f) - abs(p.y) * 0.85f);
	col += 0.7f * re2 * smoothstep(0.35f, 1.0f, tex(iChannel1, 0.075f * q * vec2(1.0f, 4.0f)).x);

	// sky
	vec3 sky = vec3(0.0f, 0.05f, 0.1f) * 1.4f;
	// stars
	sky += 0.5f * smoothstep(0.95f, 1.00f, tex(iChannel1, 0.25f * p).x);
	sky += 0.5f * smoothstep(0.85f, 1.0f, tex(iChannel1, 0.25f * p).x);
	sky += 0.2f * pow(1.0f - max(0.0f, p.y), 2.0f);
	// clouds
	float f = fbm(0.002f * vec2(p.x, 1.0f) / p.y);
	vec3 cloud = vec3(0.3f, 0.4f, 0.5f) * 0.7f * (1.0f - 0.85f * smoothstep(0.4f, 1.0f, f));
	sky = mix(sky, cloud, 0.95f * smoothstep(0.4f, 0.6f, f));
	sky = mix(sky, vec3(0.33f, 0.34f, 0.35f), pow(1.0f - max(0.0f, p.y), 2.0f));
	col = mix(col, sky, smoothstep(0.0f, 0.1f, p.y));

	// horizon
	col += 0.1f * pow(clamp(1.0f - abs(p.y), 0.0f, 1.0f), 9.0f);

	// moon
	float d = length(p - vec2(0.6f, 0.5f));
	vec3 moon = vec3(0.98f, 0.97f, 0.95f) * (1.0f - 0.1f * smoothstep(0.2f, 0.5f, f));
	col += 0.8f * moon * exp(-4.0f * d) * vec3(1.1f, 1.0f, 0.8f);
	col += 0.2f * moon * exp(-2.0f * d);
	moon *= 0.85f + 0.15f * smoothstep(0.25f, 0.7f, fbm(0.05f * p + 0.3f));
	col = mix(col, moon, 1.0f - smoothstep(0.2f, 0.22f, d));

	// postprocess
	col = pow(1.4f * col, vec3(1.5f, 1.2f, 1.0f));
	col *= clamp(1.0f - 0.3f * length(i), 0.0f, 1.0f);

	// fade
	col *= smoothstep(3.0f, 6.0f, time);
	col *= 1.0f - smoothstep(44.0f, 50.0f, time);

	*fragColor = vec4(col, 1.0f);
}

// Same image as mainImage, inlined sampling. Toggle Use Frame Shader to compare.
// Hardcoding wrap/filter (sw_texture2D_linear_repeat) can squeeze a little more.

void mainFrame(pix_t* framebuffer, int w, int h)
{
	const glTexture* t0 = pglGetTexture(iChannel0);
	const glTexture* t1 = pglGetTexture(iChannel1);
	pix_t* lastrow = framebuffer + (h - 1) * w;

#pragma omp parallel for collapse(2) schedule(runtime) num_threads(num_shader_threads)
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			vec2 fc;
			vec4 col;
			fc.x = x + 0.5f;
			fc.y = y + 0.5f;
			shade(&col, fc, t0, t1);
			lastrow[-y * w + x] = pack_clamp_rgba8(col.x, col.y, col.z, col.w);
		}
	}
}

// sound shader entrypoint
vec2 mainSound(int samp, float time)
{
	(void)samp;
	time = mod(time, 60.0f);

	vec2 y = vec2(0.0f);
	y += vec2(0.7f, 0.3f) * doChannel1(time); // main instrument
	y += vec2(0.3f, 0.7f) * doChannel2(time); // secondary instrument
	y *= 0.1f;

	return y;
}

}
