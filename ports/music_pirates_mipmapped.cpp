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
// ---------------------------------------------------------------------------
// music_pirates_mipmapped.cpp — same demo as music_pirates.cpp, but written for
// static 2D mip chains + explicit screen-space gradients (texture2DGrad).
//
// Keep music_pirates.cpp as the pre-mip baseline. This file is the reference for
// the kind of manual changes authors need when automatic LOD is not available
// (SW mainImage path has no triangle UV derivatives).
//
// mainImage: that port (texture2DGrad via the host).
// mainFrame: same image, inlined sw_texture2DGrad (toggle Use Frame Shader).
// Hardcoding wrap/filter (e.g. sw_texture2DGrad_trilinear_repeat) can squeeze a bit more.
//
// Requires SW mip support:
//   - texture_settings.mipmap + TS_LINEAR/NEAREST (SW_MIPMAP_USE_LINEAR → trilinear)
//   - texture2DGrad(tex, u, v, dUdx, dVdx, dUdy, dVdy)
//     dPdx = (dU/dx, dV/dx), dPdy = (dU/dy, dV/dy) in screen pixels
// ---------------------------------------------------------------------------

#include "glm_shader_workshop_common.h"

using namespace glm;

// Level-0 / auto path — kept next to Grad/Lod for the “before vs after” comparison
// with music_pirates.cpp (not used on the hard paths below).
[[maybe_unused]] static inline vec4 tex(GLuint ch, vec2 uv)
{
	pgl_vec4 v = texture2D(ch, uv.x, uv.y);
	return vec4(v.x, v.y, v.z, v.w);
}

[[maybe_unused]] static inline vec4 tex(const glTexture* t, vec2 uv)
{
	assert(t);
	pgl_vec4 v = sw_texture2D(t, uv.x, uv.y);
	return vec4(v.x, v.y, v.z, v.w);
}

// Explicit LOD helper (uniform-ish UV scale). Prefer Grad for this demo’s
// perspective water/clouds; left as the simpler alternative authors may use.
[[maybe_unused]] static inline vec4 texLod(GLuint ch, vec2 uv, float lod)
{
	pgl_vec4 v = texture2DLod(ch, uv.x, uv.y, lod);
	return vec4(v.x, v.y, v.z, v.w);
}

[[maybe_unused]] static inline vec4 texLod(const glTexture* t, vec2 uv, float lod)
{
	assert(t);
	pgl_vec4 v = sw_texture2DLod(t, uv.x, uv.y, lod);
	return vec4(v.x, v.y, v.z, v.w);
}

// Best fit for perspective / anisotropic footprints (water, fbm near horizon)
static inline vec4 texGrad(GLuint ch, vec2 uv, vec2 dUVdx, vec2 dUVdy)
{
	pgl_vec4 v = texture2DGrad(ch, uv.x, uv.y,
	                           dUVdx.x, dUVdx.y,
	                           dUVdy.x, dUVdy.y);
	return vec4(v.x, v.y, v.z, v.w);
}

static inline vec4 texGrad(const glTexture* t, vec2 uv, vec2 dUVdx, vec2 dUVdy)
{
	assert(t);
	pgl_vec4 v = sw_texture2DGrad(t, uv.x, uv.y,
	                             dUVdx.x, dUVdx.y,
	                             dUVdy.x, dUVdy.y);
	return vec4(v.x, v.y, v.z, v.w);
}

// Component-wise UV scale: uv = q * s  (s.x, s.y stretch U and V independently)
static inline vec2 scale_uv_deriv(vec2 dQ, vec2 s)
{
	return vec2(dQ.x * s.x, dQ.y * s.y);
}

// q = (p.x / p.y, 1 / p.y)  — same singularity as the original near p.y → 0
// Clamp |py| slightly so λ stays finite at the horizon (GPU dFdx can also blow up).
static inline vec2 dQ_from_dP(vec2 p, vec2 dP)
{
	float py = p.y;
	float apy = fabsf(py);
	if (apy < 1e-4f)
		py = (py < 0.0f) ? -1e-4f : 1e-4f;
	float inv_py2 = 1.0f / (py * py);
	return vec2((dP.x * py - p.x * dP.y) * inv_py2, -dP.y * inv_py2);
}

