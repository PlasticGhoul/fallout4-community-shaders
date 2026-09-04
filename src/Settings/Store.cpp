#include "Settings/Internal.h"

#include "Util/FileWatch.h"

#include <REX/FJsonSettingStore.h>
#include <REX/TJsonSetting.h>
#include <REX/W32/OLE32.h>
#include <REX/W32/SHELL32.h>

#include <glaze/glaze.hpp>

#include <format>
#include <limits>
#include <memory>

namespace Settings
{
	namespace
	{
		// One REX setting per record. Exactly one pointer is non-null, chosen by
		// the record's kind: only bool, double and std::string can read a file
		// at all, so a key code and a slider are both doubles.
		struct Bound
		{
			std::unique_ptr<REX::TJsonSetting<bool>> asBool;
			std::unique_ptr<REX::TJsonSetting<double>> asNumber;
			std::unique_ptr<REX::TJsonSetting<std::string>> asString;
		};

		std::map<std::string, Bound, std::less<>>& Bindings()
		{
			static std::map<std::string, Bound, std::less<>> bindings;
			return bindings;
		}

		Bound* FindBinding(std::string_view a_path) noexcept
		{
			const auto it = Bindings().find(a_path);
			return it == Bindings().end() ? nullptr : std::addressof(it->second);
		}

		// Process lifetime, because FSettingStore keeps the path it is given as
		// a string_view rather than copying it (FSettingStore.h).
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

		// REX reads through glz::get<T>, which matches the variant alternative
		// with std::same_as (glaze/core/seek.hpp:271), and glz::generic holds a
		// JSON number only ever as a double (glaze/json/generic.hpp:68). An
		// integer setting therefore never matches, and value_or hands back the
		// default without a word. Hence: store a double, clamp on the way out.
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

		// The same shape REX writes with, so a file we produce and a file REX
		// produces are indistinguishable.
		struct SaveOpts : glz::opts
		{
			static constexpr bool prettify = true;
			std::uint8_t indentation_width = 4;
		};

		// glz::generic uses ordered_small_map, so objects keep insertion order:
		// an existing file keeps the order it had, and a new key lands at the
		// end of its block. Nothing reshuffles on a write.
		glz::generic& ObjectAt(glz::generic& a_parent, const std::string& a_key)
		{
			auto& child = a_parent[a_key];
			if (!child.is_object()) {
				child = glz::generic::object_t{};
			}
			return child;
		}

		void PutValue(glz::generic& a_block, const Impl::Record& a_record)
		{
			const auto* const bound = FindBinding(a_record.path);

			switch (a_record.kind) {
			case Kind::kBool:
				a_block[a_record.key] = bound && bound->asBool ? bound->asBool->GetValue() :
				                                                 a_record.defaultBool;
				break;
			case Kind::kSlider:
			case Kind::kKey:
				a_block[a_record.key] = bound && bound->asNumber ? bound->asNumber->GetValue() :
				                                                   a_record.defaultNumber;
				break;
			case Kind::kChoice:
				a_block[a_record.key] = bound && bound->asString ? bound->asString->GetValue() :
				                                                   a_record.defaultChoice;
				break;
			}
		}

		enum class WriteMode
		{
			/// Adds what is absent and touches nothing else. This is what
			/// extending a file means: a key the player already set must not be
			/// reset by the arrival of a key they never saw.
			kFillMissing,

			/// Every declared key takes the value it currently holds. This is
			/// what saving means.
			kOverwrite
		};

		// Reads the file first and writes onto what came back. Two things
		// follow, and both are the point: unknown keys survive because we build
		// on the parsed tree, and missing keys appear because
		// glz::generic::operator[] inserts. glz::set could do neither - it bails
		// out at value.find(key) == end() (glaze/core/seek.hpp:238), which is
		// why REX's own Save can never produce a file or extend one.
		bool WriteFile(const std::filesystem::path& a_file, WriteMode a_mode)
		{
			glz::generic root{};
			if (std::filesystem::exists(a_file)) {
				(void)glz::read_file_json(root, a_file.string(), std::string{});
			}
			if (!root.is_object()) {
				root = glz::generic::object_t{};
			}

			for (const auto& entry : Impl::Records()) {
				const auto& record = entry.second;
				auto& block = ObjectAt(root, record.block);

				if (a_mode == WriteMode::kFillMissing && block.contains(record.key)) {
					continue;
				}
				PutValue(block, record);
			}

			std::error_code ec;
			if (const auto parent = a_file.parent_path(); !parent.empty()) {
				std::filesystem::create_directories(parent, ec);
			}

			if (glz::write_file_json<SaveOpts{}>(root, a_file.string(), std::string{})) {
				return false;
			}
			return true;
		}

		enum class FileState
		{
			/// Present but not JSON. Left alone: a typo must not cost the player
			/// their settings.
			kUnreadable,

			/// At least one declared key is absent. The only occasion to rewrite
			/// on startup - doing it unconditionally would churn the timestamp
			/// on every launch and risk the file to a crash mid-write.
			kIncomplete,

			kComplete
		};

