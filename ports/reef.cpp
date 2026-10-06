// GLM
//
// Port of Xor (@XorDev) "Reef"
// Tweet: https://x.com/XorDev/status/2021258388038943162
// FragCoord (same shader, formatted): https://fragcoord.xyz/s/rk14ca7w
//
// Original twigl GEEKEST source:
//
// for(float z,d,i;i++<5e1;z+=d,o+=(.9+sin(i*.1-vec4(6,1,2,0)))/d/d/z+d*z/vec4(4,2,1,0)){vec3 p=z*normalize(FC.rgb*2.-r.xyx);for(d=0.;d++<9.;)p+=.4*sin(p.yzx*d-z+t+i)/d+.5;d=length(vec4(abs(p.y+p.z*.5),sin(p-z)/7.))/(4.+z*z/1e2);}o=tanh(o/2e3);

#include "glm_shader_workshop_common.h"
using namespace glm;

#include "nuklear_shader_workshop.h"

// Fancy is the original tweet: 50 steps, 9 warp octaves.
static const int MIN_STEPS = 4;
static const int MAX_STEPS = 160;
static const int DFLT_STEPS = 50;
static const int FAST_STEPS = 16;
static const int MED_STEPS = 32;

static const int MIN_OCTAVES = 1;
static const int MAX_OCTAVES = 16;
static const int DFLT_OCTAVES = 9;
static const int FAST_OCTAVES = 4;
static const int MED_OCTAVES = 6;

static const float MIN_WARP_AMP = 0.0f;
static const float MAX_WARP_AMP = 2.0f;
static const float DFLT_WARP_AMP = 0.4f;

static const float MIN_WARP_PUSH = 0.0f;
static const float MAX_WARP_PUSH = 2.0f;
static const float DFLT_WARP_PUSH = 0.5f;

static const float MIN_TIME = 0.0f;
static const float MAX_TIME = 8.0f;
static const float DFLT_TIME = 1.0f;

static const float MIN_FOLD = 0.0f;
static const float MAX_FOLD = 2.0f;
static const float DFLT_FOLD = 0.5f;

static const float MIN_RIPPLE = 1.0f;
static const float MAX_RIPPLE = 28.0f;
static const float DFLT_RIPPLE = 7.0f;

static const float MIN_DE_BASE = 0.5f;
static const float MAX_DE_BASE = 20.0f;
static const float DFLT_DE_BASE = 4.0f;

static const float MIN_DE_CURVE = 10.0f;
static const float MAX_DE_CURVE = 500.0f;
static const float DFLT_DE_CURVE = 100.0f;

static const float MIN_GLOW = 0.0f;
static const float MAX_GLOW = 2.0f;
static const float DFLT_GLOW = 0.9f;

static const float MIN_COLOR_RATE = 0.0f;
static const float MAX_COLOR_RATE = 1.0f;
static const float DFLT_COLOR_RATE = 0.1f;

static const float MIN_HUE = -8.0f;
static const float MAX_HUE = 8.0f;
static const float DFLT_HUE = 0.0f;

static const float MIN_DEPTH = 0.0f;
static const float MAX_DEPTH = 4.0f;
static const float DFLT_DEPTH = 1.0f;

static const float MIN_TONEMAP = 50.0f;
static const float MAX_TONEMAP = 20000.0f;
static const float DFLT_TONEMAP = 2000.0f;

// No sine opcode on SSE or AVX. _mm_sin_ps is Intel SVML, not hardware.
// SSE4.1 evaluates the Cephes sinf polynomial on 4 lanes; AVX2 on 8.
// Debug builds skip this and keep the scalar mainFrame.
#if !defined(DEBUG) && defined(__AVX2__)
#include <immintrin.h>
struct ReefSimd {
	static constexpr int N = 8;
	using f = __m256;
	using i = __m256i;

