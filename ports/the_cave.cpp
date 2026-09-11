// GLM


#include "glm_shader_workshop_common.h"
#include "nuklear_shader_workshop.h"

using namespace glm;

// ported from https://www.shadertoy.com/view/MsX3RH
// Created by BoyC
// License Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License.
//
// mainImage: straightforward Shadertoy-style port (host texture2D).
// mainFrame: inlined sw_texture2D, camera/light hoisted once per frame.
// Toggle Use Frame Shader to compare. Default channels are nearest/repeat.

// constants for the camera tunnel
const vec2 cama=vec2(-2.6943f,3.0483f);
const vec2 camb=vec2(0.2516f,0.1749f);
const vec2 camc=vec2(-3.7902f,2.4478f);
const vec2 camd=vec2(0.0865f,-0.1664f);

const vec2 lighta=vec2(1.4301f,4.0985f);
const vec2 lightb=vec2(-0.1276f,0.2347f);
const vec2 lightc=vec2(-2.2655f,1.5066f);
const vec2 lightd=vec2(-0.1284f,0.0731f);

inline vec2 Position(float z, vec2 a, vec2 b, vec2 c, vec2 d)
{
	return sin(z*a)*b + cos(z*c)*d;
}

inline vec3 Position3D(float time, vec2 a, vec2 b, vec2 c, vec2 d)
{
	return vec3(Position(time,a,b,c,d),time);
}

inline float Distance(vec3 p, vec2 a, vec2 b, vec2 c, vec2 d, vec2 e, float r)
{
	vec2 pos = Position(p.z,a,b,c,d);
	float radius = max(5.0f, r + sin(p.z*e.x)*e.y) / 10000.0f;
	return radius/dot(p.xy()-pos, p.xy()-pos);
}

struct Tunnel {
	vec2 a, b, c, d, e;
	float r;
};

// [0] is the camera tunnel (path still uses cama..camd even if you drop others).
static const Tunnel g_tunnels[10] = {
	{ cama, camb, camc, camd, vec2(2.1913f,15.4634f), 70.0000f },
	{ lighta, lightb, lightc, lightd, vec2(0.3814f,12.7206f), 17.0590f },
	{ vec2(2.7377f,-1.2462f), vec2(-0.1914f,-0.2339f), vec2(-1.3698f,-0.6855f), vec2(0.1049f,-0.1347f), vec2(-1.1157f,13.6200f), 27.3718f },
	{ vec2(-2.3815f,0.2382f), vec2(-0.1528f,-0.1475f), vec2(0.9996f,-2.1459f), vec2(-0.0566f,-0.0854f), vec2(0.3287f,12.1713f), 21.8130f },
	{ vec2(-2.7424f,4.8901f), vec2(-0.1257f,0.2561f), vec2(-0.4138f,2.6706f), vec2(-0.1355f,0.1648f), vec2(2.8162f,14.8847f), 32.2235f },
	{ vec2(-2.2158f,4.5260f), vec2(0.2834f,0.2319f), vec2(4.2578f,-2.5997f), vec2(-0.0391f,-0.2070f), vec2(2.2086f,13.0546f), 30.9920f },
	{ vec2(0.9824f,4.4131f), vec2(0.2281f,-0.2955f), vec2(-0.6033f,0.4780f), vec2(-0.1544f,0.1360f), vec2(3.2020f,12.2138f), 29.1169f },
	{ vec2(1.2733f,-2.4752f), vec2(-0.2821f,-0.1180f), vec2(3.4862f,-0.7046f), vec2(0.0224f,0.2024f), vec2(-2.2714f,9.7317f), 6.3008f },
	{ vec2(2.6860f,2.3608f), vec2(-0.1486f,0.2376f), vec2(2.0568f,1.5440f), vec2(0.0367f,0.1594f), vec2(-2.0396f,10.2225f), 25.5348f },
	{ vec2(0.5009f,0.9612f), vec2(0.1818f,-0.1669f), vec2(0.0698f,-2.0880f), vec2(0.1424f,0.1063f), vec2(1.7980f,11.2733f), 35.7880f },
};

#define MAX_NUM_TUNNELS 10
static int g_num_tunnels = 10;

