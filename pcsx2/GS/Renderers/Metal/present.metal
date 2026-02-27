// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GSMTLShaderCommon.h"

using namespace metal;

// use vs_convert from convert.metal

static float4 ps_crt(float4 color, int i)
{
	constexpr float4 mask[4] =
	{
		float4(1, 0, 0, 0),
		float4(0, 1, 0, 0),
		float4(0, 0, 1, 0),
		float4(1, 1, 1, 0),
	};

	return color * saturate(mask[i] + 0.5f);
}

static float4 ps_scanlines(float4 color, int i)
{
	constexpr float4 mask[2] =
	{
		float4(1, 1, 1, 0),
		float4(0, 0, 0, 0)
	};

	return color * saturate(mask[i] + 0.5f);
}

static float4 auto_gamma(float4 color, constant GSMTLPresentPSUniform& cb);
static float3 luminance_blend(float3 color, constant GSMTLPresentPSUniform& cb);
static float3 levels(float3 color, constant GSMTLPresentPSUniform& cb);
static float3 apply_crt_guest_hd(float2 uv, float3 color, thread ConvertPSRes& res, constant GSMTLPresentPSUniform& cb);

static float2 stereo_fix_adjust_uv(float2 uv, constant GSMTLPresentPSUniform& cb)
{
	const float shift = cb.source_rect.x;
	const float tilt = cb.source_rect.y;
	const float extend_border = cb.target_rect.y;
	const bool extend_edges = (cb.time_and_pad.x > 0.5f);

	const float source_width = max(cb.rcp_source_resolution.x, 1.0f);
	const float shift_uv = shift / source_width;
	const float tilt_uv = tilt / source_width;

	uv.y = (uv.x > 0.5f) ? (uv.y + tilt_uv) : (uv.y - tilt_uv);
	const float edge_border = extend_edges ? extend_border : 0.0f;
	uv.x = (uv.x > 0.5f) ? max(uv.x - shift_uv, 0.5f + edge_border) : min(uv.x + shift_uv, 0.498f - edge_border);
	if (extend_edges)
		uv.x = clamp(uv.x, extend_border, 0.998f - extend_border);

	return uv;
}

static bool stereo_fix_is_outside(float2 uv, constant GSMTLPresentPSUniform& cb)
{
	const float source_width = max(cb.rcp_source_resolution.x, 1.0f);
	const float tilt_uv = cb.source_rect.y / source_width;

	if (uv.x < 0.5f)
		return (uv.y < tilt_uv || uv.y > 1.0f + tilt_uv);

	return (uv.y < -tilt_uv || uv.y > 1.0f - tilt_uv);
}

static float stereo_fix_fade(float2 uv, constant GSMTLPresentPSUniform& cb)
{
	const float tilt = cb.source_rect.y;
	const float vignette_size = cb.source_rect.z;
	const float vignette_x = cb.source_rect.w;
	const float vignette_y = cb.target_rect.x;
	const float cut_offset = cb.target_rect.z;
	const float vignette_offset = cb.target_rect.w;
	const bool extend_edges = (cb.time_and_pad.x > 0.5f);

	const float source_width = max(cb.rcp_source_resolution.x, 1.0f);
	const float tilt_uv = tilt / source_width;
	const float offset_x = vignette_offset / source_width;
	const float cut_offset_x = cut_offset / source_width;
	const float fade_width = (vignette_size + 0.001f) / 50.0f;
	const float edge_offset = extend_edges ? offset_x : 0.0f;

	const float outer_edge = smoothstep(0.0f, fade_width, uv.x - edge_offset) *
		smoothstep(1.0f, 1.0f - fade_width, uv.x + edge_offset);
	const float inner_edge = smoothstep(0.5f, 0.5f - fade_width, uv.x + edge_offset) +
		smoothstep(0.5f, 0.5f + fade_width, uv.x - edge_offset);
	const float horizontal_fade = outer_edge * inner_edge;

	const float v_tilt1 = (uv.x < 0.5f) ? ((tilt_uv > 0.0f) ? 0.0f : tilt_uv) : ((tilt_uv > 0.0f) ? -tilt_uv : 0.0f);
	const float v_tilt2 = (uv.x < 0.5f) ? ((tilt_uv < 0.0f) ? 0.0f : tilt_uv) : ((tilt_uv < 0.0f) ? -tilt_uv : 0.0f);
	const float vertical_fade = smoothstep(0.0f, fade_width, uv.y + v_tilt1) *
		smoothstep(1.0f, 1.0f - fade_width, uv.y + v_tilt2);

	const float outer_cut = smoothstep(0.0f, 0.01f, uv.x - cut_offset_x) * smoothstep(1.0f, 0.99f, uv.x + cut_offset_x);
	const float inner_cut = smoothstep(0.5f, 0.49f, uv.x + cut_offset_x) + smoothstep(0.5f, 0.51f, uv.x - cut_offset_x);

	const float vignette_fade = max(vignette_x * (1.0f - horizontal_fade), vignette_y * (1.0f - vertical_fade));
	const float cut_fade = (extend_edges && cut_offset > 0.0f) ? (1.0f - outer_cut * inner_cut) : 0.0f;
	return saturate(1.0f - vignette_fade - cut_fade);
}

