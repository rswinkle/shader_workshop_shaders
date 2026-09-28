// GLM

// Shader by mrange
// https://www.shadertoy.com/view/s3fXR4
//
// Ported for Shader Workshop
//
// mainFrame: PGL depth-only rasterize, then OpenMP shade via GET_Z on
// get_glContext()->zbuf. Load in shader_workshop_full_pgl.
// mainImage is the original marcher.
//
// #define USE_FULL_PGL before the common header so this TU matches the host.

#define USE_FULL_PGL
#include "glm_shader_workshop_common.h"

using namespace glm;


// CC0: A quick terrain hack
//  Created this in order to demonstrate how to implement a simple terrain
//  ray tracer for a friend.

// It would benefit from some TAA but that would make it more complicated

// This file is released under CC0 1.0 Universal (Public Domain Dedication).
// To the extent possible under law, mrange has waived all copyright
// and related or neighboring rights to this work.
// See <https://creativecommons.org/publicdomain/zero/1.0/> for details.

// License: WTFPL, author: sam hocevar, found: https://stackoverflow.com/a/17897228/418488
const vec4 hsv2rgb_K = vec4(1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 3.0f);
// License: WTFPL, author: sam hocevar, found: https://stackoverflow.com/a/17897228/418488
//  Macro version of above to enable compile-time constants
// #define HSV2RGB(c)  (c.z * mix(hsv2rgb_K.xxx, clamp(abs(fract(c.xxx + hsv2rgb_K.xyz) * 6.0 - hsv2rgb_K.www) - hsv2rgb_K.xxx, 0.0, 1.0), c.y))
static vec3 HSV2RGB(vec3 c)
{
	return c.z * mix(hsv2rgb_K.xxx(), clamp(abs(fract(c.xxx() + hsv2rgb_K.xyz()) * 6.0f - hsv2rgb_K.www()) - hsv2rgb_K.xxx(), 0.0f, 1.0f), c.y);
}

const float
  max_distance=3e1f
;

const vec3
  // Colors for various parts in the scene
  sun     = HSV2RGB(vec3(.06f,.8f,3e-3f))
, sky     = HSV2RGB(vec3(.58f,.7f,.5f))
, dust    = HSV2RGB(vec3(.02f,.7f,1))
, ground  = HSV2RGB(vec3(.03f,.8f,1))
  // Light direction
, light   = normalize(vec3(4,1,6))
  // Used to setup the ray direction
, Z       = normalize(vec3(0,-2,7))
, X       = normalize(cross(Z,vec3(0,1,0)))
, Y       = cross(X,Z)
;

// Returns terrain height
float hf(vec2 p) {
	p*=.25f;
	float
	  a=1.f
	, h=0.f
	;
	vec2
	  D=vec2(0)
	;
	vec3
	  w
	;
	vec4
	  C
	;
	// Rotation + Scaling
	const mat2 R=mat2(6,8,-8,6)/5.f;
	// FBM using 8 octaves: https://iquilezles.org/articles/fbm/
	for(int i=0;i<8;++i) {
		// Generates a Sin p.X*Sin p.Y waveform plus its' derivates
		C=cos(p.xxyy()+vec4(11,0,11,0));
		w=C.yxx()*C.zwz();

		// Accumulate the derivates
		D+=w.xy();
		// Accumulates height using the waveform and its' derivates
		//  Using the derivates creates a more interesting landscape
		//  Technique "borrowed" from IQ
		h+=(a*w.z+a)/(3.f*dot(D,D)+1.f);
		// Decrease amplitude with 50%
		a*=.5f;
		// Double frequency and rotate
		p=p*R;
		// A bit of offset
		p+=1.23f;
	}

	return h;
}

vec3 nf(vec2 p) {
	// Computes the normal
	const vec2
	  E = vec2(1e-3f, 0)
	;
	return normalize(vec3(
	  hf(p-E.xy()) - hf(p+E.xy())
	, 2.f*E.x
	, hf(p-E.yx())-hf(p+E.yx())
	));
}

float raymarch(vec3 RO, vec3 RD, float init) {
	// Simple raymarch loop to find the intersection with the terrain
	float
	  z=init
	, h
	, d
	;

	vec3
	  p
	;

	for(int i=0;i<69;++i) {
		p=z*RD+RO;
		h=hf(p.xz());
		d=p.y-h;
		if(d<1e-3f||z>max_distance) {
			break;
		}
		// Because d is an approximate distance use fraction of it to step
		z+=.8f*d;
	}

	return z;
}

