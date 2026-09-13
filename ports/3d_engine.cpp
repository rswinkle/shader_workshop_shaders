// GLM
//
// Ported from
// "3d engine": raymarched chrome sphere + equirect sky cubemap.
// https://fragcoord.xyz/s/3sg6dgao by @Xor
//
// Tests USER_CUBE0, load_user_textures URL, equirect→cube, and vflip.
//
// Mouse orbit (click-drag). Default view is +X looking at the origin.
//
// mainImage: host texture_cubemapGrad.
// mainFrame: inlined sw_textureCubemapGrad (toggle Use Frame Shader).
// Mipmap is on so Grad has a chain.

#include "glm_shader_workshop_common.h"

using namespace glm;

static const vec3 LIGHT = normalize(vec3(-0.2f, -0.5f, -0.8f));
static const float MAX_DIST = 120.0f;
static const float PRE = 0.01f;

static inline vec3 texGrad(GLuint ch, vec3 d, vec3 ddx, vec3 ddy)
{
	pgl_vec4 s = texture_cubemapGrad(ch, d.x, d.y, d.z,
	                                ddx.x, ddx.y, ddx.z,
	                                ddy.x, ddy.y, ddy.z);
	return vec3(s.x, s.y, s.z);
}

static inline vec3 texGrad(const glTexture* t, vec3 d, vec3 ddx, vec3 ddy)
{
	assert(t);
	pgl_vec4 s = sw_textureCubemapGrad(t, d.x, d.y, d.z,
	                                  ddx.x, ddx.y, ddx.z,
	                                  ddy.x, ddy.y, ddy.z);
	return vec3(s.x, s.y, s.z);
}

static float model(vec3 p)
{
	return length(p) - 8.0f;
}

static vec4 raymarch(vec3 p, vec3 d)
{
	float S = 0.0f;
	float T = 0.0f;
	vec3 D = normalize(d);
	vec3 P = p + D * S;
	for (int i = 0; i < 240; i++) {
		S = model(P);
		T += S;
		P += D * S;
		if ((T > MAX_DIST) || (S < PRE)) {
			break;
		}
	}
	return vec4(P, min(T / MAX_DIST, 1.0f));
}

static vec3 normal(vec3 p)
{
	vec2 N = vec2(-4.0f, 4.0f) * PRE;
	return normalize(model(p + N.xyy()) * N.xyy() + model(p + N.yxy()) * N.yxy() +
	                 model(p + N.yyx()) * N.yyx() + model(p + N.xxx()) * N.xxx());
}

static vec3 d_normalize(vec3 w, vec3 dw)
{
	float len = length(w);
	if (len < 1e-8f) {
		return vec3(0.0f);
	}
	vec3 n = w / len;
	return (dw - n * dot(n, dw)) / len;
}

// dR for R = D - 2 dot(D,N) N, with dN from a sphere at the origin (N = P/|P|).
static vec3 d_reflect(vec3 D, vec3 dD, vec3 N, vec3 dN)
{
	float dn = dot(D, N);
	return dD - 2.0f * (dot(dD, N) + dot(D, dN)) * N - 2.0f * dn * dN;
}

template<typename Tex>
static vec3 color(vec3 p, vec3 d, vec3 camP, Tex env, vec3 dDdx, vec3 dDdy)
{
	vec3 C(1.0f);
	vec3 N = normal(p);
	float L = max(dot(N, LIGHT), -0.5f) * 0.5f + 0.5f;
	float R = 1.0f - abs(dot(N, d));
	vec3 rd = reflect(d, N);
	// dP ≈ t dD (ignore dt). Sphere N = p/|p|; dN = project dP onto the tangent plane.
	float t = length(p - camP);
	float pr = length(p);
	vec3 dPdx = t * dDdx;
	vec3 dPdy = t * dDdy;
	vec3 dNdx = (pr > 1e-8f) ? (dPdx - N * dot(N, dPdx)) / pr : vec3(0.0f);
	vec3 dNdy = (pr > 1e-8f) ? (dPdy - N * dot(N, dPdy)) / pr : vec3(0.0f);
	return mix(C * L, texGrad(env, rd, d_reflect(d, dDdx, N, dNdx),
	                          d_reflect(d, dDdy, N, dNdy)), R);
}

static void camera(vec3& P, vec3& D, vec3& X, vec3& Y, vec3& Z)
{
	float M = float((iMouse.x + iMouse.y) > 0.0f);
	vec2 A = (0.5f - iMouse.xy() / iResolution.xy()) * vec2(6.2831f, 3.1416f);
	P = 32.0f * mix(vec3(1.0f, 0.0f, 0.0f),
	                vec3(cos(-A.x) * cos(A.y), sin(-A.x) * cos(A.y), sin(A.y)), M);
	D = -P;
	X = normalize(D);
	Y = normalize(cross(X, vec3(0.0f, 1.0f, 0.0f)));
	Z = cross(X, Y);
}

template<typename Tex>
static void shade(vec4* fragColor, vec2 fragCoord, Tex env,
                  vec3 camP, vec3 X, vec3 Y, vec3 Z)
{
	vec2 uv = (fragCoord - iResolution.xy() * 0.5f) / iResolution.y;
	vec3 W = X + uv.x * Y + uv.y * Z;
	vec3 D = normalize(W);
	float du = 1.0f / iResolution.y;
	vec3 dDdx = d_normalize(W, Y * du);
	vec3 dDdy = d_normalize(W, Z * du);

	vec4 M = raymarch(camP, D);
	float fog = pow(clamp(M.w, 0.0f, 1.0f), 4.0f);
	vec3 skycol = texGrad(env, D, dDdx, dDdy);
	vec3 col = mix(color(M.xyz(), D, camP, env, dDdx, dDdy), skycol, fog);
	*fragColor = vec4(col, 1.0f);
}

extern "C" {

void load_user_textures(user_media* m)
{
	m->cubemaps[0] = "https://dl.polyhaven.org/file/ph-assets/HDRIs/extra/Tonemapped%20JPG/kloofendal_48d_partly_cloudy.jpg";
}

void set_channels(texture_settings* ts)
{
	ts[0].ch = USER_CUBE0;
	SET_DFLT_CUBEMAP_PARAMS(ts[0]);
	ts[0].filter = TS_LINEAR;
	ts[0].vflip = GL_TRUE;
	ts[0].mipmap = GL_TRUE;
}

void mainImage(vec4* fragColor, vec2 fragCoord)
{
	vec3 P, D, X, Y, Z;
	camera(P, D, X, Y, Z);
	shade(fragColor, fragCoord, iChannel0, P, X, Y, Z);
}

void mainFrame(pix_t* framebuffer, int w, int h)
{
	const glTexture* env = pglGetTexture(iChannel0);
	vec3 P, D, X, Y, Z;
	camera(P, D, X, Y, Z);
	pix_t* lastrow = framebuffer + (h - 1) * w;

	SW_PARALLEL_FOR
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			vec2 fc;
			vec4 col;
			fc.x = x + 0.5f;
			fc.y = y + 0.5f;
			shade(&col, fc, env, P, X, Y, Z);
			lastrow[-y * w + x] = pack_clamp_rgba8(col.x, col.y, col.z, col.w);
		}
	}
}

}
