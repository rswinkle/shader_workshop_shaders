// GLM
// =============================================================================
// Fake 3D Cityscape — demoscene-style "3D without 3D"
// =============================================================================
//
// Technique (two *different* fake-3D tricks — do not conflate them):
//
//   1. Mode-7 ground + world objects that scroll with drive_speed
//      • poles: thin camera-facing billboards
//      • roadside buildings: vertical plane hits (x = ±const) so facades
//        face the road and foreshorten in perspective (trapezoids).
//      • Daytona Spyder: camera-space billboard (pixel sprite) sitting on
//        the road at a fixed depth — does not scroll away with drive_speed.
//
//   2. Distant skyline (horizon sprite)
//      Infinitely far ahead: we never get closer. Base sits on the horizon
//      (v = 0). Shape is a function of look angle only — static while
//      driving straight; yaw can pan which slice you see. No time scroll,
//      no floating bands, no side-scroller parallax.
//
// Ranking: easiest / fastest class. Target: 60 fps at 720p–1080p with -O3.
//
// Mouse X/Y: mild yaw / horizon tilt. Time drives the road only.
// =============================================================================

#include "glm_shader_workshop_common.h"
#include "nuklear_shader_workshop.h"

using namespace glm;

extern "C" {

// ---- GUI state (reset in set_channels / Reinit) -----------------------------

static nk_bool g_do_skyline = nk_true;
static nk_bool g_do_side_buildings = nk_true;
static nk_bool g_do_windows = nk_true;
static nk_bool g_do_poles   = nk_true;
static nk_bool g_do_car     = nk_true;
static nk_bool g_do_stars   = nk_true;
static nk_bool g_do_grain   = nk_true;
static nk_bool g_do_vignette = nk_true;
static float g_drive_speed  = 4.0f;
static float g_sun_glow     = 1.2f;

void load_user_textures(const char* textures[NUM_2D_USER_TEXTURES],
                        const char* music[NUM_MUSIC_USER_TEXTURES],
                        const char* videos[NUM_VIDEO_USER_TEXTURES])
{
	music[0] = "https://upload.wikimedia.org/wikipedia/en/4/4f/Intheairtonight.ogg";
}

void set_channels(texture_settings* ts)
{
	ts[0].ch = USER_MUS0;
	SET_DFLT_MUSIC_PARAMS(ts[0]);


	g_do_skyline = nk_true;
	g_do_side_buildings = nk_true;
	g_do_windows = nk_true;
	g_do_poles = nk_true;
	g_do_car = nk_true;
	g_do_stars = nk_true;
	g_do_grain = nk_true;
	g_do_vignette = nk_true;
	g_drive_speed = 4.0f;
	g_sun_glow = 1.2f;
}

nk_bool do_user_gui(struct nk_context* ctx, nk_bool is_playing)
{
	nk_bool need = nk_false;
	nk_layout_row_dynamic(ctx, 0, 1);

	nk_label(ctx, "Fake 3D Cityscape", NK_TEXT_CENTERED);

	nk_label(ctx, "Distant (static on horizon)", NK_TEXT_LEFT);
	need |= nk_checkbox_label(ctx, "Horizon skyline", &g_do_skyline);
	nk_label(ctx, "Near (scroll with road)", NK_TEXT_LEFT);
	nk_layout_row_dynamic(ctx, 0, 2);
	need |= nk_checkbox_label(ctx, "Roadside buildings", &g_do_side_buildings);
	need |= nk_checkbox_label(ctx, "Street lamp poles", &g_do_poles);
	nk_layout_row_dynamic(ctx, 0, 1);
	nk_label(ctx, "Foreground (stays with camera)", NK_TEXT_LEFT);
	need |= nk_checkbox_label(ctx, "Daytona Spyder", &g_do_car);
	nk_layout_row_dynamic(ctx, 0, 1);
	nk_label(ctx, "Shading / post", NK_TEXT_LEFT);
	nk_layout_row_dynamic(ctx, 0, 2);
	need |= nk_checkbox_label(ctx, "Building windows", &g_do_windows);
	need |= nk_checkbox_label(ctx, "Stars", &g_do_stars);
	need |= nk_checkbox_label(ctx, "Film grain", &g_do_grain);
	need |= nk_checkbox_label(ctx, "Vignette", &g_do_vignette);
	nk_layout_row_dynamic(ctx, 0, 1);
	need |= nk_property_float(ctx, "Drive speed:", 0.0f, &g_drive_speed, 12.0f, 0.25f, 0.05f);
	need |= nk_property_float(ctx, "Sun glow:", 0.0f, &g_sun_glow, 2.0f, 0.1f, 0.05f);

	nk_label(ctx, "Tip: lower Main res if needed", NK_TEXT_LEFT);
	(void)is_playing;
	return need;
}

// ---- integer hash (no sin — much cheaper on CPU) ---------------------------

static inline float hash21(float x, float y)
{
	unsigned int n = (unsigned int)(int)(x * 374761393.0f) ^
	                 (unsigned int)(int)(y * 668265263.0f);
	n = (n ^ (n >> 13)) * 1274126177u;
	n ^= n >> 16;
	return (n & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

// Integer-cell hash (stable; avoids float-mantissa collapse on large coords)
static inline float hash2i(int x, int y)
{
	unsigned int n = (unsigned int)(x * 374761393) ^ (unsigned int)(y * 668265263);
	n = (n ^ (n >> 13)) * 1274126177u;
	n ^= n >> 16;
	return (n & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

// ---- sky -------------------------------------------------------------------

static void sky_color(float u, float v, float time, float* cr, float* cg, float* cb)
{
	float t = v * 0.5f + 0.35f;
	if (t < 0.0f) t = 0.0f;
	else if (t > 1.0f) t = 1.0f;

	// horizon → mid → zenith (scalar mix)
	float r, g, b;
	if (t < 0.45f) {
		float k = t / 0.45f;
		r = 0.95f + (0.35f - 0.95f) * k;
		g = 0.45f + (0.20f - 0.45f) * k;
		b = 0.25f + (0.45f - 0.25f) * k;
	} else {
		float k = (t - 0.45f) / 0.55f;
		r = 0.35f + (0.05f - 0.35f) * k;
		g = 0.20f + (0.06f - 0.20f) * k;
		b = 0.45f + (0.18f - 0.45f) * k;
	}

	// Sun (cheap exp via poly / inverse)
	float sdx = u - 0.55f;
	float sdy = v - 0.08f;
	float sd2 = sdx * sdx + sdy * sdy;
	// exp(-18*sqrt) ≈ 1/(1+18*s) style soft falloff, cheaper than exp+sqrt chain
	float inv = 1.0f / (1.0f + 40.0f * sd2);
	float glow = g_sun_glow;
	r += 1.00f * inv * glow;
	g += 0.55f * inv * glow;
	b += 0.25f * inv * glow;
	// core
	float core = 1.0f / (1.0f + 200.0f * sd2);
	r += 0.6f * core * glow;
	g += 0.5f * core * glow;
	b += 0.3f * core * glow;

	// Stars are applied later (after skyline) so buildings don't wipe them
	// and so they show up as small bright points, not rare dim cells.

	*cr = r; *cg = g; *cb = b;
}

// Point stars in open sky. UV y only reaches ~±0.5 on a typical landscape
// frame (normalized by res.y), so the old "v > 0.20 + 1% of cells" test
// left almost nothing visible — and building layers drew on top of the rest.
static void apply_stars(float u, float v, float time, float building_a,
                        float* r, float* g, float* b)
{
	if (!g_do_stars || v < 0.02f || building_a > 0.15f)
		return;

	// Fade in above the bright horizon / sun band
	float horizon_fade = (v - 0.02f) / 0.12f;
	if (horizon_fade < 0.0f) return;
	if (horizon_fade > 1.0f) horizon_fade = 1.0f;

	// Cell size in aspect-correct UV (~few pixels at 720p–1080p)
	const float CELL = 0.028f;
	float gx = u / CELL;
	float gy = v / CELL;
	int ix = (int)floorf(gx);
	int iy = (int)floorf(gy);

	// ~12% of cells host a star (was ~1.2% and full-cell filled — invisible)
	float seed = hash2i(ix, iy * 57 + 3);
	if (seed < 0.88f)
		return;

	// Jitter star center inside the cell so the grid isn't obvious
	float jx = hash2i(ix + 11, iy - 5) - 0.5f;
	float jy = hash2i(ix - 7, iy + 19) - 0.5f;
	float fx = (gx - (float)ix) - 0.5f - jx * 0.35f;
	float fy = (gy - (float)iy) - 0.5f - jy * 0.35f;
	float d2 = fx * fx + fy * fy;

	// Soft disc; brighter stars get a slightly larger core
	float size = 0.04f + 0.05f * seed; // radius^2 threshold neighborhood
	if (d2 > size)
		return;

	float core = 1.0f - d2 / size;
	core = core * core; // sharper falloff

	float tw = 0.55f + 0.45f * sinf(time * (2.0f + seed * 5.0f) + seed * 40.0f);
	// Magnitude from seed: most dim, some bright
	float mag = 0.35f + 0.90f * ((seed - 0.88f) / 0.12f);
	float a = core * mag * tw * horizon_fade * (1.0f - building_a);

	// Cool white with a tiny blue/yellow tint variation
	float tint = hash2i(ix * 3, iy * 5);
	*r += a * (0.85f + 0.15f * tint);
	*g += a * (0.90f + 0.05f * tint);
	*b += a * (1.00f);
}

// ---- mode-7 ground ---------------------------------------------------------

static void shade_ground(float u, float v, float time, float yaw,
                         float* cr, float* cg, float* cb)
{
	float y = v;
	if (y > -0.02f) y = -0.02f;

	float cs = cosf(yaw), sn = sinf(yaw);
	float gx = u * cs - sn;
	float gz = u * sn + cs;

	float persp = -1.0f / y;
	float wx = gx * persp;
	float wz = gz * persp + time * g_drive_speed;
	float abx = wx < 0.0f ? -wx : wx;

	float r, g, b;

	if (abx >= 2.1f) {
		// dirt / sidewalk
		float dirt = 0.15f + 0.04f * hash21(floorf(wx * 3.0f), floorf(wz * 3.0f));
		r = dirt * 0.9f; g = dirt * 0.75f; b = dirt * 0.55f;
		if (abx < 3.5f) {
			int chk = ((int)floorf(wx * 2.0f) + (int)floorf(wz * 2.0f)) & 1;
			if (chk) { r = r * 0.65f + 0.22f * 0.35f; g = g * 0.65f + 0.22f * 0.35f; b = b * 0.65f + 0.24f * 0.35f; }
		}
	} else {
		// asphalt
		r = 0.08f; g = 0.08f; b = 0.10f;
		// center dashes
		if (abx < 0.06f) {
			float dash = wz * 0.35f;
			dash = dash - floorf(dash);
			if (dash > 0.5f) { r = 0.95f; g = 0.90f; b = 0.55f; }
		}
		// side lines
		float dside = abx - 1.6f;
		if (dside < 0.0f) dside = -dside;
		if (dside < 0.05f) { r = 0.90f; g = 0.90f; b = 0.95f; }
		// curb
		if (abx > 1.75f) { r = 0.35f; g = 0.18f; b = 0.12f; }
	}

	// distance fog
	float fog = 1.0f - expf(-0.015f * persp);
	if (fog < 0.0f) fog = 0.0f;
	if (fog > 0.85f) fog = 0.85f;
	r = r + (0.95f - r) * fog;
	g = g + (0.50f - g) * fog;
	b = b + (0.30f - b) * fog;

	*cr = r; *cg = g; *cb = b;
}

// ---- distant skyline (static horizon strip) --------------------------------
//
// Infinitely far: base locked to the horizon (v = 0). Profile is a pure
// function of look-angle (screen x + yaw). Driving does not scroll it.
// Turning (yaw) pans which slice of the city you see — like rotating in place.

// Screen-space roof height for one building cell along the horizon ring.
static float skyline_roof(int cell)
{
	float s0 = hash2i(cell, 991);
	float s1 = hash2i(cell, 1003);
	float h = 0.05f + 0.11f * s0 + 0.07f * s1;
	// Occasional tower / landmark
	if (s0 > 0.88f)
		h += 0.10f + 0.08f * s1;
	// Slight massing: every few cells share a lower podium read
	float block = hash2i(cell / 4, 55);
	h += 0.03f * block;
	return h;
}

// Returns coverage 0..1; color is a dusk silhouette (hazy near tops).
static float distant_skyline(float u, float v, float yaw,
                             float* cr, float* cg, float* cb)
{
	// Only above the horizon; cap max roof so zenith stays sky
	if (v < 0.0f || v > 0.42f)
		return 0.0f;

	// Angle along the horizon. No time term.
	float ang = u - yaw;
	const float DENSITY = 7.0f; // buildings across a ~1 unit of screen x
	float x = ang * DENSITY;
	int cell = (int)floorf(x);
	float local = x - (float)cell;

	float seed = hash2i(cell, 42);
	float half_w = 0.30f + 0.20f * hash2i(cell, 77);
	float adx = local - 0.5f;
	if (adx < 0.0f) adx = -adx;
	if (adx > half_w)
		return 0.0f;

	float h = skyline_roof(cell);
	if (v > h)
		return 0.0f;

	float a = 1.0f;
	// Soft vertical sides
	float edge = half_w - adx;
	if (edge < 0.03f)
		a = edge / 0.03f;

	// Dark silhouette; slightly cool, hazed into sunset toward the roof
	float r = 0.07f + 0.04f * seed;
	float g = 0.07f + 0.03f * seed;
	float b = 0.10f + 0.05f * seed;
	float roof_t = v / h; // 0 at horizon base, 1 at roof
	// Base blends a little into the warm horizon; roof stays darker
	float haze = 0.15f + 0.25f * (1.0f - roof_t);
	r = r + (0.55f - r) * haze * 0.35f;
	g = g + (0.28f - g) * haze * 0.35f;
	b = b + (0.18f - b) * haze * 0.35f;

	// Sparse distant windows (tiny; only if enabled)
	if (g_do_windows && h > 1e-4f) {
		float wy = v / h;
		float wx = (local - (0.5f - half_w)) / (2.0f * half_w);
		float gx = wx * 5.0f;
		float gy = wy * 7.0f;
		gx = gx - floorf(gx) - 0.5f; if (gx < 0.0f) gx = -gx;
		gy = gy - floorf(gy) - 0.5f; if (gy < 0.0f) gy = -gy;
		if (gx > 0.22f && gy > 0.20f && wy > 0.1f && wy < 0.9f) {
			float lit = hash2i(cell * 13 + (int)floorf(wx * 8.0f),
			                   (int)floorf(wy * 10.0f));
			if (lit > 0.72f) {
				r = r * 0.4f + 0.95f * 0.6f;
				g = g * 0.4f + 0.70f * 0.6f;
				b = b * 0.4f + 0.35f * 0.6f;
			}
		}
	}

	*cr = r; *cg = g; *cb = b;
	return a;
}

// ---- roadside buildings: plane-hit facades facing the road -----------------
//
// Same camera model as Mode-7 ground (eye at y = CAM_H, ray D = (gx, v, gz)):
//   gx = u*cos(yaw) - sin(yaw)
//   gz = u*sin(yaw) + cos(yaw)
// Ground hit: t = -CAM_H/v,  (x,z_cam) = t*(gx,gz)
//
// Facades are vertical planes x = ±BLDG_X (normals toward the centerline),
// subdivided into regular lots along Z. Per pixel:
//   t = plane_x / gx
//   z_cam = t * gz,  y = CAM_H + t*v
//   accept if y in [0, H_lot], z_cam in draw range, and lot not an alley gap.
// When looking at the ground (v < 0), also require the wall closer than the
// ground hit so road pixels aren't covered by walls behind them.
//
// Perspective is free: a wall spanning many Z maps to a trapezoid on screen.
// UVs: across = z within lot, up = y / H.

static int side_buildings(float u, float v, float time, float yaw,
                          float* cr, float* cg, float* cb)
{
	const float SPACING   = 9.0f;  // lot pitch along the road
	const float LOT_DEPTH = 6.5f;  // facade length (< SPACING → alley gaps)
	const float BLDG_X    = 4.3f;  // plane x = ± this (outside curb)
	const float NEAR_Z    = 1.8f;
	const float FAR_Z     = 32.0f;
	const float CAM_H     = 1.0f;  // must match Mode-7: contact_y = -CAM_H/depth

	float scroll = time * g_drive_speed;
	float cs = cosf(yaw), sn = sinf(yaw);
	// Ray direction XZ (same basis as shade_ground)
	float gx = u * cs - sn;
	float gz = u * sn + cs;

	// Ground intersection along this ray (if any) — for occlusion
	float t_ground = 1e30f;
	if (v < -1e-5f)
		t_ground = -CAM_H / v;

	int hit = 0;
	float best_t = 1e30f;
	float best_z = 0.0f, best_y = 0.0f, best_h = 1.0f, best_seed = 0.0f;
	float best_across = 0.0f;
	int best_row = 0, best_side = 1;

	for (int side = -1; side <= 1; side += 2) {
		float plane_x = (float)side * BLDG_X;
		if (gx * plane_x <= 0.0f)
			continue; // ray goes the other way / parallel
		if (fabsf(gx) < 1e-5f)
			continue;

		float t = plane_x / gx;
		if (t < 1e-4f)
			continue;
		// Road / sidewalk in front of the wall wins
		if (t >= t_ground)
			continue;

		float z_cam = t * gz;
		if (z_cam < NEAR_Z || z_cam > FAR_Z)
			continue;

		float y_hit = CAM_H + t * v;
		if (y_hit < 0.0f)
			continue; // below ground (shouldn't after ground test, but safe)

		// Which lot along the road?
		float wz = z_cam + scroll;
		int row = (int)floorf(wz / SPACING);
		float local_z = wz - (float)row * SPACING;
		if (local_z > LOT_DEPTH)
			continue; // alley / gap between buildings

		float seed = hash2i(row, side * 17 + 5);
		float world_h = 1.4f + 2.4f * seed;
		if (y_hit > world_h)
			continue;

		if (t < best_t) {
			best_t = t;
			best_z = z_cam;
			best_y = y_hit;
			best_h = world_h;
			best_seed = seed;
			best_across = local_z / LOT_DEPTH; // 0 = near end of lot … 1 = far
			best_row = row;
			best_side = side;
			hit = 1;
		}
	}

	if (!hit)
		return 0;

	float up = best_y / best_h; // 0 ground … 1 roof
	float seed = best_seed;

	// Facade base color
	float r = 0.14f + 0.10f * seed;
	float g = 0.14f + 0.08f * seed;
	float b = 0.18f + 0.10f * seed;
	if (seed > 0.75f) {
		r = r * 0.5f + 0.55f * 0.5f;
		g = g * 0.5f + 0.32f * 0.5f;
		b = b * 0.5f + 0.22f * 0.5f;
	}

	// Cheap facing light: normal = (-side, 0, 0) toward road; sun from upper-right
	// ndotl style shade so left/right walls don't look identical
	float ndotl = 0.55f + 0.35f * (float)(-best_side) * 0.5f; // side +1 (right wall, normal -X) slightly different
	// Add a little from "sky" based on up
	float shade = 0.45f + 0.55f * ndotl + 0.08f * up;
	r *= shade; g *= shade; b *= shade;

	if (g_do_windows) {
		// Perspective-correct enough: across along road, up along wall
		float wxn = best_across;
		float gxw = wxn * (5.0f + seed * 3.0f);
		float gyw = up * (7.0f + seed * 4.0f);
		gxw = gxw - floorf(gxw) - 0.5f; if (gxw < 0.0f) gxw = -gxw;
		gyw = gyw - floorf(gyw) - 0.5f; if (gyw < 0.0f) gyw = -gyw;
		if (gxw > 0.18f && gyw > 0.16f && up > 0.06f && up < 0.92f) {
			float lit = hash2i(best_row * 3 + (int)floorf(wxn * 10.0f),
			                   best_side * 11 + (int)floorf(up * 14.0f));
			if (lit > 0.55f) {
				r = r * 0.3f + 1.00f * 0.7f;
				g = g * 0.3f + 0.85f * 0.7f;
				b = b * 0.3f + 0.45f * 0.7f;
			}
		}
	}

	// Distance haze (same sunset fog as the road)
	float fade = best_z * 0.028f;
	if (fade > 0.55f) fade = 0.55f;
	r = r + (0.95f - r) * fade;
	g = g + (0.50f - g) * fade;
	b = b + (0.30f - b) * fade;

	*cr = r; *cg = g; *cb = b;
	return 1;
}

// ---- poles (optional, O(rows) per pixel) -----------------------------------
//
// Regular spacing on both shoulders — same world-Z scroll as the Mode-7 road:
//   depth = wz - scroll,  screen_x ≈ wx/depth,  contact_y = -1/depth
// World height H → screen height H/depth (grows as poles approach).

static int poles_billboard(float u, float v, float time, float yaw,
                           float* cr, float* cg, float* cb)
{
	const float SPACING = 20.0f;   // regular interval along the road
	const float POLE_X  = 2.6f;   // fixed offset onto each shoulder
	const float WORLD_H = 1.15f;  // world-space stem height (screen = H/depth)
	const int   ROWS    = 8;      // rows ahead of the camera to test
	const float NEAR_Z  = 1.6f;
	const float FAR_Z   = 28.0f;

	float scroll = time * g_drive_speed;
	float cs = cosf(yaw), sn = sinf(yaw);

	// Next regular row at or just ahead of the camera
	int row0 = (int)floorf(scroll / SPACING) + 1;

	// Near → far so the closest lamp wins on overlap
	for (int i = 0; i < ROWS; i++) {
		float wz = (float)(row0 + i) * SPACING;
		float depth = wz - scroll;
		if (depth < NEAR_Z || depth > FAR_Z)
			continue;

		float inv_d = 1.0f / depth;
		float contact_y = -inv_d;
		float height = WORLD_H * inv_d;

		if (v < contact_y || v > contact_y + height)
			continue;

		for (int side = -1; side <= 1; side += 2) {
			float wx = (float)side * POLE_X;
			float rx = wx * cs + depth * sn;
			float sx = rx * inv_d;

			float half_w = 0.05f * inv_d;
			if (half_w < 0.004f) half_w = 0.004f;

			float adx = u - sx;
			if (adx < 0.0f) adx = -adx;
			if (adx > half_w)
				continue;

			float along = (v - contact_y) / height;
			float r = 0.12f, g = 0.12f, b = 0.14f;
			if (along > 0.85f) { r = 1.0f; g = 0.85f; b = 0.55f; } // lamp head

			float fade = depth * 0.03f;
			if (fade > 0.5f) fade = 0.5f;
			r = r + (0.95f - r) * fade;
			g = g + (0.50f - g) * fade;
			b = b + (0.30f - b) * fade;

			*cr = r; *cg = g; *cb = b;
			return 1;
		}
	}
	return 0;
}

// ---- Daytona Spyder (pixel billboard, camera-space) ------------------------
//
// Elevated rear, top down — same view as a Mode-7 camera looking down onto
// the deck. Wide Kamm tail, split chrome bumpers, Florida plate, two round
// lights per side (red + amber), quad exhaust, black stored convertible
// behind the seats, chrome windshield. Symmetric except the steering wheel.
// 48x28 texel grid.
//
// Sits at a fixed camera depth on the centerline (yaw pans it with the road).
// Return: 0 miss, 1 opaque, 2 shadow (modulate the already-shaded road).

static int daytona_cheb(int ax, int ay, int bx, int by)
{
	int dx = ax - bx;
	int dy = ay - by;
	if (dx < 0) {
		dx = -dx;
	}
	if (dy < 0) {
		dy = -dy;
	}
	return (dx > dy) ? dx : dy;
}

static int daytona_light(int adx, int y)
{
	// two per side: inner red, outer amber (the innermost pair is gone)
	static const int pos[2] = { 13, 18 };
	for (int i = 0; i < 2; i++) {
		int d = daytona_cheb(adx, y, pos[i], 11);
		if (d > 1) {
			continue;
		}
		if (d == 0) {
			return 16;
		}
		return (i == 1) ? 21 : 15;
	}
	return 0;
}

static int shade_daytona(float lx, float ly, float* cr, float* cg, float* cb)
{
	const int W = 48;
	const int H = 28;
	if (lx < 0.0f || ly < 0.0f || lx >= 1.0f || ly >= 1.0f) {
		return 0;
	}
	int x = (int)floorf(lx * (float)W);
	int y = (int)floorf(ly * (float)H);
	if (x >= W) {
		x = W - 1;
	}
	if (y >= H) {
		y = H - 1;
	}

	int cx = x - W / 2;
	int adx = cx < 0 ? -cx : cx;

	static const signed char HW[28] = {
		0,  0, 15,
		17, 19,
		20, 21, 21,
		22, 22, 22, 21, 21, 20,
		18, 18, 17, 17, 16,
		16, 15, 15, 14,
		14, 13, 13, 12, 12
	};
	int hw = (int)HW[y];

	int id = 0;
	if (y == 23 && adx > hw && adx <= hw + 2) {
		id = 6; // side mirror
	} else if (y == 22 && adx == hw + 1) {
		id = 7;
	} else if (y == 27 && adx <= 2) {
		id = 7; // interior mirror
	} else if (y <= 2) {
		float sx = (float)adx / 20.0f;
		float sy = ((float)y - 0.2f) / 2.4f;
		if (sx * sx + sy * sy <= 1.0f) {
			id = 1;
		}
	} else if (hw > 0 && adx <= hw) {
		if (adx == hw) {
			id = 19;
		} else if ((y == 3 || y == 4) &&
		           (adx == 6 || adx == 7 || adx == 9 || adx == 10)) {
			if (y == 3) {
				id = 7;
			} else {
				id = (adx == 7 || adx == 10) ? 6 : 17;
			}
		} else if (y >= 8 && y <= 10 && adx <= 4) {
			if (y == 8 || y == 10 || adx == 4) {
				id = 7;
			} else if (y == 9 && adx <= 2) {
				id = 24;
			} else {
				id = 23;
			}
		} else if ((y == 6 || y == 7) && adx >= 6) {
			if (y == 7) {
				id = (adx <= hw - 2) ? 6 : 7;
			} else {
				id = 7;
			}
		} else if (y >= 9 && y <= 13 && daytona_light(adx, y)) {
			id = daytona_light(adx, y);
		} else if (y == 13 && adx <= 3) {
			id = 7; // Ferrari script
		} else if (y >= 14 && y <= 18 && adx <= hw - 1) {
			// stored convertible (top down), looking down onto the cover
			if (y == 14 || y == 18) {
				id = 2;
			} else {
				id = 3;
			}
		} else if (y >= 19 && y <= 22 && adx >= 5 && adx <= 9) {
			// seat backs + headrests, seen from behind
			if (y == 22) {
				id = (adx >= 6 && adx <= 8) ? 11 : 18; // tan headrest
			} else if (y == 19 || adx == 5 || adx == 9) {
				id = 12;
			} else {
				id = 11;
			}
		} else if (y >= 19 && y <= 22 && adx <= 4) {
			id = 18;
		} else if (y == 23 && adx <= hw - 2) {
			if (cx < -1 && adx >= 4 && adx <= 8) {
				id = 25; // wood wheel (LHD, left from behind)
			} else if (cx < 0 && (adx == 2 || adx == 9)) {
				id = 6;
			} else {
				id = 18;
			}
		} else if (y >= 24 && adx <= hw - 1) {
			if (adx >= hw - 2 || y == 24) {
				id = 7;
			} else {
				id = 13;
			}
		} else if (y >= 8 && y <= 13) {
			if (adx >= hw - 2) {
				id = 2;
			} else {
				id = (adx > 4) ? 20 : 3;
			}
		} else if (y <= 7) {
			id = 2;
		} else if (adx >= hw - 2) {
			id = 2;
		} else {
			id = 3;
		}
	}

	if (id == 0) {
		return 0;
	}

	static const float PAL[26][3] = {
		{ 0.0f,    0.0f,    0.0f    },
		{ 0.039f,  0.031f,  0.047f  }, // 1  shadow
		{ 0.071f,  0.071f,  0.086f  }, // 2  body darkest
		{ 0.141f,  0.141f,  0.165f  }, // 3  body
		{ 0.306f,  0.314f,  0.353f  }, // 4  body lit
		{ 0.431f,  0.329f,  0.235f  }, // 5  sun glint
		{ 0.839f,  0.855f,  0.878f  }, // 6  chrome
		{ 0.486f,  0.502f,  0.533f  }, // 7  chrome dark
		{ 0.0f,    0.0f,    0.0f    }, // 8  unused
		{ 0.0f,    0.0f,    0.0f    }, // 9  unused
		{ 0.0f,    0.0f,    0.0f    }, // 10 unused
		{ 0.78f,   0.62f,   0.42f   }, // 11 seat / headrest (tan leather)
		{ 0.55f,   0.40f,   0.26f   }, // 12 seat dark / headrest edge
		{ 0.220f,  0.282f,  0.361f  }, // 13 glass
		{ 0.588f,  0.667f,  0.745f  }, // 14 glass hi
		{ 0.784f,  0.110f,  0.094f  }, // 15 tail
		{ 1.000f,  0.353f,  0.157f  }, // 16 tail hot
		{ 0.125f,  0.118f,  0.110f  }, // 17 exhaust throat
		{ 0.031f,  0.031f,  0.039f  }, // 18 interior
		{ 0.0f,    0.0f,    0.0f    }, // 19 outline
		{ 0.094f,  0.086f,  0.102f  }, // 20 rear panel
		{ 0.863f,  0.502f,  0.141f  }, // 21 amber
		{ 0.0f,    0.0f,    0.0f    }, // 22 unused
		{ 0.769f,  0.698f,  0.282f  }, // 23 plate
		{ 0.157f,  0.188f,  0.110f  }, // 24 plate text
		{ 0.353f,  0.188f,  0.094f  }, // 25 wood wheel
	};

	*cr = PAL[id][0];
	*cg = PAL[id][1];
	*cb = PAL[id][2];
	return (id == 1) ? 2 : 1;
}

static int car_billboard(float u, float v, float time, float yaw,
                         float* cr, float* cg, float* cb)
{
	const float DEPTH    = 3.15f;
	const float CAR_H    = 0.56f; // world height above the pavement
	const float TEX_W    = 48.0f;
	const float TEX_H    = 28.0f;
	const float GROUND_LY = 2.5f / TEX_H;

	float cs = cosf(yaw), sn = sinf(yaw);
	float wx = 0.0f;
	if (g_drive_speed > 0.05f) {
		wx = 0.03f * sinf(time * 0.7f);
	}

	float inv_d = 1.0f / DEPTH;
	float contact_y = -inv_d;
	float height = CAR_H * inv_d;
	float sprite_h = height / (1.0f - GROUND_LY);
	float half_w = 0.5f * sprite_h * (TEX_W / TEX_H);

	float rx = wx * cs + DEPTH * sn;
	float sx = rx * inv_d;

	float pix = iResolution.y;
	sx = floorf(sx * pix + 0.5f) / pix;
	contact_y = floorf(contact_y * pix + 0.5f) / pix;

	float sprite_bottom = contact_y - GROUND_LY * sprite_h;
	if (v < sprite_bottom || v > sprite_bottom + sprite_h) {
		return 0;
	}
	if (u < sx - half_w || u > sx + half_w) {
		return 0;
	}

	float lx = (u - (sx - half_w)) / (2.0f * half_w);
	float ly = (v - sprite_bottom) / sprite_h;
	return shade_daytona(lx, ly, cr, cg, cb);
}

// ---- mainImage -------------------------------------------------------------

void mainImage(vec4* fragColor, vec2 fragCoord)
{
	float resy = iResolution.y;
	float u = (fragCoord.x - 0.5f * iResolution.x) / resy;
	float v = (fragCoord.y - 0.5f * iResolution.y) / resy;

	float time = iTime;
	float yaw = 0.0f;
	if (iMouse.z > 0.0f) {
		yaw = (iMouse.x / iResolution.x - 0.5f) * 0.6f;
		v  += (iMouse.y / iResolution.y - 0.5f) * 0.15f;
	}

	float r, g, b;
	float sky_cover = 0.0f; // for star occlusion (horizon silhouette + near facades)

	if (v < 0.0f) {
		shade_ground(u, v, time, yaw, &r, &g, &b);
	} else {
		sky_color(u, v, time, &r, &g, &b);

		// Distant city: static on the horizon (no drive scroll)
		if (g_do_skyline) {
			float lr, lg, lb;
			float a = distant_skyline(u, v, yaw, &lr, &lg, &lb);
			if (a > 0.0f) {
				r = r + (lr - r) * a;
				g = g + (lg - g) * a;
				b = b + (lb - b) * a;
				if (a > sky_cover) sky_cover = a;
			}
		}

		// Stars only in open sky above/around the silhouette
		apply_stars(u, v, time, sky_cover, &r, &g, &b);
	}

	// Near world billboards (grounded + scrolling) — may cover ground or sky
	if (g_do_side_buildings) {
		float br, bg, bb;
		if (side_buildings(u, v, time, yaw, &br, &bg, &bb)) {
			r = br; g = bg; b = bb;
			sky_cover = 1.0f;
		}
	}

	if (g_do_poles) {
		float pr, pg, pb;
		if (poles_billboard(u, v, time, yaw, &pr, &pg, &pb)) {
			r = pr; g = pg; b = pb;
		}
	}

	if (g_do_car) {
		float cr, cg, cb;
		int code = car_billboard(u, v, time, yaw, &cr, &cg, &cb);
		if (code == 1) {
			r = cr; g = cg; b = cb;
		} else if (code == 2) {
			r *= 0.22f;
			g *= 0.20f;
			b *= 0.24f;
		}
	}

	if (g_do_vignette) {
		float qx = fragCoord.x / iResolution.x;
		float qy = fragCoord.y / iResolution.y;
		float vig = qx * qy * (1.0f - qx) * (1.0f - qy);
		// cheap pow(~0.35): two sqrts approx poorly — use soft curve
		vig = vig * 16.0f;
		// smoothstep-ish: vig/(vig+c) 
		vig = vig / (vig + 0.35f);
		float vm = 0.40f + 0.60f * vig;
		r *= vm; g *= vm; b *= vm;
	}

	if (g_do_grain) {
		float gr = (hash21(fragCoord.x + time, fragCoord.y) - 0.5f) * 0.03f;
		r += gr; g += gr; b += gr;
	}

	if (r < 0.0f) r = 0.0f; else if (r > 1.0f) r = 1.0f;
	if (g < 0.0f) g = 0.0f; else if (g > 1.0f) g = 1.0f;
	if (b < 0.0f) b = 0.0f; else if (b > 1.0f) b = 1.0f;

	fragColor->x = r;
	fragColor->y = g;
	fragColor->z = b;
	fragColor->w = 1.0f;
}

}