	static f set1(float s) { return _mm256_set1_ps(s); }
	static f ramp(float base)
	{
		return _mm256_add_ps(_mm256_set1_ps(base), _mm256_set_ps(7.f, 6.f, 5.f, 4.f, 3.f, 2.f, 1.f, 0.f));
	}
	static f add(f a, f b) { return _mm256_add_ps(a, b); }
	static f sub(f a, f b) { return _mm256_sub_ps(a, b); }
	static f mul(f a, f b) { return _mm256_mul_ps(a, b); }
	static f div(f a, f b) { return _mm256_div_ps(a, b); }
	static f madd(f a, f b, f c)
	{
#if defined(__FMA__)
		return _mm256_fmadd_ps(a, b, c);
#else
		return _mm256_add_ps(_mm256_mul_ps(a, b), c);
#endif
	}
	static f sqrt(f a) { return _mm256_sqrt_ps(a); }
	static f and_(f a, f b) { return _mm256_and_ps(a, b); }
	static f andnot(f a, f b) { return _mm256_andnot_ps(a, b); }
	static f xor_(f a, f b) { return _mm256_xor_ps(a, b); }
	static f sign_mask() { return _mm256_castsi256_ps(_mm256_set1_epi32(int(0x80000000u))); }
	static f inv_sign() { return _mm256_castsi256_ps(_mm256_set1_epi32(0x7fffffff)); }
	static f abs(f a) { return _mm256_and_ps(a, inv_sign()); }
	static i set1_i(int s) { return _mm256_set1_epi32(s); }
	static i zero_i() { return _mm256_setzero_si256(); }
	static i cvttps(f a) { return _mm256_cvttps_epi32(a); }
	static f cvtepi(i a) { return _mm256_cvtepi32_ps(a); }
	static i add_i(i a, i b) { return _mm256_add_epi32(a, b); }
	static i and_i(i a, i b) { return _mm256_and_si256(a, b); }
	static i cmpeq_i(i a, i b) { return _mm256_cmpeq_epi32(a, b); }
	static i slli(i a, int n) { return _mm256_slli_epi32(a, n); }
	static f cast_i(i a) { return _mm256_castsi256_ps(a); }
	static void store(float* p, f a) { _mm256_storeu_ps(p, a); }
};
#elif !defined(DEBUG) && defined(__SSE4_1__)
#include <smmintrin.h>
struct ReefSimd {
	static constexpr int N = 4;
	using f = __m128;
	using i = __m128i;

	static f set1(float s) { return _mm_set1_ps(s); }
	static f ramp(float base)
	{
		return _mm_add_ps(_mm_set1_ps(base), _mm_set_ps(3.f, 2.f, 1.f, 0.f));
	}
	static f add(f a, f b) { return _mm_add_ps(a, b); }
	static f sub(f a, f b) { return _mm_sub_ps(a, b); }
	static f mul(f a, f b) { return _mm_mul_ps(a, b); }
	static f div(f a, f b) { return _mm_div_ps(a, b); }
	static f madd(f a, f b, f c) { return _mm_add_ps(_mm_mul_ps(a, b), c); }
	static f sqrt(f a) { return _mm_sqrt_ps(a); }
	static f and_(f a, f b) { return _mm_and_ps(a, b); }
	static f andnot(f a, f b) { return _mm_andnot_ps(a, b); }
	static f xor_(f a, f b) { return _mm_xor_ps(a, b); }
	static f sign_mask() { return _mm_castsi128_ps(_mm_set1_epi32(int(0x80000000u))); }
	static f inv_sign() { return _mm_castsi128_ps(_mm_set1_epi32(0x7fffffff)); }
	static f abs(f a) { return _mm_and_ps(a, inv_sign()); }
	static i set1_i(int s) { return _mm_set1_epi32(s); }
	static i zero_i() { return _mm_setzero_si128(); }
	static i cvttps(f a) { return _mm_cvttps_epi32(a); }
	static f cvtepi(i a) { return _mm_cvtepi32_ps(a); }
	static i add_i(i a, i b) { return _mm_add_epi32(a, b); }
	static i and_i(i a, i b) { return _mm_and_si128(a, b); }
	static i cmpeq_i(i a, i b) { return _mm_cmpeq_epi32(a, b); }
	static i slli(i a, int n) { return _mm_slli_epi32(a, n); }
	static f cast_i(i a) { return _mm_castsi128_ps(a); }
	static void store(float* p, f a) { _mm_storeu_ps(p, a); }
};
#endif

