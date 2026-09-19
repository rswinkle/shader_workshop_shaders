// RSW
#include "rsw_shader_workshop_common.h"
#include "nuklear_shader_workshop.h"

using namespace rsw;


// Credit
// https://www.shadertoy.com/view/scS3Wm
// by diatribes

#define T (sin(iTime*.6f)*64.f+iTime*2e2f)
#define P(z) (vec3(cos((z)*.015f)*16.f+cos((z) * .006f)  *64.f, \
                   cos((z)*.011f)*24.f+cos((z) * .009f) * 32.f, (z)))
#define R(a) mat2(cos(a+vec4(0,33,11,0)))
#define N normalize

#define DFLT_MARCH_STEPS 128
#define DFLT_REFLECT_STEPS 40
#define DFLT_STEP_RELAX 0.8f
#define DFLT_EXPOSURE 1.0f
#define DFLT_TONEMAP_E6 6.0f

#define MAX_MARCH_STEPS 256
#define MAX_REFLECT_STEPS 128
#define MAX_STEP_RELAX 1.5f
#define MAX_EXPOSURE 20.0f
#define MAX_TONEMAP_E6 50.0f

static int g_march_steps = DFLT_MARCH_STEPS;
static int g_reflect_steps = DFLT_REFLECT_STEPS;
static float g_step_relax = DFLT_STEP_RELAX;
static float g_exposure = DFLT_EXPOSURE;
static float g_tonemap_e6 = DFLT_TONEMAP_E6;
static nk_bool g_do_normals = nk_true;
static nk_bool g_do_reflect = nk_true;
static nk_bool g_do_ripples = nk_true;

static void apply_quality_preset(int q)
{
	if (q <= 0) {
		g_march_steps = 32;
		g_reflect_steps = 0;
		g_do_normals = nk_false;
		g_do_reflect = nk_false;
	} else if (q == 1) {
		g_march_steps = 64;
		g_reflect_steps = 16;
		g_do_normals = nk_true;
		g_do_reflect = nk_true;
	} else {
		g_march_steps = DFLT_MARCH_STEPS;
		g_reflect_steps = DFLT_REFLECT_STEPS;
		g_do_normals = nk_true;
		g_do_reflect = nk_true;
	}
}

static void reset_defaults(void)
{
	g_step_relax = DFLT_STEP_RELAX;
	g_exposure = DFLT_EXPOSURE;
	g_tonemap_e6 = DFLT_TONEMAP_E6;
	g_do_ripples = nk_true;
	apply_quality_preset(2);
}

float boxen(vec3 p) {
	
	p = abs(fract(p/4e1f)*4e1f - 2e1f) - 2.f;
	return min(p.x, min(p.y, p.z));

}

float map(vec3 p, vec4& lights) {
	vec3 q = P(p.z);
	float m, g = q.y-p.y + 6.f;

	m = boxen(p);

	//p.xy -= q.xy;
	p.x -= q.x;
	p.y -= q.y;

	// squiggly line along z
	float red,blue;
	float e = min(red=length(p.xy() -   sin(p.y / 12.f + vec2(5.f, 1.f))*12.f) - 1.f,
	              blue=length(p.xy() -  sin(p.y / 12.f + vec2(0, 1.f))*12.f) - 1.f);

	lights += vec4(2,1e1f,1e1f,0)/(.1f+abs(red)/1e1f);
	lights += vec4(1e1f,2,1e1f,0)/(.1f+abs(blue)/1e1f);

	p = abs(p);
	
	float tex = 0.f;
	if (g_do_ripples) {
		tex = abs(length(sin(p*cos(p.yzx()/3e1f)*4.f)/(p*4.f)));
	}
	float tun = min(64.f-p.x - p.y + m, 32.f-p.y - m);


	float d = max(min(m, g), tun)-tex;
	return min(e, d);
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
	nk_label(ctx, "Hyperkart", NK_TEXT_CENTERED);
	nk_label(ctx, "Cost ~ pixels x (march + 4*normals + reflect)", NK_TEXT_LEFT);

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
	nk_label_wrap(ctx, "Presets change march/reflect steps and normals. Fancy is the original (128 / 40).");
	nk_layout_row_dynamic(ctx, 0, 1);
	nk_label_wrap(ctx, "Tonemap is scaled so Fast/Medium stay in the same ballpark as Fancy. Use Exposure to taste. Re-initialize Shader for a full reset.");

	if (nk_tree_push(ctx, NK_TREE_TAB, "Features", NK_MINIMIZED)) {
		nk_layout_row_dynamic(ctx, 0, 1);
		need |= nk_checkbox_label(ctx, "Normals (4 extra map)", &g_do_normals);
		need |= nk_checkbox_label(ctx, "Reflections", &g_do_reflect);
		need |= nk_checkbox_label(ctx, "Surface ripples", &g_do_ripples);
		nk_label_wrap(ctx, "Ripples are the sin/cos displacement. Off is cheaper and smoother walls. Reflections need normals.");
		nk_tree_pop(ctx);
	}

	if (nk_tree_push(ctx, NK_TREE_TAB, "March", NK_MINIMIZED)) {
		nk_layout_row_dynamic(ctx, 0, 1);
		need |= nk_property_int(ctx, "March steps:", 1, &g_march_steps, MAX_MARCH_STEPS, 1, 1);
		need |= nk_property_int(ctx, "Reflect steps:", 0, &g_reflect_steps, MAX_REFLECT_STEPS, 1, 1);
		need |= nk_property_float(ctx, "Step relax:", 0.2f, &g_step_relax, MAX_STEP_RELAX, 0.05f, 0.01f);
		nk_tree_pop(ctx);
	}

	if (nk_tree_push(ctx, NK_TREE_TAB, "Tonemap", NK_MINIMIZED)) {
		nk_layout_row_dynamic(ctx, 0, 1);
		need |= nk_property_float(ctx, "Exposure:", 0.05f, &g_exposure, MAX_EXPOSURE, 0.05f, 0.01f);
		need |= nk_property_float(ctx, "Tonemap (e6):", 0.1f, &g_tonemap_e6, MAX_TONEMAP_E6, 0.5f, 0.05f);
		nk_label_wrap(ctx, "Original is Exposure 1, Tonemap 6. Divisor is 6e6 times a quality energy term (march^2 and bounce).");
		nk_tree_pop(ctx);
	}

	(void)is_playing;
	return need;
}