float sraymarch(vec3 RO, vec3 RD, float init) {
	// Simple raymarch loop to find the intersection with the terrain
	//  used for the shadows, less loops, greater step size
	float
	  z=init
	, h
	, d
	;

	vec3
	  p
	;

	for(int i=0;i<44;++i) {
		p=z*RD+RO;
		h=hf(p.xz());
		d=p.y-h;
		if(d<5e-3f||z>max_distance) {
			break;
		}
		z+=d;
	}

	return z;
}

// License: Unknown, author: Matt Taylor (https://github.com/64), found: https://64.github.io/tonemapping/
vec3 aces_approx(vec3 v) {
	const float
	  a = 2.51f
	, b = 0.03f
	, c = 2.43f
	, d = 0.59f
	, e = 0.14f
	;
	v = max(v, 0.f);
	v *= .6f;
	return clamp((v*(a*v+b))/(v*(c*v+d)+e), 0.f, 1.f);
}

extern "C" {

void mainImage(vec4* fragColor, vec2 C) {
	vec2
	  r2=iResolution.xy()
	, c2=C-.5f*r2
	;
	vec3
	  o=vec3(0)
	, y
	, RO=vec3(0,4,.3f*iTime)
	  // The ray direction
	, RD=normalize(c2.y*Y-c2.x*X+r2.y*Z)
	, p
	, n
	;

	float
	  z
	, s
	  // Intersection above the mountain range
	, tz=(3.3f-RO.y)/RD.y
	, i=0.f
	;

	// Computes the sky
	y=
	    sun/max(1e-4f, .999f-dot(light,RD))
	  + sky*smoothstep(.3f,-.15f,RD.y)
	;
	y=mix(y,dust,smoothstep(.00f,-.15f,RD.y));

	if(tz>0.f&&tz<max_distance) {
		// Only check intersection with terrain if we hit the plane in front of us and less than max distance
		z=raymarch(RO,RD,tz);
		// Current pos
		p=z*RD+RO;
		// The normal
		n=nf(p.xz());
		// Shadow ray intersection
		s=sraymarch(p+.05f*n,light,0.f);
		z=clamp(z,0.f,max_distance);
		if(z<max_distance) {
			// We hit the ground
			if(s>=max_distance) {
				// Diffuse light from the sun
				i+=max(.0f,dot(n,light));
			}
			// Diffuse light from the sky
			i+=sqrt((1.f-n.y))*.1f;
			o=ground*i;
			z-=max_distance*.5f;
			z=max(0.f,z);
			// Fade the sky and the groun for a fog like effect
			o=mix(y,o,exp(-1e-2f*z*z));
		} else {
			// Miss
			o=y;
		}
	} else {
			// Miss
		o=y;
	}

	// Post process
	o*=2.f;
	// Tone mapping
	o=aces_approx(o);
	// Approximate RBG => sRGB
	o=sqrt(o)-.04f;
	*fragColor = vec4(o,1);
}

}

// ---- PGL depth pass + OpenMP shade ----------------------------------------
// Camera matches mainImage: RD = normalize(c2.y*Y - c2.x*X + h*Z).
// Clip: gl_Position.w = camz; z is GL ndc [-1,1] (PGL maps to window [0,1]).
// GET_Z needs a TU-global named c (same as the rasterizer).

glContext* c;

#define TERRAIN_NX 256
#define TERRAIN_NZ 256
#define TERRAIN_ZNEAR 0.1f
#define TERRAIN_ZFAR 80.f

static float g_h[TERRAIN_NX * TERRAIN_NZ];
static unsigned char g_ok[TERRAIN_NX * TERRAIN_NZ];
static float g_verts[TERRAIN_NX * TERRAIN_NZ * 3];
static GLuint g_idx[(TERRAIN_NX - 1) * (TERRAIN_NZ - 1) * 6];
static float g_origin_x, g_origin_z, g_cell_x, g_cell_z;

static GLuint g_prog;
static GLuint g_vbo;
static GLuint g_ibo;
static int g_gl_ok;
static int g_vp_w, g_vp_h;

struct TerrainU {
	vec3 RO;
	float h_over_w;
	float znear;
	float zfar;
	float fw;
	float fh;
};

static TerrainU g_u;

