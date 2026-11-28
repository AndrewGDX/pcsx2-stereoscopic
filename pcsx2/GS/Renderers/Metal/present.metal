// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GSMTLShaderCommon.h"

using namespace metal;

// use vs_convert from convert.metal

struct PresentPass
{
	thread ConvertPSRes& res;
	constant GSMTLPresentPSUniform& cb;
	PresentPass(thread ConvertPSRes& r, constant GSMTLPresentPSUniform& c): res(r), cb(c) {}
	// Eye-local sampling and final color processing; flags mirror GSPresentationEffects. (AI-assisted)
	bool EffectsActive() { return (cb.flags & 127u) != 0u; }
	bool EffectEnabled(uint flag) { return (cb.flags & flag) != 0u; }

	float2 EyeUV(float2 uv)
	{
		return (uv - cb.source_rect.xy) / max(cb.source_rect.zw - cb.source_rect.xy, float2(1e-8));
	}

	float2 AdjustedEyeUV(float2 uv)
	{
		return EyeUV(uv) + float2(cb.shift, cb.tilt);
	}

	float2 SampleUV(float2 uv)
	{
		if (!EffectsActive())
			return uv;
		float2 extent = cb.source_rect.zw - cb.source_rect.xy;
		float2 half_texel = min(float2(0.5) / cb.source_resolution, extent * 0.5);
		float2 inset = half_texel;
		if (EffectEnabled(2u))
			inset = max(inset, float2(cb.extend_border, 0.0) * extent);
		return clamp(cb.source_rect.xy + AdjustedEyeUV(uv) * extent,
			cb.source_rect.xy + inset, cb.source_rect.zw - inset);
	}

	float Modulation(float luminance, float midpoint, float focus)
	{
		float distance = clamp(abs(luminance - midpoint) / max(max(midpoint, 1.0 - midpoint), 1e-6), 0.0, 1.0);
		return 1.0 - pow(distance, focus);
	}

	float4 Finish(float2 uv, float4 color)
	{
		if (!EffectsActive())
			return color;
		float2 q = EyeUV(uv);
		float2 adjusted = AdjustedEyeUV(uv);
		if (EffectEnabled(1u) && !EffectEnabled(2u) && (any(adjusted < float2(0.0, 0.0)) || any(adjusted > float2(1.0, 1.0))))
			return float4(0.0);
		if (EffectEnabled(64u))
		{
			float3 work = clamp(color.rgb, 0.0, 1.0);
			float brightness = max(max(work.r, work.g), work.b);
			float line_pos = abs(fract(q.y * ((cb.source_rect.zw - cb.source_rect.xy) * cb.source_resolution).y) - 0.5) * 2.0;
			float beam_line = line_pos * mix(0.6, 0.3, brightness);
			color.rgb = work * exp2(-mix(0.5, 1.0, line_pos) * beam_line * beam_line * 1.5);
		}
		if (EffectEnabled(8u))
		{
			float3 linear_color = pow(max(color.rgb, float3(0.0)), float3(2.2));
			float luminance = dot(linear_color, float3(0.2126, 0.7152, 0.0722));
			float contrast = mix(1.0, cb.contrast_intensity, Modulation(luminance, cb.contrast_midpoint, cb.contrast_focus));
			float adjusted_luminance = max(0.0, mix(cb.contrast_midpoint, luminance, contrast));
			float ratio = adjusted_luminance / max(luminance, 1e-6);
			float gamma = clamp(1.0 - log2(max(ratio, 1e-6)) * cb.gamma_strength * 0.25, 0.5, 1.5);
			linear_color = luminance <= 1e-6 ? float3(0.0) : linear_color * ratio;
			color.rgb = clamp(pow(linear_color, float3((1.0 / 2.2) / gamma)), 0.0, 1.0);
		}
		if (EffectEnabled(16u))
		{
			float luminance = dot(pow(max(color.rgb, float3(0.0)), float3(2.2)), float3(0.2126, 0.7152, 0.0722));
			float3 low = 2.0 * color.rgb * color.rgb;
			float3 high = 1.0 - 2.0 * (1.0 - color.rgb) * (1.0 - color.rgb);
			float3 blended = float3(color.r < 0.5 ? low.r : high.r, color.g < 0.5 ? low.g : high.g, color.b < 0.5 ? low.b : high.b);
			color.rgb = clamp(mix(color.rgb, clamp(blended, 0.0, 1.0), cb.blend_opacity * Modulation(luminance, cb.blend_midpoint, cb.blend_focus)), 0.0, 1.0);
		}
		if (EffectEnabled(32u))
			color.rgb = clamp((color.rgb * float3(1.0 + cb.temperature, 1.0, 1.0 - cb.temperature) - cb.black_point) * cb.levels_scale, 0.0, 1.0);
		float fade = 1.0;
		if (EffectEnabled(4u))
		{
			float offset = EffectEnabled(2u) ? cb.vignette_offset : 0.0;
			float horizontal = smoothstep(0.0, cb.vignette_width * 2.0, q.x - offset) * (1.0 - smoothstep(1.0 - cb.vignette_width * 2.0, 1.0, q.x + offset));
			float vertical = smoothstep(0.0, cb.vignette_width, adjusted.y) * (1.0 - smoothstep(1.0 - cb.vignette_width, 1.0, adjusted.y));
			fade -= max(cb.vignette_x * (1.0 - horizontal), cb.vignette_y * (1.0 - vertical));
		}
		if (EffectEnabled(2u) && cb.cut_offset > 0.0)
		{
			float cut = smoothstep(0.0, 0.02, q.x - cb.cut_offset) * (1.0 - smoothstep(0.98, 1.0, q.x + cb.cut_offset));
			fade -= 1.0 - cut;
		}
		return color * clamp(fade, 0.0, 1.0);
	}

