// GLM

// Shader by mrange
// https://www.shadertoy.com/view/s3fXR4
//
// Ported for Shader Workshop
//
// Rasterized mainFrame: height mesh + baked-grid shadow march (same step
// rule as sraymarch). mainImage is the original marcher (toggle Use Frame Shader).

#include "glm_shader_workshop_common.h"

#include <stdlib.h>

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

// ---- rasterized mainFrame --------------------------------------------------
// Camera matches mainImage: RD = normalize(c2.y*Y - c2.x*X + h*Z),
// so pixel = (w,h)/2 + h * (dot(P-RO,-X), dot(P-RO,Y)) / dot(P-RO,Z).

#define TERRAIN_NX 256
#define TERRAIN_NZ 256
#define TERRAIN_ZFAR 1e30f

static float g_h[TERRAIN_NX * TERRAIN_NZ];
static float g_sx[TERRAIN_NX * TERRAIN_NZ];
static float g_sy[TERRAIN_NX * TERRAIN_NZ];
static float g_cz[TERRAIN_NX * TERRAIN_NZ];
static unsigned char g_ok[TERRAIN_NX * TERRAIN_NZ];
static float g_origin_x, g_origin_z, g_cell_x, g_cell_z;

static float* g_zbuf = NULL;
static int g_zcap = 0;

static float edge2(float ax, float ay, float bx, float by, float cx, float cy)
{
	return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
}

static void raster_tri(int w, int h, float* zbuf,
	float x0, float y0, float z0,
	float x1, float y1, float z1,
	float x2, float y2, float z2)
{
	if (z0 < 1e-3f || z1 < 1e-3f || z2 < 1e-3f) {
		return;
	}

	float minx = x0;
	if (x1 < minx) {
		minx = x1;
	}
	if (x2 < minx) {
		minx = x2;
	}
	float maxx = x0;
	if (x1 > maxx) {
		maxx = x1;
	}
	if (x2 > maxx) {
		maxx = x2;
	}
	float miny = y0;
	if (y1 < miny) {
		miny = y1;
	}
	if (y2 < miny) {
		miny = y2;
	}
	float maxy = y0;
	if (y1 > maxy) {
		maxy = y1;
	}
	if (y2 > maxy) {
		maxy = y2;
	}

	int xmin = (int)minx;
	int xmax = (int)maxx;
	int ymin = (int)miny;
	int ymax = (int)maxy;
	if (xmin < 0) {
		xmin = 0;
	}
	if (ymin < 0) {
		ymin = 0;
	}
	if (xmax > w - 1) {
		xmax = w - 1;
	}
	if (ymax > h - 1) {
		ymax = h - 1;
	}
	if (xmin > xmax || ymin > ymax) {
		return;
	}

	float area = edge2(x0, y0, x1, y1, x2, y2);
	if (area > -1e-6f && area < 1e-6f) {
		return;
	}

	float inva = 1.f / area;

	for (int y = ymin; y <= ymax; ++y) {
		float py = (float)y + 0.5f;
		for (int x = xmin; x <= xmax; ++x) {
			float px = (float)x + 0.5f;
			float a0 = edge2(x1, y1, x2, y2, px, py);
			float a1 = edge2(x2, y2, x0, y0, px, py);
			float a2 = edge2(x0, y0, x1, y1, px, py);
			if (area > 0.f) {
				if (a0 < 0.f || a1 < 0.f || a2 < 0.f) {
					continue;
				}
			} else {
				if (a0 > 0.f || a1 > 0.f || a2 > 0.f) {
					continue;
				}
			}
			float b0 = a0 * inva;
			float b1 = a1 * inva;
			float b2 = a2 * inva;
			float invz = b0 / z0 + b1 / z1 + b2 / z2;
			if (invz < 1e-12f && invz > -1e-12f) {
				continue;
			}
			float depth = 1.f / invz;
			int idx = y * w + x;
			if (depth < zbuf[idx]) {
				zbuf[idx] = depth;
			}
		}
	}
}

