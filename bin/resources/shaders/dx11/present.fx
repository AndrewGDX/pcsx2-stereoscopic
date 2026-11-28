// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

struct VS_INPUT
{
	float4 p : POSITION;
	float2 t : TEXCOORD0;
	float4 c : COLOR;
};

struct VS_OUTPUT
{
	float4 p : SV_Position;
	float2 t : TEXCOORD0;
	float4 c : COLOR;
};

cbuffer cb0 : register(b0)
{
float4 u_source_rect;
float4 u_target_rect;
float2 u_source_resolution;
float2 u_target_resolution;
float4 u_geometry;
float4 u_edges;
float4 u_contrast;
float4 u_blend;
float3 u_levels_time;
uint u_flags;

};
#define u_shift u_geometry.x
#define u_tilt u_geometry.y
#define u_vignette_width u_geometry.z
#define u_vignette_x u_geometry.w
#define u_vignette_y u_edges.x
#define u_extend_border u_edges.y
#define u_cut_offset u_edges.z
#define u_vignette_offset u_edges.w
#define u_contrast_intensity u_contrast.x
#define u_contrast_focus u_contrast.y
#define u_contrast_midpoint u_contrast.z
#define u_gamma_strength u_contrast.w
#define u_blend_opacity u_blend.x
#define u_blend_focus u_blend.y
#define u_blend_midpoint u_blend.z
#define u_temperature u_blend.w
#define u_black_point u_levels_time.x
#define u_levels_scale u_levels_time.y
#define u_time u_levels_time.z
#define u_source_size ((u_source_rect.zw - u_source_rect.xy) * u_source_resolution)
#define u_target_size abs(u_target_rect.zw - u_target_rect.xy)
#define u_rcp_source_resolution (1.0 / u_source_resolution)
#define u_rcp_target_resolution (1.0 / u_target_resolution)


Texture2D Texture;
SamplerState TextureSampler;

// Eye-local sampling and final color processing; flags mirror GSPresentationEffects. (AI-assisted)
bool EffectsActive() { return (u_flags & 127u) != 0u; }
bool EffectEnabled(uint flag) { return (u_flags & flag) != 0u; }

float2 EyeUV(float2 uv)
{
	return (uv - u_source_rect.xy) / max(u_source_rect.zw - u_source_rect.xy, float2(1e-8, 1e-8));
}

float2 AdjustedEyeUV(float2 uv)
{
	return EyeUV(uv) + float2(u_shift, u_tilt);
}