	float4 sample(float2 uv) { return res.sample(SampleUV(uv)); }
};

fragment float4 ps_present_copy(ConvertShaderData data [[stage_in]], ConvertPSRes res,
	constant GSMTLPresentPSUniform& cb [[buffer(GSMTLBufferIndexUniforms)]])
{
	PresentPass pass(res, cb);
	return pass.Finish(data.t, pass.sample(data.t));
}

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

// use ps_copy from convert.metal

fragment float4 ps_filter_scanlines(ConvertShaderData data [[stage_in]], ConvertPSRes res,
	constant GSMTLPresentPSUniform& cb [[buffer(GSMTLBufferIndexUniforms)]])
{
	PresentPass pass(res, cb);
	return pass.Finish(data.t, ps_scanlines(pass.sample(data.t), uint(data.p.y) % 2));
}

fragment float4 ps_filter_diagonal(ConvertShaderData data [[stage_in]], ConvertPSRes res,
	constant GSMTLPresentPSUniform& cb [[buffer(GSMTLBufferIndexUniforms)]])
{
	PresentPass pass(res, cb);
	uint4 p = uint4(data.p);
	return pass.Finish(data.t, ps_crt(pass.sample(data.t), (p.x + (p.y % 3)) % 3));
}

fragment float4 ps_filter_triangular(ConvertShaderData data [[stage_in]], ConvertPSRes res,
	constant GSMTLPresentPSUniform& cb [[buffer(GSMTLBufferIndexUniforms)]])
{
	PresentPass pass(res, cb);
	uint4 p = uint4(data.p);
	uint val = ((p.x + ((p.y >> 1) & 1) * 3) >> 1) % 3;
	return pass.Finish(data.t, ps_crt(pass.sample(data.t), val));
}

