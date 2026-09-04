#include "Feature/FeatureSettings.h"

#include "Util/FileWatch.h"

#include <REX/FJsonSettingStore.h>
#include <REX/TJsonSetting.h>
#include <REX/W32/OLE32.h>
#include <REX/W32/SHELL32.h>

#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace Features::Settings
{
	namespace
	{
		template <class T>
		struct Entry
		{
			// Owned here and never moved after the setting is built: REX keeps
			// only a string_view of it (TJsonSetting.h:48).
			std::string path;

			// Kept apart from the setting's own default, because loading from
			// the base file overwrites that one (TJsonSetting::Load). The value
			// that was declared is what a generated file has to contain.
			T declaredDefault{};

			std::unique_ptr<REX::TJsonSetting<T>> setting;
		};

		// Node based containers on purpose. The settings hold views into the
		// paths above, so the strings must keep their addresses; a vector would
		// invalidate every one of them on the next growth.
		template <class T>
		std::map<std::string, Entry<T>, std::less<>>& Entries()
		{
			static std::map<std::string, Entry<T>, std::less<>> entries;
			return entries;
		}

		// "Block/Key" and nothing else. One slash, neither half empty.
		bool SplitPath(std::string_view a_path, std::string_view& a_block, std::string_view& a_key)
		{
			const auto slash = a_path.find('/');
			if (slash == std::string_view::npos || slash == 0 || slash + 1 >= a_path.size()) {
				return false;
			}
			if (a_path.find('/', slash + 1) != std::string_view::npos) {
				return false;
			}

			a_block = a_path.substr(0, slash);
			a_key = a_path.substr(slash + 1);
			return true;
		}

		template <class T>
		void Declare(std::string_view a_path, T a_default)
		{
			std::string_view block;
			std::string_view key;
			if (!SplitPath(a_path, block, key)) {
				REX::ERROR("setting path {} is not <Block>/<Key>, ignored", a_path);
				return;
			}

			auto& entry = Entries<T>()[std::string{ a_path }];
			if (entry.setting != nullptr) {
				return;  // Declared twice; the first declaration wins.
			}

			// A JSON pointer, not a dotted path: REX prepends a slash and hands
			// the result to glz::get, which walks one object per segment. A dot
			// would address a single top level key with a dot in its name.
			entry.path = std::string{ a_path };
			entry.declaredDefault = a_default;
			entry.setting = std::make_unique<REX::TJsonSetting<T>>(entry.path, a_default);
		}

		template <class T>
		T Get(std::string_view a_path, T a_fallback) noexcept
		{
			const auto it = Entries<T>().find(a_path);
			if (it == Entries<T>().end() || it->second.setting == nullptr) {
				return a_fallback;
			}
			return it->second.setting->GetValue();
		}

		// Every whole number is stored as a double, and that is not a matter of
		// taste. REX reads through glz::get<T>, which compares the requested
		// type against the variant alternative with std::same_as
		// (glaze/core/seek.hpp:271), and glz::generic holds a JSON number only
		// ever as a double (glaze/json/generic.hpp:68). An integer setting
		// therefore never matches, and value_or hands back the default without
		// a word. TJsonSetting<std::uint32_t> links and runs; it just cannot
		// read a file.
		std::uint32_t NarrowToUInt32(double a_value) noexcept
		{
			constexpr auto max = static_cast<double>(std::numeric_limits<std::uint32_t>::max());

			if (!(a_value >= 0.0)) {  // Also catches NaN, which fails every comparison.
				return 0;
			}
			if (a_value >= max) {
				return std::numeric_limits<std::uint32_t>::max();
			}
			return static_cast<std::uint32_t>(a_value);
		}

		// Process lifetime, because FSettingStore stores the path it is given
		// as a string_view rather than copying it (FSettingStore.h).
		std::string& StoredPath()
		{
			static std::string path;
			return path;
		}

		std::filesystem::path& CurrentFile()
		{
			static std::filesystem::path file;
			return file;
		}

		Util::FileWatch& Watch()
		{
			static Util::FileWatch watch;
			return watch;
		}

		std::filesystem::path ResolveDefaultFile()
		{
			wchar_t* raw = nullptr;
			const auto result = REX::W32::SHGetKnownFolderPath(
				REX::W32::FOLDERID_Documents,
				REX::W32::KF_FLAG_DEFAULT,
				nullptr,
				std::addressof(raw));

			const std::unique_ptr<wchar_t[], decltype(&REX::W32::CoTaskMemFree)> owned(
				raw,
				REX::W32::CoTaskMemFree);

			if (!owned || result != 0) {
				REX::ERROR("could not resolve the documents folder, settings stay at defaults");
				return {};
			}

			// Deliberately the same directory F4SE puts the log in: user
			// writable, and outside the Data tree that Vortex manages.
			std::filesystem::path path = owned.get();
			path /= std::format(
				"My Games/{}/F4SE/{}.json",
				F4SE::GetSaveFolderName(),
				F4SE::GetPluginName());
			return path;
		}

		// Collected from every kind of setting and grouped by the first path
		// segment, so that two settings in one block share one JSON object.
		//
		// Sorted by key rather than gathered per type: the file must not depend
		// on which C++ type a setting happens to have, or adding one would
		// reshuffle the block it lands in.
		std::map<std::string, std::map<std::string, std::string>> DefaultsByBlock()
		{
			std::map<std::string, std::map<std::string, std::string>> blocks;

			const auto add = [&blocks](std::string_view a_path, std::string a_literal) {
				std::string_view block;
				std::string_view key;
				if (SplitPath(a_path, block, key)) {
					blocks[std::string{ block }].insert_or_assign(std::string{ key }, std::move(a_literal));
				}
			};

			for (const auto& [path, entry] : Entries<bool>()) {
				add(path, entry.declaredDefault ? "true" : "false");
			}
			// Whole numbers included: they live among the doubles, and
			// std::format writes a value that came in as an integer back
			// without a fractional part.
			for (const auto& [path, entry] : Entries<double>()) {
				add(path, std::format("{}", entry.declaredDefault));
			}

			return blocks;
		}

		// REX saves through glz::set, and glz::set only assigns to keys that are
		// already in the document (seek_op<generic_json>). A settings file that
		// does not exist yet can therefore never be produced by saving, so the
		// first one is written here, from the declared defaults.
		void WriteDefaultFile(const std::filesystem::path& a_file)
		{
			const auto blocks = DefaultsByBlock();

			std::string text = "{\n";
			for (auto block = blocks.begin(); block != blocks.end(); ++block) {
				text += std::format("    \"{}\": {{\n", block->first);
				for (auto it = block->second.begin(); it != block->second.end(); ++it) {
					text += std::format(
						"        \"{}\": {}{}\n",
						it->first,
						it->second,
						std::next(it) == block->second.end() ? "" : ",");
				}
				text += std::format("    }}{}\n", std::next(block) == blocks.end() ? "" : ",");
			}
			text += "}\n";

			std::error_code ec;
			if (const auto parent = a_file.parent_path(); !parent.empty()) {
				std::filesystem::create_directories(parent, ec);
			}

			std::ofstream stream{ a_file, std::ios::binary };
			if (!stream) {
				REX::ERROR(
					"could not write {}, settings stay at defaults",
					a_file.generic_string());
				return;
			}

			stream.write(text.data(), static_cast<std::streamsize>(text.size()));
			REX::INFO("wrote a settings file with the defaults to {}", a_file.generic_string());
		}
	}

	void DeclareBool(std::string_view a_path, bool a_default)
	{
		Declare<bool>(a_path, a_default);
	}

	void DeclareUInt32(std::string_view a_path, std::uint32_t a_default)
	{
		Declare<double>(a_path, static_cast<double>(a_default));
	}

	bool GetBool(std::string_view a_path) noexcept
	{
		return Get<bool>(a_path, false);
	}

	std::uint32_t GetUInt32(std::string_view a_path) noexcept
	{
		return NarrowToUInt32(Get<double>(a_path, 0.0));
	}

	void DeclareFeature(std::string_view a_name, bool a_default)
	{
		DeclareBool(std::format("{}/enabled", a_name), a_default);
	}

	void Init(const std::filesystem::path& a_file)
	{
		CurrentFile() = a_file;

		if (!std::filesystem::exists(a_file)) {
			WriteDefaultFile(a_file);
		}

		StoredPath() = a_file.string();

		auto* const store = REX::FJsonSettingStore::GetSingleton();

		// fileUser stays empty: Save() writes to fileBase, so pointing fileBase
		// anywhere but the user's own file would overwrite shipped defaults.
		store->Init(StoredPath().c_str(), "");
		store->Load();

		const std::filesystem::path watched[]{ a_file };
		Watch().Reset(watched);

		REX::INFO("settings loaded from {}", a_file.generic_string());
	}

	void Init()
	{
		const auto file = ResolveDefaultFile();
		if (file.empty()) {
			return;
		}
		Init(file);
	}

	bool IsEnabled(std::string_view a_name) noexcept
	{
		// One std::string per call, once per feature per frame. Nothing at two
		// features; worth measuring if subproject F turns that into twenty.
		return GetBool(std::format("{}/enabled", a_name));
	}

	bool ReloadIfChanged() noexcept
	{
		if (!Watch().Poll()) {
			return false;
		}

		REX::INFO("settings changed, reloading");
		REX::FJsonSettingStore::GetSingleton()->Load();
		return true;
	}

	const std::filesystem::path& File() noexcept
	{
		return CurrentFile();
	}
}
