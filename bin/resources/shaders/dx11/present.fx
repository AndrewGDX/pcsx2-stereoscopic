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
	float2 u_source_size;
	float2 u_target_size;
	float2 u_target_resolution;
	float2 u_rcp_target_resolution; // 1 / u_target_resolution
	float2 u_source_resolution;
	float2 u_rcp_source_resolution; // 1 / u_source_resolution
	float4 u_time_and_pad;
};

Texture2D Texture;
SamplerState TextureSampler;

float4 AutoGamma(float4 color);
float3 LuminanceBlend(float3 color);
float3 Levels(float3 color);
float3 ApplyCRTGuestHD(float2 uv, float3 color);

bool StereoFixExtendEdgesEnabled()
{
	return ((u_time_and_pad.x > 0.5f && u_time_and_pad.x < 1.5f) ||
		(u_time_and_pad.x > 2.5f && u_time_and_pad.x < 3.5f));
}

bool StereoFixCRTFilterEnabled()
{
	return (u_time_and_pad.x > 1.5f);
}

float2 StereoFixAdjustUV(float2 uv)
{
	const float shift = u_source_rect.x;
	const float tilt = u_source_rect.y;
	const float extendBorder = u_target_rect.y;
	const bool extendEdges = StereoFixExtendEdgesEnabled();

	const float sourceWidth = max(u_rcp_source_resolution.x, 1.0f);
	const float shiftUV = shift / sourceWidth;
	const float tiltUV = tilt / sourceWidth;

	uv.y = (uv.x > 0.5f) ? (uv.y + tiltUV) : (uv.y - tiltUV);
	const float edgeBorder = extendEdges ? extendBorder : 0.0f;
	if (uv.x > 0.5f)
		uv.x = max(uv.x - shiftUV, 0.5f + edgeBorder);
	else
		uv.x = min(uv.x + shiftUV, 0.498f - edgeBorder);

	if (extendEdges)
		uv.x = clamp(uv.x, extendBorder, 0.998f - extendBorder);

	return uv;
}

bool StereoFixIsOutside(float2 uv)
{
	const float tilt = u_source_rect.y;
	const float sourceWidth = max(u_rcp_source_resolution.x, 1.0f);
	const float tiltUV = tilt / sourceWidth;

	if (uv.x < 0.5f)
		return (uv.y < tiltUV || uv.y > 1.0f + tiltUV);

	return (uv.y < -tiltUV || uv.y > 1.0f - tiltUV);
}

float StereoFixFade(float2 uv)
{
	const float tilt = u_source_rect.y;
	const float vignetteSize = u_source_rect.z;
	const float vignetteX = u_source_rect.w;
	const float vignetteY = u_target_rect.x;
	const float cutOffset = u_target_rect.z;
	const float vignetteOffset = u_target_rect.w;
	const bool extendEdges = StereoFixExtendEdgesEnabled();

	const float sourceWidth = max(u_rcp_source_resolution.x, 1.0f);
	const float tiltUV = tilt / sourceWidth;
	const float offsetX = vignetteOffset / sourceWidth;
	const float cutOffsetX = cutOffset / sourceWidth;
	const float fadeWidth = (vignetteSize + 0.001f) / 50.0f;
	const float edgeOffset = extendEdges ? offsetX : 0.0f;

	const float outerEdge = smoothstep(0.0f, fadeWidth, uv.x - edgeOffset) *
		smoothstep(1.0f, 1.0f - fadeWidth, uv.x + edgeOffset);
	const float innerEdge = smoothstep(0.5f, 0.5f - fadeWidth, uv.x + edgeOffset) +
		smoothstep(0.5f, 0.5f + fadeWidth, uv.x - edgeOffset);
	const float horizontalFade = outerEdge * innerEdge;

	const float vTilt1 = (uv.x < 0.5f) ? ((tiltUV > 0.0f) ? 0.0f : tiltUV) : ((tiltUV > 0.0f) ? -tiltUV : 0.0f);
	const float vTilt2 = (uv.x < 0.5f) ? ((tiltUV < 0.0f) ? 0.0f : tiltUV) : ((tiltUV < 0.0f) ? -tiltUV : 0.0f);
	const float verticalFade = smoothstep(0.0f, fadeWidth, uv.y + vTilt1) *
		smoothstep(1.0f, 1.0f - fadeWidth, uv.y + vTilt2);

	const float outerCut = smoothstep(0.0f, 0.01f, uv.x - cutOffsetX) * smoothstep(1.0f, 0.99f, uv.x + cutOffsetX);
	const float innerCut = smoothstep(0.5f, 0.49f, uv.x + cutOffsetX) + smoothstep(0.5f, 0.51f, uv.x - cutOffsetX);

	const float vignetteFade = max(vignetteX * (1.0f - horizontalFade), vignetteY * (1.0f - verticalFade));
	const float cutFade = (extendEdges && cutOffset > 0.0f) ? (1.0f - outerCut * innerCut) : 0.0f;
	return saturate(1.0f - vignetteFade - cutFade);
}

