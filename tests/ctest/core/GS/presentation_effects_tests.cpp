// SPDX-FileCopyrightText: 2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Presentation parameters must be independent of the GPU backend. (AI-assisted)
#include "GS/Renderers/Common/GSPresentationEffects.h"
#include "GS/Renderers/Common/GSDevice.h"
#include "ImGui/StereoscopyOverlay.h"
#include "common/MemorySettingsInterface.h"
#include "common/SettingsWrapper.h"
#include <gtest/gtest.h>
#include <limits>

using namespace GSPresentationEffects;

TEST(PresentationEffects, DefaultsLeaveEveryEffectDisabled)
{
	Pcsx2Config::GSOptions options;
	for (Eye eye : {Eye::Mono, Eye::Left, Eye::Right})
	{
		const auto p = Prepare(options, eye, GSVector2(640, 480), GSVector2(1280, 960), 7.0f, false);
		EXPECT_EQ(p.flags, 0u);
		EXPECT_EQ(p.shift, 0);
		EXPECT_EQ(p.tilt, 0);
		EXPECT_EQ(p.time, 7.0f);
	}
	EXPECT_EQ(sizeof(DisplayConstantBuffer), 128u);
}

TEST(PresentationEffects, NativePixelOffsetsIgnoreInternalAndPresentationResolution)
{
	Pcsx2Config::GSOptions options;
	options.StereoFix_EnableShiftTilt = true;
	options.StereoFix_Shift = 12;
	options.StereoFix_Tilt = 3;
	for (float scale : {1.0f, 2.0f, 4.0f})
	{
		const auto left = Prepare(options, Eye::Left, GSVector2(640, 480), GSVector2(640 * scale, 480 * scale), 0, false);
		const auto right = Prepare(options, Eye::Right, GSVector2(640, 480), GSVector2(640 * scale, 480 * scale), 0, false);
		EXPECT_FLOAT_EQ(left.shift * 640, 12);
		EXPECT_FLOAT_EQ(left.tilt * 480, -3);
		EXPECT_FLOAT_EQ(left.shift, -right.shift);
		EXPECT_FLOAT_EQ(left.tilt, -right.tilt);
	}
}

TEST(PresentationEffects, MonoRetainsOnlyColorAndCRTEffects)
{
	Pcsx2Config::GSOptions options;
	options.StereoFix_EnableShiftTilt = options.StereoFix_EnableVignette = options.StereoFix_ExtendEdges = true;
	options.StereoFix_EnableAutoGamma = options.StereoFix_EnableLuminanceBlend = true;
	options.StereoFix_EnableLevels = options.StereoFix_EnableCRTFilter = true;
	const auto mono = Prepare(options, Eye::Mono, GSVector2(640, 480), GSVector2(640, 480), 0, false);
	const auto stereo = Prepare(options, Eye::Left, GSVector2(640, 480), GSVector2(640, 480), 0, true);
	EXPECT_EQ(mono.flags, Contrast | Blend | Levels | CRT);
	EXPECT_EQ(stereo.flags, ShiftTilt | ExtendEdges | Vignette | Contrast | Blend | Levels | CRT | FlipY);
}

TEST(PresentationEffects, MalformedSettingsCannotProduceInvalidGPUParameters)
{
	Pcsx2Config::GSOptions options;
	options.StereoFix_EnableShiftTilt = options.StereoFix_EnableVignette = options.StereoFix_ExtendEdges = true;
	options.StereoFix_EnableLevels = options.StereoFix_EnableAutoGamma = true;
	options.StereoFix_Shift = std::numeric_limits<float>::infinity();
	options.StereoFix_VignetteSize = std::numeric_limits<float>::quiet_NaN();
	options.StereoFix_BlackLevel = options.StereoFix_WhiteLevel = 255;
	options.StereoFix_MidpointFocus = -1;
	const auto p = Prepare(options, Eye::Left, GSVector2(0, 0), GSVector2(0, 0), 0, false);
	EXPECT_FLOAT_EQ(p.shift, 0);
	EXPECT_GT(p.vignette_width, 0);
	EXPECT_GT(p.contrast_focus, 0);
	EXPECT_TRUE(std::isfinite(p.levels_scale));
	EXPECT_GT(p.levels_scale, 0);
}

