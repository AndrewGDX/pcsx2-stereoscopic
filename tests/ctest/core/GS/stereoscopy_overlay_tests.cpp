// SPDX-FileCopyrightText: 2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Overlay settings transactions and binding compatibility. (AI-assisted)
#include "ImGui/StereoscopyOverlay.h"
#include "GS/Renderers/Common/GSStereoPresentation.h"
#include "Input/InputManager.h"
#include "LayeredSettingsInterface.h"
#include "SIO/Pad/Pad.h"
#include "SIO/Pad/PadDualshock2.h"
#include "common/MemorySettingsInterface.h"
#include "common/SettingsWrapper.h"
#include <gtest/gtest.h>
#include <set>

namespace
{
	class StereoscopyOverlayInput : public testing::Test
	{
	protected:
		void SetUp() override
		{
			m_settings.SetLayer(LayeredSettingsInterface::LAYER_BASE, &m_base);
			m_base.SetBoolValue("UI", "StereoscopyOverlayHotkeyInitialized", true);
			m_base.SetBoolValue("InputSources", "SDL", true);
			m_base.SetStringValue("Pad1", "Type", "DualShock2");
			m_base.SetStringValue("Pad1", "LRight", "SDL-0/+LeftX");
			m_base.SetStringValue("Pad1", "LLeft", "SDL-0/-LeftX");
			m_base.SetStringValue("Pad1", "RRight", "SDL-0/+RightX");
			m_base.SetStringValue("Pad1", "RLeft", "SDL-0/-RightX");
			m_base.SetStringValue("Pad1", "Cross", "Pointer-0/LeftButton");
			m_base.SetStringValue("Pad1", "Square", "SDL-0/FaceSouth");
			m_base.SetFloatValue("Pad1", "Deadzone", 0.0f);
			m_base.SetFloatValue("Pad1", "AxisScale", 1.0f);
			{
				std::mutex mutex;
				std::unique_lock lock(mutex);
				InputManager::ReloadSources(m_settings, lock);
			}
			Pad::LoadConfig(m_settings);
			InputManager::ReloadBindings(m_settings, m_settings, m_settings, false, false);
		}

		void TearDown() override
		{
			for (const char* binding : {"SDL-0/+LeftX", "SDL-0/+RightX", "Pointer-0/LeftButton", "SDL-0/FaceSouth"})
				Input(binding, 0.0f);
			m_base.ClearSection("Pad1");
			InputManager::ReloadBindings(m_settings, m_settings, m_settings, false, false);
			Pad::Shutdown();
			InputManager::CloseSources();
		}

		void Input(const char* binding, float value)
		{
			const auto key = InputManager::ParseInputBindingKey(binding);
			ASSERT_TRUE(key.has_value()) << binding;
			InputManager::InvokeEvents(*key, value);
		}

		MemorySettingsInterface m_base;
		LayeredSettingsInterface m_settings;
	};
} // namespace

TEST(StereoscopyOverlay, LayoutFitsBothEyesInEveryOutputMode)
{
	for (unsigned mode = 0; mode < static_cast<unsigned>(GSStereoMode::Count); mode++)
	{
		for (const auto& [width, height] : {std::pair{1920, 1080}, std::pair{3840, 1080},
				 std::pair{1920, 2160}, std::pair{1101, 731}, std::pair{51, 31}})
		{
			for (float global_scale : {0.5f, 1.0f, 2.0f})
			{
				SCOPED_TRACE(testing::Message() << "mode=" << mode << " size=" << width << "x" << height << " scale=" << global_scale);
				const auto eyes = GSStereoPresentation::GetEyes(static_cast<GSStereoMode>(mode), width, height, false);
				const auto layout = StereoscopyOverlay::GetLayout(std::min(eyes[0].width, eyes[1].width),
					std::min(eyes[0].height, eyes[1].height), global_scale);
				EXPECT_GT(layout.scale, 0.0f);
				for (const auto& eye : eyes)
				{
					EXPECT_LE(layout.width + 2.0f * layout.margin, eye.width);
					EXPECT_LE(layout.height, eye.height);
					// The header and at least one grouped game-settings row must remain visible. (AI-assisted)
					EXPECT_LE((96.0f + 38.0f + 60.0f + 16.0f) * layout.scale, layout.height);
				}
			}
		}
	}
}

