// GLM
//
// Port of Sabo Sugi, "Viscous Liquid - Cursor FX"
// https://codepen.io/sabosugi/pen/MYpQgmX
// Public CodePen pen (MIT).
//
// Load this file on the Main tab. Main has four channels and only its direct
// channels run, so the sim is two RGBA32F buffers plus the photo:
//   Buffer A (recursion 24): one fluid step
//   Buffer B (recursion 1): displacement, advected dye, ripple speed
//   Main: Abstract 1, seen through the liquid
//
// Both buffers use the original sim grid. The short side defaults to 256 and
// the long side is round(short * Main aspect). That size follows the window's
// shape, not its pixel count, so Half and 1080p share one fluid.
//
// A holds post-projection velocity (rg), splatted pre-advection dye (b), pressure (a).
// B holds displacement (rg), advected dye (b), length of the would-be self-advected
// velocity (a). Velocity self-advection is applied at the start of the next frame,
// with the previous frame's dt, before the splat. That is the original end-of-frame
// advection, moved across the frame boundary so it still sits between projection
// and the next splat. B recomputes that same advection for the ripple only.
//
// The brush is a button-drag. This host updates iMouse only while a button is
// held, so the original hover mode is not available.
// Curl, divergence, Jacobi, and the pressure gradient read nearest, matching
// the original curl/pressure targets. Neighbor taps sit on texel centers, where
// a linear filter would still return one texel, and nearest skips that blend
// on the Jacobi passes. Advection and the photo stay linear.

#include "glm_shader_workshop_common.h"
using namespace glm;

#include "nuklear_shader_workshop.h"

#include <cassert>
#include <cmath>
#include <cstring>

static const int VL_PRESSURE_ITERS = 20;
static const int VL_R_SPLAT = 0;
static const int VL_R_VORTICITY = 1;
static const int VL_R_PRESSURE = 2;
static const int VL_R_JACOBI0 = 3;
static const int VL_R_PROJECT = VL_R_JACOBI0 + VL_PRESSURE_ITERS;
static const int VL_RECURSION = VL_R_PROJECT + 1;
static const int VL_SIM_SHORT = 256;
static const int VL_SIM_SHORT_MIN = 64;
static const int VL_SIM_SHORT_MAX = 384;
static const float VL_DYE_VEL_SCALE = 0.35f;

SW_STATIC_ASSERT(VL_R_PROJECT == 23);
SW_STATIC_ASSERT(VL_RECURSION == 24);
SW_STATIC_ASSERT(VL_RECURSION <= MAX_PASS_RECURSION);
SW_STATIC_ASSERT(VL_SIM_SHORT >= VL_SIM_SHORT_MIN && VL_SIM_SHORT <= VL_SIM_SHORT_MAX);

static const float VL_MIN_RADIUS = 0.005f;
static const float VL_MAX_RADIUS = 0.15f;
static const float VL_MIN_FORCE = 0.0f;
static const float VL_MAX_FORCE = 4.0f;
static const float VL_MIN_FOLLOW = 2.0f;
static const float VL_MAX_FOLLOW = 60.0f;
static const float VL_MIN_DRAG = 0.0f;
static const float VL_MAX_DRAG = 1.5f;
static const float VL_MIN_RETURN = 0.05f;
static const float VL_MAX_RETURN = 6.0f;
static const float VL_MIN_FADE = 0.0f;
static const float VL_MAX_FADE = 6.0f;
static const float VL_MIN_CURL = 0.0f;
static const float VL_MAX_CURL = 80.0f;
static const float VL_MIN_PRESSURE = 0.0f;
static const float VL_MAX_PRESSURE = 1.0f;
static const float VL_MIN_REFRACTION = 0.0f;
static const float VL_MAX_REFRACTION = 4.0f;
static const float VL_MIN_CHROMATIC = 0.0f;
static const float VL_MAX_CHROMATIC = 3.0f;
static const float VL_MIN_SURFACE = 0.0f;
static const float VL_MAX_SURFACE = 4.0f;
static const float VL_MIN_RIPPLE = 0.0f;
static const float VL_MAX_RIPPLE = 4.0f;
static const float VL_MIN_SPECULAR = 0.0f;
static const float VL_MAX_SPECULAR = 4.0f;
static const float VL_MIN_SHININESS = 8.0f;
static const float VL_MAX_SHININESS = 400.0f;
static const float VL_MIN_SHADING = 0.0f;
static const float VL_MAX_SHADING = 1.0f;
static const float VL_MIN_ANGLE = 0.0f;
static const float VL_MAX_ANGLE = 360.0f;

static float g_radius = 0.056f;
static float g_force = 1.25f;
static float g_follow = 25.0f;
static float g_drag = 1.0f;
static float g_return = 1.1f;
static float g_liquid_fade = 0.9f;
static float g_velocity_fade = 1.2f;
static float g_curl = 11.0f;
static float g_pressure = 0.8f;
static float g_refraction = 2.62f;
static float g_chromatic = 0.7f;
static float g_surface = 1.0f;
static float g_ripple = 2.67f;
static float g_specular = 1.5f;
static float g_shininess = 120.0f;
static float g_shading = 0.15f;
static float g_light_angle = 125.0f;
static nk_bool g_paused = nk_false;
// Survives vl_reset_defaults. Re-initialize resets the sliders first, then
// set_pass_config reads this. Resetting it there would drop the user's choice.
static int g_sim_short = VL_SIM_SHORT;

// Frame state. Written once before a parallel loop, then only read.
static vec2 g_pointer = vec2(0.5f, 0.5f);
static vec2 g_point_a = vec2(0.5f, 0.5f);
static vec2 g_point_b = vec2(0.5f, 0.5f);
static vec2 g_splat_vel = vec2(0.0f, 0.0f);
static float g_splat_amount = 0.0f;
static float g_aspect = 1.0f;
static float g_dt = 0.0f;
static float g_dt_advect = 0.0f;
static int g_was_down = 0;
static int g_stepping = 0;
static int g_clear = 1;

static void vl_reset_defaults(void)
{
	g_radius = 0.056f;
	g_force = 1.25f;
	g_follow = 25.0f;
	g_drag = 1.0f;
	g_return = 1.1f;
	g_liquid_fade = 0.9f;
	g_velocity_fade = 1.2f;
	g_curl = 11.0f;
	g_pressure = 0.8f;
	g_refraction = 2.62f;
	g_chromatic = 0.7f;
	g_surface = 1.0f;
	g_ripple = 2.67f;
	g_specular = 1.5f;
	g_shininess = 120.0f;
	g_shading = 0.15f;
	g_light_angle = 125.0f;
	g_paused = nk_false;
	g_pointer = vec2(0.5f, 0.5f);
	g_point_a = vec2(0.5f, 0.5f);
	g_point_b = vec2(0.5f, 0.5f);
	g_splat_vel = vec2(0.0f, 0.0f);
	g_splat_amount = 0.0f;
	g_aspect = 1.0f;
	g_dt = 0.0f;
	g_dt_advect = 0.0f;
	g_was_down = 0;
	g_stepping = 0;
	g_clear = 1;
}

static void vl_bind_sim(texture_settings* ts, int slot, int buf)
{
	ts[slot].ch = buf;
	SET_DFLT_BUFFER_PARAMS(ts[slot]);
	ts[slot].filter = TS_LINEAR;
	ts[slot].wrap = TS_CLAMP;
}

static void vl_write_float_px(pix_t* fb, int w, int h, int x, int y, const vec4& c)
{
	float* last = (float*)fb + (size_t)(h - 1) * (size_t)w * 4;
	float* px = last + (size_t)(-y * w + x) * 4;
	px[0] = c.x;
	px[1] = c.y;
	px[2] = c.z;
	px[3] = c.w;
}

static void vl_write_rgba8_px(pix_t* fb, int w, int h, int x, int y, float r, float g, float b, float a)
{
	pix_t* lastrow = fb + (h - 1) * w;
	lastrow[-y * w + x] = pack_clamp_rgba8(r, g, b, a);
}

static vec4 vl_tex_clamp(const glTexture* t, vec2 uv)
{
	pgl_vec4 v = sw_texture2D_linear_clamp(t, uv.x, uv.y);
	return vec4(v.x, v.y, v.z, v.w);
}

static vec4 vl_tex_nearest(const glTexture* t, vec2 uv)
{
	pgl_vec4 v = sw_texture2D_nearest_clamp(t, uv.x, uv.y);
	return vec4(v.x, v.y, v.z, v.w);
}

static vec4 vl_tex_mirror(const glTexture* t, vec2 uv)
{
	pgl_vec4 v = sw_texture2D_linear_mirror(t, uv.x, uv.y);
	return vec4(v.x, v.y, v.z, v.w);
}

static vec2 vl_tex_rg(const glTexture* t, vec2 uv)
{
	vec4 s = vl_tex_clamp(t, uv);
	return vec2(s.x, s.y);
}

// Screen-height units. x is scaled by width/height so the brush is round.
static float vl_capsule(vec2 uv)
{
	vec2 p = uv;
	vec2 a = g_point_a;
	vec2 b = g_point_b;
	p.x *= g_aspect;
	a.x *= g_aspect;
	b.x *= g_aspect;
	vec2 pa = p - a;
	vec2 ba = b - a;
	float denom = dot(ba, ba);
	if (denom < 1.0e-8f) {
		denom = 1.0e-8f;
	}
	float h = dot(pa, ba) / denom;
	if (h < 0.0f) {
		h = 0.0f;
	} else if (h > 1.0f) {
		h = 1.0f;
	}
	vec2 d = pa - ba * h;
	float dist2 = dot(d, d);
	float rad2 = g_radius * g_radius;
	return expf(-dist2 / rad2);
}

static float vl_curl(const glTexture* t, vec2 uv, vec2 tx, vec2 ty)
{
	float L = vl_tex_nearest(t, uv - tx).y;
	float R = vl_tex_nearest(t, uv + tx).y;
	float T = vl_tex_nearest(t, uv + ty).x;
	float B = vl_tex_nearest(t, uv - ty).x;
	return 0.5f * (R - L - T + B);
}

