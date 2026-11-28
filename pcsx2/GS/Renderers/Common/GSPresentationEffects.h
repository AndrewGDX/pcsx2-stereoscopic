// SPDX-FileCopyrightText: 2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "Config.h"
#include "GS/GSVector.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

// Presentation-only parameters, prepared once per eye rather than per pixel. (AI-assisted)
namespace GSPresentationEffects
{
	enum Flag : u32
	{
		ShiftTilt = 1,
		ExtendEdges = 2,
		Vignette = 4,
		Contrast = 8,
		Blend = 16,
		Levels = 32,
		CRT = 64,
		FlipY = 128,
	};
	enum class Eye
	{
		Mono,
		Left,
		Right
	};

	struct Parameters
	{
		float shift = 0, tilt = 0, vignette_width = 0, vignette_x = 0;
		float vignette_y = 0, extend_border = 0, cut_offset = 0, vignette_offset = 0;
		float contrast_intensity = 1, contrast_focus = 2, contrast_midpoint = 0.5f, gamma_strength = 1;
		float blend_opacity = 0, blend_focus = 8, blend_midpoint = 0.5f, temperature = 0;
		float black_point = 0, levels_scale = 1, time = 0;
		u32 flags = 0;
	};
	static_assert(sizeof(Parameters) == 80);
	static_assert(offsetof(Parameters, flags) == 76);

	inline float Finite(float value, float minimum, float maximum, float fallback = 0)
	{
		return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
	}

	// Native renderer switches on Windows/Linux need proper testing. (AI-assisted)
	inline Parameters Prepare(const Pcsx2Config::GSOptions& options, Eye eye, GSVector2 native_size,
		GSVector2 source_size, float time, bool flip_y)
	{
		Parameters p;
		p.time = time;
		p.flags = flip_y ? FlipY : 0;
		const bool stereo = eye != Eye::Mono;
		if (stereo && options.StereoFix_EnableShiftTilt)
		{
			p.flags |= ShiftTilt;
			const float sign = eye == Eye::Left ? 1.0f : -1.0f;
			p.shift = sign * Finite(options.StereoFix_Shift, -30, 30) / std::max(native_size.x, 1.0f);
			p.tilt = -sign * Finite(options.StereoFix_Tilt, -10, 10) / std::max(native_size.y, 1.0f);
		}
		if (stereo && options.StereoFix_EnableVignette)
		{
			p.flags |= Vignette;
			p.vignette_width = (Finite(options.StereoFix_VignetteSize, 0, 1, 0.13f) + 0.001f) / 50.0f;
			p.vignette_x = Finite(options.StereoFix_VignetteX, 0, 2, 0.75f);
			p.vignette_y = Finite(options.StereoFix_VignetteY, 0, 2, 0.5f);
		}
		if (stereo && options.StereoFix_ExtendEdges)
		{
			p.flags |= ExtendEdges;
			p.extend_border = Finite(options.StereoFix_ExtendBorder, 0, 0.1f) * 2.0f;
			p.cut_offset = Finite(options.StereoFix_CutOffset, 0, 200) / std::max(source_size.x, 1.0f);
			p.vignette_offset = Finite(options.StereoFix_VignetteOffset, 0, 250) / std::max(source_size.x, 1.0f);
		}
		if (options.StereoFix_EnableAutoGamma)
		{
			p.flags |= Contrast;
			p.contrast_intensity = Finite(options.StereoFix_ContrastIntensity, 0.01f, 1, 0.92f);
			p.contrast_focus = Finite(options.StereoFix_MidpointFocus, 0.01f, 32, 2);
			p.contrast_midpoint = Finite(options.StereoFix_ContrastMidpoint, 0, 1.5f, 0.5f);
			p.gamma_strength = Finite(options.StereoFix_GammaCompStrength, 0, 2, 1);
		}
		if (options.StereoFix_EnableLuminanceBlend)
		{
			p.flags |= Blend;
			p.blend_opacity = Finite(options.StereoFix_Opacity, 0, 1, 0.22f);
			p.blend_focus = Finite(options.StereoFix_MidpointFocus2, 0.01f, 32, 8);
			p.blend_midpoint = Finite(options.StereoFix_LuminanceMidpoint, 0, 1, 0.5f);
		}
		if (options.StereoFix_EnableLevels)
		{
			p.flags |= Levels;
			p.black_point = Finite(options.StereoFix_BlackLevel, 0, 16) / 255.0f;
			p.levels_scale = 255.0f / (255.0f - Finite(options.StereoFix_WhiteLevel, 0, 32) - p.black_point * 255.0f);
			p.temperature = Finite(options.StereoFix_Temperature, -1, 1) * 0.1f;
		}
		if (options.StereoFix_EnableCRTFilter)
			p.flags |= CRT;
		return p;
	}
} // namespace GSPresentationEffects