static float4 stereo_fix_finalize(float2 uv, float4 color, thread ConvertPSRes& res, constant GSMTLPresentPSUniform& cb)
{
	color.rgb = apply_crt_guest_hd(uv, color.rgb, res, cb);

	if (stereo_fix_is_outside(uv, cb))
		return float4(0.0f);

	if (cb.time_and_pad.y > 0.5f)
		color = auto_gamma(color, cb);
	if (cb.time_and_pad.z > 0.5f)
		color.rgb = luminance_blend(color.rgb, cb);
	if (cb.time_and_pad.w > 0.5f)
		color.rgb = levels(color.rgb, cb);

	return color * stereo_fix_fade(uv, cb);
}

static float4 sample_with_stereo(thread ConvertPSRes& res, float2 uv, constant GSMTLPresentPSUniform& cb)
{
	return res.sample(stereo_fix_adjust_uv(uv, cb));
}

static float3 apply_crt_guest_hd(float2 uv, float3 color, thread ConvertPSRes& res, constant GSMTLPresentPSUniform& cb)
{
	if (cb.crt_guest_params.x <= 0.5f)
		return color;

	(void)res;

	const float beam_min = 0.6f;
	const float beam_max = 0.3f;
	const float scanline1 = 0.5f;
	const float scanline2 = 1.0f;
	const float scans = 0.5f;

	const float2 source_size = max(cb.rcp_source_resolution, float2(1.0f, 1.0f));
	float3 work = saturate(color);
	const float mx = max(max(work.r, work.g), work.b);
	const float line_pos = abs(fract(uv.y * source_size.y) - 0.5f) * 2.0f;
	const float beam = mix(beam_min, beam_max, mx);
	const float shape = mix(scanline1, scanline2, line_pos);
	const float line = line_pos * beam;
	const float scan = exp2(-shape * line * line * (1.0f + scans));
	work *= scan;
	return saturate(work);
}

// use ps_copy from convert.metal

fragment float4 ps_copy_present(ConvertShaderData data [[stage_in]], ConvertPSRes res,
	constant GSMTLPresentPSUniform& cb [[buffer(GSMTLBufferIndexUniforms)]])
{
	return stereo_fix_finalize(data.t, sample_with_stereo(res, data.t, cb), res, cb);
}

fragment float4 ps_filter_scanlines(ConvertShaderData data [[stage_in]], ConvertPSRes res,
	constant GSMTLPresentPSUniform& cb [[buffer(GSMTLBufferIndexUniforms)]])
{
	return stereo_fix_finalize(data.t, ps_scanlines(sample_with_stereo(res, data.t, cb), uint(data.p.y) % 2), res, cb);
}

