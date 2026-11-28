// SPDX-FileCopyrightText: 2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

// Track a proven inversion interval, rather than guessing from shadow Z. (AI-assisted)
namespace GSStereoShadow
{
	struct Key
	{
		std::uint32_t depth_base;
		std::uint32_t buffer_width;
		bool operator==(const Key&) const = default;
	};

	using Rect = std::array<int, 4>;

	inline bool Covers(std::span<const Rect> sprites, const Rect& area)
	{
		std::int64_t covered = 0;
		for (std::size_t i = 0; i < sprites.size(); i++)
		{
			const Rect& r = sprites[i];
			if (r[0] < area[0] || r[1] < area[1] || r[2] > area[2] || r[3] > area[3] ||
				r[0] >= r[2] || r[1] >= r[3])
				return false;
			for (std::size_t j = 0; j < i; j++)
			{
				const Rect& other = sprites[j];
				if (r[0] < other[2] && r[2] > other[0] && r[1] < other[3] && r[3] > other[1])
					return false;
			}
			covered += static_cast<std::int64_t>(r[2] - r[0]) * (r[3] - r[1]);
		}
		return area[0] < area[2] && area[1] < area[3] &&
		       covered == static_cast<std::int64_t>(area[2] - area[0]) * (area[3] - area[1]);
	}

	class Tracker
	{
	public:
		void Reset() { m_inverted.reset(); }

		void Invert(const Key& key, const Rect& rect)
		{
			if (m_inverted)
			{
				// A second identical operation restores depth. An overlapping or
				// unrelated operation makes our single-interval model ambiguous.
				Reset();
				return;
			}
			if (rect[0] < rect[2] && rect[1] < rect[3])
				m_inverted = Inversion{key, rect};
		}

		bool IsInverted(const Key& key, const Rect& rect) const
		{
			return m_inverted && m_inverted->key == key &&
			       rect[0] < rect[2] && rect[1] < rect[3] &&
			       rect[0] >= m_inverted->rect[0] && rect[1] >= m_inverted->rect[1] &&
			       rect[2] <= m_inverted->rect[2] && rect[3] <= m_inverted->rect[3];
		}

	private:
		struct Inversion
		{
			Key key;
			Rect rect;
		};
		std::optional<Inversion> m_inverted;
	};
} // namespace GSStereoShadow
