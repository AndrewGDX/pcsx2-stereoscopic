// SPDX-FileCopyrightText: 2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include <algorithm>
#include <cstdint>

// Coordinate transforms shared by cache transfers and shader constant setup.
// Pixels are in the legacy, full-atlas-scaled logical space until resolved here.
// Eye placement is translation plus compression, never just a larger scale.
namespace GSPackedStereo
{
enum class Axis : std::uint8_t
{
	None,
	Horizontal,
	Vertical,
};

struct Rect
{
	float x, y, z, w;
};

inline Rect Pixels(Rect r, Axis axis, float width, float height, unsigned eye)
{
	if (axis == Axis::Horizontal)
	{
		r.x = r.x * 0.5f + eye * width * 0.5f;
		r.z = r.z * 0.5f + eye * width * 0.5f;
	}
	else if (axis == Axis::Vertical)
	{
		r.y = r.y * 0.5f + eye * height * 0.5f;
		r.w = r.w * 0.5f + eye * height * 0.5f;
	}
	return r;
}

inline Rect UV(Rect r, Axis axis, unsigned eye)
{
	return Pixels(r, axis, 1.0f, 1.0f, eye);
}

inline Rect ClampUV(Rect r, Axis axis, float width, float height, unsigned eye)
{
	// Preserve orientation (including flipped source rectangles).
	const Rect bounds = UV({0, 0, 1, 1}, axis, eye);
	const float hx = 0.5f / std::max(width, 1.0f);
	const float hy = 0.5f / std::max(height, 1.0f);
	r.x = std::clamp(r.x, bounds.x + hx, std::max(bounds.x + hx, bounds.z - hx));
	r.z = std::clamp(r.z, bounds.x + hx, std::max(bounds.x + hx, bounds.z - hx));
	r.y = std::clamp(r.y, bounds.y + hy, std::max(bounds.y + hy, bounds.w - hy));
	r.w = std::clamp(r.w, bounds.y + hy, std::max(bounds.y + hy, bounds.w - hy));
	return r;
}
} // namespace GSPackedStereo
