#include "Util/GamePaths.h"

namespace Util
{
	const std::filesystem::path& DataDirectory()
	{
		// Resolved once. The module's path cannot change while we run, and the
		// result is asked for every time a font or a translation is loaded.
		static const std::filesystem::path directory = [] {
			const std::filesystem::path exe =
				REX::FModule::GetExecutingModule().GetFileName();
			return exe.parent_path() / "Data";
		}();
		return directory;
	}

	const std::filesystem::path& PluginDataDirectory()
	{
		static const std::filesystem::path directory =
			DataDirectory() / "F4SE" / "Plugins" / F4SE::GetPluginName();
		return directory;
	}
}
