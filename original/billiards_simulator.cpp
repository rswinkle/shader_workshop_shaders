// GLM
// Top-down 2D billiards; clack one-shot from za-games (Freesound 539854).
#include "glm_shader_workshop_common.h"

using namespace glm;

#include "nuklear_shader_workshop.h"
#include "billiards_clack_pcm.h"

#include <assert.h>

extern "C" {

#define MIN_BALLS 0
#define MAX_BALLS 20
#define START_BALLS 4
#define MAX_CLACKS 32

#define MIN_SPEED 0.05f
#define MAX_SPEED 2.0f
#define DFLT_SPEED 0.35f

#define MIN_RADIUS 0.02f
#define MAX_RADIUS 0.12f
#define DFLT_RADIUS 0.035f

#define MIN_FRICTION 0.0f
#define MAX_FRICTION 2.0f
#define DFLT_FRICTION 0.0f

#define MIN_GAIN 0.0f
#define MAX_GAIN 2.0f
#define DFLT_GAIN 1.0f

#define MIN_HARDNESS 0.0f
#define MAX_HARDNESS 2.0f
#define DFLT_HARDNESS 1.0f

#define MIN_DECAY 0.5f
#define MAX_DECAY 4.0f
#define DFLT_DECAY 1.0f

#define MIN_PITCH_ST -12.0f
#define MAX_PITCH_ST 12.0f
#define DFLT_PITCH_ST 0.0f

#define DRAG_PIXELS_FOR_MAX_SPEED 150.0f

struct Ball {
	vec2 p;
	vec2 v;
	vec3 color;
	int color_id;
};

// 20 colors chosen to stay distinct on green baize
static const vec3 PALETTE[MAX_BALLS] = {
	vec3(0.96f, 0.96f, 0.96f),
	vec3(0.08f, 0.08f, 0.08f),
	vec3(0.95f, 0.84f, 0.12f),
	vec3(0.95f, 0.45f, 0.06f),
	vec3(0.90f, 0.12f, 0.12f),
	vec3(0.88f, 0.22f, 0.58f),
	vec3(0.55f, 0.16f, 0.72f),
	vec3(0.14f, 0.28f, 0.88f),
	vec3(0.10f, 0.62f, 0.95f),
	vec3(0.10f, 0.86f, 0.86f),
	vec3(0.95f, 0.55f, 0.72f),
	vec3(0.58f, 0.38f, 0.18f),
	vec3(0.88f, 0.82f, 0.55f),
	vec3(0.70f, 0.70f, 0.74f),
	vec3(0.78f, 0.12f, 0.32f),
	vec3(0.52f, 0.95f, 0.18f),
	vec3(0.95f, 0.70f, 0.12f),
	vec3(0.18f, 0.18f, 0.48f),
	vec3(0.55f, 0.86f, 0.95f),
	vec3(0.90f, 0.38f, 0.22f)
};

static Ball g_balls[MAX_BALLS];
static int g_n = 0;
static int g_color_used[MAX_BALLS];
static unsigned g_rng = 2463534242u;

static int g_clack_seq[MAX_CLACKS];
static int g_clack_head = 0;
static int g_audio_reset = 0;

static int g_last_frame = -1;
static int g_started = 0;
static float g_aspect = 1.0f;

static float g_speed = DFLT_SPEED;
static float g_radius = DFLT_RADIUS;
static float g_friction = DFLT_FRICTION;

static float g_gain = DFLT_GAIN;
static float g_hardness = DFLT_HARDNESS;
static float g_decay = DFLT_DECAY;
static float g_pitch_st = DFLT_PITCH_ST;

static int g_mouse_was_down = 0;
static vec2 g_drag_start = vec2(0.0f);

static float rand01(void)
{
	g_rng ^= g_rng << 13;
	g_rng ^= g_rng >> 17;
	g_rng ^= g_rng << 5;
	return (g_rng & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

static void record_clack(void)
{
	g_clack_seq[g_clack_head]++;
	g_clack_head = (g_clack_head + 1) % MAX_CLACKS;
}

static int pick_color(void)
{
	int unused[MAX_BALLS];
	int n = 0;
	for (int i = 0; i < MAX_BALLS; i++) {
		if (!g_color_used[i]) {
			unused[n++] = i;
		}
	}
	assert(n > 0);
	int k = (int)(rand01() * (float)n);
	int id = unused[k];
	g_color_used[id] = 1;
	return id;
}

static void spawn_ball_at(vec2 p, vec2 v)
{
	assert(g_n < MAX_BALLS);
	float aspect = iResolution.z;
	float r = g_radius;
	p.x = clamp(p.x, r, aspect - r);
	p.y = clamp(p.y, r, 1.0f - r);

	Ball b;
	b.p = p;
	b.v = v;
	b.color_id = pick_color();
	b.color = PALETTE[b.color_id];
	g_balls[g_n++] = b;
}

static void spawn_ball(void)
{
	assert(g_n < MAX_BALLS);
	float aspect = iResolution.z;
	float r = g_radius;
	vec2 p = vec2(r + (float)g_n * 2.1f * r, 0.5f);

	for (int attempt = 0; attempt < 40; attempt++) {
		vec2 cand;
		cand.x = r + rand01() * (aspect - 2.0f * r);
		cand.y = r + rand01() * (1.0f - 2.0f * r);
		int ok = 1;
		for (int i = 0; i < g_n; i++) {
			if (length(cand - g_balls[i].p) < 2.0f * r + 0.002f) {
				ok = 0;
				break;
			}
		}
		if (ok) {
			p = cand;
			break;
		}
	}

	float ang = rand01() * 6.28318530718f;
	spawn_ball_at(p, vec2(cos(ang), sin(ang)) * g_speed);
}

static void reset_clacks(void)
{
	for (int i = 0; i < MAX_CLACKS; i++) {
		g_clack_seq[i] = 0;
	}
	g_clack_head = 0;
	g_audio_reset++;
}

static void clear_ball_slot(int i)
{
	g_color_used[g_balls[i].color_id] = 0;
	g_balls[i].p = vec2(0.0f);
	g_balls[i].v = vec2(0.0f);
	g_balls[i].color = vec3(0.0f);
	g_balls[i].color_id = 0;
}

static void set_ball_count(int n)
{
	assert(n >= MIN_BALLS && n <= MAX_BALLS);
	while (g_n < n) {
		spawn_ball();
	}
	while (g_n > n) {
		g_n--;
		clear_ball_slot(g_n);
	}
	if (g_n == 0) {
		reset_clacks();
	}
}

static void apply_speed(void)
{
	for (int i = 0; i < g_n; i++) {
		float s = length(g_balls[i].v);
		if (s > 1e-6f) {
			g_balls[i].v *= g_speed / s;
		} else {
			float ang = rand01() * 6.28318530718f;
			g_balls[i].v = vec2(cos(ang), sin(ang)) * g_speed;
		}
	}
}

static void clamp_to_table(void)
{
	float aspect = iResolution.z;
	float r = g_radius;
	for (int i = 0; i < g_n; i++) {
		g_balls[i].p.x = clamp(g_balls[i].p.x, r, aspect - r);
		g_balls[i].p.y = clamp(g_balls[i].p.y, r, 1.0f - r);
	}
}

static void shuffle_balls(void)
{
	int n = g_n;
	for (int i = 0; i < n; i++) {
		clear_ball_slot(i);
	}
	g_n = 0;
	for (int i = 0; i < n; i++) {
		spawn_ball();
	}
}

static void reset_table(int n)
{
	g_rng ^= (unsigned)(iDate.w * 100000.0f) + (unsigned)iFrame * 747796405u;
	assert(g_rng != 0);
	for (int i = 0; i < MAX_BALLS; i++) {
		g_color_used[i] = 0;
	}
	g_n = 0;
	g_last_frame = -1;
	g_aspect = iResolution.z;
	g_mouse_was_down = 0;
	reset_clacks();
	set_ball_count(n);
}

static void wall_collisions(void)
{
	float aspect = iResolution.z;
	float r = g_radius;
	for (int i = 0; i < g_n; i++) {
		Ball* b = &g_balls[i];
		if (b->p.x < r && b->v.x < 0.0f) {
			b->p.x = r;
			b->v.x = -b->v.x;
		} else if (b->p.x > aspect - r && b->v.x > 0.0f) {
			b->p.x = aspect - r;
			b->v.x = -b->v.x;
		}
		if (b->p.y < r && b->v.y < 0.0f) {
			b->p.y = r;
			b->v.y = -b->v.y;
		} else if (b->p.y > 1.0f - r && b->v.y > 0.0f) {
			b->p.y = 1.0f - r;
			b->v.y = -b->v.y;
		}
	}
}

static void ball_collisions(void)
{
	float min_d = 2.0f * g_radius;
	float min_rel = 0.02f * g_speed;
	for (int i = 0; i < g_n; i++) {
		for (int j = i + 1; j < g_n; j++) {
			vec2 d = g_balls[j].p - g_balls[i].p;
			float dist = length(d);
			if (dist < 1e-6f) {
				d = vec2(1.0f, 0.0f);
				dist = 1e-6f;
			}
			vec2 n = d / dist;
			if (dist < min_d) {
				float rel = dot(g_balls[i].v - g_balls[j].v, n);
				if (rel > 0.0f) {
					g_balls[i].v -= rel * n;
					g_balls[j].v += rel * n;
					if (rel > min_rel) {
						record_clack();
					}
				}
				float pen = min_d - dist;
				g_balls[i].p -= n * (pen * 0.5f);
				g_balls[j].p += n * (pen * 0.5f);
			}
		}
	}
}

static void simulate(float dt)
{
	float aspect = iResolution.z;
	if (aspect != g_aspect) {
		g_aspect = aspect;
		clamp_to_table();
	}

	int steps = 1 + (int)(g_speed * dt / (0.25f * g_radius));
	if (steps > 8) {
		steps = 8;
	}
	float h = dt / (float)steps;
	for (int s = 0; s < steps; s++) {
		for (int i = 0; i < g_n; i++) {
			g_balls[i].p += g_balls[i].v * h;
		}
		wall_collisions();
		ball_collisions();
		if (g_friction > 0.0f) {
			for (int i = 0; i < g_n; i++) {
				float spd = length(g_balls[i].v);
				if (spd <= 0.0f) {
					continue;
				}
				float ns = spd - g_friction * h;
				if (ns <= 0.0f) {
					g_balls[i].v = vec2(0.0f);
				} else {
					g_balls[i].v *= ns / spd;
				}
			}
		}
	}
}

static vec2 pixel_to_world(vec2 pix)
{
	return pix / iResolution.y;
}

static void handle_mouse(void)
{
	int down = (iMouse.z > 0.0f) ? 1 : 0;
	if (down && !g_mouse_was_down) {
		g_drag_start = iMouse.xy();
	}
	if (!down && g_mouse_was_down && g_n < MAX_BALLS) {
		vec2 drag = iMouse.xy() - g_drag_start;
		float dist = length(drag);
		float speed = min(MAX_SPEED, dist / DRAG_PIXELS_FOR_MAX_SPEED * MAX_SPEED);
		vec2 v = vec2(0.0f);
		if (dist > 1.0f) {
			v = -drag / dist * speed;
		}
		spawn_ball_at(pixel_to_world(g_drag_start), v);
	}
	g_mouse_was_down = down;
}

static void maybe_simulate(vec2 fragCoord)
{
	if ((int)fragCoord.x != 0 || (int)fragCoord.y != 0) {
		return;
	}
	if (u_is_icon) {
		return;
	}
	if (!g_started) {
		reset_table(START_BALLS);
		g_started = 1;
	}
	handle_mouse();
	if (iFrame < g_last_frame) {
		reset_clacks();
	}
	if (g_last_frame == iFrame) {
		return;
	}
	g_last_frame = iFrame;
	float dt = iTimeDelta;
	if (dt <= 0.0f || dt > 0.1f) {
		dt = 1.0f / 60.0f;
	}
	simulate(dt);
}

static void reset_sound(void)
{
	g_gain = DFLT_GAIN;
	g_hardness = DFLT_HARDNESS;
	g_decay = DFLT_DECAY;
	g_pitch_st = DFLT_PITCH_ST;
}

void set_channels(texture_settings* ts)
{
	(void)ts;
	g_speed = DFLT_SPEED;
	g_radius = DFLT_RADIUS;
	g_friction = DFLT_FRICTION;
	reset_sound();
	g_n = 0;
	g_started = 0;
	g_last_frame = -1;
	g_mouse_was_down = 0;
	for (int i = 0; i < MAX_BALLS; i++) {
		g_color_used[i] = 0;
	}
	reset_clacks();
}

nk_bool do_user_gui(struct nk_context* ctx, nk_bool is_playing)
{
	nk_bool need = nk_false;
	int nballs = g_n;
	float old_speed = g_speed;
	float old_radius = g_radius;

	nk_layout_row_dynamic(ctx, 0, 1);
	nk_label(ctx, "Table", NK_TEXT_CENTERED);
	if (nk_property_int(ctx, "Balls:", MIN_BALLS, &nballs, MAX_BALLS, 1, 1)) {
		set_ball_count(nballs);
		need = nk_true;
	}
	need |= nk_property_float(ctx, "Speed:", MIN_SPEED, &g_speed, MAX_SPEED, 0.05f, 0.01f);
	need |= nk_property_float(ctx, "Friction:", MIN_FRICTION, &g_friction, MAX_FRICTION, 0.05f, 0.01f);
	need |= nk_property_float(ctx, "Radius:", MIN_RADIUS, &g_radius, MAX_RADIUS, 0.005f, 0.001f);
	if (g_speed != old_speed) {
		apply_speed();
	}
	if (g_radius != old_radius) {
		clamp_to_table();
		ball_collisions();
	}
	if (nk_button_label(ctx, "Shuffle")) {
		shuffle_balls();
		need = nk_true;
	}
	nk_layout_row_dynamic(ctx, ctx->style.font->height * 2.5f, 1);
	nk_label_wrap(ctx, "Drag on the table and release to add a ball (slingshot).");
	nk_layout_row_dynamic(ctx, 0, 1);

	nk_label(ctx, "Clack", NK_TEXT_CENTERED);
	need |= nk_property_float(ctx, "Gain:", MIN_GAIN, &g_gain, MAX_GAIN, 0.05f, 0.01f);
	need |= nk_property_float(ctx, "Hardness:", MIN_HARDNESS, &g_hardness, MAX_HARDNESS, 0.05f, 0.01f);
	need |= nk_property_float(ctx, "Decay:", MIN_DECAY, &g_decay, MAX_DECAY, 0.05f, 0.01f);
	need |= nk_property_float(ctx, "Pitch (st):", MIN_PITCH_ST, &g_pitch_st, MAX_PITCH_ST, 1.0f, 0.05f);
	if (nk_button_label(ctx, "Reset sound")) {
		reset_sound();
		need = nk_true;
	}

	(void)is_playing;
	return need;
}

static vec3 shade_table(vec2 fragCoord)
{
	float inv_y = 1.0f / iResolution.y;
	vec2 p = fragCoord * inv_y;
	float px = inv_y;

	vec3 col = vec3(0.06f, 0.40f, 0.18f);
	vec2 uv = fragCoord / iResolution.xy();
	float vig = 1.0f - 0.22f * dot(uv - vec2(0.5f), uv - vec2(0.5f));
	col *= vig;

	float r = g_radius;
	for (int i = 0; i < g_n; i++) {
		vec2 d = p - g_balls[i].p;
		float dist = length(d);
		float fill = smoothstep(r + px, r - px, dist);
		if (!(fill > 0.0f)) {
			continue;
		}
		float shade = 0.72f + 0.28f * clamp(0.5f - 0.9f * d.x / r + 0.7f * d.y / r, 0.0f, 1.0f);
		vec3 bc = g_balls[i].color * shade;
		float spec = smoothstep(r * 0.38f, r * 0.08f, length(d - vec2(-0.32f, 0.32f) * r));
		bc += vec3(spec * 0.45f);
		col = mix(col, bc, fill);
	}
	return col;
}

static void put_px(pix_t* lastrow, int w, int h, int x, int y, pix_t c)
{
	if (x < 0 || y < 0 || x >= w || y >= h) {
		return;
	}
	lastrow[-y * w + x] = c;
}

static void draw_drag_line(pix_t* lastrow, int w, int h)
{
	vec2 a = vec2(iMouse.z, -iMouse.w);
	vec2 b = iMouse.xy();
	vec2 d = b - a;
	float len = length(d);
	if (len > DRAG_PIXELS_FOR_MAX_SPEED) {
		b = a + d * (DRAG_PIXELS_FOR_MAX_SPEED / len);
	}
	int x0 = (int)a.x;
	int y0 = (int)a.y;
	int x1 = (int)b.x;
	int y1 = (int)b.y;
	pix_t black = pack_clamp_rgba8(0.0f, 0.0f, 0.0f, 1.0f);

	int dx = x1 - x0;
	if (dx < 0) {
		dx = -dx;
	}
	int dy = y1 - y0;
	if (dy < 0) {
		dy = -dy;
	}
	dy = -dy;
	int sx = (x0 < x1) ? 1 : -1;
	int sy = (y0 < y1) ? 1 : -1;
	int err = dx + dy;

	for (;;) {
		put_px(lastrow, w, h, x0, y0, black);
		put_px(lastrow, w, h, x0 + 1, y0, black);
		put_px(lastrow, w, h, x0, y0 + 1, black);
		if (x0 == x1 && y0 == y1) {
			break;
		}
		int e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

void mainImage(vec4* fragColor, vec2 fragCoord)
{
	maybe_simulate(fragCoord);
	*fragColor = vec4(shade_table(fragCoord), 1.0f);
}

void mainFrame(pix_t* framebuffer, int w, int h)
{
	maybe_simulate(vec2(0.5f, 0.5f));

	pix_t* lastrow = framebuffer + (h - 1) * w;
SW_PARALLEL_FOR
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			vec2 fragCoord;
			fragCoord.x = x + 0.5f;
			fragCoord.y = y + 0.5f;
			vec3 col = shade_table(fragCoord);
			lastrow[-y * w + x] = pack_clamp_rgba8(col.x, col.y, col.z, 1.0f);
		}
	}

	if (!u_is_icon && iMouse.z > 0.0f) {
		draw_drag_line(lastrow, w, h);
	}
}

static float clack_at(float t)
{
	if (t < 0.0f) {
		return 0.0f;
	}

	float pitch = pow(2.0f, g_pitch_st / 12.0f);
	float pos = t * (float)CLACK_SR * pitch;
	int i = (int)pos;
	if (i < 0 || i >= CLACK_N - 1) {
		return 0.0f;
	}

	float frac = pos - (float)i;
	float a = (float)CLACK_PCM[i] * (1.0f / 32767.0f);
	float b = (float)CLACK_PCM[i + 1] * (1.0f / 32767.0f);
	float s = a + (b - a) * frac;

	float hard = mix(0.55f, 1.35f, g_hardness);
	s *= hard * g_gain;
	if (g_decay > 1.0f) {
		s *= exp(-28.0f * (g_decay - 1.0f) * t);
	}
	return s;
}

vec2 mainSound(int samp, float time)
{
	(void)samp;

	static int seen[MAX_CLACKS];
	static float start[MAX_CLACKS];
	static int reset_seen = -1;
	static float prev_time = 0.0f;
	if (reset_seen != g_audio_reset || time < prev_time) {
		reset_seen = g_audio_reset;
		for (int i = 0; i < MAX_CLACKS; i++) {
			seen[i] = g_clack_seq[i];
			start[i] = -1000.0f;
		}
	}
	prev_time = time;

	float hit = 0.0f;
	for (int i = 0; i < MAX_CLACKS; i++) {
		if (g_clack_seq[i] != seen[i]) {
			seen[i] = g_clack_seq[i];
			start[i] = time;
		}
		hit += clack_at(time - start[i]);
	}
	hit = clamp(hit, -1.0f, 1.0f);
	return vec2(hit, hit);
}

}