fragment float4 ps_filter_diagonal(ConvertShaderData data [[stage_in]], ConvertPSRes res,
	constant GSMTLPresentPSUniform& cb [[buffer(GSMTLBufferIndexUniforms)]])
{
	uint4 p = uint4(data.p);
	return stereo_fix_finalize(data.t, ps_crt(sample_with_stereo(res, data.t, cb), (p.x + (p.y % 3)) % 3), res, cb);
}

fragment float4 ps_filter_triangular(ConvertShaderData data [[stage_in]], ConvertPSRes res,
	constant GSMTLPresentPSUniform& cb [[buffer(GSMTLBufferIndexUniforms)]])
{
	uint4 p = uint4(data.p);
	uint val = ((p.x + ((p.y >> 1) & 1) * 3) >> 1) % 3;
	return stereo_fix_finalize(data.t, ps_crt(sample_with_stereo(res, data.t, cb), val), res, cb);
}

fragment float4 ps_filter_complex(ConvertShaderData data [[stage_in]], ConvertPSRes res,
	constant GSMTLPresentPSUniform& cb [[buffer(GSMTLBufferIndexUniforms)]])
{
	float2 texdim = float2(res.texture.get_width(), res.texture.get_height());
	float factor = (0.9f - 0.4f * cos(2.f * M_PI_F * data.t.y * texdim.y));
	float ycoord = (floor(data.t.y * texdim.y) + 0.5f) / texdim.y;

	return stereo_fix_finalize(data.t, factor * sample_with_stereo(res, float2(data.t.x, ycoord), cb), res, cb);
}

#define MaskingType 4                      //[1|2|3|4] The type of CRT shadow masking used. 1: compressed TV style, 2: Aperture-grille, 3: Stretched VGA style, 4: VGA style.
#define ScanBrightness -8.00               //[-16.0 to 1.0] The overall brightness of the scanline effect. Lower for darker, higher for brighter.
#define FilterCRTAmount -3.00              //[-4.0 to 1.0] The amount of filtering used, to replicate the TV CRT look. Lower for less, higher for more.
#define HorizontalWarp 0.00                //[0.0 to 0.1] The distortion warping effect for the horizontal (x) axis of the screen. Use small increments.
#define VerticalWarp 0.00                  //[0.0 to 0.1] The distortion warping effect for the verticle (y) axis of the screen. Use small increments.
#define MaskAmountDark 0.50                //[0.0 to 1.0] The value of the dark masking line effect used. Lower for darker lower end masking, higher for brighter.
#define MaskAmountLight 1.50               //[0.0 to 2.0] The value of the light masking line effect used. Lower for darker higher end masking, higher for brighter.
#define BloomPixel -1.50                   //[-2.0 -0.5] Pixel bloom radius. Higher for increased softness of bloom.
#define BloomScanLine -2.0                 //[-4.0 -1.0] Scanline bloom radius. Higher for increased softness of bloom.
#define BloomAmount 0.15                   //[0.0 1.0] Bloom intensity. Higher for brighter.
#define Shape 2.0                          //[0.0 10.0] Kernal filter shape. Lower values will darken image and introduce moire patterns if used with curvature.
#define UseShadowMask 1                    //[0 or 1] Enables, or disables the use of the CRT shadow mask. 0 is disabled, 1 is enabled.

struct LottesCRTPass
{
	thread ConvertPSRes& res;
	constant GSMTLPresentPSUniform& uniform;
	LottesCRTPass(thread ConvertPSRes& res, constant GSMTLPresentPSUniform& uniform): res(res), uniform(uniform) {}

	float ToLinear1(float c)
	{
		return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
	}

	float3 ToLinear(float3 c)
	{
		return float3(ToLinear1(c.r), ToLinear1(c.g), ToLinear1(c.b));
	}

	float ToSrgb1(float c)
	{
		return c < 0.0031308 ? c * 12.92 : 1.055 * pow(c, 0.41666) - 0.055;
	}