float Dist2D(vec3 pos)
{
	float d = 0.0f;
	int n = g_num_tunnels;
	if (n < 1) {
		n = 1;
	}
	if (n > MAX_NUM_TUNNELS) {
		n = MAX_NUM_TUNNELS;
	}
	for (int i = 0; i < n; i++) {
		const Tunnel& t = g_tunnels[i];
		d += Distance(pos, t.a, t.b, t.c, t.d, t.e, t.r);
	}
	return d;
}

#define DFLT_NUMIT 75
#define DFLT_SHADOW_IT 15
#define DFLT_THRESHOLD 1.20f
#define DFLT_SCALE 1.5f
#define DFLT_TIME_DIV 3.0f

#define MAX_NUMIT 200
#define MAX_SHADOW_IT 40
#define MAX_THRESHOLD 4.0f
#define MAX_SCALE 5.0f
#define MAX_TIME_DIV 60.0f

static int g_quality = 2;
static int g_numit = DFLT_NUMIT;
static int g_shadow_it = DFLT_SHADOW_IT;
static float g_threshold = DFLT_THRESHOLD;
static float g_scale = DFLT_SCALE;
static float g_time_div = DFLT_TIME_DIV;
static nk_bool g_use_nmap = nk_true;
static nk_bool g_use_albedo = nk_true;
static nk_bool g_do_refine = nk_true;
static nk_bool g_do_fd_normal = nk_true;
static nk_bool g_use_wisp = nk_true;

static void apply_quality_preset(int q)
{
	g_quality = q;
	if (q <= 0) {
		g_numit = 25;
		g_shadow_it = 4;
	} else if (q == 1) {
		g_numit = 40;
		g_shadow_it = 8;
	} else {
		g_numit = DFLT_NUMIT;
		g_shadow_it = DFLT_SHADOW_IT;
	}
}

static void reset_defaults(void)
{
	g_threshold = DFLT_THRESHOLD;
	g_scale = DFLT_SCALE;
	g_time_div = DFLT_TIME_DIV;
	g_use_nmap = nk_true;
	g_use_albedo = nk_true;
	g_do_refine = nk_true;
	g_do_fd_normal = nk_true;
	g_use_wisp = nk_true;
	g_num_tunnels = MAX_NUM_TUNNELS;
	apply_quality_preset(2);
}

static inline vec4 tex(GLuint ch, vec2 uv)
{
	pgl_vec4 s = texture2D(ch, uv.x, uv.y);
	return vec4(s.x, s.y, s.z, s.w);
}

static inline vec4 tex(const glTexture* t, vec2 uv)
{
	assert(t);
	pgl_vec4 s = sw_texture2D(t, uv.x, uv.y);
	return vec4(s.x, s.y, s.z, s.w);
}

static vec3 nmap(vec2 t, GLuint tx, float str)
{
	float d=1.0f/1024.0f;
	float xy = tex(tx, t).x;
	float x2 = tex(tx, t + vec2(d, 0.f)).x;
	float y2 = tex(tx, t + vec2(0.f, d)).x;
	float s=(1.0f-str)*1.2f;
	s*=s;
	s*=s;
	return normalize(vec3(x2-xy, y2-xy, s/8.0f));
}

static vec3 nmap(vec2 t, const glTexture* tx, float str)
{
	float d=1.0f/1024.0f;
	float xy = tex(tx, t).x;
	float x2 = tex(tx, t + vec2(d, 0.f)).x;
	float y2 = tex(tx, t + vec2(0.f, d)).x;
	float s=(1.0f-str)*1.2f;
	s*=s;
	s*=s;
	return normalize(vec3(x2-xy, y2-xy, s/8.0f));
}

struct CaveView {
	float time;
	vec3 oPos;
	mat3 cam;
	vec3 lp;
	vec2 r2;
	float aspect;
};

static CaveView make_view(void)
{
	CaveView v;
	float time_div = (g_time_div > 1e-4f) ? g_time_div : 1e-4f;
	v.time = iTime / time_div + 291.0f;
	vec2 p1 = Position(v.time + 0.05f, cama, camb, camc, camd);
	v.oPos = Position3D(v.time, cama, camb, camc, camd);
	vec3 CamDir = normalize(vec3(p1.x - v.oPos.x, -p1.y + v.oPos.y, 0.1f));
	vec3 CamRight = normalize(glm::cross(CamDir, vec3(0, 1, 0)));
	vec3 CamUp = normalize(glm::cross(CamRight, CamDir));
	v.cam = mat3(CamRight, CamUp, CamDir);
	v.lp = Position3D(v.time + 0.5f, cama, camb, camc, camd);
	v.r2 = iResolution.xy();
	v.aspect = iResolution.z;
	return v;
}

