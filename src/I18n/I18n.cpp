#include "I18n/I18n.h"

#include <glaze/glaze.hpp>

#include <algorithm>
#include <format>

namespace
{
	// A locale code becomes a filename, so it is checked before it is used:
	// two or three letters, optionally an underscore and two to four more.
	// "en", "de", "zh_CN", "pt_BR". Hand written rather than a std::regex,
	// which would pull a heavy header in for ten lines of comparison.
	bool IsValidLocaleCode(std::string_view a_locale)
	{
		const auto letters = [](std::string_view a_text, std::size_t a_min, std::size_t a_max) {
			if (a_text.size() < a_min || a_text.size() > a_max) {
				return false;
			}
			return std::all_of(a_text.begin(), a_text.end(), [](char c) {
				return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
			});
		};

		const auto underscore = a_locale.find('_');
		if (underscore == std::string_view::npos) {
			return letters(a_locale, 2, 3);
		}

		return letters(a_locale.substr(0, underscore), 2, 3) &&
		       letters(a_locale.substr(underscore + 1), 2, 4);
	}

	// The display name a locale offers for itself. Empty when the file does not
	// parse, which is also how the caller learns not to offer it.
	bool ReadDisplayName(const std::filesystem::path& a_file, std::string& a_name)
	{
		glz::generic root{};
		if (glz::read_file_json(root, a_file.string(), std::string{}) || !root.is_object()) {
			return false;
		}

		a_name = a_file.stem().string();

		if (root.contains("_meta")) {
			const auto& meta = root["_meta"];
			if (meta.is_object() && meta.contains("language")) {
				if (const auto* const language = meta["language"].get_if<std::string>()) {
					a_name = *language;
				}
			}
		}
		return true;
	}
}

void I18n::Init(const std::filesystem::path& a_directory)
{
	const std::unique_lock lock{ _mutex };

	_directory = a_directory;
	_strings.clear();
	_fallback.clear();
	_inlineStorage.clear();
	_inlineIndex.clear();
	_available.clear();
	_current = "en";

	DiscoverLocales();

	if (!LoadLocaleInto("en", _fallback)) {
		REX::INFO(
			"no en.json under {}, every string falls back to its inline default",
			_directory.generic_string());
	}

	REX::INFO(
		"i18n ready, locale {}, {} locale(s) available, {} fallback key(s)",
		_current,
		_available.size(),
		_fallback.size());
}

const char* I18n::Get(std::string_view a_key, const char* a_default) const
{
	const std::string key{ a_key };

	// Fast path under a shared lock: concurrent readers are the normal case,
	// one per string per frame.
	{
		const std::shared_lock lock{ _mutex };

		if (const auto it = _strings.find(key); it != _strings.end()) {
			return it->second.c_str();
		}
		if (const auto it = _fallback.find(key); it != _fallback.end()) {
			return it->second.c_str();
		}
		if (const auto it = _inlineIndex.find(key); it != _inlineIndex.end()) {
			return it->second;
		}
	}

	// Slow path: the inline default has to be stored somewhere that outlives
	// this call, because the caller keeps the pointer.
	{
		const std::unique_lock lock{ _mutex };

		// Checked again: another thread may have inserted it between the two
		// locks.
		if (const auto it = _inlineIndex.find(key); it != _inlineIndex.end()) {
			return it->second;
		}

		// The key itself as the last resort, so a missing translation shows
		// something a bug report can name rather than nothing.
		_inlineStorage.emplace_back(a_default != nullptr ? std::string{ a_default } : key);
		const char* const stored = _inlineStorage.back().c_str();
		_inlineIndex.emplace(key, stored);
		return stored;
	}
}

std::string I18n::CurrentLocale() const
{
	const std::shared_lock lock{ _mutex };
	return _current;
}

void I18n::SetLocale(std::string_view a_locale)
{
	const std::unique_lock lock{ _mutex };

	if (a_locale == _current) {
		return;
	}

	if (!IsValidLocaleCode(a_locale)) {
		REX::WARN("rejected invalid locale code {}", a_locale);
		return;
	}

	if (a_locale == "en") {
		// English is _fallback, which is always loaded. A second copy in
		// _strings would only be another thing to keep in step.
		_strings.clear();
		_inlineStorage.clear();
		_inlineIndex.clear();
		_current = "en";
		REX::INFO("switched to English");
		return;
	}

	std::unordered_map<std::string, std::string> loaded;
	if (!LoadLocaleInto(a_locale, loaded)) {
		REX::WARN("could not load locale {}, staying on {}", a_locale, _current);
		return;
	}

	_strings = std::move(loaded);

	// Dropped, not kept: an inline default cached while another locale was
	// current would outrank the new locale's translation of the same key.
	_inlineStorage.clear();
	_inlineIndex.clear();
	_current = std::string{ a_locale };

	REX::INFO("switched to locale {}", _current);
}

std::vector<std::pair<std::string, std::string>> I18n::AvailableLocales() const
{
	const std::shared_lock lock{ _mutex };
	return _available;
}

void I18n::DiscoverLocales()
{
	std::error_code ec;
	if (!std::filesystem::exists(_directory, ec)) {
		REX::INFO("no translations directory at {}", _directory.generic_string());
		_available.emplace_back("en", "English");
		return;
	}

	for (const auto& entry : std::filesystem::directory_iterator{ _directory, ec }) {
		if (!entry.is_regular_file(ec)) {
			continue;
		}

		auto extension = entry.path().extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](char c) {
			return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		});
		if (extension != ".json") {
			continue;
		}

		const auto locale = entry.path().stem().string();
		if (!IsValidLocaleCode(locale)) {
			continue;
		}

		std::string displayName;
		if (!ReadDisplayName(entry.path(), displayName)) {
			// Not offered. A locale that cannot be parsed cannot be switched
			// to either, and offering it would be a trap rather than a choice.
			REX::WARN("translation file {} could not be read, skipped", entry.path().generic_string());
			continue;
		}

		_available.emplace_back(locale, std::move(displayName));
	}

	// English first, then by display name: the fallback belongs at the top of
	// the list, and the rest read as a list of languages, not of codes.
	std::sort(_available.begin(), _available.end(), [](const auto& a_lhs, const auto& a_rhs) {
		if (a_lhs.first == a_rhs.first) {
			return false;
		}
		if (a_lhs.first == "en") {
			return true;
		}
		if (a_rhs.first == "en") {
			return false;
		}
		if (a_lhs.second != a_rhs.second) {
			return a_lhs.second < a_rhs.second;
		}
		return a_lhs.first < a_rhs.first;
	});

	if (_available.empty()) {
		_available.emplace_back("en", "English");
	}
}

bool I18n::LoadLocaleInto(
	std::string_view a_locale,
	std::unordered_map<std::string, std::string>& a_target) const
{
	const auto file = _directory / std::format("{}.json", a_locale);

	glz::generic root{};
	if (glz::read_file_json(root, file.string(), std::string{}) || !root.is_object()) {
		return false;
	}

	std::size_t count = 0;
	for (const auto& [key, value] : root.get_object()) {
		// The metadata block is not a translation, and its nested object is not
		// a string anyway.
		if (key == "_meta") {
			continue;
		}
		if (const auto* const text = value.template get_if<std::string>()) {
			a_target.insert_or_assign(key, *text);
			++count;
		}
	}

	REX::INFO("loaded {} key(s) from {}", count, file.generic_string());
	return true;
}
