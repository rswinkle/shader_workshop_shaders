// RSW

#include "rsw_shader_workshop_common.h"
#include "nuklear_shader_workshop.h"

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


#define DFLT_STEPS 250
#define DFLT_OCTAVES 7
#define DFLT_AO 4
#define DFLT_STEP_SCALE 0.8f
#define DFLT_MAX_DIST 25.0f
#define DFLT_HIT 0.001f

#define MIN_STEPS 8
#define MAX_STEPS 400
#define MIN_OCTAVES 1
#define MAX_OCTAVES 8
#define MIN_AO 0
#define MAX_AO 8
#define MIN_STEP_SCALE 0.25f
#define MAX_STEP_SCALE 1.25f
#define MIN_MAX_DIST 4.0f
#define MAX_MAX_DIST 60.0f
#define MIN_HIT 0.0002f
#define MAX_HIT 0.02f

static int g_steps = DFLT_STEPS;
static int g_octaves = DFLT_OCTAVES;
static int g_ao = DFLT_AO;
static float g_step_scale = DFLT_STEP_SCALE;
static float g_max_dist = DFLT_MAX_DIST;
static float g_hit = DFLT_HIT;
static nk_bool g_normals = nk_true;

static void apply_quality_preset(int q)
{
	if (q <= 0) {
		g_steps = 48;
		g_octaves = 3;
		g_ao = 0;
		g_normals = nk_false;
	} else if (q == 1) {
		g_steps = 96;
		g_octaves = 5;
		g_ao = 2;
		g_normals = nk_true;
	} else {
		g_steps = DFLT_STEPS;
		g_octaves = DFLT_OCTAVES;
		g_ao = DFLT_AO;
		g_normals = nk_true;
	}
}

static void reset_defaults(void)
{
	g_step_scale = DFLT_STEP_SCALE;
	g_max_dist = DFLT_MAX_DIST;
	g_hit = DFLT_HIT;
	apply_quality_preset(2);
}

static float f(vec3 p)
{
	float v = 0.0f, a = 1.0f;
	const int oct = g_octaves;
	for (int i = 0; i < oct; i++)
	{
		v += n(p.xy() + p.z * 0.5f) * a;
		p *= 2.0f;
		a *= 0.5f;
	}
	return v;
}

static float m(vec3 p)
{
	//p.xy *= rot(p.z * 1.1f);
	vec2 tmp = p.xy() * rot(p.z * 1.1f);
	p.x = tmp.x;
	p.y = tmp.y;
	return 0.2f * (1.0f - length(p.xy())) - f(p + T * 0.1f) * 0.06f;
}

static vec4 gb(float z)
{
	float i = floor((z + 2.5f) * 0.2f);
	return vec4(cos(i * 2.4f) * 0.6f, sin(i * 2.4f) * 0.6f, i * 5.0f, i);
}

static vec3 bc(float i)
{
	float h = fract(sin(i * 13.54f) * 453.21f);
	return h < 0.33f ? vec3(1, 8, 9) * 0.1f : (h < 0.66f ? vec3(9, 2, 6) * 0.1f : vec3(10, 6, 1) * 0.1f);
}