TEST(StereoscopyOverlay, NarrowFullSbsUsesTheCurrentPanelWidth)
{
	const auto eyes = GSStereoPresentation::GetEyes(GSStereoMode::FullSideBySide, 1100, 730, false);
	const auto layout = StereoscopyOverlay::GetLayout(eyes[0].width, eyes[0].height, 1.0f);
	EXPECT_FLOAT_EQ(layout.width, 550.0f * 0.65f);
}

TEST(StereoscopyOverlay, EmptyViewportDoesNotRender)
{
	EXPECT_FLOAT_EQ(StereoscopyOverlay::GetLayout(0, 1080, 1.0f).scale, 0.0f);
	EXPECT_FLOAT_EQ(StereoscopyOverlay::GetLayout(1920, 0, 1.0f).scale, 0.0f);
}

TEST_F(StereoscopyOverlayInput, BothSticksResumeWithoutAnExactZeroReading)
{
	for (const auto& [binding, input] : {std::pair{"SDL-0/+LeftX", PadDualshock2::PAD_L_RIGHT},
			 std::pair{"SDL-0/+RightX", PadDualshock2::PAD_R_RIGHT}})
	{
		SCOPED_TRACE(binding);
		Input(binding, 0.03f);
		InputManager::BeginOverlayCapture();
		Pad::ClearInputState();
		EXPECT_FLOAT_EQ(Pad::GetPad(0)->GetEffectiveInput(input), 0.0f);
		// Closing capture must not require a drifting stick to report exactly zero. (AI-assisted)
		Input(binding, 0.8f);
		EXPECT_GT(Pad::GetPad(0)->GetEffectiveInput(input), 0.7f);
		Input(binding, -0.8f);
		EXPECT_FLOAT_EQ(Pad::GetPad(0)->GetEffectiveInput(input), 0.0f);
		Input(binding, 0.03f);
	}
}

TEST_F(StereoscopyOverlayInput, PointerAndControllerButtonsStillRequireRelease)
{
	for (const auto& [binding, input] : {std::pair{"Pointer-0/LeftButton", PadDualshock2::PAD_CROSS},
			 std::pair{"SDL-0/FaceSouth", PadDualshock2::PAD_SQUARE}})
	{
		SCOPED_TRACE(binding);
		Input(binding, 1.0f);
		EXPECT_GT(Pad::GetPad(0)->GetEffectiveInput(input), 0.9f);
		InputManager::BeginOverlayCapture();
		Pad::ClearInputState();
		Input(binding, 1.0f);
		EXPECT_FLOAT_EQ(Pad::GetPad(0)->GetEffectiveInput(input), 0.0f);
		Input(binding, 0.0f);
		Input(binding, 1.0f);
		EXPECT_GT(Pad::GetPad(0)->GetEffectiveInput(input), 0.9f);
		Input(binding, 0.0f);
	}
}

TEST_F(StereoscopyOverlayInput, SourceResetEndsCaptureWithoutAReleaseEvent)
{
	const auto key = InputManager::ParseInputBindingKey("SDL-0/FaceSouth");
	ASSERT_TRUE(key.has_value());
	Input("SDL-0/FaceSouth", 1.0f);
	InputManager::BeginOverlayCapture();
	Pad::ClearInputState();
	InputManager::ClearBindStateFromSource(*key);
	// Reconnecting or regaining focus may never deliver the old button's release. (AI-assisted)
	Input("SDL-0/FaceSouth", 1.0f);
	EXPECT_GT(Pad::GetPad(0)->GetEffectiveInput(PadDualshock2::PAD_SQUARE), 0.9f);
}

