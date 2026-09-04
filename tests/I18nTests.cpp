#include "I18n/I18n.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
	int g_failures = 0;

	void Check(bool a_passed, const char* a_what)
	{
		std::printf("%s  %s\n", a_passed ? "ok  " : "FAIL", a_what);
		if (!a_passed) {
			++g_failures;
		}
	}

	bool Same(const char* a_lhs, const char* a_rhs)
	{
		return a_lhs != nullptr && a_rhs != nullptr && std::string_view{ a_lhs } == a_rhs;
	}

	void WriteFile(const std::filesystem::path& a_path, std::string_view a_content)
	{
		std::filesystem::create_directories(a_path.parent_path());
		std::ofstream stream{ a_path, std::ios::binary };
		stream.write(a_content.data(), static_cast<std::streamsize>(a_content.size()));
	}
}

int main()
{
	const auto root = std::filesystem::temp_directory_path() / "fo4cs-i18n-tests";
	std::filesystem::remove_all(root);

	// No directory at all: English, and nothing falls over. The overlay has to
	// be readable before a single translation file exists.
	{
		I18n::GetSingleton()->Init(root / "missing");
		Check(I18n::GetSingleton()->CurrentLocale() == "en", "a missing directory means English");
		Check(Same(T("a.key", "Fallback"), "Fallback"), "and the inline default is what shows");

		const auto locales = I18n::GetSingleton()->AvailableLocales();
		Check(locales.size() == 1 && locales[0].first == "en", "English is offered regardless");
	}

	const auto dir = root / "Translations";
	WriteFile(dir / "en.json", R"({
		"_meta": { "language": "English", "locale": "en" },
		"menu.close": "Close",
		"menu.only_in_english": "English only"
	})");
	WriteFile(dir / "de.json", R"({
		"_meta": { "language": "Deutsch", "locale": "de" },
		"menu.close": "Schliessen"
	})");
	WriteFile(dir / "xy.json", "{ not json at all");

	I18n::GetSingleton()->Init(dir);

	// Discovery, and the broken file does not take the others with it.
	{
		const auto locales = I18n::GetSingleton()->AvailableLocales();
		bool hasEn = false;
		bool hasDe = false;
		bool hasBroken = false;
		for (const auto& [code, name] : locales) {
			if (code == "en") {
				hasEn = true;
				Check(name == "English", "the display name comes from _meta");
			}
			if (code == "de") {
				hasDe = true;
			}
			if (code == "xy") {
				hasBroken = true;
			}
		}
		Check(hasEn && hasDe, "both good locales are discovered");
		Check(!hasBroken, "and a malformed file is not offered");
		Check(locales.size() >= 2 && locales[0].first == "en", "English sorts first");
	}

	Check(Same(T("menu.close", "Close"), "Close"), "English reads from en.json");

	I18n::GetSingleton()->SetLocale("de");
	Check(I18n::GetSingleton()->CurrentLocale() == "de", "the locale switches");
	Check(Same(T("menu.close", "Close"), "Schliessen"), "and the text switches with it");
	Check(
		Same(T("menu.only_in_english", "Ignored"), "English only"),
		"a key missing in German falls back to English, not to the inline default");
	Check(
		Same(T("menu.nowhere", "Inline"), "Inline"),
		"a key missing everywhere falls back to the inline default");
	Check(Same(T("menu.nothing", nullptr), "menu.nothing"), "and to the key as a last resort");

	// Switching to a locale with no file leaves the current one standing.
	I18n::GetSingleton()->SetLocale("zz");
	Check(I18n::GetSingleton()->CurrentLocale() == "de", "a locale without a file is refused");
	Check(Same(T("menu.close", "Close"), "Schliessen"), "and the texts stay where they were");

	// So does a code that could never be a filename.
	I18n::GetSingleton()->SetLocale("../../etc");
	Check(I18n::GetSingleton()->CurrentLocale() == "de", "so is a code that is not a locale");

	// The pointer from a previous Get stays valid, which is what lets the panel
	// hand it straight to ImGui.
	{
		const char* const first = T("menu.close", "Close");
		const char* const second = T("menu.close", "Close");
		Check(first == second, "the same key returns the same pointer");
	}

	// Back to English, and the German text is gone with it.
	I18n::GetSingleton()->SetLocale("en");
	Check(Same(T("menu.close", "Close"), "Close"), "switching back restores English");

	std::filesystem::remove_all(root);
	std::printf("%d failure(s)\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