void mainImage(vec4* fragColor, vec2 u)
{
	float s = 0.f;
	float d = 0.f;
	vec3  r = iResolution;
	
	u = (u-r.xy()/2.f)/r.y;
	
	u.y -=.2f;
	vec4 o = vec4(0);
	vec4 lights = vec4(0);
	vec3  p = P(T),ro=p,
		  Z = N( P(T+1e1f) - p),
		  /*X = N(vec3(Z.z,0,-Z)), */ // I assume GLSL takes the first element of -Z in this case
		  X = N(vec3(Z.z,0,-Z.x)),
		  D = N(vec3(R(sin(T*.005f)*.4f)*u, 1)
			 * mat3(-X, cross(X, Z), Z));

	const int march_steps = g_march_steps;
	const int reflect_steps = g_reflect_steps;
	const float step_relax = g_step_relax;
	const nk_bool do_reflect = g_do_reflect && reflect_steps > 0;
	const nk_bool do_normals = g_do_normals || do_reflect;
	
	for (int i = 0; i < march_steps; i++) {
		p = ro + D * d;
		d += s = map(p, lights)*step_relax;
		o += lights + 1.f/max(s, .01f);
	}


	// normal
	// tetrahedron technique: https://iquilezles.org/articles/normalsSDF/
	vec3 n;
	if (do_normals) {
		const float h = 0.005f;
		const vec2 k = vec2(1,-1);
		n = N(k.xyy()*map( p + k.xyy()*h, lights ) +
			  k.yyx()*map( p + k.yyx()*h, lights ) +
			  k.yxy()*map( p + k.yxy()*h, lights ) +
			  k.xxx()*map( p + k.xxx()*h, lights ) );
	} else {
		n = -D;
	}

	// diffuse
	o *= (.1f + max(dot(n, -D), 0.f));
	
	// reflection march
	if (do_reflect) {
		vec4 ref = vec4(0);
		lights = vec4(0);
		p += n*.05f;
		D = reflect(D, n);
		s = 0.f;
		for (int i = 0; i < reflect_steps; i++) {
			p += D*s;
			s = map(p, lights)*step_relax;
			ref += lights + 1.f/max(s, .01f);
		}
		o += o*ref;
	}
	// 6e6 is tuned for 128 march + 40 reflect. Primary glow ~ march^2;
	// o += o*ref with ref ~ 2.9 * reflect_steps^2 (center-pixel probe).
	const float ref_steps_f = do_reflect ? (float)reflect_steps : 0.f;
	const float march_f = (float)march_steps;
	const float bounce = 1.f + 2.9f * ref_steps_f * ref_steps_f;
	const float bounce_ref = 1.f + 2.9f * (float)DFLT_REFLECT_STEPS * (float)DFLT_REFLECT_STEPS;
	const float n0 = (float)DFLT_MARCH_STEPS;
	const float energy = (march_f / n0) * (march_f / n0) * (bounce / bounce_ref);
	const float fade = max(fabsf(d), 1.f);
	o = tanh(o * g_exposure / (g_tonemap_e6 * 1e6f * energy) / fade);
	o.w = 1.f;

	*fragColor = o;
}

}