// Free-slip walls: an out-of-range neighbor uses the reflected center component.
static float vl_divergence(const glTexture* t, vec2 uv, vec2 tx, vec2 ty)
{
	vec2 vL = uv - tx;
	vec2 vR = uv + tx;
	vec2 vT = uv + ty;
	vec2 vB = uv - ty;
	float L = vl_tex_nearest(t, vL).x;
	float R = vl_tex_nearest(t, vR).x;
	float T = vl_tex_nearest(t, vT).y;
	float B = vl_tex_nearest(t, vB).y;
	vec4 c = vl_tex_nearest(t, uv);
	if (vL.x < 0.0f) {
		L = -c.x;
	}
	if (vR.x > 1.0f) {
		R = -c.x;
	}
	if (vT.y > 1.0f) {
		T = -c.y;
	}
	if (vB.y < 0.0f) {
		B = -c.y;
	}
	return 0.5f * (R - L + T - B);
}

// Called only from mainFrame_Buffer_A, before the pixel loop, on recursion 0.
static void vl_begin_frame(int sim_w, int sim_h)
{
	if (g_clear) {
		g_pointer = vec2(0.5f, 0.5f);
		g_point_a = vec2(0.5f, 0.5f);
		g_point_b = vec2(0.5f, 0.5f);
		g_splat_vel = vec2(0.0f, 0.0f);
		g_splat_amount = 0.0f;
		g_was_down = 0;
		g_dt_advect = 0.0f;
		g_stepping = 0;
		return;
	}

	g_splat_amount = 0.0f;
	g_splat_vel = vec2(0.0f, 0.0f);
	g_stepping = (!g_paused && iTimeDelta > 0.0f) ? 1 : 0;
	if (!g_stepping) {
		return;
	}

	float dt = iTimeDelta;
	if (dt < 1.0f / 240.0f) {
		dt = 1.0f / 240.0f;
	}
	if (dt > 1.0f / 30.0f) {
		dt = 1.0f / 30.0f;
	}
	g_dt = dt;

	assert(iMainResolution.x >= 1.0f && iMainResolution.y >= 1.0f);
	float mw = iMainResolution.x;
	float mh = iMainResolution.y;
	g_aspect = mw / mh;
	vec2 target(iMouse.x / mw, iMouse.y / mh);
	int down = (iMouse.z > 0.0f) ? 1 : 0;

	// Press edge: snap so the first capsule is not a jump across the screen.
	if (down && !g_was_down) {
		g_pointer = target;
		g_point_a = target;
		g_point_b = target;
	} else if (down) {
		float k = 1.0f - expf(-dt * g_follow);
		vec2 prev = g_pointer;
		g_pointer = prev + (target - prev) * k;
		vec2 delta = g_pointer - prev;
		g_point_a = prev;
		g_point_b = g_pointer;
		if (dot(delta, delta) >= 1.0e-10f) {
			float segment = length(vec2(delta.x * g_aspect, delta.y));
			float amount = segment / (g_radius * 1.8f);
			if (amount > 1.0f) {
				amount = 1.0f;
			}
			g_splat_amount = amount;
			g_splat_vel = delta * vec2((float)sim_w, (float)sim_h) * (g_force / dt);
		}
	} else {
		float k = 1.0f - expf(-dt * g_follow);
		g_pointer = g_pointer + (target - g_pointer) * k;
	}
	g_was_down = down;
}

static vec4 vl_sim_a(vec2 uv, vec2 texel, const glTexture* self_tex, const glTexture* dye_tex)
{
	if (g_clear) {
		return vec4(0.0f);
	}
	if (!g_stepping) {
		return vl_tex_nearest(self_tex, uv);
	}

	vec2 tx = vec2(texel.x, 0.0f);
	vec2 ty = vec2(0.0f, texel.y);

	if (u_recursion == VL_R_SPLAT) {
		// Previous frame's post-projection field, advected with that frame's dt.
		vec4 self = vl_tex_clamp(self_tex, uv);
		vec2 vel = vec2(self.x, self.y);
		vec2 coord = uv - g_dt_advect * vel * texel;
		vel = vl_tex_rg(self_tex, coord) / (1.0f + g_velocity_fade * g_dt_advect);
		float dye = vl_tex_clamp(dye_tex, uv).z;
		if (g_splat_amount > 0.0f) {
			float fall = vl_capsule(uv);
			vel += g_splat_vel * fall * g_splat_amount;
			dye = dye + fall * g_splat_amount;
			if (dye > 1.0f) {
				dye = 1.0f;
			}
		}
		return vec4(vel.x, vel.y, dye, self.w);
	}

	// Later passes copy the center texel. Nearest keeps twenty Jacobi iterations
	// from blending velocity, dye, and pressure into their neighbors.
	vec4 self = vl_tex_nearest(self_tex, uv);

	if (u_recursion == VL_R_VORTICITY) {
		float L = vl_curl(self_tex, uv - tx, tx, ty);
		float R = vl_curl(self_tex, uv + tx, tx, ty);
		float T = vl_curl(self_tex, uv + ty, tx, ty);
		float B = vl_curl(self_tex, uv - ty, tx, ty);
		float C = vl_curl(self_tex, uv, tx, ty);
		vec2 force = 0.5f * vec2(fabsf(T) - fabsf(B), fabsf(R) - fabsf(L));
		force /= (length(force) + 0.0001f);
		force *= g_curl * C;
		force.y = -force.y;
		vec2 vel = vec2(self.x, self.y) + force * g_dt;
		vel = clamp(vel, -1000.0f, 1000.0f);
		return vec4(vel.x, vel.y, self.z, self.w);
	}

	if (u_recursion == VL_R_PRESSURE) {
		return vec4(self.x, self.y, self.z, self.w * g_pressure);
	}

	if (u_recursion >= VL_R_JACOBI0 && u_recursion < VL_R_PROJECT) {
		float L = vl_tex_nearest(self_tex, uv - tx).w;
		float R = vl_tex_nearest(self_tex, uv + tx).w;
		float T = vl_tex_nearest(self_tex, uv + ty).w;
		float B = vl_tex_nearest(self_tex, uv - ty).w;
		float div = vl_divergence(self_tex, uv, tx, ty);
		float pressure = (L + R + B + T - div) * 0.25f;
		return vec4(self.x, self.y, self.z, pressure);
	}

	// Gradient subtract. No 0.5: the original pressure gradient is the raw difference.
	float L = vl_tex_nearest(self_tex, uv - tx).w;
	float R = vl_tex_nearest(self_tex, uv + tx).w;
	float T = vl_tex_nearest(self_tex, uv + ty).w;
	float B = vl_tex_nearest(self_tex, uv - ty).w;
	vec2 vel = vec2(self.x, self.y) - vec2(R - L, T - B);
	return vec4(vel.x, vel.y, self.z, self.w);
}

static vec4 vl_sim_b(vec2 uv, vec2 texel, const glTexture* vel_tex, const glTexture* self_tex)
{
	if (g_clear) {
		return vec4(0.0f);
	}
	if (!g_stepping) {
		return vl_tex_nearest(self_tex, uv);
	}

	vec4 a = vl_tex_clamp(vel_tex, uv);
	vec2 vel = vec2(a.x, a.y);
	float dye_pre = a.z;
	float wet = smoothstep(0.0f, 0.2f, dye_pre);
	vec2 step = g_dt * g_drag * wet * vel * texel;
	vec2 d = vl_tex_rg(self_tex, uv - step) - step;
	float decay = expf(-g_return * g_dt);
	float decay3 = decay * decay * decay;
	d *= vec2(decay3 + (decay - decay3) * wet);

	vec2 dye_coord = uv - g_dt * VL_DYE_VEL_SCALE * vel * texel;
	float dye = vl_tex_clamp(vel_tex, dye_coord).z / (1.0f + g_liquid_fade * g_dt);

	vec2 vel_coord = uv - g_dt * vel * texel;
	vec2 adv = vl_tex_rg(vel_tex, vel_coord) / (1.0f + g_velocity_fade * g_dt);
	float speed = length(adv);
	return vec4(d.x, d.y, dye, speed);
}

static vec3 vl_light_dir(void)
{
	float a = g_light_angle * (3.14159265f / 180.0f);
	return normalize(vec3(cosf(a), sinf(a), 1.2f));
}

static vec3 vl_spectral_weight(float t)
{
	vec3 w = vec3(1.0f) - abs(vec3(t * 2.0f) - vec3(0.0f, 1.0f, 2.0f));
	return max(w, vec3(0.0f));
}

static float vl_height(const glTexture* sim, vec2 uv)
{
	vec4 s = vl_tex_clamp(sim, uv);
	float dye = s.z;
	float speed = s.w;
	return dye * 0.6f + speed * g_ripple * 0.004f * smoothstep(0.0f, 0.3f, dye);
}

static vec2 vl_cover(vec2 uv, float screen_aspect, float media_aspect)
{
	vec2 s(1.0f, 1.0f);
	float r = screen_aspect / media_aspect;
	if (r > 1.0f) {
		s.y = 1.0f / r;
	} else {
		s.x = r;
	}
	return (uv - vec2(0.5f)) * s + vec2(0.5f);
}