		FileState Inspect(const std::filesystem::path& a_file)
		{
			glz::generic root{};
			if (glz::read_file_json(root, a_file.string(), std::string{}) || !root.is_object()) {
				return FileState::kUnreadable;
			}

			for (const auto& entry : Impl::Records()) {
				const auto& record = entry.second;
				if (!root.contains(record.block)) {
					return FileState::kIncomplete;
				}

				const auto& block = root[record.block];
				if (!block.is_object() || !block.contains(record.key)) {
					return FileState::kIncomplete;
				}
			}
			return FileState::kComplete;
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

		// Incremental, and never destructive. REX's store keeps raw ISetting*
		// in a protected vector it offers no way to clear (ISettingStore.h), so
		// a setting that has been constructed has to outlive the process. A
		// record declared after the first Init therefore gets its binding here
		// rather than nowhere.
		void Bind()
		{
			for (const auto& entry : Impl::Records()) {
				const auto& record = entry.second;
				auto& bound = Bindings()[record.path];

				switch (record.kind) {
				case Kind::kBool:
					if (!bound.asBool) {
						bound.asBool = std::make_unique<REX::TJsonSetting<bool>>(
							record.path,
							record.defaultBool);
					}
					break;
				case Kind::kSlider:
				case Kind::kKey:
					if (!bound.asNumber) {
						bound.asNumber = std::make_unique<REX::TJsonSetting<double>>(
							record.path,
							record.defaultNumber);
					}
					break;
				case Kind::kChoice:
					if (!bound.asString) {
						bound.asString = std::make_unique<REX::TJsonSetting<std::string>>(
							record.path,
							record.defaultChoice);
					}
					break;
				}
			}
		}

		// Init(X) means "reflect X, and the declared default wherever X says
		// nothing". Without this a second Init would inherit whatever the first
		// file happened to hold, for every key the second one omits.
		void ResetToDeclaredDefaults()
		{
			for (const auto& entry : Impl::Records()) {
				const auto& record = entry.second;
				auto* const bound = FindBinding(record.path);
				if (bound == nullptr) {
					continue;
				}

				if (bound->asBool) {
					bound->asBool->SetValue(record.defaultBool);
				} else if (bound->asNumber) {
					bound->asNumber->SetValue(record.defaultNumber);
				} else if (bound->asString) {
					bound->asString->SetValue(record.defaultChoice);
				}
			}
		}
	}

	bool GetBool(std::string_view a_path) noexcept
	{
		const auto* const bound = FindBinding(a_path);
		return bound && bound->asBool ? bound->asBool->GetValue() : false;
	}

	double GetDouble(std::string_view a_path) noexcept
	{
		const auto* const bound = FindBinding(a_path);
		return bound && bound->asNumber ? bound->asNumber->GetValue() : 0.0;
	}

	std::uint32_t GetUInt32(std::string_view a_path) noexcept
	{
		return NarrowToUInt32(GetDouble(a_path));
	}

	std::string GetString(std::string_view a_path) noexcept
	{
		const auto* const bound = FindBinding(a_path);
		return bound && bound->asString ? bound->asString->GetValue() : std::string{};
	}

	bool IsEnabled(std::string_view a_name) noexcept
	{
		return GetBool(std::format("{}/enabled", a_name));
	}

	void Init(const std::filesystem::path& a_file)
	{
		CurrentFile() = a_file;

		Bind();
		ResetToDeclaredDefaults();

		if (!std::filesystem::exists(a_file)) {
			if (!WriteFile(a_file, WriteMode::kFillMissing)) {
				REX::ERROR("could not write {}, settings stay at defaults", a_file.generic_string());
			}
		} else {
			switch (Inspect(a_file)) {
			case FileState::kUnreadable:
				// Named apart from the other two refusals on purpose: "could not
				// be written" and "is not valid JSON" need different answers
				// from whoever reads the log.
				REX::ERROR(
					"{} is not valid JSON, every setting stays at its default",
					a_file.generic_string());
				break;
			case FileState::kIncomplete:
				REX::INFO(
					"{} is missing declared keys, extending it",
					a_file.generic_string());
				if (!WriteFile(a_file, WriteMode::kFillMissing)) {
					REX::ERROR("could not extend {}", a_file.generic_string());
				}
				break;
			case FileState::kComplete:
				break;
			}
		}

		StoredPath() = a_file.string();

		auto* const store = REX::FJsonSettingStore::GetSingleton();

		// fileBase stays empty, and our file goes in as fileUser. That is not a
		// detail: FJsonSettingStore::Load hands fileBase to Load(data, true),
		// and TJsonSetting::Load with a_isBase reads the file's value into
		// m_valueDefault - clobbering the declared default with whatever the
		// first file happened to say. As fileUser the values land in m_value and
		// the declared defaults survive, which is what makes a missing key fall
		// back correctly. REX's own Save writes to fileBase, but we never call
		// it: it cannot create keys, so it cannot write our file anyway.
		store->Init("", StoredPath().c_str());
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

	const std::filesystem::path& File() noexcept
	{
		return CurrentFile();
	}
}