	float3 ToSrgb(float3 c)
	{
		return float3(ToSrgb1(c.r), ToSrgb1(c.g), ToSrgb1(c.b));
	}

	float3 Fetch(float2 pos, float2 off)
	{
		const float2 render_size = max(uniform.rcp_source_resolution, float2(1.0f, 1.0f));
		pos = (floor(pos * render_size + off) + float2(0.5f, 0.5f)) / render_size;
		if (max(abs(pos.x - 0.5), abs(pos.y - 0.5)) > 0.5)
		{
			return float3(0.0, 0.0, 0.0);
		}
		else
		{
			return ToLinear(sample_with_stereo(res, pos.xy, uniform).rgb);
		}
	}

	float2 Dist(float2 pos)
	{
		pos = pos * float2(640, 480);

		return -((pos - floor(pos)) - float2(0.5, 0.5));
	}

	float Gaus(float pos, float scale)
	{
		return exp2(scale * pow(abs(pos), Shape));
	}

	float3 Horz3(float2 pos, float off)
	{
		float3 b = Fetch(pos, float2(-1.0, off));
		float3 c = Fetch(pos, float2(0.0, off));
		float3 d = Fetch(pos, float2(1.0, off));
		float dst = Dist(pos).x;

		// Convert distance to weight.
		float scale = FilterCRTAmount;
		float wb = Gaus(dst - 1.0, scale);
		float wc = Gaus(dst + 0.0, scale);
		float wd = Gaus(dst + 1.0, scale);

		return (b * wb + c * wc + d * wd) / (wb + wc + wd);
	}

	float3 Horz5(float2 pos, float off)
	{
		float3 a = Fetch(pos, float2(-2.0, off));
		float3 b = Fetch(pos, float2(-1.0, off));
		float3 c = Fetch(pos, float2(0.0, off));
		float3 d = Fetch(pos, float2(1.0, off));
		float3 e = Fetch(pos, float2(2.0, off));
		float dst = Dist(pos).x;

		// Convert distance to weight.
		float scale = FilterCRTAmount;

		float wa = Gaus(dst - 2.0, scale);
		float wb = Gaus(dst - 1.0, scale);
		float wc = Gaus(dst + 0.0, scale);
		float wd = Gaus(dst + 1.0, scale);
		float we = Gaus(dst + 2.0, scale);

		return (a * wa + b * wb + c * wc + d * wd + e * we) / (wa + wb + wc + wd + we);
	}

	float3 Horz7(float2 pos, float off)
	{
		float3 a = Fetch(pos, float2(-3.0, off));
		float3 b = Fetch(pos, float2(-2.0, off));
		float3 c = Fetch(pos, float2(-1.0, off));
		float3 d = Fetch(pos, float2( 0.0, off));
		float3 e = Fetch(pos, float2( 1.0, off));
		float3 f = Fetch(pos, float2( 2.0, off));
		float3 g = Fetch(pos, float2( 3.0, off));

		float dst = Dist(pos).x;
		// Convert distance to weight.
		float scale = BloomPixel;
		float wa = Gaus(dst - 3.0, scale);
		float wb = Gaus(dst - 2.0, scale);
		float wc = Gaus(dst - 1.0, scale);
		float wd = Gaus(dst + 0.0, scale);
		float we = Gaus(dst + 1.0, scale);
		float wf = Gaus(dst + 2.0, scale);
		float wg = Gaus(dst + 3.0, scale);

		// Return filtered sample.
		return (a * wa + b * wb + c * wc + d * wd + e * we + f * wf + g * wg) / (wa + wb + wc + wd + we + wf + wg);
	}

	// Return scanline weight.
	float Scan(float2 pos, float off)
	{
		float dst = Dist(pos).y;
		return Gaus(dst + off, ScanBrightness);
	}

	float BloomScan(float2 pos, float off)
	{
		float dst = Dist(pos).y;
		
		return Gaus(dst + off, BloomScanLine);
	}