static vec3 vl_display(vec2 uv, float screen_aspect, float media_aspect, vec2 sim_texel, vec3 light, const glTexture* sim, const glTexture* photo)
{
	vec2 tx = vec2(sim_texel.x, 0.0f);
	vec2 ty = vec2(0.0f, sim_texel.y);
	vec2 grad = vec2(
		vl_height(sim, uv + tx) - vl_height(sim, uv - tx),
		vl_height(sim, uv + ty) - vl_height(sim, uv - ty)) * 0.5f;
	vec3 n = normalize(vec3(-grad.x * g_surface * 6.0f, -grad.y * g_surface * 6.0f, 1.0f));

	vec4 surf = vl_tex_clamp(sim, uv);
	vec2 disp = vec2(surf.x, surf.y);
	float dye = surf.z;
	float mask = smoothstep(0.0f, 0.35f, dye);

	vec2 nxy = vec2(n.x, n.y);
	vec2 base_uv = uv + disp - nxy * g_refraction * 0.05f;
	vec2 spread = (nxy * 0.035f + disp * 0.04f) * g_chromatic;

	vec3 col(0.0f);
	vec3 wsum(0.0f);
	for (int i = 0; i < 6; ++i) {
		float t = (float)i / 5.0f;
		vec2 p = base_uv + spread * (t - 0.5f);
		vec3 w = vl_spectral_weight(t);
		vec4 sample = vl_tex_mirror(photo, vl_cover(p, screen_aspect, media_aspect));
		col += vec3(sample.x, sample.y, sample.z) * w;
		wsum += w;
	}
	col /= wsum;

	vec3 L = normalize(light);
	vec3 H = normalize(L + vec3(0.0f, 0.0f, 1.0f));
	vec2 Lxy = normalize(vec2(L.x, L.y));
	float shade = 1.0f + dot(nxy, Lxy) * g_shading * mask;
	if (shade < 0.0f) {
		shade = 0.0f;
	}
	col *= shade;

	// Specular is not wet-masked. A flat normal contributes nothing at this shininess.
	float ndh = dot(n, H);
	if (ndh < 0.0f) {
		ndh = 0.0f;
	}
	float spec = powf(ndh, g_shininess);
	col += vec3(spec * g_specular);
	return col;
}

static void vl_icon_dye(pix_t* fb, int w, int h, const glTexture* tex, int channel)
{
SW_PARALLEL_FOR
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			vec2 uv;
			uv.x = ((float)x + 0.5f) / (float)w;
			uv.y = ((float)y + 0.5f) / (float)h;
			vec4 s = vl_tex_clamp(tex, uv);
			float dye = s.z;
			if (channel == 3) {
				dye = s.w;
			}
			vl_write_rgba8_px(fb, w, h, x, y, dye, dye, dye, 1.0f);
		}
	}
}

static nk_bool vl_gui(struct nk_context* ctx, nk_bool is_playing)
{
	nk_bool need = nk_false;

	nk_layout_row_dynamic(ctx, 0, 1);
	nk_label(ctx, "Viscous Liquid", NK_TEXT_CENTERED);
	nk_layout_row_dynamic(ctx, ctx->style.font->height * 2.6f, 1);
	nk_label_wrap(ctx, "Drag with a mouse button. Hover does not move the brush.");
	nk_layout_row_dynamic(ctx, ctx->style.font->height * 7.2f, 1);
	nk_label_wrap(ctx, "Sim short side is the fluid grid (original default 256). It follows the window aspect, so the liquid stays the same size on screen at every Main resolution. It applies on Re-initialize or a resolution change. The fluid clears when the grid size changes. Lower it if the sim is too slow. Pressure iterations stay at 20.");
	nk_layout_row_dynamic(ctx, 0, 1);
	need |= nk_property_int(ctx, "Sim short side:", VL_SIM_SHORT_MIN, &g_sim_short, VL_SIM_SHORT_MAX, 64, 64.0f);

	nk_layout_row_dynamic(ctx, 0, 2);
	if (nk_button_label(ctx, "Clear fluid")) {
		g_clear = 1;
		need = nk_true;
	}
	need |= nk_checkbox_label(ctx, "Paused", &g_paused);

	nk_layout_row_dynamic(ctx, 0, 1);
	need |= nk_property_float(ctx, "Radius:", VL_MIN_RADIUS, &g_radius, VL_MAX_RADIUS, 0.001f, 0.001f);
	need |= nk_property_float(ctx, "Force:", VL_MIN_FORCE, &g_force, VL_MAX_FORCE, 0.05f, 0.05f);
	need |= nk_property_float(ctx, "Follow:", VL_MIN_FOLLOW, &g_follow, VL_MAX_FOLLOW, 1.0f, 1.0f);

	if (nk_tree_push(ctx, NK_TREE_TAB, "Animation", NK_MINIMIZED)) {
		nk_layout_row_dynamic(ctx, 0, 1);
		need |= nk_property_float(ctx, "Image drag:", VL_MIN_DRAG, &g_drag, VL_MAX_DRAG, 0.01f, 0.01f);
		need |= nk_property_float(ctx, "Image return:", VL_MIN_RETURN, &g_return, VL_MAX_RETURN, 0.05f, 0.05f);
		need |= nk_property_float(ctx, "Liquid fade:", VL_MIN_FADE, &g_liquid_fade, VL_MAX_FADE, 0.05f, 0.05f);
		need |= nk_property_float(ctx, "Velocity drag:", VL_MIN_FADE, &g_velocity_fade, VL_MAX_FADE, 0.05f, 0.05f);
		need |= nk_property_float(ctx, "Swirl:", VL_MIN_CURL, &g_curl, VL_MAX_CURL, 0.5f, 0.5f);
		need |= nk_property_float(ctx, "Pressure:", VL_MIN_PRESSURE, &g_pressure, VL_MAX_PRESSURE, 0.01f, 0.01f);
		nk_tree_pop(ctx);
	}

	if (nk_tree_push(ctx, NK_TREE_TAB, "Material", NK_MINIMIZED)) {
		nk_layout_row_dynamic(ctx, 0, 1);
		need |= nk_property_float(ctx, "Refraction:", VL_MIN_REFRACTION, &g_refraction, VL_MAX_REFRACTION, 0.01f, 0.01f);
		need |= nk_property_float(ctx, "Chromatic:", VL_MIN_CHROMATIC, &g_chromatic, VL_MAX_CHROMATIC, 0.01f, 0.01f);
		need |= nk_property_float(ctx, "Surface:", VL_MIN_SURFACE, &g_surface, VL_MAX_SURFACE, 0.01f, 0.01f);
		need |= nk_property_float(ctx, "Ripple:", VL_MIN_RIPPLE, &g_ripple, VL_MAX_RIPPLE, 0.01f, 0.01f);
		need |= nk_property_float(ctx, "Specular:", VL_MIN_SPECULAR, &g_specular, VL_MAX_SPECULAR, 0.01f, 0.01f);
		need |= nk_property_float(ctx, "Shininess:", VL_MIN_SHININESS, &g_shininess, VL_MAX_SHININESS, 1.0f, 1.0f);
		nk_tree_pop(ctx);
	}

	if (nk_tree_push(ctx, NK_TREE_TAB, "Lighting", NK_MINIMIZED)) {
		nk_layout_row_dynamic(ctx, 0, 1);
		need |= nk_property_float(ctx, "Light angle:", VL_MIN_ANGLE, &g_light_angle, VL_MAX_ANGLE, 1.0f, 1.0f);
		need |= nk_property_float(ctx, "Shading:", VL_MIN_SHADING, &g_shading, VL_MAX_SHADING, 0.01f, 0.01f);
		nk_tree_pop(ctx);
	}

	(void)is_playing;
	return need;
}

// AVX2 mainFrame. A debug build does not pass -mavx2, so it keeps the scalar
// loops below. Stencil passes load the float buffer directly: a nearest tap
// at a texel center is that integer pixel, and these targets are invert_y,
// the same bottom-up layout as vl_write_float_px. Advection and the photo
// stay bilinear, using the workshop weights (epsilon 0.000001, upper texel
// at +0.499999, then (1-a)*(1-b) ...). Image edges stay on vl_sim_a so
// free-slip and clamp live in one place. Clear is a memset and a held frame
// is a memcpy; both still run every recursion, because the pass swaps after
// each one.
#if defined(__AVX2__)
#include <immintrin.h>

static const float VL_TEX_EPS = 0.000001f;

struct VlSoa {
	__m256 x, y, z, w;
};

struct VlFrac {
	__m256i i0, i1, j0, j1;
	__m256 alpha, beta;
};

static __m256 vl_abs8(__m256 v)
{
	return _mm256_and_ps(v, _mm256_castsi256_ps(_mm256_set1_epi32(0x7fffffff)));
}

static __m256 vl_neg8(__m256 v)
{
	return _mm256_xor_ps(v, _mm256_castsi256_ps(_mm256_set1_epi32((int)0x80000000u)));
}

// Lane 0 is pixel x. set_ps stores its arguments high lane first.
static __m256 vl_x8(int x)
{
	return _mm256_add_ps(_mm256_set1_ps((float)x + 0.5f),
		_mm256_set_ps(7.0f, 6.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f, 0.0f));
}

static const float* vl_src_row(const float* base, int y, int w, int h, int invert)
{
	if (invert) {
		y = h - 1 - y;
	}
	return base + ((size_t)y * (size_t)w * 4);
}

static float* vl_dst_row(float* base, int y, int w, int h)
{
	return base + ((size_t)(h - 1 - y) * (size_t)w * 4);
}

static VlSoa vl_load8(const float* p)
{
	__m256 a0 = _mm256_loadu_ps(p + 0);
	__m256 a1 = _mm256_loadu_ps(p + 8);
	__m256 a2 = _mm256_loadu_ps(p + 16);
	__m256 a3 = _mm256_loadu_ps(p + 24);
	__m256 t0 = _mm256_unpacklo_ps(a0, a1);
	__m256 t1 = _mm256_unpackhi_ps(a0, a1);
	__m256 t2 = _mm256_unpacklo_ps(a2, a3);
	__m256 t3 = _mm256_unpackhi_ps(a2, a3);
	__m256 s0 = _mm256_shuffle_ps(t0, t1, _MM_SHUFFLE(1, 0, 1, 0));
	__m256 s1 = _mm256_shuffle_ps(t0, t1, _MM_SHUFFLE(3, 2, 3, 2));
	__m256 s2 = _mm256_shuffle_ps(t2, t3, _MM_SHUFFLE(1, 0, 1, 0));
	__m256 s3 = _mm256_shuffle_ps(t2, t3, _MM_SHUFFLE(3, 2, 3, 2));
	__m256 xz_lo = _mm256_permute2f128_ps(s0, s2, 0x20);
	__m256 xz_hi = _mm256_permute2f128_ps(s0, s2, 0x31);
	__m256 yw_lo = _mm256_permute2f128_ps(s1, s3, 0x20);
	__m256 yw_hi = _mm256_permute2f128_ps(s1, s3, 0x31);
	VlSoa o;
	o.x = _mm256_unpacklo_ps(xz_lo, xz_hi);
	o.z = _mm256_unpackhi_ps(xz_lo, xz_hi);
	o.y = _mm256_unpacklo_ps(yw_lo, yw_hi);
	o.w = _mm256_unpackhi_ps(yw_lo, yw_hi);
	return o;
}

