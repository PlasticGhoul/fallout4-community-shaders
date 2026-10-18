#include "Bridge/StartupSwitch.h"

#include <glaze/glaze.hpp>

#include <format>
#include <fstream>
#include <sstream>

namespace Bridge
{
	namespace
	{
		/// Every feature that needs the bridge, in the order the reason names them.
		constexpr std::string_view kBlocks[]{ "FrameGeneration", "NeuralRendering" };
		constexpr std::string_view kKey = "enabled";

		/// Empty when the block's switch is on, else why not.
		std::string ReadSwitch(glz::generic& a_root, std::string_view a_block)
		{
			const std::string block{ a_block };
			if (!a_root.contains(block)) {
				return std::format("the settings file has no {} block", a_block);
			}
			auto& entry = a_root[block];
			if (!entry.is_object() || !entry.contains(kKey)) {
				return std::format("{}/enabled is missing", a_block);
			}
			const auto& value = entry[kKey];
			if (!value.is_boolean()) {
				return std::format("{}/enabled is not true or false", a_block);
			}
			if (!value.get<bool>()) {
				return std::format("{}/enabled is false", a_block);
			}
			return {};
		}
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
		std::string reasons;
		for (const auto block : kBlocks) {
			const auto reason = ReadSwitch(root, block);
			if (reason.empty()) {
				return { true, {} };
			}
			reasons += reasons.empty() ? reason : "; " + reason;
		}
		return { false, std::move(reasons) };
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