float2 SampleUV(float2 uv)
{
	if (!EffectsActive())
		return uv;
	float2 extent = u_source_rect.zw - u_source_rect.xy;
	float2 half_texel = min(0.5 / u_source_resolution, extent * 0.5);
	float2 inset = half_texel;
	if (EffectEnabled(2u))
		inset = max(inset, float2(u_extend_border, 0.0) * extent);
	return clamp(u_source_rect.xy + AdjustedEyeUV(uv) * extent,
		u_source_rect.xy + inset, u_source_rect.zw - inset);
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
		return float4(0.0, 0.0, 0.0, 0.0);
	if (EffectEnabled(64u))
	{
		float3 work = clamp(color.rgb, 0.0, 1.0);
		float brightness = max(max(work.r, work.g), work.b);
		float line_pos = abs(frac(q.y * u_source_size.y) - 0.5) * 2.0;
		float beam_line = line_pos * lerp(0.6, 0.3, brightness);
		color.rgb = work * exp2(-lerp(0.5, 1.0, line_pos) * beam_line * beam_line * 1.5);
	}
	if (EffectEnabled(8u))
	{
		float3 linear_color = pow(max(color.rgb, float3(0.0, 0.0, 0.0)), float3(2.2, 2.2, 2.2));
		float luminance = dot(linear_color, float3(0.2126, 0.7152, 0.0722));
		float contrast = lerp(1.0, u_contrast_intensity, Modulation(luminance, u_contrast_midpoint, u_contrast_focus));
		float adjusted_luminance = max(0.0, lerp(u_contrast_midpoint, luminance, contrast));
		float ratio = adjusted_luminance / max(luminance, 1e-6);
		float gamma = clamp(1.0 - log2(max(ratio, 1e-6)) * u_gamma_strength * 0.25, 0.5, 1.5);
		linear_color = luminance <= 1e-6 ? float3(0.0, 0.0, 0.0) : linear_color * ratio;
		color.rgb = clamp(pow(linear_color, ((1.0 / 2.2) / gamma).xxx), 0.0, 1.0);
	}
	if (EffectEnabled(16u))
	{
		float luminance = dot(pow(max(color.rgb, float3(0.0, 0.0, 0.0)), float3(2.2, 2.2, 2.2)), float3(0.2126, 0.7152, 0.0722));
		float3 low = 2.0 * color.rgb * color.rgb;
		float3 high = 1.0 - 2.0 * (1.0 - color.rgb) * (1.0 - color.rgb);
		float3 blended = float3(color.r < 0.5 ? low.r : high.r, color.g < 0.5 ? low.g : high.g, color.b < 0.5 ? low.b : high.b);
		color.rgb = clamp(lerp(color.rgb, clamp(blended, 0.0, 1.0), u_blend_opacity * Modulation(luminance, u_blend_midpoint, u_blend_focus)), 0.0, 1.0);
	}
	if (EffectEnabled(32u))
		color.rgb = clamp((color.rgb * float3(1.0 + u_temperature, 1.0, 1.0 - u_temperature) - u_black_point) * u_levels_scale, 0.0, 1.0);
	float fade = 1.0;
	if (EffectEnabled(4u))
	{
		float offset = EffectEnabled(2u) ? u_vignette_offset : 0.0;
		float horizontal = smoothstep(0.0, u_vignette_width * 2.0, q.x - offset) * (1.0 - smoothstep(1.0 - u_vignette_width * 2.0, 1.0, q.x + offset));
		float vertical = smoothstep(0.0, u_vignette_width, adjusted.y) * (1.0 - smoothstep(1.0 - u_vignette_width, 1.0, adjusted.y));
		fade -= max(u_vignette_x * (1.0 - horizontal), u_vignette_y * (1.0 - vertical));
	}
	if (EffectEnabled(2u) && u_cut_offset > 0.0)
	{
		float cut = smoothstep(0.0, 0.02, q.x - u_cut_offset) * (1.0 - smoothstep(0.98, 1.0, q.x + u_cut_offset));
		fade -= 1.0 - cut;
	}
	return color * clamp(fade, 0.0, 1.0);
}

float4 sample_c(float2 uv)
{
	return Texture.Sample(TextureSampler, SampleUV(uv));
}

struct PS_INPUT
{
	float4 p : SV_Position;
	float2 t : TEXCOORD0;
	float4 c : COLOR;
};

struct PS_OUTPUT
{
	float4 c : SV_Target0;
};

VS_OUTPUT vs_main(VS_INPUT input)
{
	VS_OUTPUT output;

	output.p = input.p;
	output.t = input.t;
	output.c = input.c;

	return output;
}

PS_OUTPUT ps_copy(PS_INPUT input)
{
	PS_OUTPUT output;

	output.c = Finish(input.t, sample_c(input.t));

	return output;
}

float4 ps_crt(PS_INPUT input, int i)
{
	float4 mask[4] =
		{
			float4(1, 0, 0, 0),
			float4(0, 1, 0, 0),
			float4(0, 0, 1, 0),
			float4(1, 1, 1, 0)
		};

	return sample_c(input.t) * saturate(mask[i] + 0.5f);
}

float4 ps_scanlines(PS_INPUT input, int i)
{
	float4 mask[2] =
		{
			float4(1, 1, 1, 0),
			float4(0, 0, 0, 0)
		};

	return sample_c(input.t) * saturate(mask[i] + 0.5f);
}

PS_OUTPUT ps_filter_scanlines(PS_INPUT input)
{
	PS_OUTPUT output;

	uint4 p = (uint4)input.p;

	output.c = Finish(input.t, ps_scanlines(input, p.y % 2));

	return output;
}

PS_OUTPUT ps_filter_diagonal(PS_INPUT input)
{
	PS_OUTPUT output;

	uint4 p = (uint4)input.p;

	output.c = Finish(input.t, ps_crt(input, (p.x + (p.y % 3)) % 3));

	return output;
}

PS_OUTPUT ps_filter_triangular(PS_INPUT input)
{
	PS_OUTPUT output;

	uint4 p = (uint4)input.p;

	// output.c = Finish(input.t, ps_crt(input, ((p.x + (p.y & 1) * 3) >> 1) % 3));
	output.c = Finish(input.t, ps_crt(input, ((p.x + ((p.y >> 1) & 1) * 3) >> 1) % 3));

	return output;
}