static void vl_store8(float* p, VlSoa s)
{
	alignas(32) float x[8], y[8], z[8], w[8];
	_mm256_store_ps(x, s.x);
	_mm256_store_ps(y, s.y);
	_mm256_store_ps(z, s.z);
	_mm256_store_ps(w, s.w);
	for (int i = 0; i < 8; ++i) {
		p[i * 4 + 0] = x[i];
		p[i * 4 + 1] = y[i];
		p[i * 4 + 2] = z[i];
		p[i * 4 + 3] = w[i];
	}
}

static VlFrac vl_frac8(__m256 u, __m256 v, int w, int h)
{
	__m256 xw = _mm256_mul_ps(u, _mm256_set1_ps((float)w - VL_TEX_EPS));
	__m256 yh = _mm256_mul_ps(v, _mm256_set1_ps((float)h - VL_TEX_EPS));
	__m256 half = _mm256_set1_ps(0.5f);
	VlFrac f;
	f.i0 = _mm256_cvttps_epi32(_mm256_floor_ps(_mm256_sub_ps(xw, half)));
	f.j0 = _mm256_cvttps_epi32(_mm256_floor_ps(_mm256_sub_ps(yh, half)));
	f.i1 = _mm256_cvttps_epi32(_mm256_floor_ps(_mm256_add_ps(xw, _mm256_set1_ps(0.499999f))));
	f.j1 = _mm256_cvttps_epi32(_mm256_floor_ps(_mm256_add_ps(yh, _mm256_set1_ps(0.499999f))));
	__m256 ax = _mm256_add_ps(xw, half);
	__m256 ay = _mm256_add_ps(yh, half);
	f.alpha = _mm256_sub_ps(ax, _mm256_floor_ps(ax));
	f.beta = _mm256_sub_ps(ay, _mm256_floor_ps(ay));
	return f;
}

static void vl_clamp_idx(VlFrac* f, int w, int h)
{
	__m256i z = _mm256_setzero_si256();
	__m256i wm = _mm256_set1_epi32(w - 1);
	__m256i hm = _mm256_set1_epi32(h - 1);
	f->i0 = _mm256_min_epi32(wm, _mm256_max_epi32(z, f->i0));
	f->i1 = _mm256_min_epi32(wm, _mm256_max_epi32(z, f->i1));
	f->j0 = _mm256_min_epi32(hm, _mm256_max_epi32(z, f->j0));
	f->j1 = _mm256_min_epi32(hm, _mm256_max_epi32(z, f->j1));
}

static void vl_mirror_idx(VlFrac* f, int w, int h)
{
	alignas(32) int i0[8], i1[8], j0[8], j1[8];
	_mm256_store_si256((__m256i*)i0, f->i0);
	_mm256_store_si256((__m256i*)i1, f->i1);
	_mm256_store_si256((__m256i*)j0, f->j0);
	_mm256_store_si256((__m256i*)j1, f->j1);
	for (int i = 0; i < 8; ++i) {
		i0[i] = sw_wrap_mirror(i0[i], w);
		i1[i] = sw_wrap_mirror(i1[i], w);
		j0[i] = sw_wrap_mirror(j0[i], h);
		j1[i] = sw_wrap_mirror(j1[i], h);
	}
	f->i0 = _mm256_load_si256((const __m256i*)i0);
	f->i1 = _mm256_load_si256((const __m256i*)i1);
	f->j0 = _mm256_load_si256((const __m256i*)j0);
	f->j1 = _mm256_load_si256((const __m256i*)j1);
}

static __m256i vl_pix_index(__m256i x, __m256i y, int w, int h, int invert)
{
	if (invert) {
		y = _mm256_sub_epi32(_mm256_set1_epi32(h - 1), y);
	}
	__m256i idx = _mm256_mullo_epi32(y, _mm256_set1_epi32(w));
	return _mm256_add_epi32(idx, x);
}

static VlSoa vl_gather_f(const float* base, __m256i x, __m256i y, int w, int h, int invert)
{
	__m256i idx = _mm256_slli_epi32(vl_pix_index(x, y, w, h, invert), 2);
	__m256i one = _mm256_set1_epi32(1);
	VlSoa s;
	s.x = _mm256_i32gather_ps(base, idx, 4);
	s.y = _mm256_i32gather_ps(base, _mm256_add_epi32(idx, one), 4);
	s.z = _mm256_i32gather_ps(base, _mm256_add_epi32(idx, _mm256_set1_epi32(2)), 4);
	s.w = _mm256_i32gather_ps(base, _mm256_add_epi32(idx, _mm256_set1_epi32(3)), 4);
	return s;
}

static VlSoa vl_gather_u8(const u8* base, __m256i x, __m256i y, int w, int h, int invert, int srgb)
{
	__m256i idx = vl_pix_index(x, y, w, h, invert);
	__m256i pix = _mm256_i32gather_epi32((const int*)base, idx, 4);
	__m256i mask = _mm256_set1_epi32(255);
	__m256i r = _mm256_and_si256(pix, mask);
	__m256i g = _mm256_and_si256(_mm256_srli_epi32(pix, 8), mask);
	__m256i b = _mm256_and_si256(_mm256_srli_epi32(pix, 16), mask);
	__m256i a = _mm256_srli_epi32(pix, 24);
	__m256 den = _mm256_set1_ps(255.0f);
	VlSoa s;
	if (!srgb) {
		s.x = _mm256_div_ps(_mm256_cvtepi32_ps(r), den);
		s.y = _mm256_div_ps(_mm256_cvtepi32_ps(g), den);
		s.z = _mm256_div_ps(_mm256_cvtepi32_ps(b), den);
	} else {
		alignas(32) int rr[8], gg[8], bb[8];
		alignas(32) float fx[8], fy[8], fz[8];
		_mm256_store_si256((__m256i*)rr, r);
		_mm256_store_si256((__m256i*)gg, g);
		_mm256_store_si256((__m256i*)bb, b);
		for (int i = 0; i < 8; ++i) {
			fx[i] = sw_srgb_decode_u8[rr[i]];
			fy[i] = sw_srgb_decode_u8[gg[i]];
			fz[i] = sw_srgb_decode_u8[bb[i]];
		}
		s.x = _mm256_load_ps(fx);
		s.y = _mm256_load_ps(fy);
		s.z = _mm256_load_ps(fz);
	}
	s.w = _mm256_div_ps(_mm256_cvtepi32_ps(a), den);
	return s;
}

static __m256 vl_mix4(__m256 c00, __m256 c10, __m256 c01, __m256 c11,
	__m256 w00, __m256 w10, __m256 w01, __m256 w11)
{
	__m256 r = _mm256_mul_ps(c00, w00);
	r = _mm256_add_ps(r, _mm256_mul_ps(c10, w10));
	r = _mm256_add_ps(r, _mm256_mul_ps(c01, w01));
	r = _mm256_add_ps(r, _mm256_mul_ps(c11, w11));
	return r;
}

static VlSoa vl_blend4(VlSoa c00, VlSoa c10, VlSoa c01, VlSoa c11, __m256 alpha, __m256 beta)
{
	__m256 one = _mm256_set1_ps(1.0f);
	__m256 wa = _mm256_sub_ps(one, alpha);
	__m256 wb = _mm256_sub_ps(one, beta);
	__m256 w00 = _mm256_mul_ps(wa, wb);
	__m256 w10 = _mm256_mul_ps(alpha, wb);
	__m256 w01 = _mm256_mul_ps(wa, beta);
	__m256 w11 = _mm256_mul_ps(alpha, beta);
	VlSoa o;
	o.x = vl_mix4(c00.x, c10.x, c01.x, c11.x, w00, w10, w01, w11);
	o.y = vl_mix4(c00.y, c10.y, c01.y, c11.y, w00, w10, w01, w11);
	o.z = vl_mix4(c00.z, c10.z, c01.z, c11.z, w00, w10, w01, w11);
	o.w = vl_mix4(c00.w, c10.w, c01.w, c11.w, w00, w10, w01, w11);
	return o;
}

static VlSoa vl_bilerp_clamp_f(const float* base, __m256 u, __m256 v, int w, int h, int invert)
{
	VlFrac f = vl_frac8(u, v, w, h);
	vl_clamp_idx(&f, w, h);
	VlSoa c00 = vl_gather_f(base, f.i0, f.j0, w, h, invert);
	VlSoa c10 = vl_gather_f(base, f.i1, f.j0, w, h, invert);
	VlSoa c01 = vl_gather_f(base, f.i0, f.j1, w, h, invert);
	VlSoa c11 = vl_gather_f(base, f.i1, f.j1, w, h, invert);
	return vl_blend4(c00, c10, c01, c11, f.alpha, f.beta);
}

