// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

//#version 420 // Keep it for editor detection


#ifdef VERTEX_SHADER

layout(location = 0) in vec2 POSITION;
layout(location = 1) in vec2 TEXCOORD0;
layout(location = 7) in vec4 COLOR;

// FIXME set the interpolation (don't know what dx do)
// flat means that there is no interpolation. The value given to the fragment shader is based on the provoking vertex conventions.
//
// noperspective means that there will be linear interpolation in window-space. This is usually not what you want, but it can have its uses.
//
// smooth, the default, means to do perspective-correct interpolation.
//
// The centroid qualifier only matters when multisampling. If this qualifier is not present, then the value is interpolated to the pixel's center, anywhere in the pixel, or to one of the pixel's samples. This sample may lie outside of the actual primitive being rendered, since a primitive can cover only part of a pixel's area. The centroid qualifier is used to prevent this; the interpolation point must fall within both the pixel's area and the primitive's area.
out vec4 PSin_p;
out vec2 PSin_t;
out vec4 PSin_c;

void vs_main()
{
	PSin_p = vec4(POSITION, 0.5f, 1.0f);
	PSin_t = TEXCOORD0;
	PSin_c = COLOR;
	gl_Position = vec4(POSITION, 0.5f, 1.0f); // NOTE I don't know if it is possible to merge POSITION_OUT and gl_Position
}

#endif

#ifdef FRAGMENT_SHADER

uniform vec4 u_source_rect;
uniform vec4 u_target_rect;
uniform vec2 u_source_size;
uniform vec2 u_target_size;
uniform vec2 u_target_resolution;
uniform vec2 u_rcp_target_resolution; // 1 / u_target_resolution
uniform vec2 u_source_resolution;
uniform vec2 u_rcp_source_resolution; // 1 / u_source_resolution
uniform vec4 u_time_and_pad;

in vec4 PSin_p;
in vec2 PSin_t;
in vec4 PSin_c;

layout(binding = 0) uniform sampler2D TextureSampler;

layout(location = 0) out vec4 SV_Target0;

vec4 AutoGamma(vec4 color);
vec3 LuminanceBlend(vec3 color);
vec3 Levels(vec3 color);
vec3 ApplyCRTGuestHD(vec2 uv, vec3 color);

float StereoFixPackedFlags()
{
	return floor(u_time_and_pad.x + 0.5);
}

bool StereoFixExtendEdgesEnabled()
{
	return (mod(StereoFixPackedFlags(), 2.0) > 0.5);
}

bool StereoFixCRTFilterEnabled()
{
	return (StereoFixPackedFlags() >= 2.0);
}

vec2 StereoFixAdjustUV(vec2 uv)
{
	float shift = u_source_rect.x;
	float tilt = u_source_rect.y;
	float extendBorder = u_target_rect.y;
	bool extendEdges = StereoFixExtendEdgesEnabled();

	float sourceWidth = max(u_rcp_source_resolution.x, 1.0);
	float shiftUV = shift / sourceWidth;
	float tiltUV = tilt / sourceWidth;

	uv.y = (uv.x > 0.5) ? (uv.y + tiltUV) : (uv.y - tiltUV);
	float edgeBorder = extendEdges ? extendBorder : 0.0;
	uv.x = (uv.x > 0.5) ? max(uv.x - shiftUV, 0.5 + edgeBorder) : min(uv.x + shiftUV, 0.498 - edgeBorder);
	if (extendEdges)
		uv.x = clamp(uv.x, extendBorder, 0.998 - extendBorder);

	return uv;
}

bool StereoFixIsOutside(vec2 uv)
{
	float tilt = u_source_rect.y;
	float sourceWidth = max(u_rcp_source_resolution.x, 1.0);
	float tiltUV = tilt / sourceWidth;

	if (uv.x < 0.5)
		return (uv.y < tiltUV || uv.y > 1.0 + tiltUV);

	return (uv.y < -tiltUV || uv.y > 1.0 - tiltUV);
}