static void shade_frame(vec4* fragColor, vec2 fragCoord, const CaveView& view,
	const glTexture* ch0, const glTexture* ch1, const glTexture* ch2)
{
	vec3 Pos = view.oPos;
	vec3 oPos = view.oPos;
	vec3 Dir = normalize(vec3((2.f * fragCoord / view.r2 - 1.f) * vec2(view.aspect, 1.0f), 1.0f)) * view.cam;

	float fade = 0.0f;
	const int numit = g_numit;
	const float threshold = g_threshold;
	const float scale = g_scale;
	vec3 Posm1 = Pos;

	for (int x = 0; x < numit; x++) {
		if (Dist2D(Pos) < threshold) {
			fade = 1.0f - (float)x / (float)numit;
			break;
		}
		Posm1 = Pos;
		Pos += Dir / (float)numit * scale;
	}

	if (g_do_refine) {
		for (int x = 0; x < 6; x++) {
			vec3 p2 = (Posm1 + Pos) / 2.f;
			if (Dist2D(p2) < threshold) {
				Pos = p2;
			} else {
				Posm1 = p2;
			}
		}
	}

	vec3 n;
	if (g_do_fd_normal) {
		n = normalize(vec3(Dist2D(Pos+vec3(0.01f,0,0))-Dist2D(Pos+vec3(-0.01f,0,0)),
		                   Dist2D(Pos+vec3(0,0.01f,0))-Dist2D(Pos+vec3(0,-0.01f,0)),
		                   Dist2D(Pos+vec3(0,0,0.01f))-Dist2D(Pos+vec3(0,0,-0.01f))));
	} else {
		n = -Dir;
	}

	vec3 tpn = normalize(max(vec3(0.0f), (abs(n)-vec3(0.2f))*7.f))*0.5f;
	vec3 lp = view.lp;
	vec3 ld = lp - Pos;
	float lv = 1.0f;
	const int ShadowIT = g_shadow_it;
	for (int x = 1; x < ShadowIT; x++) {
		if (Dist2D(Pos + ld * ((float)x / (float)ShadowIT)) < threshold) {
			lv = 0.0f;
			break;
		}
	}

	vec3 tuv = Pos * vec3(3.0f, 3.0f, 1.5f);
	float dd;
	if (g_use_nmap) {
		float nms = 0.19f;
		vec3 nmx = nmap(tuv.yz(), ch0, nms) + nmap(-tuv.yz(), ch0, nms);
		vec3 nmy = nmap(tuv.xz(), ch1, nms) + nmap(-tuv.xz(), ch1, nms);
		vec3 nmz = nmap(tuv.xy(), ch2, nms) + nmap(-tuv.xy(), ch2, nms);
		vec3 nn = normalize(nmx*tpn.x + nmy*tpn.y + nmz*tpn.z);
		dd = max(0.0f, dot(nn, normalize(ld * mat3(vec3(1,0,0), vec3(0,0,1), n))));
	} else {
		dd = max(0.0f, dot(n, normalize(ld)));
	}

	vec4 diff = vec4(dd * 1.2f * lv) + vec4(0.2f);
	float w = 0.0f;
	if (g_use_wisp) {
		w = pow(dot(normalize(Pos - oPos), normalize(lp - oPos)), 5000.0f);
		if (length(Pos - oPos) < length(lp - oPos)) {
			w = 0.0f;
		}
	}

	vec4 col;
	if (g_use_albedo) {
		vec4 tx = tex(ch0, tuv.yz()) + tex(ch0, -tuv.yz());
		vec4 ty = tex(ch1, tuv.xz()) + tex(ch1, -tuv.xz());
		vec4 tz = tex(ch2, tuv.xy()) + tex(ch2, -tuv.xy());
		col = tx*tpn.x + ty*tpn.y + tz*tpn.z;
	} else {
		col = vec4(1.2f);
	}

	*fragColor = col * diff * min(1.f, fade * 10.f) + w;
}