static VlSoa vl_bilerp_mirror(const glTexture* tex, __m256 u, __m256 v)
{
	int w = (int)tex->w;
	int h = (int)tex->h;
	int invert = tex->invert_y ? 1 : 0;
	VlFrac f = vl_frac8(u, v, w, h);
	vl_mirror_idx(&f, w, h);
	VlSoa c00, c10, c01, c11;
	if (tex->datatype == GL_FLOAT) {
		const float* base = (const float*)tex->data;
		c00 = vl_gather_f(base, f.i0, f.j0, w, h, invert);
		c10 = vl_gather_f(base, f.i1, f.j0, w, h, invert);
		c01 = vl_gather_f(base, f.i0, f.j1, w, h, invert);
		c11 = vl_gather_f(base, f.i1, f.j1, w, h, invert);
	} else {
		int srgb = tex->is_srgb ? 1 : 0;
		c00 = vl_gather_u8(tex->data, f.i0, f.j0, w, h, invert, srgb);
		c10 = vl_gather_u8(tex->data, f.i1, f.j0, w, h, invert, srgb);
		c01 = vl_gather_u8(tex->data, f.i0, f.j1, w, h, invert, srgb);
		c11 = vl_gather_u8(tex->data, f.i1, f.j1, w, h, invert, srgb);
	}
	return vl_blend4(c00, c10, c01, c11, f.alpha, f.beta);
}

struct VlNb {
	VlSoa L, R, T, B, C;
};

static VlNb vl_load_nb(const float* src, int x, int y, int w, int h, int invert)
{
	const float* row = vl_src_row(src, y, w, h, invert);
	VlNb n;
	n.C = vl_load8(row + x * 4);
	n.L = vl_load8(row + (x - 1) * 4);
	n.R = vl_load8(row + (x + 1) * 4);
	n.T = vl_load8(vl_src_row(src, y + 1, w, h, invert) + x * 4);
	n.B = vl_load8(vl_src_row(src, y - 1, w, h, invert) + x * 4);
	return n;
}

static void vl_scalar_a(pix_t* fb, int w, int h, int x, int y, const glTexture* self_tex, const glTexture* dye_tex)
{
	vec2 uv;
	uv.x = ((float)x + 0.5f) / (float)w;
	uv.y = ((float)y + 0.5f) / (float)h;
	vec2 texel(1.0f / (float)w, 1.0f / (float)h);
	vl_write_float_px(fb, w, h, x, y, vl_sim_a(uv, texel, self_tex, dye_tex));
}

static void vl_scalar_b(pix_t* fb, int w, int h, int x, int y, const glTexture* vel_tex, const glTexture* self_tex)
{
	vec2 uv;
	uv.x = ((float)x + 0.5f) / (float)w;
	uv.y = ((float)y + 0.5f) / (float)h;
	vec2 texel(1.0f / (float)w, 1.0f / (float)h);
	vl_write_float_px(fb, w, h, x, y, vl_sim_b(uv, texel, vel_tex, self_tex));
}

// Interior only. x >= 1, x + 8 <= w - 1, and y is off the top and bottom rows.
static void vl_jacobi8(const float* src, float* dst, int x, int y, int w, int h, int invert)
{
	VlNb n = vl_load_nb(src, x, y, w, h, invert);
	__m256 div = _mm256_sub_ps(n.R.x, n.L.x);
	div = _mm256_add_ps(div, n.T.y);
	div = _mm256_sub_ps(div, n.B.y);
	div = _mm256_mul_ps(div, _mm256_set1_ps(0.5f));
	__m256 p = _mm256_add_ps(n.L.w, n.R.w);
	p = _mm256_add_ps(p, n.B.w);
	p = _mm256_add_ps(p, n.T.w);
	p = _mm256_sub_ps(p, div);
	p = _mm256_mul_ps(p, _mm256_set1_ps(0.25f));
	float* o = vl_dst_row(dst, y, w, h);
	const float* row = vl_src_row(src, y, w, h, invert);
	memcpy(o + x * 4, row + x * 4, 8 * 4 * sizeof(float));
	alignas(32) float pw[8];
	_mm256_store_ps(pw, p);
	for (int i = 0; i < 8; ++i) {
		o[(x + i) * 4 + 3] = pw[i];
	}
}

static void vl_project8(const float* src, float* dst, int x, int y, int w, int h, int invert)
{
	VlNb n = vl_load_nb(src, x, y, w, h, invert);
	VlSoa o;
	o.x = _mm256_sub_ps(n.C.x, _mm256_sub_ps(n.R.w, n.L.w));
	o.y = _mm256_sub_ps(n.C.y, _mm256_sub_ps(n.T.w, n.B.w));
	o.z = n.C.z;
	o.w = n.C.w;
	vl_store8(vl_dst_row(dst, y, w, h) + x * 4, o);
}

// Curl at 8 pixels whose own neighbors are all in range.
static __m256 vl_curl8(const float* src, int p, int y, int w, int h, int invert)
{
	VlSoa L = vl_load8(vl_src_row(src, y, w, h, invert) + (p - 1) * 4);
	VlSoa R = vl_load8(vl_src_row(src, y, w, h, invert) + (p + 1) * 4);
	VlSoa T = vl_load8(vl_src_row(src, y + 1, w, h, invert) + p * 4);
	VlSoa B = vl_load8(vl_src_row(src, y - 1, w, h, invert) + p * 4);
	__m256 c = _mm256_sub_ps(R.y, L.y);
	c = _mm256_sub_ps(c, T.x);
	c = _mm256_add_ps(c, B.x);
	return _mm256_mul_ps(c, _mm256_set1_ps(0.5f));
}

static void vl_vorticity8(const float* src, float* dst, int x, int y, int w, int h, int invert)
{
	__m256 Lc = vl_curl8(src, x - 1, y, w, h, invert);
	__m256 Rc = vl_curl8(src, x + 1, y, w, h, invert);
	__m256 Tc = vl_curl8(src, x, y + 1, w, h, invert);
	__m256 Bc = vl_curl8(src, x, y - 1, w, h, invert);
	__m256 Cc = vl_curl8(src, x, y, w, h, invert);
	VlSoa self = vl_load8(vl_src_row(src, y, w, h, invert) + x * 4);
	__m256 half = _mm256_set1_ps(0.5f);
	__m256 fx = _mm256_mul_ps(half, _mm256_sub_ps(vl_abs8(Tc), vl_abs8(Bc)));
	__m256 fy = _mm256_mul_ps(half, _mm256_sub_ps(vl_abs8(Rc), vl_abs8(Lc)));
	__m256 len = _mm256_sqrt_ps(_mm256_add_ps(_mm256_mul_ps(fx, fx), _mm256_mul_ps(fy, fy)));
	__m256 denom = _mm256_add_ps(len, _mm256_set1_ps(0.0001f));
	fx = _mm256_div_ps(fx, denom);
	fy = _mm256_div_ps(fy, denom);
	__m256 scale = _mm256_mul_ps(_mm256_set1_ps(g_curl), Cc);
	fx = _mm256_mul_ps(fx, scale);
	fy = _mm256_mul_ps(fy, scale);
	fy = vl_neg8(fy);
	__m256 dt = _mm256_set1_ps(g_dt);
	__m256 vx = _mm256_add_ps(self.x, _mm256_mul_ps(fx, dt));
	__m256 vy = _mm256_add_ps(self.y, _mm256_mul_ps(fy, dt));
	__m256 lo = _mm256_set1_ps(-1000.0f);
	__m256 hi = _mm256_set1_ps(1000.0f);
	VlSoa o;
	o.x = _mm256_min_ps(hi, _mm256_max_ps(lo, vx));
	o.y = _mm256_min_ps(hi, _mm256_max_ps(lo, vy));
	o.z = self.z;
	o.w = self.w;
	vl_store8(vl_dst_row(dst, y, w, h) + x * 4, o);
}

// One row mixes a SIMD run with scalar edges, so this is not SW_PARALLEL_FOR.
static void vl_rows_stencil(pix_t* fb, int w, int h, const float* src, int invert,
	const glTexture* self_tex, const glTexture* dye_tex, int vorticity)
{
	float* dst = (float*)fb;
	#pragma omp parallel for schedule(runtime) num_threads(num_shader_threads)
	for (int y = 0; y < h; ++y) {
		int x = 0;
		int row_ok = 0;
		int prefix = 1;
		int guard = 8;
		if (vorticity) {
			row_ok = (y >= 2 && y <= h - 3);
			prefix = 2;
			guard = 9;
		} else {
			row_ok = (y > 0 && y < h - 1);
		}
		if (row_ok) {
			for (; x < prefix && x < w; ++x) {
				vl_scalar_a(fb, w, h, x, y, self_tex, dye_tex);
			}
			for (; x + guard <= w - 1; x += 8) {
				if (vorticity) {
					vl_vorticity8(src, dst, x, y, w, h, invert);
				} else if (u_recursion == VL_R_PROJECT) {
					vl_project8(src, dst, x, y, w, h, invert);
				} else {
					vl_jacobi8(src, dst, x, y, w, h, invert);
				}
			}
		}
		for (; x < w; ++x) {
			vl_scalar_a(fb, w, h, x, y, self_tex, dye_tex);
		}
	}
}

static void vl_simd_pressure(const float* src, float* dst, int w, int h, int invert, float scale)
{
	__m256 k = _mm256_setr_ps(1.0f, 1.0f, 1.0f, scale, 1.0f, 1.0f, 1.0f, scale);
	#pragma omp parallel for schedule(runtime) num_threads(num_shader_threads)
	for (int y = 0; y < h; ++y) {
		const float* srow = vl_src_row(src, y, w, h, invert);
		float* drow = vl_dst_row(dst, y, w, h);
		int x = 0;
		for (; x + 2 <= w; x += 2) {
			__m256 v = _mm256_loadu_ps(srow + x * 4);
			_mm256_storeu_ps(drow + x * 4, _mm256_mul_ps(v, k));
		}
		if (x < w) {
			drow[x * 4 + 0] = srow[x * 4 + 0];
			drow[x * 4 + 1] = srow[x * 4 + 1];
			drow[x * 4 + 2] = srow[x * 4 + 2];
			drow[x * 4 + 3] = srow[x * 4 + 3] * scale;
		}
	}
}