float StereoFixFade(vec2 uv)
{
	float tilt = u_source_rect.y;
	float vignetteSize = u_source_rect.z;
	float vignetteX = u_source_rect.w;
	float vignetteY = u_target_rect.x;
	float cutOffset = u_target_rect.z;
	float vignetteOffset = u_target_rect.w;
	bool extendEdges = StereoFixExtendEdgesEnabled();

	float sourceWidth = max(u_rcp_source_resolution.x, 1.0);
	float tiltUV = tilt / sourceWidth;
	float offsetX = vignetteOffset / sourceWidth;
	float cutOffsetX = cutOffset / sourceWidth;
	float fadeWidth = (vignetteSize + 0.001) / 50.0;
	float edgeOffset = extendEdges ? offsetX : 0.0;

	float outerEdge = smoothstep(0.0, fadeWidth, uv.x - edgeOffset) * smoothstep(1.0, 1.0 - fadeWidth, uv.x + edgeOffset);
	float innerEdge = smoothstep(0.5, 0.5 - fadeWidth, uv.x + edgeOffset) + smoothstep(0.5, 0.5 + fadeWidth, uv.x - edgeOffset);
	float horizontalFade = outerEdge * innerEdge;

	float vTilt1 = (uv.x < 0.5) ? ((tiltUV > 0.0) ? 0.0 : tiltUV) : ((tiltUV > 0.0) ? -tiltUV : 0.0);
	float vTilt2 = (uv.x < 0.5) ? ((tiltUV < 0.0) ? 0.0 : tiltUV) : ((tiltUV < 0.0) ? -tiltUV : 0.0);
	float verticalFade = smoothstep(0.0, fadeWidth, uv.y + vTilt1) * smoothstep(1.0, 1.0 - fadeWidth, uv.y + vTilt2);

	float outerCut = smoothstep(0.0, 0.01, uv.x - cutOffsetX) * smoothstep(1.0, 0.99, uv.x + cutOffsetX);
	float innerCut = smoothstep(0.5, 0.49, uv.x + cutOffsetX) + smoothstep(0.5, 0.51, uv.x - cutOffsetX);

	float vignetteFade = max(vignetteX * (1.0 - horizontalFade), vignetteY * (1.0 - verticalFade));
	float cutFade = (extendEdges && cutOffset > 0.0) ? (1.0 - outerCut * innerCut) : 0.0;
	return clamp(1.0 - vignetteFade - cutFade, 0.0, 1.0);
}

vec4 StereoFixFinalize(vec2 uv, vec4 color)
{
	color.rgb = ApplyCRTGuestHD(uv, color.rgb);

	if (StereoFixIsOutside(uv))
		return vec4(0.0);

	if (u_time_and_pad.y > 0.5)
		color = AutoGamma(color);
	if (u_time_and_pad.z > 0.5)
		color.rgb = LuminanceBlend(color.rgb);
	if (u_time_and_pad.w > 0.5)
		color.rgb = Levels(color.rgb);

	return color * StereoFixFade(uv);
}

vec4 sample_c(vec2 uv)
{
	return texture(TextureSampler, StereoFixAdjustUV(uv));
}

vec4 sample_c()
{
	return sample_c(PSin_t);
}

vec3 ApplyCRTGuestHD(vec2 uv, vec3 color)
{
	if (!StereoFixCRTFilterEnabled())
		return color;

	const float beamMin = 0.6;
	const float beamMax = 0.3;
	const float scanline1 = 0.5;
	const float scanline2 = 1.0;
	const float scans = 0.5;

	vec2 sourceSize = max(u_rcp_source_resolution, vec2(1.0, 1.0));
	vec3 work = clamp(color, vec3(0.0), vec3(1.0));
	float mx = max(max(work.r, work.g), work.b);
	float linePos = abs(fract(uv.y * sourceSize.y) - 0.5) * 2.0;
	float beam = mix(beamMin, beamMax, mx);
	float shape = mix(scanline1, scanline2, linePos);
	float line = linePos * beam;
	float scan = exp2(-shape * line * line * (1.0 + scans));
	work *= scan;
	return clamp(work, vec3(0.0), vec3(1.0));
}

vec4 ps_crt(uint i)
{
	vec4 mask[4] = vec4[4](
		vec4(1, 0, 0, 0),
		vec4(0, 1, 0, 0),
		vec4(0, 0, 1, 0),
		vec4(1, 1, 1, 0));
	return sample_c() * clamp((mask[i] + 0.5f), 0.0f, 1.0f);
}

#ifdef ps_copy
void ps_copy()
{
	SV_Target0 = StereoFixFinalize(PSin_t, sample_c());
}
#endif

#ifdef ps_filter_scanlines
vec4 ps_scanlines(uint i)
{
	vec4 mask[2] = vec4[2]
	(
		vec4(1, 1, 1, 0),
		vec4(0, 0, 0, 0)
	);

	return sample_c() * clamp((mask[i] + 0.5f), 0.0f, 1.0f);
}

void ps_filter_scanlines() // scanlines
{
	highp uvec4 p = uvec4(gl_FragCoord);

	vec4 c = ps_scanlines(p.y % 2u);

	SV_Target0 = StereoFixFinalize(PSin_t, c);
}
#endif

