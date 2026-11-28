// SPDX-FileCopyrightText: 2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Stereoscopy configuration and presentation regression tests. (AI-assisted)
#include "Config.h"
#include "GS/Renderers/Common/GSPackedStereo.h"
#include "GS/Renderers/Common/GSStereoPresentation.h"
#include "common/MemorySettingsInterface.h"
#include "common/SettingsWrapper.h"

#include <gtest/gtest.h>

namespace
{
	void ExpectRect(const GSVector4& rect, float x, float y, float z, float w)
	{
		EXPECT_FLOAT_EQ(rect.x, x);
		EXPECT_FLOAT_EQ(rect.y, y);
		EXPECT_FLOAT_EQ(rect.z, z);
		EXPECT_FLOAT_EQ(rect.w, w);
	}

	GSVector4 FitFourByThree(int width, int height, unsigned)
	{
		const float w = std::min(static_cast<float>(width), static_cast<float>(height) * (4.0f / 3.0f));
		const float h = w * 0.75f;
		const float x = (width - w) * 0.5f;
		const float y = (height - h) * 0.5f;
		return GSVector4(x, y, x + w, y + h);
	}
} // namespace

TEST(Stereoscopy, FreshConfigurationUsesHalfSbsAndPackedDefaults)
{
	MemorySettingsInterface settings;
	SettingsLoadWrapper load(settings);
	Pcsx2Config::GSOptions options;
	options.LoadSave(load);
	EXPECT_EQ(options.StereoMode, GSStereoMode::HalfSideBySide);
	EXPECT_EQ(options.StereoDebugAction, GSStereoDebugAction::None);
	EXPECT_TRUE(options.StereoPackedEnable);
	EXPECT_FALSE(options.StereoProjectionDepth);
	EXPECT_FALSE(options.StereoFixStencilShadows4);
	EXPECT_EQ(options.StereoFeedbackLoopFix, 0);
	EXPECT_EQ(options.StereoSbsRemapFix, 3);
}

TEST(Stereoscopy, EveryOutputAndDebugActionRoundTrips)
{
	for (unsigned mode = 0; mode < static_cast<unsigned>(GSStereoMode::Count); mode++)
	{
		for (unsigned action = 0; action < static_cast<unsigned>(GSStereoDebugAction::Count); action++)
		{
			SCOPED_TRACE(testing::Message() << "mode=" << mode << " action=" << action);
			MemorySettingsInterface settings;
			Pcsx2Config::GSOptions options;
			options.StereoMode = static_cast<GSStereoMode>(mode);
			options.StereoDebugAction = static_cast<GSStereoDebugAction>(action);
			options.StereoSeparation = 13.7f;
			options.StereoConvergence = 567.8f;
			options.StereoFeedbackLoopFix = 2;
			options.StereoSbsRemapFix = 1;
			options.StereoProjectionDepth = true;
			options.StereoFixStencilShadows4 = true;
			SettingsSaveWrapper save(settings);
			options.LoadSave(save);
			Pcsx2Config::GSOptions restored;
			SettingsLoadWrapper load(settings);
			restored.LoadSave(load);
			EXPECT_EQ(restored.StereoMode, options.StereoMode);
			EXPECT_EQ(restored.StereoDebugAction, options.StereoDebugAction);
			EXPECT_FLOAT_EQ(restored.StereoSeparation, 13.7f);
			EXPECT_FLOAT_EQ(restored.StereoConvergence, 567.8f);
			EXPECT_EQ(restored.StereoFeedbackLoopFix, 2);
			EXPECT_EQ(restored.StereoSbsRemapFix, 1);
			EXPECT_TRUE(restored.StereoProjectionDepth);
			EXPECT_TRUE(restored.StereoFixStencilShadows4);
			EXPECT_FALSE(settings.ContainsValue("EmuCore/GS", "StereoDisableInstancedRendering"));
			EXPECT_FALSE(settings.ContainsValue("EmuCore/GS", "StereoFlipRendering"));
			EXPECT_FALSE(settings.ContainsValue("EmuCore/GS", "StereoEnableFullSbsAspectRatio"));
			EXPECT_FALSE(settings.ContainsValue("EmuCore/GS", "StereoMasterFix1"));
			EXPECT_FALSE(settings.ContainsValue("EmuCore/GS", "StereoRenderCheckedObjectsMono"));
			EXPECT_FALSE(settings.ContainsValue("EmuCore/GS", "StereoRejectFmvActive"));
		}
	}
}

