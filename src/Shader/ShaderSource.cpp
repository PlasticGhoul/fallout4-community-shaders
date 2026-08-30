#include "Shader/ShaderSource.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>

namespace Shader
{
	namespace
	{
		// Deep enough for any sane shader tree, shallow enough that a cycle we
		// somehow failed to spot still terminates.
		constexpr std::size_t kMaxIncludeDepth = 16;

		void SkipSpace(std::string_view& a_view) noexcept
		{
			while (!a_view.empty() && (a_view.front() == ' ' || a_view.front() == '\t')) {
				a_view.remove_prefix(1);
			}
		}

		// Returns the file named by an `#include "..."` line, or an empty view
		// when the line is anything else. Angle brackets are deliberately not
		// handled: there is no system include path to search.
		std::string_view IncludeTarget(std::string_view a_line) noexcept
		{
			SkipSpace(a_line);
			if (!a_line.starts_with("#")) {
				return {};
			}
			a_line.remove_prefix(1);

			SkipSpace(a_line);
			if (!a_line.starts_with("include")) {
				return {};
			}
			a_line.remove_prefix(7);

			SkipSpace(a_line);
			if (!a_line.starts_with("\"")) {
				return {};
			}
			a_line.remove_prefix(1);

			const auto end = a_line.find('"');
			if (end == std::string_view::npos) {
				return {};
			}

			return a_line.substr(0, end);
		}

		std::optional<std::string> ReadWholeFile(const std::filesystem::path& a_path)
		{
			std::ifstream stream{ a_path, std::ios::binary };
			if (!stream) {
				return std::nullopt;
			}

			std::ostringstream buffer;
			buffer << stream.rdbuf();
			return buffer.str();
		}

		// Path comparison by component, not by string prefix: a plain prefix
		// test would accept a sibling directory whose name merely starts with
		// the root's name.
		bool IsInside(const std::filesystem::path& a_child, const std::filesystem::path& a_root)
		{
			const auto pair =
				std::mismatch(a_root.begin(), a_root.end(), a_child.begin(), a_child.end());
			return pair.first == a_root.end();
		}

		class Splicer
		{
		public:
			explicit Splicer(const std::filesystem::path& a_root) :
				_root(std::filesystem::weakly_canonical(a_root))
			{}

			std::expected<SourceText, std::string> Run(const std::filesystem::path& a_relative)
			{
				if (auto error = Splice(_root / a_relative); error.has_value()) {
					return std::unexpected(*error);
				}
				return std::move(_out);
			}

		private:
			std::optional<std::string> Splice(const std::filesystem::path& a_file);

			std::filesystem::path _root;
			SourceText _out;
			std::vector<std::filesystem::path> _open;
		};

		std::optional<std::string> Splicer::Splice(const std::filesystem::path& a_file)
		{
			std::error_code ec;
			const auto canonical = std::filesystem::weakly_canonical(a_file, ec);
			if (ec) {
				return std::format("cannot resolve {}", a_file.generic_string());
			}

			if (!IsInside(canonical, _root)) {
				return std::format(
					"{} lies outside the shader directory",
					canonical.generic_string());
			}

			if (std::ranges::find(_open, canonical) != _open.end()) {
				return std::format("include cycle through {}", canonical.generic_string());
			}

			if (_open.size() >= kMaxIncludeDepth) {
				return std::format("includes nested deeper than {}", kMaxIncludeDepth);
			}

			const auto content = ReadWholeFile(canonical);
			if (!content.has_value()) {
				return std::format("cannot read {}", canonical.generic_string());
			}

			if (std::ranges::find(_out.files, canonical) == _out.files.end()) {
				_out.files.push_back(canonical);
			}

			_open.push_back(canonical);

			// Forward slashes: a backslash would open an escape sequence in the
			// string literal the compiler parses out of the directive.
			const auto directive = canonical.generic_string();
			_out.text += std::format("#line 1 \"{}\"\n", directive);

			std::istringstream lines{ *content };
			std::string line;
			std::uint32_t lineNumber = 1;

			while (std::getline(lines, line)) {
				if (!line.empty() && line.back() == '\r') {
					line.pop_back();
				}

				const auto target = IncludeTarget(line);
				if (target.empty()) {
					_out.text += line;
					_out.text += '\n';
				} else {
					if (auto error = Splice(canonical.parent_path() / target);
						error.has_value()) {
						return error;
					}
					// Back in this file, on the line after the include.
					_out.text += std::format("#line {} \"{}\"\n", lineNumber + 1, directive);
				}

				++lineNumber;
			}

			_open.pop_back();
			return std::nullopt;
		}
	}

	std::expected<SourceText, std::string> LoadSource(
		const std::filesystem::path& a_root,
		const std::filesystem::path& a_relative)
	{
		Splicer splicer{ a_root };
		return splicer.Run(a_relative);
	}
}