#if !defined(DEBUG) && (defined(__AVX2__) || defined(__SSE4_1__))
// Cephes sinf: reduce by 4/pi, then a minimax polynomial on [-pi/4, pi/4].
// Separate mul/add in the reduction keeps the extra bits. Do not FMA those.
[[gnu::always_inline]] static inline ReefSimd::f reef_sin(ReefSimd::f x)
{
	using f = ReefSimd::f;
	using i = ReefSimd::i;

	f sign_bit = ReefSimd::and_(x, ReefSimd::sign_mask());
	x = ReefSimd::and_(x, ReefSimd::inv_sign());

	f y = ReefSimd::mul(x, ReefSimd::set1(1.27323954473516f));
	i quad = ReefSimd::cvttps(y);
	quad = ReefSimd::add_i(quad, ReefSimd::set1_i(1));
	quad = ReefSimd::and_i(quad, ReefSimd::set1_i(~1));
	y = ReefSimd::cvtepi(quad);

	i swap_i = ReefSimd::slli(ReefSimd::and_i(quad, ReefSimd::set1_i(4)), 29);
	i poly_i = ReefSimd::cmpeq_i(ReefSimd::and_i(quad, ReefSimd::set1_i(2)), ReefSimd::zero_i());
	sign_bit = ReefSimd::xor_(sign_bit, ReefSimd::cast_i(swap_i));
	f poly_mask = ReefSimd::cast_i(poly_i);

	x = ReefSimd::add(x, ReefSimd::mul(y, ReefSimd::set1(-0.78515625f)));
	x = ReefSimd::add(x, ReefSimd::mul(y, ReefSimd::set1(-2.4187564849853515625e-4f)));
	x = ReefSimd::add(x, ReefSimd::mul(y, ReefSimd::set1(-3.77489497744594108e-8f)));

	f z = ReefSimd::mul(x, x);

	f yc = ReefSimd::set1(2.443315711809948e-5f);
	yc = ReefSimd::madd(yc, z, ReefSimd::set1(-1.388731625493765e-3f));
	yc = ReefSimd::madd(yc, z, ReefSimd::set1(4.166664568298827e-2f));
	yc = ReefSimd::mul(yc, z);
	yc = ReefSimd::mul(yc, z);
	yc = ReefSimd::sub(yc, ReefSimd::mul(z, ReefSimd::set1(0.5f)));
	yc = ReefSimd::add(yc, ReefSimd::set1(1.0f));

	f ys = ReefSimd::set1(-1.9515295891e-4f);
	ys = ReefSimd::madd(ys, z, ReefSimd::set1(8.3321608736e-3f));
	ys = ReefSimd::madd(ys, z, ReefSimd::set1(-1.6666654611e-1f));
	ys = ReefSimd::mul(ys, z);
	ys = ReefSimd::madd(ys, x, x);

	f picked = ReefSimd::add(ReefSimd::andnot(poly_mask, yc), ReefSimd::and_(poly_mask, ys));
	return ReefSimd::xor_(picked, sign_bit);
}
#endif