// fbm: scale UV *and* screen derivatives by each octave frequency so higher
// octaves pick higher mips under minification (classic GPU fbm+mip pattern).
static float fbm(vec2 p, vec2 dPdx, vec2 dPdy)
{
	const float f0 = 1.00f, f1 = 2.02f, f2 = 4.03f, f3 = 8.04f;
	float n = 0.0f;
	n += 0.5000f * texGrad(iChannel1, p * f0, dPdx * f0, dPdy * f0).x;
	n += 0.2500f * texGrad(iChannel1, p * f1, dPdx * f1, dPdy * f1).x;
	n += 0.1250f * texGrad(iChannel1, p * f2, dPdx * f2, dPdy * f2).x;
	n += 0.0625f * texGrad(iChannel1, p * f3, dPdx * f3, dPdy * f3).x;
	return n;
}

static float fbm(vec2 p, vec2 dPdx, vec2 dPdy, const glTexture* t1)
{
	const float f0 = 1.00f, f1 = 2.02f, f2 = 4.03f, f3 = 8.04f;
	float n = 0.0f;
	n += 0.5000f * texGrad(t1, p * f0, dPdx * f0, dPdy * f0).x;
	n += 0.2500f * texGrad(t1, p * f1, dPdx * f1, dPdy * f1).x;
	n += 0.1250f * texGrad(t1, p * f2, dPdx * f2, dPdy * f2).x;
	n += 0.0625f * texGrad(t1, p * f3, dPdx * f3, dPdy * f3).x;
	return n;
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
// Linear + mipmap → GL_LINEAR_MIPMAP_LINEAR
void set_channels(texture_settings* ts)
{
	ts[0].ch = TEX_STARS;
	SET_DFLT_TEX_PARAMS(ts[0]);
	ts[0].filter = TS_LINEAR;
	ts[0].wrap = TS_REPEAT;
	ts[0].mipmap = GL_TRUE;

	ts[1].ch = TEX_GRAY_NOISE_MEDIUM;
	SET_DFLT_TEX_PARAMS(ts[1]);
	ts[1].filter = TS_LINEAR;
	ts[1].wrap = TS_REPEAT;
	ts[1].mipmap = GL_TRUE;
}

static void shade(vec4* fragColor, vec2 fragCoord, const glTexture* tex0, const glTexture* tex1)
{
	float time = mod(iTime, 60.0f);

	// Base NDC-ish coords and screen-space derivatives (∂/∂fragCoord).
	// p0 = (2*fragCoord - res) / res.y
	vec2 p = (2.0f * fragCoord - iResolution.xy()) / iResolution.y;
	vec2 i = p;
	float dxy = 2.0f / iResolution.y;
	vec2 dPdx = vec2(dxy, 0.0f);
	vec2 dPdy = vec2(0.0f, dxy);

	// camera shake (fragCoord-independent → derivatives unchanged)
	p += vec2(1.0f, 3.0f) * 0.001f * 2.0f * cos(iTime * 5.0f + vec2(0.0f, 1.5f));
	p += vec2(1.0f, 3.0f) * 0.001f * 1.0f * cos(iTime * 9.0f + vec2(1.0f, 4.5f));
	float an = 0.3f * sin(0.1f * time);
	float co = cos(an);
	float si = sin(an);
	// GLSL mat2(co,-si,si,co)*p  (column-major ctor) — same as baseline
	mat2 R = mat2(co, -si, si, co);
	p = (R * p) * 0.85f;
	dPdx = (R * dPdx) * 0.85f;
	dPdy = (R * dPdy) * 0.85f;

	// water domain q = (p.x, 1)/p.y  (perspective stretch; mips matter most here)
	vec2 q = vec2(p.x, 1.0f) / p.y;
	vec2 dQdx = dQ_from_dP(p, dPdx);
	vec2 dQdy = dQ_from_dP(p, dPdy);
	// time scroll: zero screen derivatives
	q.y -= 0.9f * time;

	vec2 s_w0 = 0.1f * vec2(1.0f, 2.0f);
	vec2 uv0 = s_w0 * q - vec2(0.0f, 0.007f * iTime);
	vec4 t0 = texGrad(tex0, uv0,
	                  scale_uv_deriv(dQdx, s_w0),
	                  scale_uv_deriv(dQdy, s_w0));
	vec2 off = vec2(t0.x, t0.y);

	// Displacement from first sample is piecewise-smooth noise; treat as constant
	// for LOD (common approximation — avoids noise-dependent derivatives).
	q += 0.4f * (-1.0f + 2.0f * off);

	vec2 s_w1 = 0.05f * vec2(1.0f, 4.0f);
	vec2 uv1 = s_w1 * q + vec2(0.0f, 0.01f * iTime);
	vec3 tw = texGrad(tex0, uv1,
	                  scale_uv_deriv(dQdx, s_w1),
	                  scale_uv_deriv(dQdy, s_w1)).zyx();
	vec3 col = 0.2f * sqrt(tw);
	float re = 1.0f - smoothstep(0.0f, 0.7f, abs(p.x - 0.6f) - abs(p.y) * 0.5f + 0.2f);
	col += 1.0f * vec3(1.0f, 0.9f, 0.73f) * re * 0.2f * (0.1f + 0.9f * off.y) * 5.0f * (1.0f - col.x);
	float re2 = 1.0f - smoothstep(0.0f, 2.0f, abs(p.x - 0.6f) - abs(p.y) * 0.85f);

	vec2 s_sp = 0.075f * vec2(1.0f, 4.0f);
	float spark = texGrad(tex1, s_sp * q,
	                      scale_uv_deriv(dQdx, s_sp),
	                      scale_uv_deriv(dQdy, s_sp)).x;
	col += 0.7f * re2 * smoothstep(0.35f, 1.0f, spark);

	// sky
	vec3 sky = vec3(0.0f, 0.05f, 0.1f) * 1.4f;
	// stars — affine in p (Grad or Lod both fine; Grad keeps one code path)
	vec2 suv = 0.25f * p;
	vec2 dSdx = 0.25f * dPdx;
	vec2 dSdy = 0.25f * dPdy;
	sky += 0.5f * smoothstep(0.95f, 1.00f, texGrad(tex1, suv, dSdx, dSdy).x);
	sky += 0.5f * smoothstep(0.85f, 1.0f, texGrad(tex1, suv, dSdx, dSdy).x);
	sky += 0.2f * pow(1.0f - max(0.0f, p.y), 2.0f);
	// clouds — same perspective map as water (no time scroll on this UV)
	vec2 pc = 0.002f * vec2(p.x, 1.0f) / p.y;
	vec2 dPcdx = 0.002f * dQ_from_dP(p, dPdx);
	vec2 dPcdy = 0.002f * dQ_from_dP(p, dPdy);
	float f = fbm(pc, dPcdx, dPcdy, tex1);
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
	moon *= 0.85f + 0.15f * smoothstep(0.25f, 0.7f,
	                                    fbm(0.05f * p + 0.3f, 0.05f * dPdx, 0.05f * dPdy, tex1));
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

	// Base NDC-ish coords and screen-space derivatives (∂/∂fragCoord).
	// p0 = (2*fragCoord - res) / res.y
	vec2 p = (2.0f * fragCoord - iResolution.xy()) / iResolution.y;
	vec2 i = p;
	float dxy = 2.0f / iResolution.y;
	vec2 dPdx = vec2(dxy, 0.0f);
	vec2 dPdy = vec2(0.0f, dxy);

	// camera shake (fragCoord-independent → derivatives unchanged)
	p += vec2(1.0f, 3.0f) * 0.001f * 2.0f * cos(iTime * 5.0f + vec2(0.0f, 1.5f));
	p += vec2(1.0f, 3.0f) * 0.001f * 1.0f * cos(iTime * 9.0f + vec2(1.0f, 4.5f));
	float an = 0.3f * sin(0.1f * time);
	float co = cos(an);
	float si = sin(an);
	// GLSL mat2(co,-si,si,co)*p  (column-major ctor) — same as baseline
	mat2 R = mat2(co, -si, si, co);
	p = (R * p) * 0.85f;
	dPdx = (R * dPdx) * 0.85f;
	dPdy = (R * dPdy) * 0.85f;

	// water domain q = (p.x, 1)/p.y  (perspective stretch; mips matter most here)
	vec2 q = vec2(p.x, 1.0f) / p.y;
	vec2 dQdx = dQ_from_dP(p, dPdx);
	vec2 dQdy = dQ_from_dP(p, dPdy);
	// time scroll: zero screen derivatives
	q.y -= 0.9f * time;

	vec2 s_w0 = 0.1f * vec2(1.0f, 2.0f);
	vec2 uv0 = s_w0 * q - vec2(0.0f, 0.007f * iTime);
	vec4 t0 = texGrad(iChannel0, uv0,
	                  scale_uv_deriv(dQdx, s_w0),
	                  scale_uv_deriv(dQdy, s_w0));
	vec2 off = vec2(t0.x, t0.y);

	// Displacement from first sample is piecewise-smooth noise; treat as constant
	// for LOD (common approximation — avoids noise-dependent derivatives).
	q += 0.4f * (-1.0f + 2.0f * off);

	vec2 s_w1 = 0.05f * vec2(1.0f, 4.0f);
	vec2 uv1 = s_w1 * q + vec2(0.0f, 0.01f * iTime);
	vec3 tw = texGrad(iChannel0, uv1,
	                  scale_uv_deriv(dQdx, s_w1),
	                  scale_uv_deriv(dQdy, s_w1)).zyx();
	vec3 col = 0.2f * sqrt(tw);
	float re = 1.0f - smoothstep(0.0f, 0.7f, abs(p.x - 0.6f) - abs(p.y) * 0.5f + 0.2f);
	col += 1.0f * vec3(1.0f, 0.9f, 0.73f) * re * 0.2f * (0.1f + 0.9f * off.y) * 5.0f * (1.0f - col.x);
	float re2 = 1.0f - smoothstep(0.0f, 2.0f, abs(p.x - 0.6f) - abs(p.y) * 0.85f);

	vec2 s_sp = 0.075f * vec2(1.0f, 4.0f);
	float spark = texGrad(iChannel1, s_sp * q,
	                      scale_uv_deriv(dQdx, s_sp),
	                      scale_uv_deriv(dQdy, s_sp)).x;
	col += 0.7f * re2 * smoothstep(0.35f, 1.0f, spark);

	// sky
	vec3 sky = vec3(0.0f, 0.05f, 0.1f) * 1.4f;
	// stars — affine in p (Grad or Lod both fine; Grad keeps one code path)
	vec2 suv = 0.25f * p;
	vec2 dSdx = 0.25f * dPdx;
	vec2 dSdy = 0.25f * dPdy;
	sky += 0.5f * smoothstep(0.95f, 1.00f, texGrad(iChannel1, suv, dSdx, dSdy).x);
	sky += 0.5f * smoothstep(0.85f, 1.0f, texGrad(iChannel1, suv, dSdx, dSdy).x);
	sky += 0.2f * pow(1.0f - max(0.0f, p.y), 2.0f);
	// clouds — same perspective map as water (no time scroll on this UV)
	vec2 pc = 0.002f * vec2(p.x, 1.0f) / p.y;
	vec2 dPcdx = 0.002f * dQ_from_dP(p, dPdx);
	vec2 dPcdy = 0.002f * dQ_from_dP(p, dPdy);
	float f = fbm(pc, dPcdx, dPcdy);
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
	moon *= 0.85f + 0.15f * smoothstep(0.25f, 0.7f,
	                                    fbm(0.05f * p + 0.3f, 0.05f * dPdx, 0.05f * dPdy));
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
// Hardcoding wrap/filter (sw_texture2DGrad_trilinear_repeat) can squeeze a little more.

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