#ifdef ps_filter_diagonal
void ps_filter_diagonal() // diagonal
{
	highp uvec4 p = uvec4(gl_FragCoord);

	vec4 c = ps_crt((p.x + (p.y % 3u)) % 3u);

	SV_Target0 = StereoFixFinalize(PSin_t, c);
}
#endif

#ifdef ps_filter_triangular
void ps_filter_triangular() // triangular
{
	highp uvec4 p = uvec4(gl_FragCoord);

	vec4 c = ps_crt(((p.x + ((p.y >> 1u) & 1u) * 3u) >> 1u) % 3u);

	SV_Target0 = StereoFixFinalize(PSin_t, c);
}
#endif

#ifdef ps_filter_complex
void ps_filter_complex()
{
	const float PI = 3.14159265359f;
	vec2 texdim = vec2(textureSize(TextureSampler, 0));
	float factor = (0.9f - 0.4f * cos(2.0f * PI * PSin_t.y * texdim.y));
	vec4 c = factor * sample_c(vec2(PSin_t.x, (floor(PSin_t.y * texdim.y) + 0.5f) / texdim.y));

	SV_Target0 = StereoFixFinalize(PSin_t, c);
}
#endif

#ifdef ps_filter_lottes

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
	return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

vec3 ToLinear(vec3 c)
{
	return vec3(ToLinear1(c.r), ToLinear1(c.g), ToLinear1(c.b));
}

float ToSrgb1(float c)
{
	return c < 0.0031308 ? c * 12.92 : 1.055 * pow(c, 0.41666) - 0.055;
}

vec3 ToSrgb(vec3 c)
{
	return vec3(ToSrgb1(c.r), ToSrgb1(c.g), ToSrgb1(c.b));
}

vec3 Fetch(vec2 pos, vec2 off)
{
	vec2 renderSize = max(u_rcp_source_resolution, vec2(1.0, 1.0));
	pos = (floor(pos * renderSize + off) + vec2(0.5, 0.5)) / renderSize;
	if (max(abs(pos.x - 0.5), abs(pos.y - 0.5)) > 0.5)
	{
		return vec3(0.0, 0.0, 0.0);
	}
	else
	{
		return ToLinear(sample_c(pos.xy).rgb);
	}
}

vec2 Dist(vec2 pos)
{
	pos = pos * vec2(640, 480);

	return -((pos - floor(pos)) - vec2(0.5, 0.5));
}

float Gaus(float pos, float scale)
{
	return exp2(scale * pow(abs(pos), Shape));
}

vec3 Horz3(vec2 pos, float off)
{
	vec3 b = Fetch(pos, vec2(-1.0, off));
	vec3 c = Fetch(pos, vec2(0.0, off));
	vec3 d = Fetch(pos, vec2(1.0, off));
	float dst = Dist(pos).x;

	// Convert distance to weight.
	float scale = FilterCRTAmount;
	float wb = Gaus(dst - 1.0, scale);
	float wc = Gaus(dst + 0.0, scale);
	float wd = Gaus(dst + 1.0, scale);

	return (b * wb + c * wc + d * wd) / (wb + wc + wd);
}