fragment float4 ps_filter_complex(ConvertShaderData data [[stage_in]], ConvertPSRes res,
	constant GSMTLPresentPSUniform& cb [[buffer(GSMTLBufferIndexUniforms)]])
{
	PresentPass pass(res, cb);
	float2 texdim = float2(res.texture.get_width(), res.texture.get_height());
	float factor = (0.9f - 0.4f * cos(2.f * M_PI_F * data.t.y * texdim.y));
	float ycoord = (floor(data.t.y * texdim.y) + 0.5f) / texdim.y;

	return pass.Finish(data.t, factor * pass.sample(float2(data.t.x, ycoord)));
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
		float2 size = (uniform.flags & 127u) != 0u ? (uniform.source_rect.zw - uniform.source_rect.xy) * uniform.source_resolution : abs(uniform.target_rect.zw - uniform.target_rect.xy);
		pos = (floor(pos * size + off) + float2(0.5, 0.5)) / size;
		if (max(abs(pos.x - 0.5), abs(pos.y - 0.5)) > 0.5)
		{
			return float3(0.0, 0.0, 0.0);
		}
		else
		{
			PresentPass pass(res, uniform);
			float2 uv = pass.EffectsActive() ? uniform.source_rect.xy + pos.xy * (uniform.source_rect.zw - uniform.source_rect.xy) : pos.xy;
			return ToLinear(pass.sample(uv).rgb);
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

	float4 Run(float4 fragcoord)
	{
		float4 color = float4(0.0, 0.0, 0.0, 1.0);
		fragcoord -= uniform.target_rect;
		float2 inSize = ((uniform.flags & 127u) != 0u ? abs(uniform.target_rect.zw - uniform.target_rect.xy) : uniform.target_resolution - (2 * uniform.target_rect.xy));

		float2 pos = Warp(fragcoord.xy / inSize);
		color.rgb = Tri(pos);
		color.rgb += Bloom(pos) * BloomAmount;
	#if UseShadowMask
		color.rgb *= Mask(fragcoord.xy);
	#endif
		color.rgb = ToSrgb(color.rgb);

		return color;
	}
};

fragment float4 ps_filter_lottes(ConvertShaderData data [[stage_in]], ConvertPSRes res,
	constant GSMTLPresentPSUniform& uniform [[buffer(GSMTLBufferIndexUniforms)]])
{
	PresentPass pass(res, uniform);
	return pass.Finish(data.t, LottesCRTPass(res, uniform).Run(data.p));
}

fragment float4 ps_4x_rgss(ConvertShaderData data [[stage_in]], ConvertPSRes res,
	constant GSMTLPresentPSUniform& cb [[buffer(GSMTLBufferIndexUniforms)]])
{
	PresentPass pass(res, cb);
	float2 dxy = float2(dfdx(data.t.x), dfdy(data.t.y));
	float3 color = 0;

	float s = 1.0/8.0;
	float l = 3.0/8.0;

	color += pass.sample(data.t + float2( s, l) * dxy).rgb;
	color += pass.sample(data.t + float2( l,-s) * dxy).rgb;
	color += pass.sample(data.t + float2(-s,-l) * dxy).rgb;
	color += pass.sample(data.t + float2(-l, s) * dxy).rgb;

	return pass.Finish(data.t, float4(color * 0.25,1));
}

fragment float4 ps_automagical_supersampling(ConvertShaderData data [[stage_in]], ConvertPSRes res,
	constant GSMTLPresentPSUniform& cb [[buffer(GSMTLBufferIndexUniforms)]])
{
	PresentPass pass(res, cb);
	float2 ratio = (((cb.source_rect.zw - cb.source_rect.xy) * cb.source_resolution) / abs(cb.target_rect.zw - cb.target_rect.xy)) * 0.5;
	float2 steps = floor(ratio);
	float3 col = pass.sample(data.t).rgb;
	float div = 1;

	for (float y = 0; y < steps.y; y++)
	{
		for (float x = 0; x < steps.x; x++)
		{
			float2 offset = float2(x,y) - ratio * 0.5;
			col += pass.sample(data.t + offset * (1.0 / cb.source_resolution) * 2.0).rgb;
			div++;
		}
	}

	return pass.Finish(data.t, float4(col / div, 1));
}