float4 StereoFixFinalize(float2 uv, float4 color)
{
	color.rgb = ApplyCRTGuestHD(uv, color.rgb);

	if (StereoFixIsOutside(uv))
		return 0.0f;

	if (u_time_and_pad.y > 0.5f)
		color = AutoGamma(color);
	if (u_time_and_pad.z > 0.5f)
		color.rgb = LuminanceBlend(color.rgb);
	if (u_time_and_pad.w > 0.5f)
		color.rgb = Levels(color.rgb);

	return color * StereoFixFade(uv);
}

float4 sample_c(float2 uv)
{
	return Texture.Sample(TextureSampler, StereoFixAdjustUV(uv));
}

float3 ApplyCRTGuestHD(float2 uv, float3 color)
{
	if (!StereoFixCRTFilterEnabled())
		return color;

	const float beamMin = 0.6f;
	const float beamMax = 0.3f;
	const float scanline1 = 0.5f;
	const float scanline2 = 1.0f;
	const float scans = 0.5f;

	const float2 sourceSize = max(u_rcp_source_resolution, float2(1.0f, 1.0f));
	float3 work = saturate(color);
	const float mx = max(max(work.r, work.g), work.b);
	const float linePos = abs(frac(uv.y * sourceSize.y) - 0.5f) * 2.0f;
	const float beam = lerp(beamMin, beamMax, mx);
	const float shape = lerp(scanline1, scanline2, linePos);
	const float lineVal = linePos * beam;
	const float scan = exp2(-shape * lineVal * lineVal * (1.0f + scans));
	work *= scan;
	return saturate(work);
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

	output.c = StereoFixFinalize(input.t, sample_c(input.t));

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

	output.c = StereoFixFinalize(input.t, ps_scanlines(input, p.y % 2));

	return output;
}

PS_OUTPUT ps_filter_diagonal(PS_INPUT input)
{
	PS_OUTPUT output;

	uint4 p = (uint4)input.p;

	output.c = StereoFixFinalize(input.t, ps_crt(input, (p.x + (p.y % 3)) % 3));

	return output;
}

PS_OUTPUT ps_filter_triangular(PS_INPUT input)
{
	PS_OUTPUT output;

	uint4 p = (uint4)input.p;

	// output.c = ps_crt(input, ((p.x + (p.y & 1) * 3) >> 1) % 3);
	output.c = StereoFixFinalize(input.t, ps_crt(input, ((p.x + ((p.y >> 1) & 1) * 3) >> 1) % 3));

	return output;
}

