// GLM
//
// Port of Yohei Nishitsuji (@YoheiNishitsuji) "geometric play -pp-"
// Tweet: https://x.com/YoheiNishitsuji/status/2093337808106668254
// Expanded: https://fragcoord.xyz/s/gof15tt0
//
// Original twigl GEEKEST source:
//
// for(float i=0.,z=0.,d=0.,s=0.;i++<1e2;){vec3 q=z*normalize(vec3(FC.xy*2.-r,r.y));q.z+=t*4.;q.xy*=rotate2D(q.z*.1);for(s=1.;s<32.;s/=.5)q+=(abs(cos(q.yzx*s))-.68)/s;z+=d=.005+abs((length(q.yx)-6.)*q.x)/8.;o.rgb+=hsv(.3+sin(q.x)*.2,.5,.2)*exp(-z*.1)/d;}o=tanh(o/2e2);

#include "glm_shader_workshop_common.h"
using namespace glm;

#include "nuklear_shader_workshop.h"

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 @YoheiNishitsuji
//[LICENSE] https://opensource.org/licenses/MIT

extern "C" {

// Default / Fancy = original tweet. Fast and Medium only drop march cost.
static int g_quality = 2;
static int g_max_steps = 100;
static int g_warp_octaves = 5;
static float g_far_plane = 100.0f;
static float g_time_speed = 4.0f;
static float g_twist = 0.1f;
static nk_bool g_cw_spiral = nk_true;
static float g_warp_offset = 0.68f;
static float g_min_step = 0.005f;
static float g_tube_radius = 6.0f;
static float g_de_scale = 8.0f;
static float g_hue = 0.3f;
static float g_hue_amp = 0.2f;
static float g_sat = 0.5f;
static float g_val = 0.2f;
static float g_fog = 0.1f;
static float g_tonemap = 200.0f;

static void apply_quality_preset(int q)
{
	g_quality = q;
	if (q <= 0) {
		g_max_steps = 20;
		g_far_plane = 28.0f;
		g_min_step = 0.012f;
		g_warp_octaves = 2;
	} else if (q == 1) {
		g_max_steps = 40;
		g_far_plane = 48.0f;
		g_min_step = 0.008f;
		g_warp_octaves = 3;
	} else {
		g_max_steps = 100;
		g_far_plane = 100.0f;
		g_min_step = 0.005f;
		g_warp_octaves = 5;
	}
}

void set_channels(texture_settings* ts)
{
	(void)ts;
	g_time_speed = 4.0f;
	g_twist = 0.1f;
	g_cw_spiral = nk_true;
	g_warp_offset = 0.68f;
	g_tube_radius = 6.0f;
	g_de_scale = 8.0f;
	g_hue = 0.3f;
	g_hue_amp = 0.2f;
	g_sat = 0.5f;
	g_val = 0.2f;
	g_fog = 0.1f;
	g_tonemap = 200.0f;
	apply_quality_preset(2);
}

nk_bool do_user_gui(struct nk_context* ctx, nk_bool is_playing)
{
	nk_bool need = nk_false;

	nk_layout_row_dynamic(ctx, 0, 1);
	nk_label(ctx, "Geometric Play", NK_TEXT_CENTERED);
	nk_label(ctx, "Cost ~ pixels x steps x warp octaves", NK_TEXT_LEFT);

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
	nk_label_wrap(ctx, "Presets only change march cost, not color/twist. Fancy is the original tweet.");
	nk_layout_row_dynamic(ctx, 0, 1);
	nk_label_wrap(ctx, "Use Re-initialize Shader for a full reset.");

	if (nk_tree_push(ctx, NK_TREE_TAB, "March", NK_MINIMIZED)) {
		nk_layout_row_dynamic(ctx, 0, 1);
		need |= nk_property_int(ctx, "Max steps:", 4, &g_max_steps, 250, 1, 1);
		need |= nk_property_float(ctx, "Far plane:", 4.0f, &g_far_plane, 400.0f, 1.0f, 0.25f);
		need |= nk_property_float(ctx, "Min step:", 0.0005f, &g_min_step, 0.05f, 0.001f, 0.0001f);
		need |= nk_property_float(ctx, "Tube radius:", 0.5f, &g_tube_radius, 20.0f, 0.25f, 0.05f);
		need |= nk_property_float(ctx, "DE scale:", 1.0f, &g_de_scale, 32.0f, 0.25f, 0.05f);
		nk_tree_pop(ctx);
	}

	if (nk_tree_push(ctx, NK_TREE_TAB, "Motion / warp", NK_MINIMIZED)) {
		nk_layout_row_dynamic(ctx, 0, 1);
		need |= nk_property_float(ctx, "Time speed:", 0.0f, &g_time_speed, 12.0f, 0.25f, 0.05f);
		need |= nk_property_float(ctx, "Twist:", 0.0f, &g_twist, 1.0f, 0.01f, 0.001f);
		need |= nk_checkbox_label(ctx, "CW spiral", &g_cw_spiral);
		need |= nk_property_int(ctx, "Warp octaves:", 1, &g_warp_octaves, 8, 1, 1);
		need |= nk_property_float(ctx, "Warp offset:", 0.0f, &g_warp_offset, 2.0f, 0.01f, 0.005f);
		nk_tree_pop(ctx);
	}

	if (nk_tree_push(ctx, NK_TREE_TAB, "Color", NK_MINIMIZED)) {
		nk_layout_row_dynamic(ctx, 0, 1);
		need |= nk_property_float(ctx, "Hue:", 0.0f, &g_hue, 1.0f, 0.01f, 0.005f);
		need |= nk_property_float(ctx, "Hue amount:", 0.0f, &g_hue_amp, 1.0f, 0.01f, 0.005f);
		need |= nk_property_float(ctx, "Saturation:", 0.0f, &g_sat, 1.0f, 0.01f, 0.005f);
		need |= nk_property_float(ctx, "Value:", 0.0f, &g_val, 1.0f, 0.01f, 0.005f);
		need |= nk_property_float(ctx, "Fog:", 0.0f, &g_fog, 1.0f, 0.01f, 0.005f);
		need |= nk_property_float(ctx, "Tonemap:", 10.0f, &g_tonemap, 2000.0f, 10.0f, 1.0f);
		nk_tree_pop(ctx);
	}

	(void)is_playing;
	return need;
}

vec3 hsv(float h, float s, float v)
{
	vec4 t = vec4(1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 3.0f);
	vec3 p = abs(fract(vec3(h) + t.xyz()) * 6.0f - vec3(t.w));
	return v * mix(vec3(t.x), clamp(p - vec3(t.x), 0.0f, 1.0f), s);
}

// 2D rotation matrix
mat2 rotate2D(float a)
{
	float c = cos(a);
	float s = sin(a);

	if (g_cw_spiral) {
		// CW spiral
		return mat2(c, -s, s, c);
	} else {
		// CCW spiral
		return mat2(c, s, -s, c);
	}
}

void mainImage(vec4* fragColor, vec2 fragCoord)
{
	vec2 r = iResolution.xy();
	float tshift = iTime * g_time_speed;
	vec4 o = vec4(0.0f, 0.0f, 0.0f, 1.0f);

	// Ray direction is center-based / r.y aspect-safe; hoist out of the march.
	vec3 rd = normalize(vec3(fragCoord * 2.0f - r, r.y));

	int octaves = g_warp_octaves;
	if (octaves < 1) {
		octaves = 1;
	}

	// ---- Volumetric raymarch ---
	// changed original i++ to i+++z based on @Xor's idea to reduce artefacts
	float z = 0.0f;
	for (int i = 0; i < g_max_steps; i++) {
		if (z >= g_far_plane) {
			break;
		}

		// Sample at distance z. Forward drift on z (+t*4) + a twist that increases
		// with depth (rotate2D(q.z*.1)) -> a rotating tunnel that flies toward us.
		vec3 q = z * rd;
		q.z += tshift;
		// q.xy *= rotate2D(q.z * .1)
		vec2 qxy = q.xy() * rotate2D(q.z * g_twist);
		q.x = qxy.x;
		q.y = qxy.y;

		// Domain-warp turbulence (s = 1,2,4,...). Original tweet used s<32 (5 octaves).
		float s = 1.0f;
		for (int o = 0; o < octaves; o++) {
			q += (abs(cos(q.yzx() * s)) - g_warp_offset) / s;
			s *= 2.0f;
		}

		// Distance estimate to a tube/torus-like surface (length(q.yx)-6)
		float d = g_min_step + abs((length(q.yx()) - g_tube_radius) * q.x) / g_de_scale;
		z += d;

		// Additive glow
		o += vec4(hsv(g_hue + sin(q.x) * g_hue_amp, g_sat, g_val) * exp(-z * g_fog) / d, 0.0f);
	}

	// Tonemap: tanh compresses the accumulated HDR glow into displayable range.
	// Thanks to @Xor
	o = tanh(o / g_tonemap);

	o.w = 1.0f;
	*fragColor = o;
}

void mainFrame(pix_t* framebuffer, int w, int h)
{
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
