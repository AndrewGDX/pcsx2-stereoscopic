// SPDX-FileCopyrightText: 2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Live stereoscopy overlay and isolated settings preview. (AI-assisted)
#include "StereoscopyOverlay.h"
#include "GS/Renderers/Common/GSStereoPresentation.h"
#include "GS/GS.h"
#include "Host.h"
#include "INISettingsInterface.h"
#include "common/FileSystem.h"
#include "ImGuiManager.h"
#include "Input/InputManager.h"
#include "SIO/Pad/Pad.h"
#include "VMManager.h"
#include "common/Error.h"
#include "common/MemorySettingsInterface.h"
#include "common/SettingsWrapper.h"
#include "common/StringUtil.h"
#include "fmt/format.h"
#include "FullscreenUI.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <optional>
#include <vector>
#include <map>
#include <unordered_set>

namespace
{
	using namespace StereoscopyOverlay;
	constexpr const char* SECTION = "EmuCore/GS";
	const char* const s_StereoUiDetectMode[] = {"Off", "Unsafe", "Safe", "Advanced", nullptr};
	const char* const s_StereoFeedbackLoopFix[] = {"Off", "Safe", "Moderate", "Full", nullptr};
	const char* const s_StereoSbsRemapFix[] = {"Off", "Safe", "Moderate", "Full", nullptr};
	const Setting s_settings[] = {
		{"StereoMode", "Stereoscopic 3D Mode", "Stereo Output", Control::Enum, 0, 4, 1, Pcsx2Config::GSOptions::StereoModeNames},
		{"StereoDominantEye", "Dominant Eye", "Stereo Output", Control::Enum, 0, 2, 1, Pcsx2Config::GSOptions::StereoDominantEyeNames},
		{"StereoSwapEyes", "Swap Eyes", "Stereo Output", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoSeparation", "Separation", "Depth", Control::Number, 0, 100.0f, 0.1f, nullptr},
		{"StereoConvergence", "Convergence", "Depth", Control::Number, 0, 1000.0f, 0.1f, nullptr},
		{"StereoProjectionDepth", "Projection-aware stereo depth (experimental)", "Depth", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoUiDetectMode", "UI Detection Mode", "UI Detection", Control::Choice, 0, 3, 1, s_StereoUiDetectMode},
		{"StereoUiDepth", "UI Depth", "UI Detection", Control::Number, -100, 50, 1, nullptr},
		{"StereoUiSecondLayerDepth", "Second Layer Parallax", "UI Detection", Control::Number, -25, 25, 1, nullptr},
		{"StereoUiDetectThreshold", "UI Detection Threshold", "UI Detection", Control::Number, 0, 1000, 1, nullptr},
		{"StereoRejectZTestAlways", "Z test is Always", "Additional UI Detection", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoStencilRequireZTestGequal", "Z test is not GEQUAL or GREATER", "Additional UI Detection", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoRequireZVaries", "Constant vertex Z", "Additional UI Detection", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoRejectFixedQ", "Constant vertex Q", "Additional UI Detection", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoUiDetectSkyDepth", "Near-zero depth", "Additional UI Detection", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoUiDetectFst", "Fixed texture coordinates (FST)", "Additional UI Detection", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoUiDetectFullscreenDrawArea", "Fullscreen draw area", "Additional UI Detection", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoUiDetectFullscreenSprite", "Fullscreen sprite", "Additional UI Detection", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoUiSecondLayerSelector1", "Fullscreen draw area", "Second Layer UI Detection", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoUiSecondLayerSelector2", "Constant Z and Q with depth testing", "Second Layer UI Detection", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoUiSecondLayerSelector3", "No blend mixing", "Second Layer UI Detection", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoUiSecondLayerSelector4", "Secondary color disabled with blend mixing", "Second Layer UI Detection", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoUiSecondLayerSelector5", "Advanced layer detection", "Second Layer UI Detection", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoPackedEnable", "Enable improved packed renderer", "Improved Packed Renderer", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoPackedIntegerReads", "Accurate integer framebuffer reads", "Improved Packed Renderer", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoPackedMonoFullscreen", "Keep overlays at screen depth", "Improved Packed Renderer", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoPackedEffectDepthFix", "Fix framebuffer effect depth", "Improved Packed Renderer", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoDisableCorrectSbsFramebufferSize", "Alternative Rendering (Disable full framebuffer for each eye)", "Presentation Fixes", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoSbsRemapMono", "Picture-in-Picture Fix: Mono SBS Remap", "Presentation Fixes", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoFeedbackLoopFix", "Feedback Loop Fix", "Presentation Fixes", Control::Choice, 0, 3, 1, s_StereoFeedbackLoopFix},
		{"StereoSbsRemapFix", "SBS Remap Fix", "Presentation Fixes", Control::Choice, 0, 3, 1, s_StereoSbsRemapFix},
		{"StereoCloseDepthFix", "Depth Fix: God of War, Gun", "Rendering Fixes", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoSkyFix", "Sky Fix: Killzone, Gran Turismo 4", "Rendering Fixes", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoRequireRtaCorrection", "Shadows Fix 0: Target Alpha Correction (MK, COD)", "Rendering Fixes", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoFixStencilShadows1", "Shadows Fix 1: Fix Stencil Shadows (Tekken 5, MGS 3)", "Rendering Fixes", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoFixStencilShadows2", "Shadows Fix 2: Fix Stencil Shadows (Tekken 4, Onimusha 3, Medal of Honor)", "Rendering Fixes", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoFixStencilShadows3", "Shadows Fix 3: Fix Stencil Shadows (God of War, Gun)", "Rendering Fixes", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoRejectRegionRect", "PostFX Overlay Fix 1: Reject Region Rect", "Rendering Fixes", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoRejectTfxDecal", "PostFX Overlay Fix 2: Reject TFX Decal", "Rendering Fixes", Control::Toggle, 0, 1, 1, nullptr},
		{"StereoDebugAction", "Debug Action", "Debug Rendering", Control::Enum, 0, 5, 1, Pcsx2Config::GSOptions::StereoDebugActionNames},
	};

	constexpr size_t SETTING_COUNT = std::size(s_settings);
	struct Value
	{
		std::string current;
		std::string fallback;
		std::string factory;
		bool custom = false;
	};
	struct State
	{
		std::array<Value, SETTING_COUNT> values;
		Pcsx2Config::GSOptions options;
		std::string subtitle;
		SettingsTransaction edits;
		std::string filename;
		SettingsInterface* target = nullptr; // CPU thread only; finalized before layer replacement.
		bool game = false;
		int position = 0;
		int initial_position = 0;
		size_t selected = 0;
	};
	std::mutex s_mutex;
	State s_state;
	std::atomic_bool s_open{false};
	bool s_pad_chord_enabled = false;
	bool s_chord_latched = false;
	std::array<bool, static_cast<size_t>(GenericInputBinding::Count)> s_buttons{};
	std::array<std::unordered_set<u64>, static_cast<size_t>(GenericInputBinding::Count)> s_button_sources;
	using Clock = std::chrono::steady_clock;
	Clock::time_point s_next_repeat;
	Clock::time_point s_hold_start;
	Clock::time_point s_next_scope_check;
	bool s_preview_dirty = false;
	bool s_pending_base_save = false;
	std::map<std::string, SettingsTransaction> s_pending_saves;

	std::string GetGameSettingsFilename()
	{
		std::string filename = VMManager::GetCurrentGameSettingsPath();
		if (!filename.empty())
		{
			{
				auto lock = Host::GetSettingsLock();
				SettingsInterface* game = Host::Internal::GetGameSettingsLayer();
				if (game && static_cast<INISettingsInterface*>(game)->GetFileName() == filename)
					return filename;
			}
			// A newly created file only changes scope if the VM can load it. (AI-assisted)
			INISettingsInterface settings(filename);
			if (!settings.Load())
				filename.clear();
		}
		return filename;
	}

	int PositionIndex(std::string_view value)
	{
		for (int i = 0; i < static_cast<int>(std::size(POSITIONS)); i++)
			if (value == POSITIONS[i])
				return i;
		return 0;
	}
	bool Down(GenericInputBinding key) { return s_buttons[static_cast<size_t>(key)]; }
	bool ChordDown()
	{
		return Down(GenericInputBinding::L1) && Down(GenericInputBinding::L2) && Down(GenericInputBinding::L3);
	}
	float Number(const std::string& value)
	{
		return StringUtil::FromChars<float>(value).value_or(0.0f);
	}
	int ChoiceIndex(const Setting& setting, const Value& value)
	{
		if (setting.control != Control::Enum)
			return static_cast<int>(Number(value.current));
		for (int i = 0; setting.choices[i]; i++)
			if (value.current == setting.choices[i])
				return i;
		return 0;
	}
	bool Checked(const std::string& value)
	{
		return StringUtil::FromChars<bool>(value).value_or(false);
	}
	void Change(int direction, bool reset, bool confirm, float acceleration = 1.0f)
	{
		std::lock_guard lock(s_mutex);
		if (s_state.selected == SETTING_COUNT)
		{
			s_state.position = reset ? 0 : std::clamp(s_state.position + direction, 0, static_cast<int>(std::size(POSITIONS)) - 1);
			return;
		}
		const Setting& setting = s_settings[s_state.selected];
		if (!IsEnabled(setting, s_state.options))
			return;
		Value& value = s_state.values[s_state.selected];
		const std::string previous = value.current;
		if (reset)
			value.current = s_state.game ? value.fallback : value.factory;
		else if (setting.control == Control::Toggle)
			value.current = (confirm ? !Checked(value.current) : direction > 0) ? "true" : "false";
		else if (setting.control == Control::Enum || setting.control == Control::Choice)
		{
			const int index = std::clamp(ChoiceIndex(setting, value) + direction, 0, static_cast<int>(setting.maximum));
			value.current = setting.control == Control::Enum ? setting.choices[index] : std::to_string(index);
		}
		else if (!confirm)
		{
			const float number = std::clamp(Number(value.current) + direction * setting.step * acceleration,
				setting.minimum, setting.maximum);
			value.current = fmt::format("{:.1f}", number);
		}
		else
			return;
		if (!reset && value.current == previous)
			return;
		s_state.edits.Set(setting.key, reset ? std::nullopt : std::optional(value.current), value.current);
		value.custom = s_state.game && !reset;
		s_preview_dirty = true;
		if (std::string_view(setting.key) == "StereoPackedEnable" && Checked(value.current) && !reset)
		{
			for (size_t i = 0; i < SETTING_COUNT; i++)
			{
				const std::string_view key(s_settings[i].key);
				if (key == "StereoFeedbackLoopFix" || key == "StereoSbsRemapFix")
				{
					Value& dependent = s_state.values[i];
					dependent.current = key == "StereoFeedbackLoopFix" ? "0" : "3";
					s_state.edits.Set(s_settings[i].key, dependent.current, dependent.current);
					dependent.custom = s_state.game;
				}
			}
		}
	}
	void Navigate(int direction)
	{
		std::lock_guard lock(s_mutex);
		const int count = static_cast<int>(SETTING_COUNT) + 1;
		s_state.selected = (static_cast<int>(s_state.selected) + direction + count) % count;
	}
	void DirectionAction(float acceleration)
	{
		if (Down(GenericInputBinding::DPadUp))
			Navigate(-1);
		else if (Down(GenericInputBinding::DPadDown))
			Navigate(1);
		else if (Down(GenericInputBinding::DPadLeft))
			Change(-1, false, false, acceleration);
		else if (Down(GenericInputBinding::DPadRight))
			Change(1, false, false, acceleration);
	}
} // namespace

void StereoscopyOverlay::SettingsTransaction::Set(std::string_view key, std::optional<std::string> stored, std::string effective)
{
	for (Edit& edit : m_edits)
	{
		if (edit.key != key)
			continue;
		edit.stored = std::move(stored);
		edit.effective = std::move(effective);
		return;
	}
	m_edits.push_back({std::string(key), std::move(stored), std::move(effective)});
}

void StereoscopyOverlay::SettingsTransaction::Merge(const SettingsTransaction& newer)
{
	for (const Edit& edit : newer.m_edits)
		Set(edit.key, edit.stored, edit.effective);
}

void StereoscopyOverlay::SettingsTransaction::ApplyPreview(SettingsInterface& settings) const
{
	for (const Edit& edit : m_edits)
		settings.SetStringValue(SECTION, edit.key.c_str(), edit.effective.c_str());
}

void StereoscopyOverlay::SettingsTransaction::Commit(SettingsInterface& settings) const
{
	for (const Edit& edit : m_edits)
	{
		if (edit.stored)
			settings.SetStringValue(SECTION, edit.key.c_str(), edit.stored->c_str());
		else
			settings.DeleteValue(SECTION, edit.key.c_str());
	}
}

std::span<const StereoscopyOverlay::Setting> StereoscopyOverlay::GetSettings() { return s_settings; }
bool StereoscopyOverlay::IsOpen() { return s_open.load(std::memory_order_acquire); }

bool StereoscopyOverlay::IsEnabled(const Setting& setting, const Pcsx2Config::GSOptions& options)
{
	const std::string_view key(setting.key);
	if (key == "StereoMode")
		return true;
	if (options.StereoMode == GSStereoMode::Off)
		return false;
	if (key == "StereoUiDetectThreshold" && options.StereoUiDetectMode == 0)
		return false;
	if (key.starts_with("StereoPacked") && key != "StereoPackedEnable")
		return options.StereoPackedEnable;
	return true;
}

void StereoscopyOverlay::MigrateHotkey(SettingsInterface& settings)
{
	if (settings.GetBoolValue("UI", "StereoscopyOverlayHotkeyInitialized", false))
		return;
	if (!settings.ContainsValue("Hotkeys", HOTKEY))
		settings.SetStringList("Hotkeys", HOTKEY, {"Keyboard/Control & Keyboard/Shift & Keyboard/F1", PAD_CHORD});
	settings.SetBoolValue("UI", "StereoscopyOverlayHotkeyInitialized", true);
}

void StereoscopyOverlay::Toggle()
{
	if (IsOpen())
	{
		Close();
		return;
	}
	if (!VMManager::HasValidVM())
		return;
	const std::string filename = GetGameSettingsFilename();
	bool scope_changed;
	{
		auto lock = Host::GetSettingsLock();
		SettingsInterface* game = Host::Internal::GetGameSettingsLayer();
		scope_changed = filename != (game ? static_cast<INISettingsInterface*>(game)->GetFileName() : std::string());
	}
	if (scope_changed)
	{
		// Reloading also rebuilds input bindings, so defer until the current input dispatch finishes. (AI-assisted)
		Host::RunOnCPUThread([] {
			if (!VMManager::HasValidVM() || IsOpen())
				return;
			VMManager::ReloadGameSettings();
			Toggle();
		});
		return;
	}
	MemorySettingsInterface effective, defaults;
	SettingsSaveWrapper save(effective), save_defaults(defaults);
	Pcsx2Config::GSOptions options = EmuConfig.GS;
	options.LoadSave(save);
	Pcsx2Config::GSOptions factory;
	factory.LoadSave(save_defaults);
	State state;
	{
		auto lock = Host::GetSettingsLock();
		SettingsInterface* game = Host::Internal::GetGameSettingsLayer();
		SettingsInterface* base = Host::Internal::GetBaseSettingsLayer();
		state.target = game ? game : base;
		state.game = game != nullptr;
		if (game)
			state.filename = static_cast<INISettingsInterface*>(game)->GetFileName();
		state.options = options;
		state.subtitle = game ? fmt::format("{} — {}", TRANSLATE("StereoscopyOverlay", "Game settings"), VMManager::GetTitle(false)) : TRANSLATE("StereoscopyOverlay", "Global settings");
		state.position = PositionIndex(base->GetStringValue("UI", POSITION_KEY, "Left"));
		state.initial_position = state.position;
		for (size_t i = 0; i < SETTING_COUNT; i++)
		{
			const char* key = s_settings[i].key;
			Value& value = state.values[i];
			value.factory = defaults.GetStringValue(SECTION, key);
			value.current = effective.GetStringValue(SECTION, key, value.factory.c_str());
			value.fallback = base->GetStringValue(SECTION, key, value.factory.c_str());
			value.custom = game && game->ContainsValue(SECTION, key);
		}
	}
	{
		std::lock_guard lock(s_mutex);
		state.selected = s_state.selected;
		s_state = std::move(state);
	}
	s_preview_dirty = false;
	s_next_scope_check = Clock::now() + std::chrono::milliseconds(500);
	s_buttons.fill(false);
	for (auto& sources : s_button_sources)
		sources.clear();
	s_open.store(true, std::memory_order_release);
	InputManager::BeginOverlayCapture();
	Pad::ClearInputState();
}

void StereoscopyOverlay::Close(bool reload_game_settings)
{
	if (!s_open.exchange(false, std::memory_order_acq_rel))
		return;
	State state;
	{
		std::lock_guard lock(s_mutex);
		state = s_state;
	}
	const bool position_changed = state.position != state.initial_position;
	const bool scope_changed = state.filename != GetGameSettingsFilename();
	{
		auto lock = Host::GetSettingsLock();
		if (scope_changed)
		{
			// Edits belong to the scope shown when they were made, never a replacement scope. (AI-assisted)
			state.edits = {};
			if (state.game)
				s_pending_saves.erase(state.filename);
		}
		state.edits.Commit(*state.target);
		// The merged disk transaction below owns retries; the runtime cache must not autosave. (AI-assisted)
		if (state.game)
			static_cast<INISettingsInterface*>(state.target)->ClearDirtyFlag();
		if (position_changed)
			Host::Internal::GetBaseSettingsLayer()->SetStringValue("UI", POSITION_KEY, POSITIONS[state.position]);
		if (state.game && !state.edits.IsEmpty())
			s_pending_saves[state.filename].Merge(state.edits);
		// Retry previous failures even when this opening did not change a setting.
		for (auto it = s_pending_saves.begin(); it != s_pending_saves.end();)
		{
			if (!FileSystem::FileExists(it->first.c_str()))
			{
				it = s_pending_saves.erase(it);
				continue;
			}
			INISettingsInterface disk(it->first);
			Error error;
			const bool loaded = disk.Load();
			if (!loaded && FileSystem::FileExists(it->first.c_str()))
			{
				Host::AddOSDMessage(TRANSLATE("StereoscopyOverlay", "Could not read game settings; stereoscopy edits retained for retry."), 10.0f);
				++it;
				continue;
			}
			it->second.Commit(disk);
			if (disk.Save(&error))
				it = s_pending_saves.erase(it);
			else
			{
				Host::AddOSDMessage(fmt::format("{}: {}", TRANSLATE("StereoscopyOverlay", "Could not save stereoscopy settings"), error.GetDescription()), 10.0f);
				++it;
			}
		}
		s_pending_base_save |= position_changed || (!state.game && !state.edits.IsEmpty());
		if (s_pending_base_save)
		{
			Error error;
			if (Host::Internal::GetBaseSettingsLayer()->Save(&error))
				s_pending_base_save = false;
			else
				Host::AddOSDMessage(fmt::format("{}: {}", TRANSLATE("StereoscopyOverlay", "Could not save stereoscopy settings"), error.GetDescription()), 10.0f);
		}
	}
	s_buttons.fill(false);
	for (auto& sources : s_button_sources)
		sources.clear();
	s_preview_dirty = false;
	if (scope_changed && reload_game_settings)
	{
		// Close can run inside ProcessEvent; reloading bindings there invalidates its iterators. (AI-assisted)
		Host::RunOnCPUThread([] {
			if (VMManager::HasValidVM())
				VMManager::ReloadGameSettings();
		});
	}
	else if (VMManager::HasValidVM())
		VMManager::RefreshStereoscopySettings();
}

void StereoscopyOverlay::ApplyPreview(Pcsx2Config::GSOptions& options)
{
	if (!IsOpen())
		return;
	MemorySettingsInterface preview;
	SettingsSaveWrapper save(preview);
	options.LoadSave(save);
	{
		std::lock_guard lock(s_mutex);
		s_state.edits.ApplyPreview(preview);
	}
	SettingsLoadWrapper load(preview);
	options.LoadSave(load);
	{
		std::lock_guard lock(s_mutex);
		s_state.options = options;
	}
}

void StereoscopyOverlay::SetDefaultPadChordEnabled(bool enabled)
{
	s_pad_chord_enabled = enabled;
	s_chord_latched = false;
	s_buttons.fill(false);
	for (auto& sources : s_button_sources)
		sources.clear();
}

void StereoscopyOverlay::OnPadInput(GenericInputBinding binding, float value, u64 source)
{
	// Stick motion must neither navigate nor restart D-pad key repeat. (AI-assisted)
	if ((binding >= GenericInputBinding::LeftStickUp && binding <= GenericInputBinding::LeftStickLeft) ||
		(binding >= GenericInputBinding::RightStickUp && binding <= GenericInputBinding::RightStickLeft))
	{
		return;
	}
	const size_t index = static_cast<size_t>(binding);
	if (index >= s_buttons.size() || binding == GenericInputBinding::Unknown)
		return;
	if (value >= 0.5f)
		s_button_sources[index].insert(source);
	else
		s_button_sources[index].erase(source);
	const bool down = !s_button_sources[index].empty();
	const bool was_down = s_buttons[index];
	s_buttons[index] = down;
	if (!Down(GenericInputBinding::L1) && !Down(GenericInputBinding::L2) && !Down(GenericInputBinding::L3))
		s_chord_latched = false;
	if (s_pad_chord_enabled && ChordDown() && !s_chord_latched)
	{
		s_chord_latched = true;
		Toggle();
		return;
	}
	if (!IsOpen() || down == was_down || !down || s_chord_latched)
		return;
	s_hold_start = Clock::now();
	s_next_repeat = s_hold_start + std::chrono::milliseconds(350);
	if (binding == GenericInputBinding::Circle)
		Close();
	else if (binding == GenericInputBinding::Triangle)
		Change(1, true, false);
	else if (binding == GenericInputBinding::Cross)
		Change(1, false, true);
	else
		DirectionAction(1.0f);
}

void StereoscopyOverlay::Update()
{
	if (!IsOpen())
		return;
	const auto now = Clock::now();
	if (now >= s_next_scope_check)
	{
		s_next_scope_check = now + std::chrono::milliseconds(500);
		if (s_state.filename != GetGameSettingsFilename())
		{
			// Update runs after input dispatch, where rebuilding bindings is safe. (AI-assisted)
			Close(false);
			VMManager::ReloadGameSettings();
			Toggle();
			return;
		}
	}
	if (now >= s_next_repeat && !s_chord_latched)
	{
		const float held = std::chrono::duration<float>(now - s_hold_start).count();
		DirectionAction(held > 3.0f ? 100.0f : held > 1.5f ? 10.0f :
															 1.0f);
		s_next_repeat = now + std::chrono::milliseconds(80);
	}
	if (s_preview_dirty)
	{
		s_preview_dirty = false;
		VMManager::RefreshStereoscopySettings();
	}
}

void StereoscopyOverlay::Render()
{
	if (!IsOpen())
		return;
	if (FullscreenUI::HasActiveWindow())
	{
		Host::RunOnCPUThread([]() { Close(); });
		return;
	}
	State state;
	{
		std::lock_guard lock(s_mutex);
		state = s_state;
	}
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	const auto eyes = GSStereoPresentation::GetEyes(GSConfig.StereoMode, static_cast<int>(display.x), static_cast<int>(display.y), false);
	const int eye_count = GSConfig.StereoMode == GSStereoMode::Off ? 1 : 2;
	ImDrawList* draw = ImGui::GetForegroundDrawList();
	ImFont* font = ImGuiManager::GetStandardFont();
	static size_t first = 0;
	for (int eye = 0; eye < eye_count; eye++)
	{
		const auto& viewport = eyes[eye];
		const float available_width = static_cast<float>(std::min(eyes[0].width, eyes[1].width));
		const float available_height = static_cast<float>(std::min(eyes[0].height, eyes[1].height));
		// Mixed-DPI monitor transitions need proper testing on Windows and Linux. (AI-assisted)
		const float height = available_height * 0.9f;
		const float scale = std::max(0.1f, std::min({ImGuiManager::GetGlobalScale(), available_width * 0.65f / 680.0f, height / 480.0f}));
		const float width = 680.0f * scale;
		const float margin = 12.0f;
		const float x = state.position == 0 ? margin : state.position == 1 ? viewport.width - width - margin :
		                                                                     (viewport.width - width) * 0.5f;
		const float y = (viewport.height - height) * 0.5f;
		const float pad = 20.0f * scale;
		const float row_height = (state.game ? 60.0f : 52.0f) * scale;
		const float group_height = 38.0f * scale;
		const float top = y + 96.0f * scale;
		const float position_y = y + height - 64.0f * scale;
		const float bottom = position_y - 16.0f * scale;
		auto starts_group = [](size_t index, size_t first_index) {
			return index == first_index || std::string_view(s_settings[index].group) != s_settings[index - 1].group;
		};
		auto rows_height = [&](size_t start, size_t end) {
			float total = 0;
			for (size_t i = start; i <= end; i++)
				total += row_height + (starts_group(i, start) ? group_height : 0.0f);
			return total;
		};
		if (state.selected < SETTING_COUNT)
		{
			if (state.selected < first)
				first = state.selected;
			while (first < state.selected && rows_height(first, state.selected) > bottom - top)
				first++;
		}
		// Draw in logical eye coordinates, then transform both vertices and scissor rectangles. (AI-assisted)
		const int vertex_start = draw->VtxBuffer.Size;
		auto point = [&](float px, float py) { return ImVec2(px * viewport.scale_x + viewport.offset_x, py * viewport.scale_y + viewport.offset_y); };
		draw->PushClipRect(ImVec2(0, 0), ImVec2(static_cast<float>(viewport.width), static_cast<float>(viewport.height)), false);
		draw->AddDrawCmd();
		const int command_start = draw->CmdBuffer.Size - 1;
		draw->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + height), IM_COL32(0, 0, 0, 191), 12.0f * scale);
		auto text = [&](float px, float py, std::string_view str, float size, float max_width, bool enabled = true) {
			const float measured = font->CalcTextSizeA(size, FLT_MAX, 0, str.data(), str.data() + str.size()).x;
			if (measured > max_width && measured > 0)
				size *= max_width / measured;
			draw->AddText(font, size, ImVec2(px, py), enabled ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 110), str.data(), str.data() + str.size());
		};
		text(x + pad, y + 16 * scale, TRANSLATE("StereoscopyOverlay", "Stereoscopy"), 32 * scale, width - pad * 2);
		text(x + pad, y + 56 * scale, state.subtitle, 20 * scale, width - pad * 2 - 70 * scale);
		draw->AddLine(ImVec2(x + pad, y + 86 * scale), ImVec2(x + width - pad, y + 86 * scale), IM_COL32(255, 255, 255, 90));
		auto row = [&](size_t index, float ry) {
			const bool position = index == SETTING_COUNT;
			const Setting* setting = position ? nullptr : &s_settings[index];
			const bool enabled = position || IsEnabled(*setting, state.options);
			const float item_height = position ? 48 * scale : row_height;
			if (state.selected == index)
				draw->AddRectFilled(ImVec2(x + 8 * scale, ry), ImVec2(x + width - 8 * scale, ry + item_height - 3 * scale), IM_COL32(255, 255, 255, 48), 5 * scale);
			const float control_x = x + width * 0.60f;
			const char* label = position ? TRANSLATE("StereoscopyOverlay", "Overlay position") : TRANSLATE("GraphicsSettingsWidget", setting->label);
			const float label_width = width * 0.56f - pad;
			float label_size = 22 * scale;
			const float label_height = (state.game && !position ? 35.0f : 43.0f) * scale;
			while (label_size > 12 * scale && font->CalcTextSizeA(label_size, FLT_MAX, label_width, label).y > label_height)
				label_size -= scale;
			draw->AddText(font, label_size, ImVec2(x + pad, ry + 5 * scale), enabled ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 110), label, nullptr, label_width);
			if (state.game && !position)
				text(x + pad, ry + 43 * scale, state.values[index].custom ? TRANSLATE("StereoscopyOverlay", "Custom") : TRANSLATE("StereoscopyOverlay", "Global"), 11 * scale, width * 0.5f, enabled);
			if (!position && setting->control == Control::Toggle)
			{
				const ImVec2 a(x + width - pad - 24 * scale, ry + 9 * scale), b(a.x + 22 * scale, a.y + 22 * scale);
				draw->AddRect(a, b, IM_COL32_WHITE, 2 * scale, 2 * scale);
				if (Checked(state.values[index].current))
				{
					draw->AddLine(ImVec2(a.x + 4 * scale, a.y + 11 * scale), ImVec2(a.x + 9 * scale, a.y + 17 * scale), IM_COL32_WHITE, 2 * scale);
					draw->AddLine(ImVec2(a.x + 9 * scale, a.y + 17 * scale), ImVec2(a.x + 19 * scale, a.y + 4 * scale), IM_COL32_WHITE, 2 * scale);
				}
			}
			else if (!position && setting->control == Control::Number)
			{
				const float number = Number(state.values[index].current);
				const float right = x + width - pad - 83 * scale;
				const float center = ry + 22 * scale;
				const float fraction = std::clamp((number - setting->minimum) / (setting->maximum - setting->minimum), 0.0f, 1.0f);
				draw->AddLine(ImVec2(control_x, center), ImVec2(right, center), IM_COL32(255, 255, 255, 90), 3 * scale);
				draw->AddLine(ImVec2(control_x, center), ImVec2(control_x + (right - control_x) * fraction, center), IM_COL32_WHITE, 3 * scale);
				draw->AddCircleFilled(ImVec2(control_x + (right - control_x) * fraction, center), 5 * scale, IM_COL32_WHITE);
				text(right + 10 * scale, ry + 8 * scale, setting->step < 1 ? fmt::format("{:.1f}", number) : fmt::format("{:.0f}", number), 22 * scale, 75 * scale, enabled);
			}
			else
			{
				const char* value = position ? POSITIONS[state.position] : setting->choices[std::clamp(ChoiceIndex(*setting, state.values[index]), 0, static_cast<int>(setting->maximum))];
				const int choice = position ? state.position : ChoiceIndex(*setting, state.values[index]);
				const int last_choice = position ? static_cast<int>(std::size(POSITIONS)) - 1 : static_cast<int>(setting->maximum);
				text(control_x, ry + 8 * scale, "<", 22 * scale, 15 * scale, enabled && choice > 0);
				text(control_x + 24 * scale, ry + 8 * scale, TRANSLATE("GraphicsSettingsWidget", value), 22 * scale, width * 0.4f - pad - 48 * scale, enabled);
				text(x + width - pad - 14 * scale, ry + 8 * scale, ">", 22 * scale, 15 * scale, enabled && choice < last_choice);
			}
		};
		float row_y = top;
		for (size_t i = first; i < SETTING_COUNT; i++)
		{
			const bool group = starts_group(i, first);
			if (row_y + row_height + (group ? group_height : 0.0f) > bottom)
				break;
			if (group)
			{
				// Keep section boundaries visible even when scrolling starts midway through a group. (AI-assisted)
				draw->AddRectFilled(ImVec2(x + 8 * scale, row_y + 4 * scale), ImVec2(x + width - 8 * scale, row_y + group_height - 4 * scale), IM_COL32(255, 255, 255, 28), 3 * scale);
				draw->AddRectFilled(ImVec2(x + 8 * scale, row_y + 4 * scale), ImVec2(x + 11 * scale, row_y + group_height - 4 * scale), IM_COL32_WHITE);
				text(x + pad, row_y + 8 * scale, TRANSLATE("GraphicsSettingsWidget", s_settings[i].group), 19 * scale, width - pad * 2);
				row_y += group_height;
			}
			row(i, row_y);
			row_y += row_height;
		}
		draw->AddLine(ImVec2(x + pad, position_y - 6 * scale), ImVec2(x + width - pad, position_y - 6 * scale), IM_COL32(255, 255, 255, 70));
		row(SETTING_COUNT, position_y);
		text(x + width - pad - 50 * scale, y + 56 * scale, fmt::format("{}/{}", std::min(state.selected + 1, SETTING_COUNT), SETTING_COUNT), 14 * scale, 50 * scale);
		for (int v = vertex_start; v < draw->VtxBuffer.Size; v++)
			draw->VtxBuffer[v].pos = point(draw->VtxBuffer[v].pos.x, draw->VtxBuffer[v].pos.y);
		for (int c = command_start; c < draw->CmdBuffer.Size; c++)
		{
			ImVec4& clip = draw->CmdBuffer[c].ClipRect;
			const ImVec2 a = point(clip.x, clip.y), b = point(clip.z, clip.w);
			clip = ImVec4(a.x, a.y, b.x, b.y);
		}
		draw->PopClipRect();
	}
}