	float3 Tri(float2 pos)
	{
		float3 a = Horz3(pos, -1.0);
		float3 b = Horz5(pos, 0.0);
		float3 c = Horz3(pos, 1.0);

		float wa = Scan(pos, -1.0);
		float wb = Scan(pos, 0.0);
		float wc = Scan(pos, 1.0);

		return (a * wa) + (b * wb) + (c * wc);
	}

	float3 Bloom(float2 pos)
	{
		float3 a = Horz5(pos,-2.0);
		float3 b = Horz7(pos,-1.0);
		float3 c = Horz7(pos, 0.0);
		float3 d = Horz7(pos, 1.0);
		float3 e = Horz5(pos, 2.0);

		float wa = BloomScan(pos,-2.0);
		float wb = BloomScan(pos,-1.0); 
		float wc = BloomScan(pos, 0.0);
		float wd = BloomScan(pos, 1.0);
		float we = BloomScan(pos, 2.0);

		return a * wa + b * wb + c * wc + d * wd + e * we;
	}

	float2 Warp(float2 pos)
	{
		pos = pos * 2.0 - 1.0;
		pos *= float2(1.0 + (pos.y * pos.y) * HorizontalWarp, 1.0 + (pos.x * pos.x) * VerticalWarp);
		return pos * 0.5 + 0.5;
	}

	float3 Mask(float2 pos)
	{
#if MaskingType == 1
		// Very compressed TV style shadow mask.
		float lines = MaskAmountLight;
		float odd = 0.0;

		if (fract(pos.x / 6.0) < 0.5)
		{
			odd = 1.0;
		}
		if (fract((pos.y + odd) / 2.0) < 0.5)
		{
			lines = MaskAmountDark;
		}
		pos.x = fract(pos.x / 3.0);
		float3 mask = float3(MaskAmountDark, MaskAmountDark, MaskAmountDark);

		if (pos.x < 0.333)
		{
			mask.r = MaskAmountLight;
		}
		else if (pos.x < 0.666)
		{
			mask.g = MaskAmountLight;
		}
		else
		{
			mask.b = MaskAmountLight;
		}

		mask *= lines;

		return mask;

#elif MaskingType == 2
		// Aperture-grille.
		pos.x = fract(pos.x / 3.0);
		float3 mask = float3(MaskAmountDark, MaskAmountDark, MaskAmountDark);

		if (pos.x < 0.333)
		{
			mask.r = MaskAmountLight;
		}
		else if (pos.x < 0.666)
		{
			mask.g = MaskAmountLight;
		}
		else
		{
			mask.b = MaskAmountLight;
		}

		return mask;

#elif MaskingType == 3
		// Stretched VGA style shadow mask (same as prior shaders).
		pos.x += pos.y * 3.0;
		float3 mask = float3(MaskAmountDark, MaskAmountDark, MaskAmountDark);
		pos.x = fract(pos.x / 6.0);

		if (pos.x < 0.333)
		{
			mask.r = MaskAmountLight;
		}
		else if (pos.x < 0.666)
		{
			mask.g = MaskAmountLight;
		}
		else
		{
			mask.b = MaskAmountLight;
		}

		return mask;

#else
		// VGA style shadow mask.
		pos.xy = floor(pos.xy * float2(1.0, 0.5));
		pos.x += pos.y * 3.0;

		float3 mask = float3(MaskAmountDark, MaskAmountDark, MaskAmountDark);
		pos.x = fract(pos.x / 6.0);

		if (pos.x < 0.333)
		{
			mask.r = MaskAmountLight;
		}
		else if (pos.x < 0.666)
		{
			mask.g = MaskAmountLight;
		}
		else
		{
			mask.b = MaskAmountLight;
		}
		return mask;
#endif
	}

