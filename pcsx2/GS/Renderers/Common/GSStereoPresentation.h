// SPDX-FileCopyrightText: 2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "Config.h"
#include "GS/GSVector.h"

#include <array>

namespace GSStereoPresentation
{
	// Half formats fit the whole image before compression. Full formats fit each
	// eye independently, preserving its aspect ratio. (AI-assisted)
	struct Eye
	{
		int width;
		int height;
		float scale_x = 1.0f;
		float scale_y = 1.0f;
		float offset_x = 0.0f;
		float offset_y = 0.0f;

		GSVector4 Transform(GSVector4 rect) const
		{
			return rect * GSVector4(scale_x, scale_y, scale_x, scale_y) +
			       GSVector4(offset_x, offset_y, offset_x, offset_y);
		}
	};

	inline std::array<Eye, 2> GetEyes(GSStereoMode mode, int width, int height, bool flip_y)
	{
		std::array<Eye, 2> eyes = {{{width, height}, {width, height}}};
		switch (mode)
		{
			case GSStereoMode::HalfSideBySide:
				eyes[0].scale_x = eyes[1].scale_x = 0.5f;
				eyes[1].offset_x = static_cast<float>(width) * 0.5f;
				break;
			case GSStereoMode::FullSideBySide:
				eyes[0].width = width / 2;
				eyes[1].width = width - eyes[0].width;
				eyes[1].offset_x = static_cast<float>(eyes[0].width);
				break;
			case GSStereoMode::HalfTopAndBottom:
				eyes[0].scale_y = eyes[1].scale_y = 0.5f;
				eyes[flip_y ? 0 : 1].offset_y = static_cast<float>(height) * 0.5f;
				break;
			case GSStereoMode::FullTopAndBottom:
				eyes[0].height = height / 2;
				eyes[1].height = height - eyes[0].height;
				eyes[flip_y ? 0 : 1].offset_y = static_cast<float>(eyes[flip_y ? 1 : 0].height);
				break;
			default:
				break;
		}
		return eyes;
	}

	// Compute both rectangles before swapping, since odd source widths can give
	// the two eyes different aspect-ratio fits.
	template <typename FitRect>
	std::array<GSVector4, 2> GetRects(GSStereoMode mode, int width, int height, bool flip_y,
		bool swap_eyes, FitRect fit_rect)
	{
		const auto eyes = GetEyes(mode, width, height, flip_y);
		std::array<GSVector4, 2> rects = {
			eyes[0].Transform(fit_rect(eyes[0].width, eyes[0].height, 0)),
			eyes[1].Transform(fit_rect(eyes[1].width, eyes[1].height, 1))};
		if (swap_eyes)
			std::swap(rects[0], rects[1]);
		return rects;
	}
} // namespace GSStereoPresentation
