#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace Features::Settings
{
	/// Declares one setting under a two segment path, "Block/Key". The store
	/// addresses its values as JSON pointers with one object per segment, so
	/// anything else would address a top level key with a slash in its name.
	/// A malformed path is refused with a log line rather than accepted.
	void DeclareBool(std::string_view a_path, bool a_default);
	void DeclareUInt32(std::string_view a_path, std::uint32_t a_default);

	/// False, respectively zero, for a path that was never declared.
	[[nodiscard]] bool GetBool(std::string_view a_path) noexcept;
	[[nodiscard]] std::uint32_t GetUInt32(std::string_view a_path) noexcept;

	/// Declares the feature's enable switch. Must run before Init: a REX
	/// setting registers itself with its store at construction, and Init is
	/// what walks that registration.
	///
	/// Shorthand for DeclareBool("<a_name>/enabled", a_default).
	void DeclareFeature(std::string_view a_name, bool a_default);

	/// Points the store at a_file and loads it. The overload without an
	/// argument resolves <Documents>/My Games/<save folder>/F4SE/<plugin>.json,
	/// the same directory F4SE puts the log in.
	///
	/// A file that is not there yet is written from the declared defaults
	/// first. That is not a convenience: REX saves through glz::set, which
	/// only ever overwrites keys that already exist, so a settings file has to
	/// come into being some other way.
	void Init(const std::filesystem::path& a_file);
	void Init();

	/// False for a name that was never declared.
	[[nodiscard]] bool IsEnabled(std::string_view a_name) noexcept;

	/// Reloads when the watched file changed. Returns whether it did, which is
	/// also the signal that refused features deserve another try.
	bool ReloadIfChanged() noexcept;

	[[nodiscard]] const std::filesystem::path& File() noexcept;
}