	float4 Run(float2 uv)
	{
		float4 color;
		const float2 inSize = max(uniform.rcp_source_resolution, float2(1.0f, 1.0f));
		const float2 fragcoord = uv * inSize;

		float2 pos = Warp(fragcoord / inSize);
		color.rgb = Tri(pos);
		color.rgb += Bloom(pos) * BloomAmount;
	#if UseShadowMask
		color.rgb *= Mask(fragcoord);
	#endif
		color.rgb = ToSrgb(color.rgb);

		return color;
	}
};

fragment float4 ps_filter_lottes(ConvertShaderData data [[stage_in]], ConvertPSRes res,
	constant GSMTLPresentPSUniform& uniform [[buffer(GSMTLBufferIndexUniforms)]])
{
	return stereo_fix_finalize(data.t, LottesCRTPass(res, uniform).Run(data.t), res, uniform);
}

fragment float4 ps_4x_rgss(ConvertShaderData data [[stage_in]], ConvertPSRes res,
	constant GSMTLPresentPSUniform& cb [[buffer(GSMTLBufferIndexUniforms)]])
{
	float2 dxy = float2(dfdx(data.t.x), dfdy(data.t.y));
	float3 color = 0;

	float s = 1.0/8.0;
	float l = 3.0/8.0;

	color += sample_with_stereo(res, data.t + float2( s, l) * dxy, cb).rgb;
	color += sample_with_stereo(res, data.t + float2( l,-s) * dxy, cb).rgb;
	color += sample_with_stereo(res, data.t + float2(-s,-l) * dxy, cb).rgb;
	color += sample_with_stereo(res, data.t + float2(-l, s) * dxy, cb).rgb;

	return stereo_fix_finalize(data.t, float4(color * 0.25f, 1.0f), res, cb);
}

fragment float4 ps_automagical_supersampling(ConvertShaderData data [[stage_in]], ConvertPSRes res,
	constant GSMTLPresentPSUniform& cb [[buffer(GSMTLBufferIndexUniforms)]])
{
	const float2 source_size = max(cb.rcp_source_resolution, float2(1.0f, 1.0f));
	const float2 uv_step = abs(float2(dfdx(data.t.x), dfdy(data.t.y)));
	float2 ratio = uv_step * source_size * 0.5f;
	float2 steps = floor(ratio);
	float3 col = sample_with_stereo(res, data.t, cb).rgb;
	float div = 1.0f;

	for (float y = 0; y < steps.y; y++)
	{
		for (float x = 0; x < steps.x; x++)
		{
			float2 offset = float2(x,y) - ratio * 0.5;
			col += sample_with_stereo(res, data.t + offset / source_size * 2.0f, cb).rgb;
			div++;
		}
	}

	return stereo_fix_finalize(data.t, float4(col / div, 1.0f), res, cb);
}

static float3 srgb_to_linear(float3 color_srgb)
{
	return pow(max(float3(0.0f), color_srgb), float3(2.2f));
}

static float3 linear_to_srgb_dynamic(float3 color_linear, float exponent)
{
	return pow(max(float3(0.0f), color_linear), float3(exponent));
}

static float get_luminance(float3 color_linear)
{
	return dot(color_linear, float3(0.2126f, 0.7152f, 0.0722f));
}

static float adjust_luminance_contrast(float lum, float contrast, float midpoint)
{
	return mix(midpoint, lum, contrast);
}