static __m256 vl_capsule8(__m256 uvx, __m256 uvy)
{
	float ax = g_point_a.x * g_aspect;
	float ay = g_point_a.y;
	float bx = g_point_b.x * g_aspect;
	float by = g_point_b.y;
	float bax = bx - ax;
	float bay = by - ay;
	float denom = bax * bax + bay * bay;
	if (denom < 1.0e-8f) {
		denom = 1.0e-8f;
	}
	__m256 pax = _mm256_sub_ps(_mm256_mul_ps(uvx, _mm256_set1_ps(g_aspect)), _mm256_set1_ps(ax));
	__m256 pay = _mm256_sub_ps(uvy, _mm256_set1_ps(ay));
	__m256 h = _mm256_add_ps(_mm256_mul_ps(pax, _mm256_set1_ps(bax)), _mm256_mul_ps(pay, _mm256_set1_ps(bay)));
	h = _mm256_div_ps(h, _mm256_set1_ps(denom));
	h = _mm256_min_ps(_mm256_set1_ps(1.0f), _mm256_max_ps(_mm256_setzero_ps(), h));
	__m256 dx = _mm256_sub_ps(pax, _mm256_mul_ps(_mm256_set1_ps(bax), h));
	__m256 dy = _mm256_sub_ps(pay, _mm256_mul_ps(_mm256_set1_ps(bay), h));
	__m256 dist2 = _mm256_add_ps(_mm256_mul_ps(dx, dx), _mm256_mul_ps(dy, dy));
	float rad2 = g_radius * g_radius;
	alignas(32) float d[8], e[8];
	_mm256_store_ps(d, dist2);
	for (int i = 0; i < 8; ++i) {
		e[i] = expf(-d[i] / rad2);
	}
	return _mm256_load_ps(e);
}

static void vl_simd_splat(pix_t* fb, int w, int h, const float* self, int inv_s,
	const float* dye, int inv_d, const glTexture* self_tex, const glTexture* dye_tex)
{
	float* dst = (float*)fb;
	float inv_w = 1.0f / (float)w;
	float inv_h = 1.0f / (float)h;
	float denom = 1.0f + g_velocity_fade * g_dt_advect;
	float amount = g_splat_amount;
	float svx = g_splat_vel.x;
	float svy = g_splat_vel.y;
	float dt = g_dt_advect;
	#pragma omp parallel for schedule(runtime) num_threads(num_shader_threads)
	for (int y = 0; y < h; ++y) {
		__m256 uvy = _mm256_set1_ps(((float)y + 0.5f) * inv_h);
		int x = 0;
		for (; x + 8 <= w; x += 8) {
			__m256 uvx = _mm256_mul_ps(vl_x8(x), _mm256_set1_ps(inv_w));
			VlSoa center = vl_bilerp_clamp_f(self, uvx, uvy, w, h, inv_s);
			__m256 cx = _mm256_mul_ps(_mm256_set1_ps(dt), center.x);
			cx = _mm256_mul_ps(cx, _mm256_set1_ps(inv_w));
			cx = _mm256_sub_ps(uvx, cx);
			__m256 cy = _mm256_mul_ps(_mm256_set1_ps(dt), center.y);
			cy = _mm256_mul_ps(cy, _mm256_set1_ps(inv_h));
			cy = _mm256_sub_ps(uvy, cy);
			VlSoa adv = vl_bilerp_clamp_f(self, cx, cy, w, h, inv_s);
			__m256 vx = _mm256_div_ps(adv.x, _mm256_set1_ps(denom));
			__m256 vy = _mm256_div_ps(adv.y, _mm256_set1_ps(denom));
			VlSoa dye_s = vl_bilerp_clamp_f(dye, uvx, uvy, w, h, inv_d);
			__m256 dye_v = dye_s.z;
			if (amount > 0.0f) {
				__m256 fall = vl_capsule8(uvx, uvy);
				__m256 kick = _mm256_mul_ps(fall, _mm256_set1_ps(amount));
				vx = _mm256_add_ps(vx, _mm256_mul_ps(_mm256_set1_ps(svx), kick));
				vy = _mm256_add_ps(vy, _mm256_mul_ps(_mm256_set1_ps(svy), kick));
				dye_v = _mm256_min_ps(_mm256_add_ps(dye_v, kick), _mm256_set1_ps(1.0f));
			}
			VlSoa o;
			o.x = vx;
			o.y = vy;
			o.z = dye_v;
			o.w = center.w;
			vl_store8(vl_dst_row(dst, y, w, h) + x * 4, o);
		}
		for (; x < w; ++x) {
			vl_scalar_a(fb, w, h, x, y, self_tex, dye_tex);
		}
	}
}

static __m256 vl_smoothstep8(float e0, float e1, __m256 x)
{
	__m256 t = _mm256_sub_ps(x, _mm256_set1_ps(e0));
	t = _mm256_div_ps(t, _mm256_set1_ps(e1 - e0));
	t = _mm256_min_ps(_mm256_set1_ps(1.0f), _mm256_max_ps(_mm256_setzero_ps(), t));
	__m256 poly = _mm256_sub_ps(_mm256_set1_ps(3.0f), _mm256_mul_ps(_mm256_set1_ps(2.0f), t));
	return _mm256_mul_ps(_mm256_mul_ps(t, t), poly);
}

static void vl_simd_b(pix_t* fb, int w, int h, const float* vel, int inv_v,
	const float* self, int inv_s, const glTexture* vel_tex, const glTexture* self_tex)
{
	float* dst = (float*)fb;
	float inv_w = 1.0f / (float)w;
	float inv_h = 1.0f / (float)h;
	float decay = expf(-g_return * g_dt);
	float decay3 = decay * decay * decay;
	float decay_span = decay - decay3;
	float dye_den = 1.0f + g_liquid_fade * g_dt;
	float vel_den = 1.0f + g_velocity_fade * g_dt;
	float dt_drag = g_dt * g_drag;
	float dt_dye = g_dt * VL_DYE_VEL_SCALE;
	float dt = g_dt;
	#pragma omp parallel for schedule(runtime) num_threads(num_shader_threads)
	for (int y = 0; y < h; ++y) {
		__m256 uvy = _mm256_set1_ps(((float)y + 0.5f) * inv_h);
		int x = 0;
		for (; x + 8 <= w; x += 8) {
			__m256 uvx = _mm256_mul_ps(vl_x8(x), _mm256_set1_ps(inv_w));
			VlSoa a = vl_bilerp_clamp_f(vel, uvx, uvy, w, h, inv_v);
			__m256 wet = vl_smoothstep8(0.0f, 0.2f, a.z);
			__m256 sx = _mm256_mul_ps(_mm256_set1_ps(dt_drag), wet);
			sx = _mm256_mul_ps(sx, a.x);
			sx = _mm256_mul_ps(sx, _mm256_set1_ps(inv_w));
			__m256 sy = _mm256_mul_ps(_mm256_set1_ps(dt_drag), wet);
			sy = _mm256_mul_ps(sy, a.y);
			sy = _mm256_mul_ps(sy, _mm256_set1_ps(inv_h));
			VlSoa disp = vl_bilerp_clamp_f(self, _mm256_sub_ps(uvx, sx), _mm256_sub_ps(uvy, sy), w, h, inv_s);
			__m256 k = _mm256_add_ps(_mm256_set1_ps(decay3), _mm256_mul_ps(_mm256_set1_ps(decay_span), wet));
			__m256 dx = _mm256_mul_ps(_mm256_sub_ps(disp.x, sx), k);
			__m256 dy = _mm256_mul_ps(_mm256_sub_ps(disp.y, sy), k);

			__m256 ox = _mm256_mul_ps(_mm256_set1_ps(dt_dye), a.x);
			ox = _mm256_mul_ps(ox, _mm256_set1_ps(inv_w));
			__m256 oy = _mm256_mul_ps(_mm256_set1_ps(dt_dye), a.y);
			oy = _mm256_mul_ps(oy, _mm256_set1_ps(inv_h));
			VlSoa dye_s = vl_bilerp_clamp_f(vel, _mm256_sub_ps(uvx, ox), _mm256_sub_ps(uvy, oy), w, h, inv_v);
			__m256 dye_v = _mm256_div_ps(dye_s.z, _mm256_set1_ps(dye_den));

			__m256 vx = _mm256_mul_ps(_mm256_set1_ps(dt), a.x);
			vx = _mm256_mul_ps(vx, _mm256_set1_ps(inv_w));
			__m256 vy = _mm256_mul_ps(_mm256_set1_ps(dt), a.y);
			vy = _mm256_mul_ps(vy, _mm256_set1_ps(inv_h));
			VlSoa adv = vl_bilerp_clamp_f(vel, _mm256_sub_ps(uvx, vx), _mm256_sub_ps(uvy, vy), w, h, inv_v);
			__m256 ax = _mm256_div_ps(adv.x, _mm256_set1_ps(vel_den));
			__m256 ay = _mm256_div_ps(adv.y, _mm256_set1_ps(vel_den));
			__m256 speed = _mm256_sqrt_ps(_mm256_add_ps(_mm256_mul_ps(ax, ax), _mm256_mul_ps(ay, ay)));
			VlSoa o;
			o.x = dx;
			o.y = dy;
			o.z = dye_v;
			o.w = speed;
			vl_store8(vl_dst_row(dst, y, w, h) + x * 4, o);
		}
		for (; x < w; ++x) {
			vl_scalar_b(fb, w, h, x, y, vel_tex, self_tex);
		}
	}
}

static __m256 vl_height8(VlSoa s, float ripple)
{
	__m256 sm = vl_smoothstep8(0.0f, 0.3f, s.z);
	__m256 rip = _mm256_mul_ps(s.w, _mm256_set1_ps(ripple));
	rip = _mm256_mul_ps(rip, _mm256_set1_ps(0.004f));
	rip = _mm256_mul_ps(rip, sm);
	return _mm256_add_ps(_mm256_mul_ps(s.z, _mm256_set1_ps(0.6f)), rip);
}