vec3 Horz5(vec2 pos, float off)
{
	vec3 a = Fetch(pos, vec2(-2.0, off));
	vec3 b = Fetch(pos, vec2(-1.0, off));
	vec3 c = Fetch(pos, vec2(0.0, off));
	vec3 d = Fetch(pos, vec2(1.0, off));
	vec3 e = Fetch(pos, vec2(2.0, off));
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

vec3 Horz7(vec2 pos, float off)
{
	vec3 a = Fetch(pos, vec2(-3.0, off));
	vec3 b = Fetch(pos, vec2(-2.0, off));
	vec3 c = Fetch(pos, vec2(-1.0, off));
	vec3 d = Fetch(pos, vec2( 0.0, off));
	vec3 e = Fetch(pos, vec2( 1.0, off));
	vec3 f = Fetch(pos, vec2( 2.0, off));
	vec3 g = Fetch(pos, vec2( 3.0, off));

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
float Scan(vec2 pos, float off)
{
	float dst = Dist(pos).y;
	return Gaus(dst + off, ScanBrightness);
}

float BloomScan(vec2 pos, float off)
{
	float dst = Dist(pos).y;

	return Gaus(dst + off, BloomScanLine);
}

vec3 Tri(vec2 pos)
{
	vec3 a = Horz3(pos, -1.0);
	vec3 b = Horz5(pos, 0.0);
	vec3 c = Horz3(pos, 1.0);

	float wa = Scan(pos, -1.0);
	float wb = Scan(pos, 0.0);
	float wc = Scan(pos, 1.0);

	return (a * wa) + (b * wb) + (c * wc);
}

vec3 Bloom(vec2 pos)
{
	vec3 a = Horz5(pos,-2.0);
	vec3 b = Horz7(pos,-1.0);
	vec3 c = Horz7(pos, 0.0);
	vec3 d = Horz7(pos, 1.0);
	vec3 e = Horz5(pos, 2.0);

	float wa = BloomScan(pos,-2.0);
	float wb = BloomScan(pos,-1.0); 
	float wc = BloomScan(pos, 0.0);
	float wd = BloomScan(pos, 1.0);
	float we = BloomScan(pos, 2.0);

	return a * wa + b * wb + c * wc + d * wd + e * we;
}

vec2 Warp(vec2 pos)
{
	pos = pos * 2.0 - 1.0;
	pos *= vec2(1.0 + (pos.y * pos.y) * HorizontalWarp, 1.0 + (pos.x * pos.x) * VerticalWarp);
	return pos * 0.5 + 0.5;
}

vec3 Mask(vec2 pos)
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
	vec3 mask = vec3(MaskAmountDark, MaskAmountDark, MaskAmountDark);

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
	vec3 mask = vec3(MaskAmountDark, MaskAmountDark, MaskAmountDark);

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
	vec3 mask = vec3(MaskAmountDark, MaskAmountDark, MaskAmountDark);
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
	pos.xy = floor(pos.xy * vec2(1.0, 0.5));
	pos.x += pos.y * 3.0;

	vec3 mask = vec3(MaskAmountDark, MaskAmountDark, MaskAmountDark);
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

vec4 LottesCRTPass()
{
	vec4 color;
	vec2 inSize = max(u_rcp_source_resolution, vec2(1.0, 1.0));
	vec2 fragcoord = vec2(PSin_t.x, 1.0 - PSin_t.y) * inSize;

	vec2 pos = Warp(fragcoord / inSize);
	color.rgb = Tri(pos);
	color.rgb += Bloom(pos) * BloomAmount;
#if UseShadowMask
	color.rgb *= Mask(fragcoord);
#endif
	color.rgb = ToSrgb(color.rgb);

	return color;
}

void ps_filter_lottes()
{
	SV_Target0 = StereoFixFinalize(PSin_t, LottesCRTPass());
}

#endif

#ifdef ps_4x_rgss
void ps_4x_rgss()
{
	vec2 dxy = vec2(dFdx(PSin_t.x), dFdy(PSin_t.y));
	vec3 color = vec3(0);

	float s = 1.0/8.0;
	float l = 3.0/8.0;

	color += sample_c(PSin_t + vec2( s, l) * dxy).rgb;
	color += sample_c(PSin_t + vec2( l,-s) * dxy).rgb;
	color += sample_c(PSin_t + vec2(-s,-l) * dxy).rgb;
	color += sample_c(PSin_t + vec2(-l, s) * dxy).rgb;

	SV_Target0 = StereoFixFinalize(PSin_t, vec4(color * 0.25, 1.0));
}
#endif

#ifdef ps_automagical_supersampling
void ps_automagical_supersampling()
{
	vec2 sourceSize = max(u_rcp_source_resolution, vec2(1.0, 1.0));
	vec2 uvStep = abs(vec2(dFdx(PSin_t.x), dFdy(PSin_t.y)));
	vec2 ratio = uvStep * sourceSize * 0.5;
	vec2 steps = floor(ratio);
	vec3 col = sample_c(PSin_t).rgb;
	float div = 1.0;

	for (float y = 0; y < steps.y; y++)
	{
		for (float x = 0; x < steps.x; x++)
		{
			vec2 offset = vec2(x,y) - ratio * 0.5;
			col += sample_c(PSin_t + offset / sourceSize * 2.0).rgb;
			div++;
		}
	}

	SV_Target0 = StereoFixFinalize(PSin_t, vec4(col / div, 1.0));
}
#endif

// Stereoscopic adjustment helpers shared by all present shaders.
vec3 srgb_to_linear(vec3 color_srgb)
{
	return pow(max(vec3(0.0), color_srgb), vec3(2.2));
}

vec3 linear_to_srgb_dynamic(vec3 color_linear, float exponent)
{
	return pow(max(vec3(0.0), color_linear), vec3(exponent));
}

float GetLuminance(vec3 color_linear)
{
	return dot(color_linear, vec3(0.2126, 0.7152, 0.0722));
}

float AdjustLuminanceContrast(float lum, float contrast, float midpoint)
{
	return mix(midpoint, lum, contrast);
}

vec4 AutoGamma(vec4 color)
{
	float contrastIntensity = u_source_size.x;
	float midpointFocus = u_source_size.y;
	float contrastMidpoint = u_target_size.x;
	float gammaCompStrength = u_target_size.y;

	vec3 color_linear = srgb_to_linear(color.rgb);
	vec3 processed_linear = color_linear;
	float pixelLumLinear = GetLuminance(color_linear);
	float gammaCorrectionFactor = 1.0;

	float distFromMid = abs(pixelLumLinear - contrastMidpoint);
	float normRange = max(contrastMidpoint, 1.0 - contrastMidpoint);
	float distScaled = clamp(distFromMid / max(normRange, 1e-6), 0.0, 1.0);
	float falloff = pow(distScaled, midpointFocus);
	float contrastModulationFactor = clamp(1.0 - falloff, 0.0, 1.0);

	if (contrastModulationFactor > 0.0)
	{
		float dynamicContrast = mix(1.0, contrastIntensity, contrastModulationFactor);
		float adjustedLum = AdjustLuminanceContrast(pixelLumLinear, dynamicContrast, contrastMidpoint);
		adjustedLum = max(0.0, adjustedLum);

		float luminanceRatio = adjustedLum / max(pixelLumLinear, 1e-6);
		float evShift = log2(max(luminanceRatio, 1e-6));
		gammaCorrectionFactor = 1.0 - (evShift * gammaCompStrength * 0.25);
		gammaCorrectionFactor = clamp(gammaCorrectionFactor, 0.5, 1.5);

		if (pixelLumLinear <= 1e-6)
			processed_linear = vec3(0.0);
		else
			processed_linear = color_linear * luminanceRatio;
	}

	float final_exponent = (1.0 / 2.2) / gammaCorrectionFactor;
	vec3 final_srgb = linear_to_srgb_dynamic(processed_linear, final_exponent);
	return vec4(clamp(final_srgb, 0.0, 1.0), color.a);
}

float CalculateModulationFactor(float pixelLum, float targetPoint, float focus)
{
	float distFromPoint = abs(pixelLum - targetPoint);
	float normRange = max(targetPoint, 1.0 - targetPoint);
	float distScaled = clamp(distFromPoint / max(normRange, 1e-6), 0.0, 1.0);
	float falloff = pow(distScaled, focus);
	return clamp(1.0 - falloff, 0.0, 1.0);
}

vec3 ApplyBlend(vec3 base, vec3 blend)
{
	vec3 r;
	r.r = (base.r < 0.5) ? (2.0 * base.r * blend.r) : (1.0 - 2.0 * (1.0 - base.r) * (1.0 - blend.r));
	r.g = (base.g < 0.5) ? (2.0 * base.g * blend.g) : (1.0 - 2.0 * (1.0 - base.g) * (1.0 - blend.g));
	r.b = (base.b < 0.5) ? (2.0 * base.b * blend.b) : (1.0 - 2.0 * (1.0 - base.b) * (1.0 - blend.b));
	return r;
}

vec3 LuminanceBlend(vec3 color)
{
	float opacity = u_target_resolution.x;
	float midpointFocus2 = u_target_resolution.y;
	float luminanceMidpoint = u_rcp_target_resolution.x;
	float pixelLumLinear = GetLuminance(srgb_to_linear(color));
	float blendModFactor = CalculateModulationFactor(pixelLumLinear, luminanceMidpoint, midpointFocus2);
	if (blendModFactor <= 0.0)
		return color;

	vec3 blended_full = ApplyBlend(color, color);
	float actualOpacity = opacity * blendModFactor;
	return clamp(mix(color, clamp(blended_full, 0.0, 1.0), actualOpacity), 0.0, 1.0);
}

vec3 Levels(vec3 color)
{
	float blackLevel = u_rcp_target_resolution.y;
	float whiteLevel = u_source_resolution.x;
	float temperature = u_source_resolution.y;
	float black_point = blackLevel / 255.0;
	float white_point = 255.0 / ((255.0 - whiteLevel) - blackLevel);
	color *= vec3(1.0 + temperature * 0.1, 1.0, 1.0 - temperature * 0.1);
	return clamp(color * white_point - (black_point * white_point), 0.0, 1.0);
}

#ifdef ps_stereoscopic_fixes
void ps_stereoscopic_fixes()
{
	SV_Target0 = StereoFixFinalize(PSin_t, sample_c(PSin_t));
}
#endif

#endif
