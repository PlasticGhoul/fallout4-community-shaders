#include "Bridge/StartupSwitch.h"

#include <glaze/glaze.hpp>

#include <fstream>
#include <sstream>

namespace Bridge
{
	namespace
	{
		constexpr std::string_view kBlock = "FrameGeneration";
		constexpr std::string_view kKey = "enabled";
	}

	StartupSwitch ParseStartupSwitch(std::string_view a_json)
	{
		// A copy: glaze reads null-terminated buffers by default, and a view
		// promises no terminator.
		const std::string buffer{ a_json };
		glz::generic root{};
		if (glz::read_json(root, buffer) || !root.is_object()) {
			return { false, "the settings file is not valid JSON" };
		}
		if (!root.contains(kBlock)) {
			return { false, "the settings file has no FrameGeneration block" };
		}
		const auto& block = root[kBlock];
		if (!block.is_object() || !block.contains(kKey)) {
			return { false, "FrameGeneration/enabled is missing" };
		}
		const auto& value = block[kKey];
		if (!value.is_boolean()) {
			return { false, "FrameGeneration/enabled is not true or false" };
		}
		if (!value.get<bool>()) {
			return { false, "FrameGeneration/enabled is false" };
		}
		return { true, {} };
	}

	StartupSwitch ReadStartupSwitch(const std::filesystem::path& a_file)
	{
		if (a_file.empty()) {
			return { false, "the settings file could not be located" };
		}
		std::ifstream stream(a_file, std::ios::binary);
		if (!stream) {
			return { false, "no settings file yet" };
		}
		std::ostringstream text;
		text << stream.rdbuf();
		return ParseStartupSwitch(text.str());
	}
}