extern "C" {

static int g_steps = DFLT_STEPS;
static int g_octaves = DFLT_OCTAVES;
static float g_warp_amp = DFLT_WARP_AMP;
static float g_warp_push = DFLT_WARP_PUSH;
static float g_time_speed = DFLT_TIME;
static float g_fold = DFLT_FOLD;
static float g_ripple = DFLT_RIPPLE;
static float g_de_base = DFLT_DE_BASE;
static float g_de_curve = DFLT_DE_CURVE;
static float g_glow = DFLT_GLOW;
static float g_color_rate = DFLT_COLOR_RATE;
static float g_hue = DFLT_HUE;
static float g_depth_tint = DFLT_DEPTH;
static float g_tonemap = DFLT_TONEMAP;

static void apply_quality_preset(int q)
{
	if (q <= 0) {
		g_steps = FAST_STEPS;
		g_octaves = FAST_OCTAVES;
	} else if (q == 1) {
		g_steps = MED_STEPS;
		g_octaves = MED_OCTAVES;
	} else {
		g_steps = DFLT_STEPS;
		g_octaves = DFLT_OCTAVES;
	}
}

static void reset_defaults(void)
{
	g_warp_amp = DFLT_WARP_AMP;
	g_warp_push = DFLT_WARP_PUSH;
	g_time_speed = DFLT_TIME;
	g_fold = DFLT_FOLD;
	g_ripple = DFLT_RIPPLE;
	g_de_base = DFLT_DE_BASE;
	g_de_curve = DFLT_DE_CURVE;
	g_glow = DFLT_GLOW;
	g_color_rate = DFLT_COLOR_RATE;
	g_hue = DFLT_HUE;
	g_depth_tint = DFLT_DEPTH;
	g_tonemap = DFLT_TONEMAP;
	apply_quality_preset(2);
}

void set_channels(texture_settings* ts)
{
	(void)ts;
	reset_defaults();
}

nk_bool do_user_gui(struct nk_context* ctx, nk_bool is_playing)
{
	nk_bool need = nk_false;

	nk_layout_row_dynamic(ctx, 0, 1);
	nk_label(ctx, "Reef by @XorDev", NK_TEXT_CENTERED);
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
	nk_layout_row_dynamic(ctx, 0, 1);
	nk_label_wrap(ctx, "Presets only change step and octave counts. Use Re-initialize Shader for a full reset.");

	if (nk_tree_push(ctx, NK_TREE_TAB, "Warp", NK_MINIMIZED)) {
		nk_layout_row_dynamic(ctx, 0, 1);
		need |= nk_property_int(ctx, "Max steps:", MIN_STEPS, &g_steps, MAX_STEPS, 1, 1);
		need |= nk_property_int(ctx, "Warp octaves:", MIN_OCTAVES, &g_octaves, MAX_OCTAVES, 1, 1);

		need |= nk_property_float(ctx, "Warp amount:", MIN_WARP_AMP, &g_warp_amp, MAX_WARP_AMP, 0.02f, 0.005f);
		need |= nk_property_float(ctx, "Warp push:", MIN_WARP_PUSH, &g_warp_push, MAX_WARP_PUSH, 0.02f, 0.005f);
		need |= nk_property_float(ctx, "Time speed:", MIN_TIME, &g_time_speed, MAX_TIME, 0.1f, 0.02f);
		nk_tree_pop(ctx);
	}

	if (nk_tree_push(ctx, NK_TREE_TAB, "Surface", NK_MINIMIZED)) {
		nk_layout_row_dynamic(ctx, 0, 1);
		need |= nk_property_float(ctx, "Fold:", MIN_FOLD, &g_fold, MAX_FOLD, 0.01f, 0.005f);
		need |= nk_property_float(ctx, "Ripple div:", MIN_RIPPLE, &g_ripple, MAX_RIPPLE, 0.25f, 0.05f);
		need |= nk_property_float(ctx, "DE base:", MIN_DE_BASE, &g_de_base, MAX_DE_BASE, 0.25f, 0.05f);
		need |= nk_property_float(ctx, "DE curve:", MIN_DE_CURVE, &g_de_curve, MAX_DE_CURVE, 5.0f, 1.0f);
		nk_layout_row_dynamic(ctx, ctx->style.font->height * 3.5f, 1);
		nk_label_wrap(ctx, "DE base is the near-field divisor. DE curve is the z squared term. Together they set how fast steps shrink with distance.");
		nk_tree_pop(ctx);
	}

	if (nk_tree_push(ctx, NK_TREE_TAB, "Color", NK_MINIMIZED)) {
		nk_layout_row_dynamic(ctx, 0, 1);
		need |= nk_property_float(ctx, "Glow:", MIN_GLOW, &g_glow, MAX_GLOW, 0.05f, 0.01f);
		need |= nk_property_float(ctx, "Color rate:", MIN_COLOR_RATE, &g_color_rate, MAX_COLOR_RATE, 0.01f, 0.005f);
		need |= nk_property_float(ctx, "Hue shift:", MIN_HUE, &g_hue, MAX_HUE, 0.05f, 0.01f);
		need |= nk_property_float(ctx, "Depth tint:", MIN_DEPTH, &g_depth_tint, MAX_DEPTH, 0.05f, 0.01f);
		need |= nk_property_float(ctx, "Tonemap:", MIN_TONEMAP, &g_tonemap, MAX_TONEMAP, 50.0f, 10.0f);
		nk_layout_row_dynamic(ctx, ctx->style.font->height * 2.5f, 1);
		nk_label_wrap(ctx, "Hue shift adds to the original phase (6, 1, 2). Depth tint scales the d*z fog.");
		nk_tree_pop(ctx);
	}

	(void)is_playing;
	return need;
}

void mainImage(vec4* fragColor, vec2 fragCoord)
{
	vec2 r = iResolution.xy();
	float t = iTime * g_time_speed;
	vec4 o = vec4(0.0f);

	// FC.rgb*2.-r.xyx  (twigl gl_FragCoord.z is ~0.5 on a fullscreen pass)
	vec3 rd = normalize(vec3(fragCoord * 2.0f - r, 1.0f - r.x));

	// Original condition is i++<steps, so the body sees i = 1..steps.
	float z = 0.0f;
	for (int n = 0; n < g_steps; n++) {
		float i = (float)(n + 1);
		vec3 p = z * rd;

		// Domain-warp turbulence. Original counter is d++<octaves, so d = 1..octaves.
		for (int k = 1; k <= g_octaves; k++) {
			float od = (float)k;
			p += g_warp_amp * sin(p.yzx() * od - z + t + i) / od + g_warp_push;
		}

		// Distance estimate. Color runs after z += d, same as the golfed increment.
		float d = length(vec4(abs(p.y + p.z * g_fold), sin(p - z) / g_ripple)) / (g_de_base + z * z / g_de_curve);
		z += d;

		vec4 phase = vec4(6.0f, 1.0f, 2.0f, 0.0f) + g_hue;
		vec4 glow = g_glow + sin(i * g_color_rate - phase);
		vec4 depth = vec4(d * z / 4.0f, d * z / 2.0f, d * z, 0.0f) * g_depth_tint;
		o += glow / (d * d * z) + depth;
	}

	o = tanh(o / g_tonemap);
	o.w = 1.0f;
	*fragColor = o;
}

#if !defined(DEBUG) && (defined(__AVX2__) || defined(__SSE4_1__))
static void reef_mainframe_simd(pix_t* framebuffer, int w, int h)
{
	int steps = g_steps;
	int octaves = g_octaves;
	float t = iTime * g_time_speed;
	float amp_s = g_warp_amp;
	float push_s = g_warp_push;
	float fold_s = g_fold;
	float ripple_s = g_ripple;
	float de_base_s = g_de_base;
	float de_curve_s = g_de_curve;
	float depth_r = 0.25f * g_depth_tint;
	float depth_g = 0.5f * g_depth_tint;
	float depth_b = g_depth_tint;
	float tonemap = g_tonemap;
	float res_x = iResolution.x;
	float res_y = iResolution.y;
	float nz_s = 1.0f - res_x;
	float phase_r = 6.0f + g_hue;
	float phase_g = 1.0f + g_hue;
	float phase_b = 2.0f + g_hue;

	// Glow depends only on the step index. One table for the whole frame.
	float glow_r[MAX_STEPS];
	float glow_g[MAX_STEPS];
	float glow_b[MAX_STEPS];
	for (int n = 0; n < steps; n++) {
		float fi = (float)(n + 1) * g_color_rate;
		glow_r[n] = g_glow + std::sin(fi - phase_r);
		glow_g[n] = g_glow + std::sin(fi - phase_g);
		glow_b[n] = g_glow + std::sin(fi - phase_b);
	}

	pix_t* lastrow = framebuffer + (h - 1) * w;

	// x strides by the SIMD width, so this is not the unit-step SW_PARALLEL_FOR loop.
	#pragma omp parallel for collapse(2) schedule(runtime) num_threads(num_shader_threads)
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; x += ReefSimd::N) {
			using f = ReefSimd::f;

			f amp = ReefSimd::set1(amp_s);
			f push = ReefSimd::set1(push_s);
			f fold = ReefSimd::set1(fold_s);
			f ripple = ReefSimd::set1(ripple_s);
			f de_base = ReefSimd::set1(de_base_s);
			f de_curve = ReefSimd::set1(de_curve_s);
			f dep_r = ReefSimd::set1(depth_r);
			f dep_g = ReefSimd::set1(depth_g);
			f dep_b = ReefSimd::set1(depth_b);

			f fx = ReefSimd::ramp((float)x + 0.5f);
			f nx = ReefSimd::sub(ReefSimd::mul(fx, ReefSimd::set1(2.0f)), ReefSimd::set1(res_x));
			f ny = ReefSimd::set1(2.0f * ((float)y + 0.5f) - res_y);
			f nz = ReefSimd::set1(nz_s);
			f len2 = ReefSimd::madd(nx, nx, ReefSimd::madd(ny, ny, ReefSimd::mul(nz, nz)));
			f invlen = ReefSimd::div(ReefSimd::set1(1.0f), ReefSimd::sqrt(len2));
			f rdx = ReefSimd::mul(nx, invlen);
			f rdy = ReefSimd::mul(ny, invlen);
			f rdz = ReefSimd::mul(nz, invlen);

			f z = ReefSimd::set1(0.0f);
			f ox = ReefSimd::set1(0.0f);
			f oy = ReefSimd::set1(0.0f);
			f oz = ReefSimd::set1(0.0f);

			for (int n = 0; n < steps; n++) {
				float fi = (float)(n + 1);
				f px = ReefSimd::mul(z, rdx);
				f py = ReefSimd::mul(z, rdy);
				f pz = ReefSimd::mul(z, rdz);
				f tfi = ReefSimd::set1(t + fi);

				for (int k = 1; k <= octaves; k++) {
					f odf = ReefSimd::set1((float)k);
					f scale = ReefSimd::div(amp, odf);
					f argx = ReefSimd::add(ReefSimd::sub(ReefSimd::mul(py, odf), z), tfi);
					f argy = ReefSimd::add(ReefSimd::sub(ReefSimd::mul(pz, odf), z), tfi);
					f argz = ReefSimd::add(ReefSimd::sub(ReefSimd::mul(px, odf), z), tfi);
					px = ReefSimd::add(px, ReefSimd::madd(reef_sin(argx), scale, push));
					py = ReefSimd::add(py, ReefSimd::madd(reef_sin(argy), scale, push));
					pz = ReefSimd::add(pz, ReefSimd::madd(reef_sin(argz), scale, push));
				}

				f fold_term = ReefSimd::abs(ReefSimd::madd(pz, fold, py));
				f sx = ReefSimd::div(reef_sin(ReefSimd::sub(px, z)), ripple);
				f sy = ReefSimd::div(reef_sin(ReefSimd::sub(py, z)), ripple);
				f sz = ReefSimd::div(reef_sin(ReefSimd::sub(pz, z)), ripple);
				f len = ReefSimd::sqrt(ReefSimd::madd(fold_term, fold_term,
					ReefSimd::madd(sx, sx, ReefSimd::madd(sy, sy, ReefSimd::mul(sz, sz)))));
				f denom = ReefSimd::madd(z, ReefSimd::div(z, de_curve), de_base);
				f d = ReefSimd::div(len, denom);
				z = ReefSimd::add(z, d);

				f dz = ReefSimd::mul(d, z);
				f inv_ddz = ReefSimd::div(ReefSimd::set1(1.0f), ReefSimd::mul(d, dz));
				ox = ReefSimd::add(ox, ReefSimd::madd(ReefSimd::set1(glow_r[n]), inv_ddz, ReefSimd::mul(dz, dep_r)));
				oy = ReefSimd::add(oy, ReefSimd::madd(ReefSimd::set1(glow_g[n]), inv_ddz, ReefSimd::mul(dz, dep_g)));
				oz = ReefSimd::add(oz, ReefSimd::madd(ReefSimd::set1(glow_b[n]), inv_ddz, ReefSimd::mul(dz, dep_b)));
			}

			alignas(32) float rx[8] = {};
			alignas(32) float ry[8] = {};
			alignas(32) float rz[8] = {};
			ReefSimd::store(rx, ox);
			ReefSimd::store(ry, oy);
			ReefSimd::store(rz, oz);

			int nstore = ReefSimd::N;
			if (x + nstore > w) {
				nstore = w - x;
			}
			float inv_tm = 1.0f / tonemap;
			for (int lane = 0; lane < nstore; lane++) {
				float r = std::tanh(rx[lane] * inv_tm);
				float g = std::tanh(ry[lane] * inv_tm);
				float b = std::tanh(rz[lane] * inv_tm);
				lastrow[-y * w + (x + lane)] = pack_clamp_rgba8(r, g, b, 1.0f);
			}
		}
	}
}
#endif

void mainFrame(pix_t* framebuffer, int w, int h)
{
#if !defined(DEBUG) && (defined(__AVX2__) || defined(__SSE4_1__))
	reef_mainframe_simd(framebuffer, w, h);
#else
	pix_t* lastrow = framebuffer + (h - 1) * w;

SW_PARALLEL_FOR
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			vec2 fragCoord;
			vec4 fragColor;

			fragCoord.x = x + 0.5f;
			fragCoord.y = y + 0.5f;

			mainImage(&fragColor, fragCoord);
			lastrow[-y * w + x] = pack_clamp_rgba8(fragColor.x, fragColor.y, fragColor.z, fragColor.w);
		}
	}
#endif
}

}