static const float PI = 3.14159265359f;
PS_OUTPUT ps_filter_complex(PS_INPUT input) // triangular
{
	PS_OUTPUT output;

	float2 texdim; 
	Texture.GetDimensions(texdim.x, texdim.y);

	output.c = Finish(input.t, (0.9 - 0.4 * cos(2 * PI * input.t.y * texdim.y)) * sample_c(float2(input.t.x, (floor(input.t.y * texdim.y) + 0.5) / texdim.y)));

	return output;
}

//Lottes CRT
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

float ToLinear1(float c)
{
	return c <= 0.04045 ? c / 12.92 : pow((abs(c) + 0.055) / 1.055, 2.4);
}

float3 ToLinear(float3 c)
{
	return float3(ToLinear1(c.r), ToLinear1(c.g), ToLinear1(c.b));
}

float ToSrgb1(float c)
{
	return c < 0.0031308 ? c * 12.92 : 1.055 * pow(abs(c), 0.41666) - 0.055;
}

float3 ToSrgb(float3 c)
{
	return float3(ToSrgb1(c.r), ToSrgb1(c.g), ToSrgb1(c.b));
}

float3 Fetch(float2 pos, float2 off)
{
	pos = (floor(pos * (EffectsActive() ? u_source_size : u_target_size) + off) + float2(0.5, 0.5)) / (EffectsActive() ? u_source_size : u_target_size);
	if (max(abs(pos.x - 0.5), abs(pos.y - 0.5)) > 0.5)
	{
		return float3(0.0, 0.0, 0.0);
	}
	else
	{
		return ToLinear(sample_c(EffectsActive() ? u_source_rect.xy + pos.xy * (u_source_rect.zw - u_source_rect.xy) : pos.xy).rgb);
	}
}

float2 Dist(float2 pos)
{
	pos = pos * float2(640, 480);

	return -((pos - floor(pos)) - float2(0.5, 0.5));
}

float Gaus(float pos, float scale)
{
	return exp2(scale * pos * pos);
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

	if (frac(pos.x / 6.0) < 0.5)
	{
		odd = 1.0;
	}
	if (frac((pos.y + odd) / 2.0) < 0.5)
	{
		lines = MaskAmountDark;
	}
	pos.x = frac(pos.x / 3.0);
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
	pos.x = frac(pos.x / 3.0);
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
	pos.x = frac(pos.x / 6.0);

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
	pos.x = frac(pos.x / 6.0);

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

float4 LottesCRTPass(float4 fragcoord)
{
	float4 color = float4(0.0, 0.0, 0.0, 1.0);
	fragcoord -= u_target_rect;
	float2 inSize = (EffectsActive() ? u_target_size : u_target_resolution - (2 * u_target_rect.xy));

	float2 pos = Warp(fragcoord.xy / inSize);
	color.rgb = Tri(pos);
	color.rgb += Bloom(pos) * BloomAmount;
#if UseShadowMask
	color.rgb *= Mask(fragcoord.xy);
#endif
	color.rgb = ToSrgb(color.rgb);
	color.a = 1.0;

	return color;
}

PS_OUTPUT ps_filter_lottes(PS_INPUT input)
{
	PS_OUTPUT output;
	output.c = Finish(input.t, LottesCRTPass(input.p));

	return output;
}

PS_OUTPUT ps_4x_rgss(PS_INPUT input)
{
	PS_OUTPUT output;

	float2 dxy = float2(ddx(input.t.x), ddy(input.t.y));
	float3 color = 0;

	float s = 1.0/8.0;
	float l = 3.0/8.0;

	color += sample_c(input.t + float2( s, l) * dxy).rgb;
	color += sample_c(input.t + float2( l,-s) * dxy).rgb;
	color += sample_c(input.t + float2(-s,-l) * dxy).rgb;
	color += sample_c(input.t + float2(-l, s) * dxy).rgb;

	output.c = Finish(input.t, float4(color * 0.25,1));
	return output;
}

PS_OUTPUT ps_automagical_supersampling(PS_INPUT input)
{
	PS_OUTPUT output;

	float2 ratio = (u_source_size / u_target_size) * 0.5;
	float2 steps = floor(ratio);
	float3 col = sample_c(input.t).rgb;
	float div = 1;

	for (float y = 0; y < steps.y; y++)
	{
		for (float x = 0; x < steps.x; x++)
		{
			float2 offset = float2(x,y) - ratio * 0.5;
			col += sample_c(input.t + offset * u_rcp_source_resolution * 2.0).rgb;
			div++;
		}
	}

	output.c = Finish(input.t, float4(col / div, 1));
	return output;
}