static float sample_h(float wx, float wz)
{
	float fx = (wx - g_origin_x) / g_cell_x;
	float fz = (wz - g_origin_z) / g_cell_z;
	if (fx < 0.f || fz < 0.f || fx > (float)(TERRAIN_NX - 1) || fz > (float)(TERRAIN_NZ - 1)) {
		return -1e3f;
	}
	int ix = (int)fx;
	int iz = (int)fz;
	if (ix >= TERRAIN_NX - 1) {
		ix = TERRAIN_NX - 2;
	}
	if (iz >= TERRAIN_NZ - 1) {
		iz = TERRAIN_NZ - 2;
	}
	float tx = fx - (float)ix;
	float tz = fz - (float)iz;
	int i00 = iz * TERRAIN_NX + ix;
	float h00 = g_h[i00];
	float h10 = g_h[i00 + 1];
	float h01 = g_h[i00 + TERRAIN_NX];
	float h11 = g_h[i00 + TERRAIN_NX + 1];
	float h0 = h00 + (h10 - h00) * tx;
	float h1 = h01 + (h11 - h01) * tx;
	return h0 + (h1 - h0) * tz;
}

// Same contract as sraymarch: lit only if the ray reaches max_distance.
static int grid_unshadowed(vec3 ro)
{
	float z = 0.f;
	for (int i = 0; i < 44; ++i) {
		vec3 p = z * light + ro;
		float d = p.y - sample_h(p.x, p.z);
		if (d < 5e-3f || z > max_distance) {
			break;
		}
		z += d;
	}
	return z >= max_distance;
}

static vec3 shade_sky(vec3 RD)
{
	vec3 y =
	    sun / max(1e-4f, 0.999f - dot(light, RD))
	  + sky * smoothstep(0.3f, -0.15f, RD.y);
	y = mix(y, dust, smoothstep(0.00f, -0.15f, RD.y));
	return y;
}

static vec3 tonemap_out(vec3 o)
{
	o *= 2.f;
	o = aces_approx(o);
	o = sqrt(o) - 0.04f;
	return o;
}

static void terrain_vs(float* vs_output, pgl_vec4* vattr, Shader_Builtins* builtins, void* uniforms)
{
	(void)vs_output;
	TerrainU* u = (TerrainU*)uniforms;
	vec3 P = vec3(vattr[0].x, vattr[0].y, vattr[0].z);
	vec3 rel = P - u->RO;
	float cx = -dot(rel, X);
	float cy = dot(rel, Y);
	float cz = dot(rel, Z);
	float n = u->znear;
	float f = u->zfar;
	builtins->gl_Position.x = 2.f * u->h_over_w * cx;
	builtins->gl_Position.y = 2.f * cy;
	builtins->gl_Position.z = ((f + n) / (f - n)) * cz + (-2.f * f * n) / (f - n);
	builtins->gl_Position.w = cz;
}

static void terrain_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
	(void)fs_input;
	(void)uniforms;
	builtins->gl_FragColor.x = 0.f;
	builtins->gl_FragColor.y = 0.f;
	builtins->gl_FragColor.z = 0.f;
	builtins->gl_FragColor.w = 1.f;
}

static void setup_gl(void)
{
	if (!g_gl_ok) {
		g_prog = pglCreateProgram(terrain_vs, terrain_fs, 0, NULL, GL_FALSE);
		assert(g_prog);
		glGenBuffers(1, &g_vbo);
		glGenBuffers(1, &g_ibo);
		assert(g_vbo && g_ibo);
		g_gl_ok = 1;
	}

	g_u.znear = TERRAIN_ZNEAR;
	g_u.zfar = TERRAIN_ZFAR;
	glUseProgram(g_prog);
	pglSetUniform(&g_u);

	glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ibo);

	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDepthMask(GL_TRUE);
}