static const float PI = 3.14159265359f;
PS_OUTPUT ps_filter_complex(PS_INPUT input) // triangular
{
	PS_OUTPUT output;

	float2 texdim; 
	Texture.GetDimensions(texdim.x, texdim.y);

	output.c = StereoFixFinalize(input.t,
		(0.9 - 0.4 * cos(2 * PI * input.t.y * texdim.y)) * sample_c(float2(input.t.x, (floor(input.t.y * texdim.y) + 0.5) / texdim.y)));

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
	const float2 renderSize = max(u_rcp_source_resolution, float2(1.0f, 1.0f));
	pos = (floor(pos * renderSize + off) + float2(0.5f, 0.5f)) / renderSize;
	if (max(abs(pos.x - 0.5), abs(pos.y - 0.5)) > 0.5)
	{
		return float3(0.0, 0.0, 0.0);
	}
	else
	{
		return ToLinear(sample_c(pos.xy).rgb);
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

float4 LottesCRTPass(float2 uv)
{
	float4 color;
	const float2 inSize = max(u_rcp_source_resolution, float2(1.0f, 1.0f));
	const float2 fragcoord = uv * inSize;

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
	output.c = StereoFixFinalize(input.t, LottesCRTPass(input.t));

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

	output.c = StereoFixFinalize(input.t, float4(color * 0.25f, 1.0f));
	return output;
}

PS_OUTPUT ps_automagical_supersampling(PS_INPUT input)
{
	PS_OUTPUT output;

	const float2 sourceSize = max(u_rcp_source_resolution, float2(1.0f, 1.0f));
	const float2 uvStep = abs(float2(ddx(input.t.x), ddy(input.t.y)));
	float2 ratio = uvStep * sourceSize * 0.5f;
	float2 steps = clamp(floor(ratio), 0.0f, 4.0f);
	float3 col = sample_c(input.t).rgb;
	float div = 1.0f;

	for (int y = 0; y < (int)steps.y; y++)
	{
		for (int x = 0; x < (int)steps.x; x++)
		{
			float2 offset = float2(x,y) - ratio * 0.5;
			col += sample_c(input.t + offset / sourceSize * 2.0f).rgb;
			div++;
		}
	}

	output.c = StereoFixFinalize(input.t, float4(col / div, 1.0f));
	return output;
}

float3 srgb_to_linear(float3 color_srgb)
{
	return pow(max(0.0f, color_srgb), 2.2f);
}

float3 linear_to_srgb_dynamic(float3 color_linear, float exponent)
{
	return pow(max(0.0f, color_linear), exponent);
}

float GetLuminance(float3 color_linear)
{
	return dot(color_linear, float3(0.2126f, 0.7152f, 0.0722f));
}

float AdjustLuminanceContrast(float lum, float contrast, float midpoint)
{
	return lerp(midpoint, lum, contrast);
}

float4 AutoGamma(float4 color)
{
	const float contrastIntensity = u_source_size.x;
	const float midpointFocus = u_source_size.y;
	const float contrastMidpoint = u_target_size.x;
	const float gammaCompStrength = u_target_size.y;

	float3 color_linear = srgb_to_linear(color.rgb);
	float3 processed_linear = color_linear;
	const float pixelLumLinear = GetLuminance(color_linear);
	float gammaCorrectionFactor = 1.0f;

	const float distFromMid = abs(pixelLumLinear - contrastMidpoint);
	const float normRange = max(contrastMidpoint, 1.0f - contrastMidpoint);
	const float distScaled = saturate(distFromMid / max(normRange, 1e-6f));
	const float falloff = pow(distScaled, midpointFocus);
	const float contrastModulationFactor = saturate(1.0f - falloff);

	if (contrastModulationFactor > 0.0f)
	{
		const float dynamicContrast = lerp(1.0f, contrastIntensity, contrastModulationFactor);
		float adjustedLum = AdjustLuminanceContrast(pixelLumLinear, dynamicContrast, contrastMidpoint);
		adjustedLum = max(0.0f, adjustedLum);

		const float luminanceRatio = adjustedLum / max(pixelLumLinear, 1e-6f);
		const float evShift = log2(max(luminanceRatio, 1e-6f));
		gammaCorrectionFactor = 1.0f - (evShift * gammaCompStrength * 0.25f);
		gammaCorrectionFactor = clamp(gammaCorrectionFactor, 0.5f, 1.5f);

		if (pixelLumLinear <= 1e-6f)
			processed_linear = 0.0f;
		else
			processed_linear = color_linear * luminanceRatio;
	}

	const float final_exponent = (1.0f / 2.2f) / gammaCorrectionFactor;
	const float3 final_srgb = linear_to_srgb_dynamic(processed_linear, final_exponent);
	return float4(saturate(final_srgb), color.a);
}

float CalculateModulationFactor(float pixelLum, float targetPoint, float focus)
{
	const float distFromPoint = abs(pixelLum - targetPoint);
	const float normRange = max(targetPoint, 1.0f - targetPoint);
	const float distScaled = saturate(distFromPoint / max(normRange, 1e-6f));
	const float falloff = pow(distScaled, focus);
	return saturate(1.0f - falloff);
}

float3 ApplyBlend(float3 base, float3 blend)
{
	float3 r;
	r.r = (base.r < 0.5f) ? (2.0f * base.r * blend.r) : (1.0f - 2.0f * (1.0f - base.r) * (1.0f - blend.r));
	r.g = (base.g < 0.5f) ? (2.0f * base.g * blend.g) : (1.0f - 2.0f * (1.0f - base.g) * (1.0f - blend.g));
	r.b = (base.b < 0.5f) ? (2.0f * base.b * blend.b) : (1.0f - 2.0f * (1.0f - base.b) * (1.0f - blend.b));
	return r;
}

float3 LuminanceBlend(float3 color)
{
	const float opacity = u_target_resolution.x;
	const float midpointFocus2 = u_target_resolution.y;
	const float luminanceMidpoint = u_rcp_target_resolution.x;

	const float pixelLumLinear = GetLuminance(srgb_to_linear(color));
	const float blendModFactor = CalculateModulationFactor(pixelLumLinear, luminanceMidpoint, midpointFocus2);
	if (blendModFactor <= 0.0f)
		return color;

	const float3 blended_full = ApplyBlend(color, color);
	const float actualOpacity = opacity * blendModFactor;
	return saturate(lerp(color, saturate(blended_full), actualOpacity));
}

float3 Levels(float3 color)
{
	const float blackLevel = u_rcp_target_resolution.y;
	const float whiteLevel = u_source_resolution.x;
	const float temperature = u_source_resolution.y;

	const float black_point = blackLevel / 255.0f;
	const float white_point = 255.0f / ((255.0f - whiteLevel) - blackLevel);
	color *= float3(1.0f + temperature * 0.1f, 1.0f, 1.0f - temperature * 0.1f);
	return saturate(color * white_point - (black_point * white_point));
}

PS_OUTPUT ps_stereoscopic_fixes(PS_INPUT input)
{
	PS_OUTPUT output;
	output.c = StereoFixFinalize(input.t, sample_c(input.t));
	return output;
}