static void vl_norm3(__m256* x, __m256* y, __m256* z)
{
	__m256 d = _mm256_add_ps(_mm256_mul_ps(*x, *x), _mm256_mul_ps(*y, *y));
	d = _mm256_add_ps(d, _mm256_mul_ps(*z, *z));
	__m256 inv = _mm256_div_ps(_mm256_set1_ps(1.0f), _mm256_sqrt_ps(d));
	*x = _mm256_mul_ps(*x, inv);
	*y = _mm256_mul_ps(*y, inv);
	*z = _mm256_mul_ps(*z, inv);
}

static void vl_simd_main(pix_t* fb, int w, int h, const glTexture* sim, const glTexture* photo)
{
	assert(sim->data && photo->data);
	assert(sim->datatype == GL_FLOAT);
	assert(sim->components == 4 && photo->components == 4);
	assert(photo->datatype == GL_FLOAT || photo->datatype == GL_UNSIGNED_BYTE);

	const float* sim_f = (const float*)sim->data;
	int sw = (int)sim->w;
	int sh = (int)sim->h;
	int sinv = sim->invert_y ? 1 : 0;
	float inv_sw = 1.0f / (float)sw;
	float inv_sh = 1.0f / (float)sh;
	float screen_aspect = (float)w / (float)h;
	float media_aspect = (float)photo->w / (float)photo->h;
	vec2 sim_texel(inv_sw, inv_sh);
	vec3 light = vl_light_dir();
	vec3 L = normalize(light);
	vec3 Hn = normalize(L + vec3(0.0f, 0.0f, 1.0f));
	vec2 Lxy = normalize(vec2(L.x, L.y));
	float cover_r = screen_aspect / media_aspect;
	float cover_sx = 1.0f;
	float cover_sy = 1.0f;
	if (cover_r > 1.0f) {
		cover_sy = 1.0f / cover_r;
	} else {
		cover_sx = cover_r;
	}
	vec3 tap_w[6];
	vec3 wsum(0.0f);
	for (int i = 0; i < 6; ++i) {
		tap_w[i] = vl_spectral_weight((float)i / 5.0f);
		wsum += tap_w[i];
	}
	float ripple = g_ripple;
	float surface = g_surface;
	float refraction = g_refraction;
	float chromatic = g_chromatic;
	float shading = g_shading;
	float shininess = g_shininess;
	float specular = g_specular;
	pix_t* lastrow = fb + (h - 1) * w;

	#pragma omp parallel for schedule(runtime) num_threads(num_shader_threads)
	for (int y = 0; y < h; ++y) {
		__m256 uvy = _mm256_set1_ps(((float)y + 0.5f) / (float)h);
		__m256 half = _mm256_set1_ps(0.5f);
		int x = 0;
		for (; x + 8 <= w; x += 8) {
			__m256 uvx = _mm256_div_ps(vl_x8(x), _mm256_set1_ps((float)w));
			__m256 tx = _mm256_set1_ps(inv_sw);
			__m256 ty = _mm256_set1_ps(inv_sh);
			__m256 hL = vl_height8(vl_bilerp_clamp_f(sim_f, _mm256_sub_ps(uvx, tx), uvy, sw, sh, sinv), ripple);
			__m256 hR = vl_height8(vl_bilerp_clamp_f(sim_f, _mm256_add_ps(uvx, tx), uvy, sw, sh, sinv), ripple);
			__m256 hB = vl_height8(vl_bilerp_clamp_f(sim_f, uvx, _mm256_sub_ps(uvy, ty), sw, sh, sinv), ripple);
			__m256 hT = vl_height8(vl_bilerp_clamp_f(sim_f, uvx, _mm256_add_ps(uvy, ty), sw, sh, sinv), ripple);
			__m256 gx = _mm256_mul_ps(_mm256_sub_ps(hR, hL), half);
			__m256 gy = _mm256_mul_ps(_mm256_sub_ps(hT, hB), half);
			__m256 nx = _mm256_sub_ps(_mm256_setzero_ps(), gx);
			nx = _mm256_mul_ps(nx, _mm256_set1_ps(surface));
			nx = _mm256_mul_ps(nx, _mm256_set1_ps(6.0f));
			__m256 ny = _mm256_sub_ps(_mm256_setzero_ps(), gy);
			ny = _mm256_mul_ps(ny, _mm256_set1_ps(surface));
			ny = _mm256_mul_ps(ny, _mm256_set1_ps(6.0f));
			__m256 nz = _mm256_set1_ps(1.0f);
			vl_norm3(&nx, &ny, &nz);

			VlSoa surf = vl_bilerp_clamp_f(sim_f, uvx, uvy, sw, sh, sinv);
			__m256 mask = vl_smoothstep8(0.0f, 0.35f, surf.z);
			__m256 bu = _mm256_add_ps(uvx, surf.x);
			__m256 bx = _mm256_mul_ps(nx, _mm256_set1_ps(refraction));
			bx = _mm256_mul_ps(bx, _mm256_set1_ps(0.05f));
			bu = _mm256_sub_ps(bu, bx);
			__m256 bv = _mm256_add_ps(uvy, surf.y);
			__m256 by = _mm256_mul_ps(ny, _mm256_set1_ps(refraction));
			by = _mm256_mul_ps(by, _mm256_set1_ps(0.05f));
			bv = _mm256_sub_ps(bv, by);
			__m256 spu = _mm256_add_ps(_mm256_mul_ps(nx, _mm256_set1_ps(0.035f)), _mm256_mul_ps(surf.x, _mm256_set1_ps(0.04f)));
			__m256 spv = _mm256_add_ps(_mm256_mul_ps(ny, _mm256_set1_ps(0.035f)), _mm256_mul_ps(surf.y, _mm256_set1_ps(0.04f)));
			spu = _mm256_mul_ps(spu, _mm256_set1_ps(chromatic));
			spv = _mm256_mul_ps(spv, _mm256_set1_ps(chromatic));

			__m256 cr = _mm256_setzero_ps();
			__m256 cg = _mm256_setzero_ps();
			__m256 cb = _mm256_setzero_ps();
			for (int i = 0; i < 6; ++i) {
				float kt = (float)i / 5.0f - 0.5f;
				__m256 pu = _mm256_add_ps(bu, _mm256_mul_ps(spu, _mm256_set1_ps(kt)));
				__m256 pv = _mm256_add_ps(bv, _mm256_mul_ps(spv, _mm256_set1_ps(kt)));
				__m256 cu = _mm256_add_ps(_mm256_mul_ps(_mm256_sub_ps(pu, half), _mm256_set1_ps(cover_sx)), half);
				__m256 cv = _mm256_add_ps(_mm256_mul_ps(_mm256_sub_ps(pv, half), _mm256_set1_ps(cover_sy)), half);
				VlSoa sample = vl_bilerp_mirror(photo, cu, cv);
				cr = _mm256_add_ps(cr, _mm256_mul_ps(sample.x, _mm256_set1_ps(tap_w[i].x)));
				cg = _mm256_add_ps(cg, _mm256_mul_ps(sample.y, _mm256_set1_ps(tap_w[i].y)));
				cb = _mm256_add_ps(cb, _mm256_mul_ps(sample.z, _mm256_set1_ps(tap_w[i].z)));
			}
			cr = _mm256_div_ps(cr, _mm256_set1_ps(wsum.x));
			cg = _mm256_div_ps(cg, _mm256_set1_ps(wsum.y));
			cb = _mm256_div_ps(cb, _mm256_set1_ps(wsum.z));

			__m256 dotxy = _mm256_add_ps(_mm256_mul_ps(nx, _mm256_set1_ps(Lxy.x)), _mm256_mul_ps(ny, _mm256_set1_ps(Lxy.y)));
			__m256 shade = _mm256_mul_ps(dotxy, _mm256_set1_ps(shading));
			shade = _mm256_mul_ps(shade, mask);
			shade = _mm256_add_ps(_mm256_set1_ps(1.0f), shade);
			shade = _mm256_max_ps(shade, _mm256_setzero_ps());
			cr = _mm256_mul_ps(cr, shade);
			cg = _mm256_mul_ps(cg, shade);
			cb = _mm256_mul_ps(cb, shade);

			__m256 ndh = _mm256_mul_ps(nx, _mm256_set1_ps(Hn.x));
			ndh = _mm256_add_ps(ndh, _mm256_mul_ps(ny, _mm256_set1_ps(Hn.y)));
			ndh = _mm256_add_ps(ndh, _mm256_mul_ps(nz, _mm256_set1_ps(Hn.z)));
			ndh = _mm256_max_ps(ndh, _mm256_setzero_ps());
			alignas(32) float ndhs[8], specs[8], rs[8], gs[8], bs[8];
			_mm256_store_ps(ndhs, ndh);
			_mm256_store_ps(rs, cr);
			_mm256_store_ps(gs, cg);
			_mm256_store_ps(bs, cb);
			for (int i = 0; i < 8; ++i) {
				specs[i] = powf(ndhs[i], shininess) * specular;
				lastrow[-y * w + x + i] = pack_clamp_rgba8(rs[i] + specs[i], gs[i] + specs[i], bs[i] + specs[i], 1.0f);
			}
		}
		for (; x < w; ++x) {
			vec2 uv;
			uv.x = ((float)x + 0.5f) / (float)w;
			uv.y = ((float)y + 0.5f) / (float)h;
			vec3 col = vl_display(uv, screen_aspect, media_aspect, sim_texel, light, sim, photo);
			lastrow[-y * w + x] = pack_clamp_rgba8(col.x, col.y, col.z, 1.0f);
		}
	}
}