TEST_F(StereoscopyOverlayInput, SourceResetPreservesOtherSourcesCapture)
{
	const auto key = InputManager::ParseInputBindingKey("Pointer-0/LeftButton");
	ASSERT_TRUE(key.has_value());
	Input("SDL-0/FaceSouth", 1.0f);
	Input("Pointer-0/LeftButton", 1.0f);
	InputManager::BeginOverlayCapture();
	Pad::ClearInputState();
	InputManager::ClearBindStateFromSource(*key);
	Input("Pointer-0/LeftButton", 1.0f);
	Input("SDL-0/FaceSouth", 1.0f);
	EXPECT_GT(Pad::GetPad(0)->GetEffectiveInput(PadDualshock2::PAD_CROSS), 0.9f);
	EXPECT_FLOAT_EQ(Pad::GetPad(0)->GetEffectiveInput(PadDualshock2::PAD_SQUARE), 0.0f);
}

TEST(StereoscopyOverlay, AddsBothDefaultsOnceAndPreservesCustomBindings)
{
	MemorySettingsInterface settings;
	StereoscopyOverlay::MigrateHotkey(settings);
	auto bindings = settings.GetStringList("Hotkeys", StereoscopyOverlay::HOTKEY);
	ASSERT_EQ(bindings.size(), 2u);
	EXPECT_EQ(bindings[0], "Keyboard/Control & Keyboard/Shift & Keyboard/F1");
	EXPECT_EQ(bindings[1], StereoscopyOverlay::PAD_CHORD);
	settings.SetStringList("Hotkeys", StereoscopyOverlay::HOTKEY, {"Keyboard/F12"});
	StereoscopyOverlay::MigrateHotkey(settings);
	EXPECT_EQ(settings.GetStringList("Hotkeys", StereoscopyOverlay::HOTKEY), std::vector<std::string>{"Keyboard/F12"});
	settings.DeleteValue("Hotkeys", StereoscopyOverlay::HOTKEY);
	StereoscopyOverlay::MigrateHotkey(settings);
	EXPECT_TRUE(settings.GetStringList("Hotkeys", StereoscopyOverlay::HOTKEY).empty());
}

TEST(StereoscopyOverlay, MigrationRetainsAnExistingBinding)
{
	MemorySettingsInterface settings;
	settings.SetStringList("Hotkeys", StereoscopyOverlay::HOTKEY, {"Keyboard/F10"});
	StereoscopyOverlay::MigrateHotkey(settings);
	EXPECT_EQ(settings.GetStringList("Hotkeys", StereoscopyOverlay::HOTKEY), std::vector<std::string>{"Keyboard/F10"});
}

TEST(StereoscopyOverlay, PreviewCannotWritePersistentSettings)
{
	MemorySettingsInterface persistent, preview;
	persistent.SetFloatValue("EmuCore/GS", "StereoSeparation", 20.0f);
	persistent.SetBoolValue("EmuCore/GS", "StereoRejectPrimTme", true);
	persistent.SetStringValue("Unrelated", "Key", "keep");
	for (const auto& [key, value] : persistent.GetKeyValueList("EmuCore/GS"))
		preview.SetStringValue("EmuCore/GS", key.c_str(), value.c_str());
	StereoscopyOverlay::SettingsTransaction edits;
	EXPECT_TRUE(edits.IsEmpty());
	edits.Set("StereoSeparation", "35.1", "35.1");
	edits.Set("StereoConvergence", "125.8", "125.8");
	edits.ApplyPreview(preview);
	EXPECT_FLOAT_EQ(preview.GetFloatValue("EmuCore/GS", "StereoSeparation"), 35.1f);
	EXPECT_FLOAT_EQ(persistent.GetFloatValue("EmuCore/GS", "StereoSeparation"), 20.0f);
	EXPECT_FALSE(persistent.ContainsValue("EmuCore/GS", "StereoConvergence"));
	// A concurrent edit to another key survives close.
	persistent.SetStringValue("Unrelated", "Key", "newer");
	edits.Commit(persistent);
	EXPECT_FLOAT_EQ(persistent.GetFloatValue("EmuCore/GS", "StereoConvergence"), 125.8f);
	EXPECT_EQ(persistent.GetStringValue("Unrelated", "Key"), "newer");
	EXPECT_TRUE(persistent.GetBoolValue("EmuCore/GS", "StereoRejectPrimTme"));
}

