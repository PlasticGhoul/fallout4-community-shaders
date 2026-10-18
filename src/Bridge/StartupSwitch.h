#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Bridge
{
	/// FrameGeneration/enabled or NeuralRendering/enabled as the settings file
	/// says it at load time - either feature needs the bridge. The game
	/// creates its device long before kGameDataReady, and whether the bridge
	/// exists is decided then - before Settings::Init has run and before any
	/// feature has declared anything. So this reads the two keys itself
	/// instead of going through the store.
	struct StartupSwitch
	{
		bool on{ false };
		/// Why it is off, for the log and the feature page; empty when on.
		std::string reason;
	};

	[[nodiscard]] StartupSwitch ParseStartupSwitch(std::string_view a_json);
	[[nodiscard]] StartupSwitch ReadStartupSwitch(const std::filesystem::path& a_file);
}
