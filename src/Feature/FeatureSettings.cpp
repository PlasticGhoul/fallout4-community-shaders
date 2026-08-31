#include "Feature/FeatureSettings.h"

#include "Util/FileWatch.h"

#include <REX/FJsonSettingStore.h>
#include <REX/TJsonSetting.h>
#include <REX/W32/OLE32.h>
#include <REX/W32/SHELL32.h>

#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <string>

namespace Features::Settings
{
	namespace
	{
		struct Entry
		{
			// Owned here and never moved after the setting is built: REX keeps
			// only a string_view of it (TJsonSetting.h:48).
			std::string path;

			// Kept apart from the setting's own default, because loading from
			// the base file overwrites that one (TJsonSetting::Load). The value
			// the feature declared is what a generated file has to contain.
			bool declaredDefault{ false };

			std::unique_ptr<REX::TJsonSetting<bool>> setting;
		};

		// A node based container on purpose. The settings hold views into the
		// paths above, so the strings must keep their addresses; a vector would
		// invalidate every one of them on the next growth.
		std::map<std::string, Entry, std::less<>>& Entries()
		{
			static std::map<std::string, Entry, std::less<>> entries;
			return entries;
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

		// REX saves through glz::set, and glz::set only assigns to keys that are
		// already in the document (seek_op<generic_json>). A settings file that
		// does not exist yet can therefore never be produced by saving, so the
		// first one is written here, from the declared defaults.
		void WriteDefaultFile(const std::filesystem::path& a_file)
		{
			std::string text = "{\n";
			for (auto it = Entries().begin(); it != Entries().end(); ++it) {
				text += std::format(
					"    \"{}\": {{\n        \"enabled\": {}\n    }}{}\n",
					it->first,
					it->second.declaredDefault ? "true" : "false",
					std::next(it) == Entries().end() ? "" : ",");
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

	void DeclareFeature(std::string_view a_name, bool a_default)
	{
		auto& entry = Entries()[std::string{ a_name }];
		if (entry.setting != nullptr) {
			return;  // Declared twice; the first declaration wins.
		}

		// A JSON pointer, not a dotted path: REX prepends a slash and hands the
		// result to glz::get, which walks one object per segment. A dot would
		// address a single top level key with a dot in its name instead.
		entry.path = std::format("{}/enabled", a_name);
		entry.declaredDefault = a_default;
		entry.setting = std::make_unique<REX::TJsonSetting<bool>>(entry.path, a_default);
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
		const auto it = Entries().find(a_name);
		if (it == Entries().end() || it->second.setting == nullptr) {
			return false;
		}
		return it->second.setting->GetValue();
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