static void vl_simd_buffer_a(pix_t* fb, int w, int h, const glTexture* self_tex, const glTexture* dye_tex)
{
	assert(self_tex->data && dye_tex->data);
	assert(self_tex->datatype == GL_FLOAT && dye_tex->datatype == GL_FLOAT);
	assert(self_tex->components == 4 && dye_tex->components == 4);
	assert(self_tex->invert_y && dye_tex->invert_y);
	assert((u8*)fb != self_tex->data);

	float* dst = (float*)fb;
	size_t nbytes = (size_t)w * (size_t)h * 4 * sizeof(float);
	if (g_clear) {
		memset(dst, 0, nbytes);
		return;
	}
	const float* self = (const float*)self_tex->data;
	if (!g_stepping) {
		memcpy(dst, self, nbytes);
		return;
	}
	int inv_s = self_tex->invert_y ? 1 : 0;
	int inv_d = dye_tex->invert_y ? 1 : 0;
	int rec = u_recursion;
	if (rec == VL_R_PRESSURE) {
		vl_simd_pressure(self, dst, w, h, inv_s, g_pressure);
		return;
	}
	if (rec == VL_R_SPLAT) {
		vl_simd_splat(fb, w, h, self, inv_s, (const float*)dye_tex->data, inv_d, self_tex, dye_tex);
		return;
	}
	vl_rows_stencil(fb, w, h, self, inv_s, self_tex, dye_tex, rec == VL_R_VORTICITY);
}

static void vl_simd_buffer_b(pix_t* fb, int w, int h, const glTexture* vel_tex, const glTexture* self_tex)
{
	assert(vel_tex->data && self_tex->data);
	assert(vel_tex->datatype == GL_FLOAT && self_tex->datatype == GL_FLOAT);
	assert(vel_tex->components == 4 && self_tex->components == 4);
	assert(vel_tex->invert_y && self_tex->invert_y);
	assert((u8*)fb != self_tex->data);

	float* dst = (float*)fb;
	size_t nbytes = (size_t)w * (size_t)h * 4 * sizeof(float);
	if (g_clear) {
		memset(dst, 0, nbytes);
		return;
	}
	if (!g_stepping) {
		memcpy(dst, self_tex->data, nbytes);
		return;
	}
	vl_simd_b(fb, w, h, (const float*)vel_tex->data, vel_tex->invert_y ? 1 : 0,
		(const float*)self_tex->data, self_tex->invert_y ? 1 : 0, vel_tex, self_tex);
}
#endif

extern "C" {

// Pen getSimResolution: short side fixed, long side round(short * max(aspect, 1/aspect)).
// Equal sides (a square window) take the short-side branch, which is the same size.
static void vl_sim_dims(float mw, float mh, int sim_short, int* sw, int* sh)
{
	assert(sw && sh);
	assert(sim_short >= 1);
	if (mw < 1.0f || mh < 1.0f) {
		*sw = sim_short;
		*sh = sim_short;
		return;
	}
	float aspect = mw / mh;
	if (aspect < 1.0f) {
		aspect = 1.0f / aspect;
	}
	int max_side = (int)lroundf((float)sim_short * aspect);
	if (max_side < sim_short) {
		max_side = sim_short;
	}
	// width > height, matching the pen. A square falls through to short x short.
	if (mw > mh) {
		*sw = max_side;
		*sh = sim_short;
	} else {
		*sw = sim_short;
		*sh = max_side;
	}
}

void set_pass_config(pass_config cfg[NUM_PASSES])
{
	int sim_short = g_sim_short;
	if (sim_short < VL_SIM_SHORT_MIN) {
		sim_short = VL_SIM_SHORT_MIN;
	}
	if (sim_short > VL_SIM_SHORT_MAX) {
		sim_short = VL_SIM_SHORT_MAX;
	}
	int sw = sim_short;
	int sh = sim_short;
	vl_sim_dims(iMainResolution.x, iMainResolution.y, sim_short, &sw, &sh);

	cfg[PASS_A].format = PASS_FMT_RGBA32F;
	cfg[PASS_A].absolute = 1;
	cfg[PASS_A].w = sw;
	cfg[PASS_A].h = sh;
	cfg[PASS_A].scale_x = 1.0f;
	cfg[PASS_A].scale_y = 1.0f;
	cfg[PASS_A].recursion = VL_RECURSION;
	cfg[PASS_B] = cfg[PASS_A];
	cfg[PASS_B].recursion = 1;
}

void set_channels_Buffer_A(texture_settings* ts)
{
	vl_bind_sim(ts, 0, BUFFER_A);
	vl_bind_sim(ts, 1, BUFFER_B);
}

void mainImage_Buffer_A(vec4* fragColor, vec2 fragCoord)
{
	vec2 res = vec2(iResolution.x, iResolution.y);
	vec2 uv = fragCoord / res;
	vec2 texel = vec2(1.0f) / res;
	*fragColor = vl_sim_a(uv, texel, pglGetTexture(iChannel0), pglGetTexture(iChannel1));
}

void mainFrame_Buffer_A(pix_t* framebuffer, int w, int h)
{
	if (u_is_icon) {
		vl_icon_dye(framebuffer, w, h, pglGetTexture(iChannel0), 2);
		return;
	}

	if (u_recursion == 0) {
		vl_begin_frame(w, h);
	}

	const glTexture* self_tex = pglGetTexture(iChannel0);
	const glTexture* dye_tex = pglGetTexture(iChannel1);
#if defined(__AVX2__)
	vl_simd_buffer_a(framebuffer, w, h, self_tex, dye_tex);
#else
	vec2 res((float)w, (float)h);
	vec2 texel = vec2(1.0f) / res;

SW_PARALLEL_FOR
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			vec2 uv;
			uv.x = ((float)x + 0.5f) / res.x;
			uv.y = ((float)y + 0.5f) / res.y;
			vec4 col = vl_sim_a(uv, texel, self_tex, dye_tex);
			vl_write_float_px(framebuffer, w, h, x, y, col);
		}
	}
#endif

	// r==0 read the previous frame's dt. Later passes, and Buffer B, use this frame's.
	if (u_recursion == 0 && g_stepping) {
		g_dt_advect = g_dt;
	}
}

nk_bool do_user_gui_Buffer_A(struct nk_context* ctx, nk_bool is_playing)
{
	return vl_gui(ctx, is_playing);
}

void set_channels_Buffer_B(texture_settings* ts)
{
	vl_bind_sim(ts, 0, BUFFER_A);
	vl_bind_sim(ts, 1, BUFFER_B);
}

void mainImage_Buffer_B(vec4* fragColor, vec2 fragCoord)
{
	vec2 res = vec2(iResolution.x, iResolution.y);
	vec2 uv = fragCoord / res;
	vec2 texel = vec2(1.0f) / res;
	*fragColor = vl_sim_b(uv, texel, pglGetTexture(iChannel0), pglGetTexture(iChannel1));
}

void mainFrame_Buffer_B(pix_t* framebuffer, int w, int h)
{
	if (u_is_icon) {
		vl_icon_dye(framebuffer, w, h, pglGetTexture(iChannel1), 2);
		return;
	}

	const glTexture* vel_tex = pglGetTexture(iChannel0);
	const glTexture* self_tex = pglGetTexture(iChannel1);
	int clear_now = g_clear;
#if defined(__AVX2__)
	vl_simd_buffer_b(framebuffer, w, h, vel_tex, self_tex);
#else
	vec2 res((float)w, (float)h);
	vec2 texel = vec2(1.0f) / res;

SW_PARALLEL_FOR
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			vec2 uv;
			uv.x = ((float)x + 0.5f) / res.x;
			uv.y = ((float)y + 0.5f) / res.y;
			vec4 col = vl_sim_b(uv, texel, vel_tex, self_tex);
			vl_write_float_px(framebuffer, w, h, x, y, col);
		}
	}
#endif

	if (clear_now) {
		g_clear = 0;
	}
}

nk_bool do_user_gui_Buffer_B(struct nk_context* ctx, nk_bool is_playing)
{
	return vl_gui(ctx, is_playing);
}

void set_channels(texture_settings* ts)
{
	vl_bind_sim(ts, 0, BUFFER_A);
	vl_bind_sim(ts, 1, BUFFER_B);
	ts[2].ch = TEX_ABSTRACT_1;
	SET_DFLT_TEX_PARAMS(ts[2]);
	ts[2].filter = TS_LINEAR;
	ts[2].wrap = TS_MIRROR;
	vl_reset_defaults();
}

void mainImage(vec4* fragColor, vec2 fragCoord)
{
	const glTexture* sim = pglGetTexture(iChannel1);
	const glTexture* photo = pglGetTexture(iChannel2);
	vec2 screen(iResolution.x, iResolution.y);
	vec2 uv = fragCoord / screen;
	float media_aspect = (float)photo->w / (float)photo->h;
	vec2 sim_texel(1.0f / (float)sim->w, 1.0f / (float)sim->h);
	vec3 col = vl_display(uv, screen.x / screen.y, media_aspect, sim_texel, vl_light_dir(), sim, photo);
	*fragColor = vec4(col, 1.0f);
}

void mainFrame(pix_t* framebuffer, int w, int h)
{
	const glTexture* sim = pglGetTexture(iChannel1);
	const glTexture* photo = pglGetTexture(iChannel2);
#if defined(__AVX2__)
	vl_simd_main(framebuffer, w, h, sim, photo);
#else
	float media_aspect = (float)photo->w / (float)photo->h;
	vec2 sim_texel(1.0f / (float)sim->w, 1.0f / (float)sim->h);
	vec2 screen((float)w, (float)h);
	float screen_aspect = screen.x / screen.y;
	vec3 light = vl_light_dir();
	pix_t* lastrow = framebuffer + (h - 1) * w;

SW_PARALLEL_FOR
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			vec2 uv;
			uv.x = ((float)x + 0.5f) / screen.x;
			uv.y = ((float)y + 0.5f) / screen.y;
			vec3 col = vl_display(uv, screen_aspect, media_aspect, sim_texel, light, sim, photo);
			lastrow[-y * w + x] = pack_clamp_rgba8(col.x, col.y, col.z, 1.0f);
		}
	}
#endif
}

nk_bool do_user_gui(struct nk_context* ctx, nk_bool is_playing)
{
	return vl_gui(ctx, is_playing);
}

}