extern "C" {

void set_channels(texture_settings* ts)
{
	(void)ts;
	reset_defaults();
}

nk_bool do_user_gui(struct nk_context* ctx, nk_bool is_playing)
{
	nk_bool need = nk_false;

	nk_layout_row_dynamic(ctx, 0, 1);
	nk_label(ctx, "Light Limestone Cave", NK_TEXT_CENTERED);
	nk_label(ctx, "Cost ~ steps x octaves x (march + normals + AO)", NK_TEXT_LEFT);

	nk_layout_row_dynamic(ctx, 0, 3);
	if (nk_button_label(ctx, "Fast")) {
		apply_quality_preset(0);
		need = nk_true;
	}
	if (nk_button_label(ctx, "Medium")) {
		apply_quality_preset(1);
		need = nk_true;
	}
	if (nk_button_label(ctx, "Fancy")) {
		apply_quality_preset(2);
		need = nk_true;
	}
	nk_layout_row_dynamic(ctx, ctx->style.font->height * 2.5f, 1);
	nk_label_wrap(ctx, "Presets set steps, octaves, AO, and normals. Fancy is the original (250 / 7 / 4). Fast turns normals off.");
	nk_layout_row_dynamic(ctx, 0, 1);
	nk_label_wrap(ctx, "Use Re-initialize Shader for a full reset.");

	if (nk_tree_push(ctx, NK_TREE_TAB, "Features", NK_MINIMIZED)) {
		nk_layout_row_dynamic(ctx, 0, 1);
		need |= nk_checkbox_label(ctx, "Surface normals (6 extra)", &g_normals);
		nk_label_wrap(ctx, "Off shades with the view direction. AO samples along that same direction.");
		nk_tree_pop(ctx);
	}

	if (nk_tree_push(ctx, NK_TREE_TAB, "March", NK_MINIMIZED)) {
		nk_layout_row_dynamic(ctx, 0, 1);
		need |= nk_property_int(ctx, "March steps:", MIN_STEPS, &g_steps, MAX_STEPS, 1, 1);
		need |= nk_property_int(ctx, "Octaves:", MIN_OCTAVES, &g_octaves, MAX_OCTAVES, 1, 1);
		need |= nk_property_int(ctx, "AO samples:", MIN_AO, &g_ao, MAX_AO, 1, 1);
		need |= nk_property_float(ctx, "Step scale:", MIN_STEP_SCALE, &g_step_scale, MAX_STEP_SCALE, 0.05f, 0.01f);
		need |= nk_property_float(ctx, "Max distance:", MIN_MAX_DIST, &g_max_dist, MAX_MAX_DIST, 1.0f, 0.25f);
		need |= nk_property_float(ctx, "Hit epsilon:", MIN_HIT, &g_hit, MAX_HIT, 0.0005f, 0.0001f);
		nk_label_wrap(ctx, "Octaves run inside every distance check. Step scale is the fraction taken each iteration (original 0.8). Hit epsilon is the stop threshold (original 0.001).");
		nk_tree_pop(ctx);
	}

	(void)is_playing;
	return need;
}

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

	const int steps = g_steps;
	const int ao_n = g_ao;
	const int do_normals = g_normals;
	const float step_scale = g_step_scale;
	const float max_dist = g_max_dist;
	const float hit = g_hit;

	for (int i = 0; i < steps; i++)
	{
		p = o + d * t;
		dc = m(p);
		bi = gb(p.z);
		db = length(p - bi.xyz()) - 0.03f;

		w = min(dc, db);
		g1 += 0.002f / (0.01f + abs(dc));
		g2 += (vec3(0.0003f / (0.001f + db * db)) + bc(bi.w) * 0.005f / (0.02f + abs(db))) * fk(bi.w);

		if (abs(w) < hit + t / 1000.0f || t > max_dist)
		{
			if (db < dc)
			{
				ht = true;
				hi = bi.w;
			}
			break;
		}
		t += w * step_scale;
	}

	if (t <= max_dist)
	{
		if (ht) c = vec3(12) + bc(hi) * 5.0f;
		else
		{
			if (do_normals) {
				vec2 e = vec2(hit + t / 1000.0f, 0);
				nn = normalize(vec3(m(p + e.xyy()) - m(p - e.xyy()), m(p + e.yxy()) - m(p - e.yxy()), m(p + e.yyx()) - m(p - e.yyx())));
			} else {
				nn = -d;
			}

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
			for (int i = 0; i < ao_n; i++)
			{
				float h = 0.01f + 0.03f * float(i + 2);
				oa += (h - m(p + h * nn)) * s;
				s *= 0.9f;
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

			lastrow[-y * w + x] = pack_clamp_rgba8(fragColor.x, fragColor.y, fragColor.z, fragColor.w);
		}
	}
}

}