TEST(PresentationEffects, LegacyKeysRoundTripAndParticipateInChangeDetection)
{
	Pcsx2Config::GSOptions original;
	original.StereoFix_EnableShiftTilt = true;
	original.StereoFix_EnableAutoGamma = true;
	original.StereoFix_Shift = 9;
	original.StereoFix_ExtendBorder = 0.0001f;
	original.StereoFix_MidpointFocus = 3;
	original.StereoFix_ContrastMidpoint = 1.25f;
	original.StereoFix_GammaCompStrength = 0.75f;
	original.StereoFix_MidpointFocus2 = 7;
	original.StereoFix_LuminanceMidpoint = 0.4f;
	MemorySettingsInterface settings;
	SettingsSaveWrapper save(settings);
	original.LoadSave(save);
	Pcsx2Config::GSOptions restored;
	SettingsLoadWrapper load(settings);
	restored.LoadSave(load);
	EXPECT_EQ(original, restored);
	EXPECT_FLOAT_EQ(Prepare(restored, Eye::Mono, GSVector2(640, 480), GSVector2(640, 480), 0, false).contrast_midpoint, 1.25f);
	restored.StereoFix_ExtendBorder += 0.0001f;
	EXPECT_NE(original, restored);
}

TEST(PresentationEffects, TabsKeepIndependentSelectionsAndWrap)
{
	using namespace StereoscopyOverlay;
	Navigation nav;
	const auto stereo = GetSettings(Tab::Stereoscopy);
	const auto shaders = GetSettings(Tab::Shaders);
	ASSERT_FALSE(stereo.empty());
	ASSERT_FALSE(shaders.empty());
	EXPECT_EQ(stereo.size() + shaders.size(), GetSettings().size());
	nav.Move(3);
	nav.first[0] = 2;
	nav.Switch(1);
	EXPECT_EQ(nav.tab, Tab::Shaders);
	EXPECT_EQ(std::string_view(GetSettings()[nav.Index()].key), "StereoFix_EnableShiftTilt");
	nav.Move(-1);
	EXPECT_EQ(nav.Index(), GetSettings().size());
	nav.Move(1);
	nav.Move(8);
	nav.first[1] = 6;
	nav.Switch(1);
	EXPECT_EQ(nav.tab, Tab::Stereoscopy);
	EXPECT_EQ(nav.Index(), 3u);
	EXPECT_EQ(nav.first[0], 2u);
	nav.Switch(-1);
	EXPECT_EQ(nav.tab, Tab::Shaders);
	EXPECT_EQ(nav.selected[1], 8u);
	EXPECT_EQ(nav.first[1], 6u);
}

TEST(PresentationEffects, ShaderControlsRetainPrecisionAndEnableDependencies)
{
	using namespace StereoscopyOverlay;
	Pcsx2Config::GSOptions options;
	for (const auto& setting : GetSettings(Tab::Shaders))
	{
		const std::string_view key(setting.key);
		if (key == "StereoFix_ExtendBorder")
		{
			EXPECT_EQ(FormatNumber(setting, 0.0001f), "0.0001");
			EXPECT_FALSE(IsEnabled(setting, options));
			options.StereoFix_ExtendEdges = true;
			EXPECT_TRUE(IsEnabled(setting, options));
		}
		if (key == "StereoFix_ContrastIntensity")
		{
			EXPECT_EQ(FormatNumber(setting, 0.92f), "0.92");
			EXPECT_FALSE(IsEnabled(setting, options));
			options.StereoFix_EnableAutoGamma = true;
			options.StereoMode = GSStereoMode::Off;
			EXPECT_TRUE(IsEnabled(setting, options));
		}
		if (key == "StereoFix_EnableShiftTilt")
		{
			options.StereoMode = GSStereoMode::Off;
			EXPECT_FALSE(IsEnabled(setting, options));
			options.StereoMode = GSStereoMode::HalfSideBySide;
		}
	}
}

TEST(PresentationEffects, TransactionsMergeAcrossTabsAndResetToInheritance)
{
	MemorySettingsInterface persistent, preview;
	persistent.SetFloatValue("EmuCore/GS", "StereoFix_ExtendBorder", 0.1f);
	persistent.SetStringValue("Unrelated", "Key", "keep");
	StereoscopyOverlay::SettingsTransaction stereo, shaders;
	stereo.Set("StereoSeparation", "25", "25");
	shaders.Set("StereoFix_ExtendBorder", std::nullopt, "0.0001");
	stereo.Merge(shaders);
	stereo.ApplyPreview(preview);
	EXPECT_FLOAT_EQ(preview.GetFloatValue("EmuCore/GS", "StereoFix_ExtendBorder"), 0.0001f);
	EXPECT_TRUE(persistent.ContainsValue("EmuCore/GS", "StereoFix_ExtendBorder"));
	stereo.Commit(persistent);
	EXPECT_FALSE(persistent.ContainsValue("EmuCore/GS", "StereoFix_ExtendBorder"));
	EXPECT_FLOAT_EQ(persistent.GetFloatValue("EmuCore/GS", "StereoSeparation"), 25);
	EXPECT_EQ(persistent.GetStringValue("Unrelated", "Key"), "keep");
}