TEST(StereoscopyOverlay, ResetPreviewsInheritedValueAndDeletesOnlyTheOverride)
{
	MemorySettingsInterface game, preview;
	game.SetFloatValue("EmuCore/GS", "StereoSeparation", 75.0f);
	game.SetBoolValue("EmuCore/GS", "StereoPackedEnable", false);
	StereoscopyOverlay::SettingsTransaction edits;
	edits.Set("StereoSeparation", "90", "90");
	edits.Set("StereoSeparation", std::nullopt, "23.5");
	edits.ApplyPreview(preview);
	EXPECT_FLOAT_EQ(preview.GetFloatValue("EmuCore/GS", "StereoSeparation"), 23.5f);
	EXPECT_FLOAT_EQ(game.GetFloatValue("EmuCore/GS", "StereoSeparation"), 75.0f);
	edits.Commit(game);
	EXPECT_FALSE(game.ContainsValue("EmuCore/GS", "StereoSeparation"));
	EXPECT_TRUE(game.ContainsValue("EmuCore/GS", "StereoPackedEnable"));
}

TEST(StereoscopyOverlay, SaveRetryUsesTheNewestEditForEachKey)
{
	StereoscopyOverlay::SettingsTransaction pending, latest;
	pending.Set("StereoSeparation", "30", "30");
	pending.Set("StereoConvergence", "100", "100");
	latest.Set("StereoSeparation", std::nullopt, "20");
	pending.Merge(latest);
	MemorySettingsInterface target;
	pending.Commit(target);
	EXPECT_FALSE(target.ContainsValue("EmuCore/GS", "StereoSeparation"));
	EXPECT_EQ(target.GetStringValue("EmuCore/GS", "StereoConvergence"), "100");
}

TEST(StereoscopyOverlay, ControlsMatchSerializedSettingsAndExcludeDebugFilters)
{
	MemorySettingsInterface settings;
	SettingsSaveWrapper save(settings);
	Pcsx2Config::GSOptions options;
	options.LoadSave(save);
	std::set<std::string_view> seen;
	bool debug_action = false;
	for (const auto& setting : StereoscopyOverlay::GetSettings())
	{
		EXPECT_TRUE(seen.insert(setting.key).second);
		EXPECT_TRUE(settings.ContainsValue("EmuCore/GS", setting.key)) << setting.key;
		if (std::string_view(setting.group) == "Debug Rendering")
		{
			EXPECT_EQ(std::string_view(setting.key), "StereoDebugAction");
			debug_action = true;
		}
		if (setting.control == StereoscopyOverlay::Control::Number)
			EXPECT_GT(setting.maximum, setting.minimum);
	}
	EXPECT_TRUE(debug_action);
	EXPECT_EQ(seen.size(), 61u);
}

TEST(StereoscopyOverlay, DisabledControlsFollowQtDependencies)
{
	Pcsx2Config::GSOptions options;
	options.StereoMode = GSStereoMode::Off;
	for (const auto& setting : StereoscopyOverlay::GetSettings(StereoscopyOverlay::Tab::Stereoscopy))
		EXPECT_EQ(StereoscopyOverlay::IsEnabled(setting, options), std::string_view(setting.key) == "StereoMode");
	options.StereoMode = GSStereoMode::HalfSideBySide;
	options.StereoPackedEnable = false;
	options.StereoUiDetectMode = 0;
	for (const auto& setting : StereoscopyOverlay::GetSettings())
	{
		const std::string_view key(setting.key);
		if ((key.starts_with("StereoPacked") && key != "StereoPackedEnable") || key == "StereoUiDetectThreshold")
			EXPECT_FALSE(StereoscopyOverlay::IsEnabled(setting, options));
	}
}