static float4 auto_gamma(float4 color, constant GSMTLPresentPSUniform& cb)
{
	const float contrast_intensity = cb.source_size.x;
	const float midpoint_focus = cb.source_size.y;
	const float contrast_midpoint = cb.target_size.x;
	const float gamma_comp_strength = cb.target_size.y;

	float3 color_linear = srgb_to_linear(color.rgb);
	float3 processed_linear = color_linear;
	const float pixel_lum_linear = get_luminance(color_linear);
	float gamma_correction_factor = 1.0f;

	const float dist_from_mid = abs(pixel_lum_linear - contrast_midpoint);
	const float norm_range = max(contrast_midpoint, 1.0f - contrast_midpoint);
	const float dist_scaled = saturate(dist_from_mid / max(norm_range, 1e-6f));
	const float falloff = pow(dist_scaled, midpoint_focus);
	const float contrast_modulation_factor = saturate(1.0f - falloff);

	if (contrast_modulation_factor > 0.0f)
	{
		const float dynamic_contrast = mix(1.0f, contrast_intensity, contrast_modulation_factor);
		float adjusted_lum = adjust_luminance_contrast(pixel_lum_linear, dynamic_contrast, contrast_midpoint);
		adjusted_lum = max(0.0f, adjusted_lum);

		const float luminance_ratio = adjusted_lum / max(pixel_lum_linear, 1e-6f);
		const float ev_shift = log2(max(luminance_ratio, 1e-6f));
		gamma_correction_factor = 1.0f - (ev_shift * gamma_comp_strength * 0.25f);
		gamma_correction_factor = clamp(gamma_correction_factor, 0.5f, 1.5f);

		if (pixel_lum_linear <= 1e-6f)
			processed_linear = float3(0.0f);
		else
			processed_linear = color_linear * luminance_ratio;
	}

	const float final_exponent = (1.0f / 2.2f) / gamma_correction_factor;
	const float3 final_srgb = linear_to_srgb_dynamic(processed_linear, final_exponent);
	return float4(saturate(final_srgb), color.a);
}

static float calculate_modulation_factor(float pixel_lum, float target_point, float focus)
{
	const float dist_from_point = abs(pixel_lum - target_point);
	const float norm_range = max(target_point, 1.0f - target_point);
	const float dist_scaled = saturate(dist_from_point / max(norm_range, 1e-6f));
	const float falloff = pow(dist_scaled, focus);
	return saturate(1.0f - falloff);
}

static float3 apply_blend(float3 base, float3 blend)
{
	float3 r;
	r.r = (base.r < 0.5f) ? (2.0f * base.r * blend.r) : (1.0f - 2.0f * (1.0f - base.r) * (1.0f - blend.r));
	r.g = (base.g < 0.5f) ? (2.0f * base.g * blend.g) : (1.0f - 2.0f * (1.0f - base.g) * (1.0f - blend.g));
	r.b = (base.b < 0.5f) ? (2.0f * base.b * blend.b) : (1.0f - 2.0f * (1.0f - base.b) * (1.0f - blend.b));
	return r;
}

static float3 luminance_blend(float3 color, constant GSMTLPresentPSUniform& cb)
{
	const float opacity = cb.target_resolution.x;
	const float midpoint_focus2 = cb.target_resolution.y;
	const float luminance_midpoint = cb.rcp_target_resolution.x;
	const float pixel_lum_linear = get_luminance(srgb_to_linear(color));
	const float blend_mod_factor = calculate_modulation_factor(pixel_lum_linear, luminance_midpoint, midpoint_focus2);
	if (blend_mod_factor <= 0.0f)
		return color;

	const float3 blended_full = apply_blend(color, color);
	const float actual_opacity = opacity * blend_mod_factor;
	return saturate(mix(color, saturate(blended_full), actual_opacity));
}

static float3 levels(float3 color, constant GSMTLPresentPSUniform& cb)
{
	const float black_level = cb.rcp_target_resolution.y;
	const float white_level = cb.source_resolution.x;
	const float temperature = cb.source_resolution.y;
	const float black_point = black_level / 255.0f;
	const float white_point = 255.0f / ((255.0f - white_level) - black_level);
	color *= float3(1.0f + temperature * 0.1f, 1.0f, 1.0f - temperature * 0.1f);
	return saturate(color * white_point - (black_point * white_point));
}

fragment float4 ps_stereoscopic_fixes(ConvertShaderData data [[stage_in]], ConvertPSRes res,
	constant GSMTLPresentPSUniform& cb [[buffer(GSMTLBufferIndexUniforms)]])
{
	return stereo_fix_finalize(data.t, sample_with_stereo(res, data.t, cb), res, cb);
}