extern "C" {

void set_channels(texture_settings* ts)
{
	ts[0].ch = 9;
	SET_DFLT_TEX_PARAMS(ts[0]);
	ts[1].ch = 12;
	SET_DFLT_TEX_PARAMS(ts[1]);
	ts[2].ch = 13;
	SET_DFLT_TEX_PARAMS(ts[2]);
	reset_defaults();
}

nk_bool do_user_gui(struct nk_context* ctx, nk_bool is_playing)
{
	(void)is_playing;
	nk_bool need = nk_false;

	nk_layout_row_dynamic(ctx, 0, 1);
	nk_label(ctx, "The Cave", NK_TEXT_CENTERED);

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
	nk_label_wrap(ctx, "Presets only change march/shadow steps. Fancy is the original quality (75 / 15).");
	nk_layout_row_dynamic(ctx, 0, 1);
	nk_label_wrap(ctx, "Use Re-initialize Shader for a full reset.");
	if (nk_tree_push(ctx, NK_TREE_TAB, "Features", NK_MINIMIZED)) {
		nk_layout_row_dynamic(ctx, 0, 2);
		need |= nk_checkbox_label(ctx, "Normal maps", &g_use_nmap);
		need |= nk_checkbox_label(ctx, "Albedo textures", &g_use_albedo);
		need |= nk_checkbox_label(ctx, "Hit refine (6 Dist2D)", &g_do_refine);
		need |= nk_checkbox_label(ctx, "Finite-diff normals (6 Dist2D)", &g_do_fd_normal);
		need |= nk_checkbox_label(ctx, "Wisp", &g_use_wisp);
		nk_tree_pop(ctx);
	}

	if (nk_tree_push(ctx, NK_TREE_TAB, "March", NK_MINIMIZED)) {
		nk_layout_row_dynamic(ctx, 0, 1);
		need |= nk_property_int(ctx, "Steps:", 8, &g_numit, MAX_NUMIT, 1, 1);
		need |= nk_property_int(ctx, "Shadow steps:", 0, &g_shadow_it, MAX_SHADOW_IT, 1, 1);
		need |= nk_property_float(ctx, "Threshold:", 0.2f, &g_threshold, MAX_THRESHOLD, 0.05f, 0.01f);
		need |= nk_property_float(ctx, "Z scale:", 0.2f, &g_scale, MAX_SCALE, 0.05f, 0.01f);
		need |= nk_property_int(ctx, "Tunnels:", 1, &g_num_tunnels, MAX_NUM_TUNNELS, 1, 1);
		nk_label_wrap(ctx, "1 = camera tunnel only. 10 = original network. Not a quality preset.");
		nk_tree_pop(ctx);
	}

	if (nk_tree_push(ctx, NK_TREE_TAB, "Time", NK_MINIMIZED)) {
		nk_layout_row_dynamic(ctx, 0, 1);
		need |= nk_property_float(ctx, "iTime divisor:", 0.5f, &g_time_div, MAX_TIME_DIV, 0.5f, 0.05f);
		nk_label_wrap(ctx, "Original Shadertoy is 3. Larger = slower camera.");
		nk_tree_pop(ctx);
	}

	return need;
}

void mainImage(vec4* fragColor, vec2 fragCoord)
{
	float time_div = (g_time_div > 1e-4f) ? g_time_div : 1e-4f;
	float time = iTime / time_div + 291.0f;

	vec2 p1=Position(time+0.05f,cama,camb,camc,camd);
	vec3 Pos=Position3D(time,cama,camb,camc,camd);
	vec3 oPos=Pos;

	vec3 CamDir = normalize(vec3(p1.x-Pos.x,-p1.y+Pos.y,0.1f));
	vec3 CamRight = normalize(glm::cross(CamDir,vec3(0,1,0)));
	vec3 CamUp = normalize(glm::cross(CamRight,CamDir));
	mat3 cam = mat3(CamRight,CamUp,CamDir);

	vec2 uv = 2.f*fragCoord/iResolution.xy()-1.f;
	float aspect = iResolution.z;
	vec3 Dir = normalize(vec3(uv*vec2(aspect,1.0f),1.0f)) * cam;

	float fade=0.0f;
	const int numit = g_numit;
	const float threshold = g_threshold;
	const float scale = g_scale;
	vec3 Posm1=Pos;

	for (int x=0; x<numit; x++)
	{
		if (Dist2D(Pos)<threshold)
		{
			fade=1.0f-(float)x/(float)numit;
			break;
		}
		Posm1=Pos;
		Pos+=Dir/(float)numit*scale;
	}

	if (g_do_refine) {
		for (int x=0; x<6; x++)
		{
			vec3 p2=(Posm1+Pos)/2.f;
			if (Dist2D(p2)<threshold) {
				Pos=p2;
			} else {
				Posm1=p2;
			}
		}
	}

	vec3 n;
	if (g_do_fd_normal) {
		n=normalize(vec3(Dist2D(Pos+vec3(0.01f,0,0))-Dist2D(Pos+vec3(-0.01f,0,0)),
						  Dist2D(Pos+vec3(0,0.01f,0))-Dist2D(Pos+vec3(0,-0.01f,0)),
						  Dist2D(Pos+vec3(0,0,0.01f))-Dist2D(Pos+vec3(0,0,-0.01f))));
	} else {
		n = -Dir;
	}

	vec3 tpn = normalize(max(vec3(0.0f), (abs(n)-vec3(0.2f))*7.f))*0.5f;
	vec3 lp = Position3D(time+0.5f,cama,camb,camc,camd);
	vec3 ld = lp-Pos;
	float lv=1.0f;
	const int ShadowIT = g_shadow_it;
	for (int x=1; x<ShadowIT; x++) {
		if (Dist2D(Pos+ld*((float)x/(float)ShadowIT))<threshold) {
			lv=0.0f;
			break;
		}
	}

	vec3 tuv=Pos*vec3(3.0f,3.0f,1.5f);

	float dd;
	if (g_use_nmap) {
		float nms=0.19f;
		vec3 nmx = nmap(tuv.yz(), iChannel0, nms) + nmap(-tuv.yz(), iChannel0, nms);
		vec3 nmy = nmap(tuv.xz(), iChannel1, nms) + nmap(-tuv.xz(), iChannel1, nms);
		vec3 nmz = nmap(tuv.xy(), iChannel2, nms) + nmap(-tuv.xy(), iChannel2, nms);
		vec3 nn=normalize(nmx*tpn.x+nmy*tpn.y+nmz*tpn.z);
		dd=max(0.0f ,dot(nn,normalize(ld*mat3(vec3(1,0,0),vec3(0,0,1),n))));
	} else {
		dd=max(0.0f ,dot(n,normalize(ld)));
	}

	vec4 diff=vec4(dd*1.2f*lv)+vec4(0.2f);
	float w=0.0f;
	if (g_use_wisp) {
		w=pow(dot(normalize(Pos-oPos),normalize(lp-oPos)),5000.0f);
		if (length(Pos-oPos) < length(lp-oPos)) {
			w=0.0f;
		}
	}

	vec4 col;
	if (g_use_albedo) {
		vec4 tx = tex(iChannel0, tuv.yz()) + tex(iChannel0, -tuv.yz());
		vec4 ty = tex(iChannel1, tuv.xz()) + tex(iChannel1, -tuv.xz());
		vec4 tz = tex(iChannel2, tuv.xy()) + tex(iChannel2, -tuv.xy());
		col = tx*tpn.x+ty*tpn.y+tz*tpn.z;
	} else {
		col = vec4(1.2f);
	}

	*fragColor = col*diff*min(1.f, fade*10.f) + w;
}

void mainFrame(pix_t* framebuffer, int w, int h)
{
	const glTexture* t0 = pglGetTexture(iChannel0);
	const glTexture* t1 = pglGetTexture(iChannel1);
	const glTexture* t2 = pglGetTexture(iChannel2);
	CaveView view = make_view();
	pix_t* lastrow = framebuffer + (h - 1) * w;

SW_PARALLEL_FOR
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			vec2 fc;
			vec4 col;
			fc.x = x + 0.5f;
			fc.y = y + 0.5f;
			shade_frame(&col, fc, view, t0, t1, t2);
			lastrow[-y * w + x] = pack_clamp_rgba8(col.x, col.y, col.z, col.w);
		}
	}
}

}