TEST(Stereoscopy, ObsoleteModeFallsBackWithoutMigration)
{
	MemorySettingsInterface settings;
	settings.SetStringValue("EmuCore/GS", "StereoMode", "Side by Side");
	settings.SetBoolValue("EmuCore/GS", "StereoEnableFullSbsAspectRatio", true);
	settings.SetBoolValue("EmuCore/GS", "StereoRenderCheckedObjectsMono", true);
	SettingsLoadWrapper load(settings);
	Pcsx2Config::GSOptions options;
	options.LoadSave(load);
	EXPECT_EQ(options.StereoMode, GSStereoMode::HalfSideBySide);
	EXPECT_EQ(options.StereoDebugAction, GSStereoDebugAction::None);
}

TEST(Stereoscopy, HalfFormatsCompressTheWholeImage)
{
	const auto sbs = GSStereoPresentation::GetRects(GSStereoMode::HalfSideBySide, 1920, 1080, false, false, FitFourByThree);
	ExpectRect(sbs[0], 120, 0, 840, 1080);
	ExpectRect(sbs[1], 1080, 0, 1800, 1080);
	const auto tab = GSStereoPresentation::GetRects(GSStereoMode::HalfTopAndBottom, 1920, 1080, false, false, FitFourByThree);
	ExpectRect(tab[0], 240, 0, 1680, 540);
	ExpectRect(tab[1], 240, 540, 1680, 1080);
}

TEST(Stereoscopy, FullFormatsPreserveEachEyesAspectRatio)
{
	const auto sbs = GSStereoPresentation::GetRects(GSStereoMode::FullSideBySide, 1920, 1080, false, false, FitFourByThree);
	ExpectRect(sbs[0], 0, 180, 960, 900);
	ExpectRect(sbs[1], 960, 180, 1920, 900);
	const auto tab = GSStereoPresentation::GetRects(GSStereoMode::FullTopAndBottom, 1920, 1080, false, false, FitFourByThree);
	ExpectRect(tab[0], 600, 0, 1320, 540);
	ExpectRect(tab[1], 600, 540, 1320, 1080);
}

TEST(Stereoscopy, OddDimensionsAndLowerLeftOriginPreserveEveryPixel)
{
	const auto fill = [](int width, int height, unsigned) { return GSVector4(0, 0, width, height); };
	const auto sbs = GSStereoPresentation::GetRects(GSStereoMode::FullSideBySide, 101, 51, false, false, fill);
	ExpectRect(sbs[0], 0, 0, 50, 51);
	ExpectRect(sbs[1], 50, 0, 101, 51);
	const auto tab = GSStereoPresentation::GetRects(GSStereoMode::FullTopAndBottom, 101, 51, true, false, fill);
	ExpectRect(tab[0], 0, 26, 101, 51);
	ExpectRect(tab[1], 0, 0, 101, 26);
	const auto half = GSStereoPresentation::GetRects(GSStereoMode::HalfTopAndBottom, 101, 51, true, false, fill);
	ExpectRect(half[0], 0, 25.5f, 101, 51);
	ExpectRect(half[1], 0, 0, 101, 25.5f);
}

TEST(Stereoscopy, EyeSwapUsesCompletedRectangles)
{
	const auto fit = [](int width, int height, unsigned eye) { return GSVector4(eye, 0, width - eye, height); };
	for (GSStereoMode mode : {GSStereoMode::HalfSideBySide, GSStereoMode::FullSideBySide,
			 GSStereoMode::HalfTopAndBottom, GSStereoMode::FullTopAndBottom})
	{
		for (bool flip_y : {false, true})
		{
			const auto normal = GSStereoPresentation::GetRects(mode, 101, 51, flip_y, false, fit);
			const auto swapped = GSStereoPresentation::GetRects(mode, 101, 51, flip_y, true, fit);
			ExpectRect(swapped[0], normal[1].x, normal[1].y, normal[1].z, normal[1].w);
			ExpectRect(swapped[1], normal[0].x, normal[0].y, normal[0].z, normal[0].w);
		}
	}
}

TEST(Stereoscopy, PackedEyeSamplingCannotCrossTheSeam)
{
	for (unsigned eye = 0; eye < 2; eye++)
	{
		const auto uv = GSPackedStereo::ClampUV({-1, -1, 2, 2}, GSPackedStereo::Axis::Horizontal, 128, 64, eye);
		EXPECT_FLOAT_EQ(uv.x, eye * 0.5f + 0.5f / 128);
		EXPECT_FLOAT_EQ(uv.z, (eye + 1) * 0.5f - 0.5f / 128);
		EXPECT_FLOAT_EQ(uv.y, 0.5f / 64);
		EXPECT_FLOAT_EQ(uv.w, 1.0f - 0.5f / 64);
	}
}
