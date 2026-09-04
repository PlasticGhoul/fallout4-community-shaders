#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Settings
{
	/// What a setting is, not merely what type it has. The type alone is not
	/// enough: a virtual key code is a double like any other, and neither a
	/// slider nor a number box is the right thing to put in front of it.
	enum class Kind
	{
		kBool,
		kSlider,
		kChoice,
		kKey
	};

	/// A read-only view of one declared setting. Every string_view points into
	/// the schema's own table, which is node based and outlives the process.
	struct Entry
	{
		std::string_view path;
		std::string_view block;
		std::string_view key;
		Kind kind{ Kind::kBool };

		std::string_view labelKey;
		std::string_view labelText;
		std::string_view helpKey;
		std::string_view helpText;

		// kSlider only.
		double min{ 0.0 };
		double max{ 0.0 };

		// kChoice only.
		std::span<const std::string> choices;

		bool defaultBool{ false };
		double defaultNumber{ 0.0 };
		std::string_view defaultChoice;

		/// The feature's own on/off switch, declared through DeclareFeature.
		/// The panel draws it as the checkbox of the heading rather than as a
		/// line among the feature's settings.
		bool isFeatureSwitch{ false };
	};

	/// Returned by every Declare. A view of the record just declared, valid
	/// only for the length of the declaration expression.
	class Handle
	{
	public:
		explicit Handle(void* a_record) noexcept :
			_record(a_record)
		{}

		/// The English default plus its translation key. Both are written out
		/// rather than derived from the path: a derivation would have to be
		/// kept identical in C++ and in tools/extract-i18n.py, and a drift
		/// between the two loses a translation without breaking anything.
		Handle& Label(std::string_view a_key, std::string_view a_text) noexcept;
		Handle& Help(std::string_view a_key, std::string_view a_text) noexcept;

	private:
		// Impl::Record*, or null when the declaration was refused.
		void* _record;
	};

	/// Declares one setting under a two segment path, "Block/Key". The store
	/// addresses its values as JSON pointers with one object per segment, so
	/// anything else would address a top level key with a slash in its name.
	/// A malformed path is refused with a log line rather than accepted; the
	/// returned handle then does nothing.
	Handle DeclareBool(std::string_view a_path, bool a_default);
	Handle DeclareSlider(std::string_view a_path, double a_default, double a_min, double a_max);
	Handle DeclareChoice(
		std::string_view a_path,
		std::string_view a_default,
		std::vector<std::string> a_choices);

	/// A virtual key code. Stored as a double like every whole number, because
	/// REX cannot read an integer setting back from the file.
	Handle DeclareKey(std::string_view a_path, std::uint32_t a_default);

	/// Shorthand for DeclareBool("<a_name>/enabled", a_default), additionally
	/// marked as the block's feature switch.
	Handle DeclareFeature(std::string_view a_name, bool a_default);

	/// Blocks in declaration order, entries within a block in declaration
	/// order. Deterministic, so the written file does not reshuffle itself.
	void ForEachBlock(const std::function<void(std::string_view a_block)>& a_visit);
	void ForEachEntry(
		std::string_view a_block,
		const std::function<void(const Entry& a_entry)>& a_visit);

	/// False, zero, respectively empty, for a path that was never declared.
	[[nodiscard]] bool GetBool(std::string_view a_path) noexcept;
	[[nodiscard]] double GetDouble(std::string_view a_path) noexcept;
	[[nodiscard]] std::uint32_t GetUInt32(std::string_view a_path) noexcept;
	[[nodiscard]] std::string GetString(std::string_view a_path) noexcept;

	/// Writes into the value only. Nothing reaches the disk until Save, and
	/// every Set marks the settings as changed for ConsumeChanged.
	void SetBool(std::string_view a_path, bool a_value) noexcept;
	void SetDouble(std::string_view a_path, double a_value) noexcept;
	void SetUInt32(std::string_view a_path, std::uint32_t a_value) noexcept;
	void SetString(std::string_view a_path, std::string_view a_value) noexcept;

	/// Every declared setting back to its declared default. Marks changed.
	void RestoreDefaults() noexcept;

	/// Points the store at a_file and loads it. The overload without an
	/// argument resolves <Documents>/My Games/<save folder>/F4SE/<plugin>.json,
	/// the same directory F4SE puts the log in.
	///
	/// A file that is not there is written from the declared defaults. A file
	/// that is missing declared keys is extended by them, keeping the values
	/// and the unknown keys it already had.
	void Init(const std::filesystem::path& a_file);
	void Init();

	/// Writes every declared setting, then rebases the watch so that our own
	/// write does not come back as somebody else's change.
	void Save() noexcept;

	/// True when the file changed on disk or something was Set since the last
	/// call. Both are the same occasion to give a refused feature another try.
	[[nodiscard]] bool ConsumeChanged() noexcept;

	/// False for a name that was never declared.
	[[nodiscard]] bool IsEnabled(std::string_view a_name) noexcept;

	[[nodiscard]] const std::filesystem::path& File() noexcept;
}
