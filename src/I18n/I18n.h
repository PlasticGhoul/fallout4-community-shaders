#pragma once

#include <deque>
#include <filesystem>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

/// Flat-JSON translations, one file per locale, loaded from a directory.
///
/// Every string handed out is owned here and stays valid until the next
/// SetLocale or Init, which is what makes it safe to pass straight to ImGui.
///
/// Deliberately independent of Settings: the language is a setting, but the
/// engine that answers a lookup must not need one to be exercised.
class I18n
{
public:
	static I18n* GetSingleton()
	{
		static I18n singleton;
		return std::addressof(singleton);
	}

	/// Points at a_directory, discovers the locales in it and loads English as
	/// the fallback. Repeatable: everything loaded before is dropped first.
	///
	/// A directory that is not there leaves English with an empty table, which
	/// still works - every lookup then yields its inline default. The overlay
	/// is readable with no translation files at all.
	void Init(const std::filesystem::path& a_directory);

	/// Lookup order: the current locale, then English, then a_default, then the
	/// key itself. a_default may be null.
	[[nodiscard]] const char* Get(std::string_view a_key, const char* a_default) const;

	[[nodiscard]] std::string CurrentLocale() const;

	/// Ignored, with a log line, for a locale that was never discovered or that
	/// will not load. The current locale then stands.
	void SetLocale(std::string_view a_locale);

	/// Code and display name, English first and the rest by display name. A
	/// file that does not parse is not offered: a locale that cannot load is a
	/// trap, not a choice.
	[[nodiscard]] std::vector<std::pair<std::string, std::string>> AvailableLocales() const;

private:
	I18n() = default;

	void DiscoverLocales();
	bool LoadLocaleInto(
		std::string_view a_locale,
		std::unordered_map<std::string, std::string>& a_target) const;

	std::filesystem::path _directory;
	std::string _current{ "en" };

	std::unordered_map<std::string, std::string> _strings;   // The current locale.
	std::unordered_map<std::string, std::string> _fallback;  // Always en.json.

	/// Inline defaults handed out as const char*. A deque because it never
	/// invalidates a pointer on push_back, which a vector would - and the
	/// pointer is what the caller keeps.
	mutable std::deque<std::string> _inlineStorage;
	mutable std::unordered_map<std::string, const char*> _inlineIndex;

	mutable std::shared_mutex _mutex;

	std::vector<std::pair<std::string, std::string>> _available;
};

/// The call every user-visible string goes through. The second argument is
/// both the fallback and the text tools/extract-i18n.py reads to generate
/// en.json, which is why it must always be a string literal.
[[nodiscard]] inline const char* T(std::string_view a_key, const char* a_default)
{
	return I18n::GetSingleton()->Get(a_key, a_default);
}