extern "C" {

void set_channels(texture_settings ts[NUM_CHANNELS])
{
	(void)ts;
	c = get_glContext();
	assert(c);
	assert(c->zbuf.buf);
	setup_gl();
}

void mainFrame(pix_t* framebuffer, int w, int h)
{
	assert(g_gl_ok);

	pix_t* lastrow = framebuffer + (h - 1) * w;
	vec3 RO = vec3(0.f, 4.f, 0.3f * iTime);
	float fw = (float)w;
	float fh = (float)h;
	float aspect = fw / fh;
	float x_ext = 0.6f * aspect * max_distance + 8.f;
	float x0 = -x_ext;
	float x1 = x_ext + max_distance * light.x;
	float z0w = RO.z - 6.f;
	float z1w = RO.z + max_distance + 2.f + max_distance * light.z;
	float dx = (x1 - x0) / (float)(TERRAIN_NX - 1);
	float dz = (z1w - z0w) / (float)(TERRAIN_NZ - 1);
	x0 = floor(x0 / dx) * dx;
	z0w = floor(z0w / dz) * dz;
	g_origin_x = x0;
	g_origin_z = z0w;
	g_cell_x = dx;
	g_cell_z = dz;

SW_PARALLEL_FOR
	for (int iz = 0; iz < TERRAIN_NZ; ++iz) {
		for (int ix = 0; ix < TERRAIN_NX; ++ix) {
			float wx = x0 + dx * (float)ix;
			float wz = z0w + dz * (float)iz;
			int i = iz * TERRAIN_NX + ix;
			float hy = hf(vec2(wx, wz));
			g_h[i] = hy;
			g_verts[i * 3 + 0] = wx;
			g_verts[i * 3 + 1] = hy;
			g_verts[i * 3 + 2] = wz;
			vec3 rel = vec3(wx, hy, wz) - RO;
			float cz = dot(rel, Z);
			g_ok[i] = (cz > TERRAIN_ZNEAR) ? 1 : 0;
		}
	}

	int nidx = 0;
	for (int iz = 0; iz < TERRAIN_NZ - 1; ++iz) {
		for (int ix = 0; ix < TERRAIN_NX - 1; ++ix) {
			GLuint i00 = (GLuint)(iz * TERRAIN_NX + ix);
			GLuint i10 = i00 + 1;
			GLuint i01 = i00 + TERRAIN_NX;
			GLuint i11 = i01 + 1;
			if (g_ok[i00] && g_ok[i10] && g_ok[i01]) {
				g_idx[nidx++] = i00;
				g_idx[nidx++] = i10;
				g_idx[nidx++] = i01;
			}
			if (g_ok[i10] && g_ok[i11] && g_ok[i01]) {
				g_idx[nidx++] = i10;
				g_idx[nidx++] = i11;
				g_idx[nidx++] = i01;
			}
		}
	}

	pglSetBackBuffer(framebuffer, w, h, GL_TRUE);
	if (w != g_vp_w || h != g_vp_h) {
		glViewport(0, 0, w, h);
		g_vp_w = w;
		g_vp_h = h;
	}

	g_u.RO = RO;
	g_u.h_over_w = fh / fw;
	g_u.fw = fw;
	g_u.fh = fh;

	glClear(GL_DEPTH_BUFFER_BIT);
	if (nidx > 0) {
		glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(g_verts), g_verts, GL_DYNAMIC_DRAW);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)nidx * (GLsizeiptr)sizeof(GLuint), g_idx, GL_DYNAMIC_DRAW);
		glDrawElements(GL_TRIANGLES, nidx, GL_UNSIGNED_INT, 0);
	}

	float znear = g_u.znear;
	float zfar = g_u.zfar;
	int zw = c->zbuf.w;

SW_PARALLEL_FOR
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			vec2 c2 = vec2((float)x + 0.5f, (float)y + 0.5f) - 0.5f * vec2(fw, fh);
			vec3 RD = normalize(c2.y * Y - c2.x * X + fh * Z);
			vec3 ycol = shade_sky(RD);
			vec3 o = ycol;

			int zi = -y * zw + x;
			u32 zpacked = GET_Z(zi);
			if (zpacked < PGL_MAX_Z) {
				float win_z = (float)zpacked / (float)PGL_MAX_Z;
				float ndc_z = win_z * 2.f - 1.f;
				float denom_p = (zfar + znear) - ndc_z * (zfar - znear);
				if (denom_p > 1e-6f) {
					float camz = (2.f * zfar * znear) / denom_p;
					float denom = dot(RD, Z);
					if (denom > 1e-6f) {
						float t = camz / denom;
						if (t > 0.f && t < max_distance) {
							vec3 p = RO + t * RD;
							vec3 nrm = nf(p.xz());
							float iacc = 0.f;
							if (grid_unshadowed(p + 0.05f * nrm)) {
								iacc += max(0.f, dot(nrm, light));
							}
							iacc += sqrt((1.f - nrm.y)) * 0.1f;
							o = ground * iacc;
							float zfog = t - max_distance * 0.5f;
							zfog = max(0.f, zfog);
							o = mix(ycol, o, exp(-1e-2f * zfog * zfog));
						}
					}
				}
			}

			o = tonemap_out(o);
			lastrow[-y * w + x] = pack_clamp_rgba8(o.x, o.y, o.z, 1.f);
		}
	}
}

}
