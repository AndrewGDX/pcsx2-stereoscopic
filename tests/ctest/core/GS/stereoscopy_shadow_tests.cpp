// SPDX-FileCopyrightText: 2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Regression coverage for alternating normal/inverted Jak II shadow passes. (AI-assisted)
#include "GS/Renderers/Common/GSStereoShadow.h"
#include <gtest/gtest.h>

namespace
{
	constexpr GSStereoShadow::Key scene{0x2600, 8};
	constexpr GSStereoShadow::Rect screen{0, 0, 512, 416};
	constexpr GSStereoShadow::Rect shadow{240, 225, 260, 238};
} // namespace

TEST(StereoscopyShadow, NormalPassesStayUnchangedBeforeAndAfterInversion)
{
	GSStereoShadow::Tracker tracker;
	for (unsigned frame = 0; frame < 6; frame++)
	{
		EXPECT_FALSE(tracker.IsInverted(scene, shadow));
		tracker.Invert(scene, screen);
		EXPECT_TRUE(tracker.IsInverted(scene, shadow));
		tracker.Invert(scene, screen);
		EXPECT_FALSE(tracker.IsInverted(scene, shadow));
	}
}

TEST(StereoscopyShadow, InversionDoesNotFollowOtherDepthStorageOrWidths)
{
	GSStereoShadow::Tracker tracker;
	tracker.Invert(scene, screen);
	EXPECT_FALSE(tracker.IsInverted({0x1200, 8}, shadow));
	EXPECT_FALSE(tracker.IsInverted({0x2600, 4}, shadow));
	EXPECT_TRUE(tracker.IsInverted(scene, shadow));
}

TEST(StereoscopyShadow, DrawMustBeContainedInInvertedRegion)
{
	GSStereoShadow::Tracker tracker;
	tracker.Invert(scene, shadow);
	EXPECT_TRUE(tracker.IsInverted(scene, shadow));
	EXPECT_FALSE(tracker.IsInverted(scene, screen));
	EXPECT_FALSE(tracker.IsInverted(scene, {240, 225, 240, 238}));
	tracker.Invert(scene, screen);
	EXPECT_FALSE(tracker.IsInverted(scene, shadow));
}

TEST(StereoscopyShadow, UnknownWritesAndFrameBoundariesDiscardInversion)
{
	GSStereoShadow::Tracker tracker;
	tracker.Invert(scene, screen);
	tracker.Reset();
	EXPECT_FALSE(tracker.IsInverted(scene, shadow));
	tracker.Invert(scene, {0, 0, 0, 0});
	EXPECT_FALSE(tracker.IsInverted(scene, shadow));
}

TEST(StereoscopyShadow, InversionRequiresCompleteNonOverlappingCoverage)
{
	const std::array<GSStereoShadow::Rect, 2> strips{{{0, 0, 256, 416}, {256, 0, 512, 416}}};
	EXPECT_TRUE(GSStereoShadow::Covers(strips, screen));
	auto gap = strips;
	gap[1][0]++;
	EXPECT_FALSE(GSStereoShadow::Covers(gap, screen));
	auto overlap = strips;
	overlap[1][0]--;
	EXPECT_FALSE(GSStereoShadow::Covers(overlap, screen));
	EXPECT_FALSE(GSStereoShadow::Covers({}, screen));
}
