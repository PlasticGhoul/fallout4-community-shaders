#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace Shader
{
	/// One shader translation unit, with every #include already spliced in.
	struct SourceText
	{
		/// What the compiler is handed. Carries #line directives so that
		/// diagnostics keep pointing at the file the author actually wrote.
		std::string text;

		/// Every file that contributed, the root file first, each canonical.
		/// This is the set the watcher polls.
		std::vector<std::filesystem::path> files;
	};

	/// Reads a_root / a_relative and splices its `#include "..."` lines.
	///
	/// Includes are resolved relative to the including file and must stay
	/// inside a_root: a shader must not be able to read the rest of the disk.
	/// Returns the message to log instead of a translation unit when a file is
	/// missing, an include escapes a_root, or the includes form a cycle.
	[[nodiscard]] std::expected<SourceText, std::string> LoadSource(
		const std::filesystem::path& a_root,
		const std::filesystem::path& a_relative);
}
