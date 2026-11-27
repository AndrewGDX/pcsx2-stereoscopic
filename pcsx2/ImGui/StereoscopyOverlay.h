// SPDX-FileCopyrightText: 2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "Config.h"
#include <span>
#include <optional>
#include <string>
#include <vector>
#include <string_view>

class SettingsInterface;
enum class GenericInputBinding : u8;

namespace StereoscopyOverlay
{
	inline constexpr const char* HOTKEY = "ToggleStereoscopyOverlay";
	inline constexpr const char* PAD_CHORD = "Pad1/L1 & Pad1/L2 & Pad1/L3";
	inline constexpr const char* POSITION_KEY = "StereoscopyOverlayPosition";
	inline constexpr const char* POSITIONS[] = {"Left", "Right", "Center"};

	enum class Control
	{
		Toggle,
		Number,
		Choice,
		Enum
	};
	struct Setting
	{
		const char* key;
		const char* label;
		const char* group;
		Control control;
		float minimum = 0;
		float maximum = 1;
		float step = 1;
		const char* const* choices = nullptr;
	};

	// Owns only edited keys. Preview and persistence are deliberately separate. (AI-assisted)
	class SettingsTransaction
	{
	public:
		void Set(std::string_view key, std::optional<std::string> stored, std::string effective);
		void Merge(const SettingsTransaction& newer);
		void ApplyPreview(SettingsInterface& settings) const;
		void Commit(SettingsInterface& settings) const;
		bool IsEmpty() const { return m_edits.empty(); }

	private:
		struct Edit
		{
			std::string key;
			std::optional<std::string> stored;
			std::string effective;
		};
		std::vector<Edit> m_edits;
	};

	std::span<const Setting> GetSettings();
	bool IsEnabled(const Setting& setting, const Pcsx2Config::GSOptions& options);

	// CPU thread lifecycle/input. Rendering only reads a synchronized snapshot.
	bool IsOpen();
	void Toggle();
	// Layer replacement already reloads settings; input handlers must defer that reload. (AI-assisted)
	void Close(bool reload_game_settings = true);
	void SetDefaultPadChordEnabled(bool enabled);
	void OnPadInput(GenericInputBinding binding, float value, u64 source = 0);
	void Update();
	void ApplyPreview(Pcsx2Config::GSOptions& options);
	void Render();
	void MigrateHotkey(SettingsInterface& settings);
} // namespace StereoscopyOverlay