static void raster_grid(int w, int h, float* zbuf)
{
	for (int iz = 0; iz < TERRAIN_NZ - 1; ++iz) {
		for (int ix = 0; ix < TERRAIN_NX - 1; ++ix) {
			int i00 = iz * TERRAIN_NX + ix;
			int i10 = i00 + 1;
			int i01 = i00 + TERRAIN_NX;
			int i11 = i01 + 1;
			if (g_ok[i00] && g_ok[i10] && g_ok[i01]) {
				raster_tri(w, h, zbuf,
					g_sx[i00], g_sy[i00], g_cz[i00],
					g_sx[i10], g_sy[i10], g_cz[i10],
					g_sx[i01], g_sy[i01], g_cz[i01]);
			}
			if (g_ok[i10] && g_ok[i11] && g_ok[i01]) {
				raster_tri(w, h, zbuf,
					g_sx[i10], g_sy[i10], g_cz[i10],
					g_sx[i11], g_sy[i11], g_cz[i11],
					g_sx[i01], g_sy[i01], g_cz[i01]);
			}
		}
	}
}

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
// Running out of 44 steps with z still short is a miss in the original too
// (that is what makes the low sun throw long shadows).
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

extern "C" {

void mainFrame(pix_t* framebuffer, int w, int h)
{
	int npix = w * h;
	if (npix > g_zcap) {
		float* nb = (float*)realloc(g_zbuf, (size_t)npix * sizeof(float));
		assert(nb);
		g_zbuf = nb;
		g_zcap = npix;
	}

	pix_t* lastrow = framebuffer + (h - 1) * w;
	vec3 RO = vec3(0.f, 4.f, 0.3f * iTime);
	float fw = (float)w;
	float fh = (float)h;
	float aspect = fw / fh;
	float x_ext = 0.6f * aspect * max_distance + 8.f;
	float x0 = -x_ext;
	float x1 = x_ext + max_distance * light.x;
	float z0 = RO.z - 6.f;
	float z1 = RO.z + max_distance + 2.f + max_distance * light.z;
	float dx = (x1 - x0) / (float)(TERRAIN_NX - 1);
	float dz = (z1 - z0) / (float)(TERRAIN_NZ - 1);
	x0 = floor(x0 / dx) * dx;
	z0 = floor(z0 / dz) * dz;
	g_origin_x = x0;
	g_origin_z = z0;
	g_cell_x = dx;
	g_cell_z = dz;

SW_PARALLEL_FOR
	for (int iz = 0; iz < TERRAIN_NZ; ++iz) {
		for (int ix = 0; ix < TERRAIN_NX; ++ix) {
			float wx = x0 + dx * (float)ix;
			float wz = z0 + dz * (float)iz;
			int i = iz * TERRAIN_NX + ix;
			float hy = hf(vec2(wx, wz));
			g_h[i] = hy;
			vec3 P = vec3(wx, hy, wz);
			vec3 rel = P - RO;
			float cx = -dot(rel, X);
			float cy = dot(rel, Y);
			float cz = dot(rel, Z);
			g_cz[i] = cz;
			if (cz > 1e-3f) {
				g_sx[i] = 0.5f * fw + fh * cx / cz;
				g_sy[i] = 0.5f * fh + fh * cy / cz;
				g_ok[i] = 1;
			} else {
				g_sx[i] = 0.f;
				g_sy[i] = 0.f;
				g_ok[i] = 0;
			}
		}
	}

	for (int i = 0; i < npix; ++i) {
		g_zbuf[i] = TERRAIN_ZFAR;
	}

	raster_grid(w, h, g_zbuf);

SW_PARALLEL_FOR
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			vec2 c2 = vec2((float)x + 0.5f, (float)y + 0.5f) - 0.5f * vec2(fw, fh);
			vec3 RD = normalize(c2.y * Y - c2.x * X + fh * Z);
			vec3 ycol = shade_sky(RD);
			vec3 o = ycol;

			float camz = g_zbuf[y * w + x];
			if (camz < TERRAIN_ZFAR * 0.5f) {
				float denom = dot(RD, Z);
				if (denom > 1e-6f) {
					float t = camz / denom;
					if (t > 0.f && t < max_distance) {
						vec3 p = RO + t * RD;
						vec3 n = nf(p.xz());
						float iacc = 0.f;
						if (grid_unshadowed(p + 0.05f * n)) {
							iacc += max(0.f, dot(n, light));
						}
						iacc += sqrt((1.f - n.y)) * 0.1f;
						o = ground * iacc;
						float z = t - max_distance * 0.5f;
						z = max(0.f, z);
						o = mix(ycol, o, exp(-1e-2f * z * z));
					}
				}
			}

			o = tonemap_out(o);
			lastrow[-y * w + x] = pack_clamp_rgba8(o.x, o.y, o.z, 1.f);
		}
	}
}

}
