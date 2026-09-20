// RSW

#include "rsw_shader_workshop_common.h"

using namespace rsw;

// Original here:
// https://fragcoord.xyz/s/lenp0a1d

// SPDX-License-Identifier: CC-BY-4.0
// Copyright (c) 2026 @altunenes
//[LICENSE] https://creativecommons.org/licenses/by/4.0/

//#define T u_time
//#define R u_resolution.xy
#define T iTime
#define R iResolution.xy()
#define rot(a) mat2(cos(a), cos((a)+11.0f), cos((a)+33.0f), cos(a))
#define P(x, y) pow(abs(x), y)
#define X(a, b) max(dot(a, b), 0.0f)
#define fk(w) (1.0f+0.1f*(sin(T*25.0f+(w)*93.1f)*cos(T*14.0f+(w)*17.4f)))
#define n(p) (sin((p).x*3.0f+sin((p).y*2.7f))*cos((p).y*1.1f+cos((p).x*2.3f)))

float f(vec3 p)
{
	float v = 0.0f, a = 1.0f;
	for (int i = 0; i++ < 7; p *= 2.0f, a *= 0.5f)
	{
		v += n(p.xy() + p.z * 0.5f) * a;
	}
	return v;
}

float m(vec3 p)
{
	//p.xy *= rot(p.z * 1.1f);
	vec2 tmp = p.xy() * rot(p.z * 1.1f);
	p.x = tmp.x;
	p.y = tmp.y;
	return 0.2f * (1.0f - length(p.xy())) - f(p + T * 0.1f) * 0.06f;
}

vec4 gb(float z)
{
	float i = floor((z + 2.5f) * 0.2f);
	return vec4(cos(i * 2.4f) * 0.6f, sin(i * 2.4f) * 0.6f, i * 5.0f, i);
}

vec3 bc(float i)
{
	float h = fract(sin(i * 13.54f) * 453.21f);
	return h < 0.33f ? vec3(1, 8, 9) * 0.1f : (h < 0.66f ? vec3(9, 2, 6) * 0.1f : vec3(10, 6, 1) * 0.1f);
}

extern "C" {

void mainImage(vec4* fragColor, vec2 fragCoord)
{
	vec2 U = fragCoord;

	vec3 d = normalize(vec3((U - 0.5f * R) / R.y, 1.0f)),
	o = vec3(sin(T * 0.3f) * 0.2f, cos(T * 0.2f) * 0.2f, T * 1.2f),
	p, c, g2 = vec3(0), nn, l, b;

	//d.xy *= rot(T * 0.15);
	vec2 tmp = d.xy() * rot(T * 0.15f);
	d.x = tmp.x;
	d.y = tmp.y;
	bool ht = false;
	float t = 0.0f, w, hi, g1 = 0.0f, dc, db;
	vec4 bi;

	for (int i = 0; i++ < 250;)
	{
		p = o + d * t;
		dc = m(p);
		bi = gb(p.z);
		db = length(p - bi.xyz()) - 0.03f;

		w = min(dc, db);
		g1 += 0.002f / (0.01f + abs(dc));
		g2 += (vec3(0.0003f / (0.001f + db * db)) + bc(bi.w) * 0.005f / (0.02f + abs(db))) * fk(bi.w);

		if (abs(w) < 0.001f + t / 1000.0f || t > 25.0f)
		{
			if (db < dc)
			{
				ht = true;
				hi = bi.w;
			}
			break;
		}
		t += w * 0.8f;
	}

	if (t <= 25.0f)
	{
		if (ht) c = vec3(12) + bc(hi) * 5.0f;
		else
		{
			vec2 e = vec2(0.001f + t / 1000.0f, 0);
			nn = normalize(vec3(m(p + e.xyy()) - m(p - e.xyy()), m(p + e.yxy()) - m(p - e.yxy()), m(p + e.yyx()) - m(p - e.yyx())));

			vec3 q = p;
			//q.xy *= rot(q.z * 1.1);
			vec2 tmp = q.xy() * rot(q.z * 1.1f);
			q.x = tmp.x;
			q.y = tmp.y;

			c = mix(vec3(1, 3, 8) * 0.05f, vec3(9, 4, 1) * 0.1f, P(max(min(f(q + T * 0.1f) + 0.5f, 1.0f), 0.0f), 2.0f));

			l = o + vec3(0, 0, 5) - p;
			float d1 = length(l);
			l /= d1;

			c = c * 0.03f + (c * X(nn, l) * 1.5f + vec3(1, 0.8f, 0.6f) * P(X(nn, normalize(l - d)), 24.0f) * smoothstep(20.0f, 5.0f, t) * 1.5f) / (1.0f + d1 * d1 * 0.08f);

			bi = gb(p.z);
			l = bi.xyz() - p;
			float d2 = length(l);
			l /= d2;
			b = bc(bi.w);

			c += ((c * X(nn, l) * 2.5f) + (b * P(X(nn, normalize(l - d)), 16.0f) * 4.0f)) * b * fk(bi.w) * (0.5f + 0.5f * fract(sin(bi.w * 88.1f) * 12.3f)) / (1.0f + d2 * d2 * 1.5f);

			float oa = 0.0f, s = 1.0f;
			for (int i = 1; i++ < 5; s *= 0.9f)
			{
				float h = 0.01f + 0.03f * float(i);
				oa += (h - m(p + h * nn)) * s;
				if (oa > 0.33f) break;
			}

			c = (c + vec3(5, 3, 8) * 0.1f * P(1.0f - X(nn, -d), 4.0f) * 0.6f / (1.0f + d1 * d1 * 0.08f)) * max(1.0f - 3.0f * oa, 0.0f);
		}
	}

	c = mix(vec3(2, 0, 5) * 0.01f, c, 1.0f / exp(0.12f * t))
	+ vec3(9, 3, 1) * 0.1f * g1 * 0.02f / exp(0.05f * t)
	+ g2 / exp(0.03f * t);

	c = c * (2.51f * c + 0.03f) / (c * (2.43f * c + 0.59f) + 0.14f);

	U /= R;
	U *= 1.0f - U;
	*fragColor = vec4(P(c * P(16.0f * U.x * U.y, 0.25f), vec3(2.5f)), 1);
}

// Just using this instead of the frag shader above gets us from 15 fps to 60, single threaded
void mainFrame(pix_t* framebuffer, int w, int h)
{
	vec2 fragCoord;
	vec4 fragColor;
	pix_t* lastrow = framebuffer + (h-1)*w;

SW_PARALLEL_FOR
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {

			vec2 fragCoord;   // private to this pixel (automatically)
			vec4 fragColor;

			fragCoord.x = x + 0.5f;
			fragCoord.y = y + 0.5f;

			mainImage(&fragColor, fragCoord);

			// Doing this to avoid type issues vec4/pgl_vec4
			/*fragColor.x = pgl_clamp_01(fragColor.x);*/
			/*fragColor.y = pgl_clamp_01(fragColor.y);*/
			/*fragColor.z = pgl_clamp_01(fragColor.z);*/
			/*fragColor.w = pgl_clamp_01(fragColor.w);*/

			pgl_Color src_color = make_Color(fragColor.x*PGL_RMAX,
			                                 fragColor.y*PGL_GMAX,
			                                 fragColor.z*PGL_BMAX,
			                                 fragColor.w*PGL_AMAX);

			lastrow[-y * w + x] = RGBA_TO_PIXEL(src_color.r,
			                                    src_color.g,
			                                    src_color.b,
			                                    src_color.a);
		}
	}
}

}
