#pragma once

#include <filesystem>

namespace Util
{
	/// The game's Data directory.
	///
	/// Derived from the executing module rather than the working directory:
	/// the working directory is not ours to rely on, and a launcher may set it
	/// anywhere. Subproject C established this route for the shader tree; it is
	/// here so the font and the translations do not each rediscover it.
	[[nodiscard]] const std::filesystem::path& DataDirectory();

	/// Data/F4SE/Plugins/<PluginName>, where everything we read at runtime
	/// lives. Fallout 4 loads from F4SE, not from SKSE.
	[[nodiscard]] const std::filesystem::path& PluginDataDirectory();
}
