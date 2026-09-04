#include "Settings/Settings.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
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

	void WriteFile(const std::filesystem::path& a_path, std::string_view a_content)
	{
		std::filesystem::create_directories(a_path.parent_path());
		std::ofstream stream{ a_path, std::ios::binary };
		stream.write(a_content.data(), static_cast<std::streamsize>(a_content.size()));
	}

	std::string ReadFile(const std::filesystem::path& a_path)
	{
		std::ifstream stream{ a_path, std::ios::binary };
		std::ostringstream text;
		text << stream.rdbuf();
		return text.str();
	}

	// Whitespace-insensitive: the exact indentation is not what these tests are
	// about.
	bool Contains(const std::string& a_haystack, std::string_view a_needle)
	{
		std::string stripped;
		stripped.reserve(a_haystack.size());
		for (const char c : a_haystack) {
			if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
				stripped.push_back(c);
			}
		}
		return stripped.find(a_needle) != std::string::npos;
	}
}

int main()
{
	const auto root = std::filesystem::temp_directory_path() / "fo4cs-store-tests";
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);

	// Declared before Init: REX registers a setting with its store at
	// construction, and Init is what walks that registration.
	Settings::DeclareFeature("Alpha", false);
	Settings::DeclareFeature("Beta", true);
	Settings::DeclareSlider("Menu/fontSize", 18.0, 12.0, 32.0);
	Settings::DeclareKey("Menu/toggleKey", 0x23);
	Settings::DeclareChoice("Menu/language", "en", { "en", "de" });

	// A file that is not there is written from the declared defaults, because
	// REX saves through glz::set, which only overwrites keys that exist.
	const auto fresh = root / "fresh.json";
	Settings::Init(fresh);

	Check(std::filesystem::exists(fresh), "a missing file is written");

	{
		const auto text = ReadFile(fresh);
		Check(Contains(text, R"("Alpha":{"enabled":false})"), "with a feature block");
		Check(Contains(text, R"("toggleKey":35)"), "with the key code as a number");
		Check(Contains(text, R"("language":"en")"), "and the choice as a string");
	}

	Check(!Settings::IsEnabled("Alpha"), "the declared default is what got loaded");
	Check(Settings::IsEnabled("Beta"), "for both features");
	Check(Settings::GetUInt32("Menu/toggleKey") == 35, "and the key reads back");
	Check(Settings::GetDouble("Menu/fontSize") == 18.0, "and the slider");
	Check(Settings::GetString("Menu/language") == "en", "and the choice");

	// An existing file wins over the defaults.
	const auto existing = root / "existing.json";
	WriteFile(existing, R"({
		"Alpha": { "enabled": true },
		"Beta": { "enabled": false },
		"Menu": { "fontSize": 24, "toggleKey": 112, "language": "de" }
	})");
	Settings::Init(existing);

	Check(Settings::IsEnabled("Alpha"), "the file turns a default-off feature on");
	Check(!Settings::IsEnabled("Beta"), "and a default-on feature off");
	Check(Settings::GetUInt32("Menu/toggleKey") == 112, "the file overrides the key");
	Check(Settings::GetDouble("Menu/fontSize") == 24.0, "and the slider");
	Check(Settings::GetString("Menu/language") == "de", "and the choice");

	// The gap E1 recorded: an incomplete file is extended, and nothing that was
	// already in it is lost. Note this runs straight after loading a file that
	// set fontSize to 24 - the added key must carry the declared default, not
	// what the previous file happened to hold.
	const auto partial = root / "partial.json";
	WriteFile(partial, R"({
		"Alpha": { "enabled": true },
		"Menu": { "toggleKey": 112, "somebodyElsesKey": 7 }
	})");
	Settings::Init(partial);

	{
		const auto text = ReadFile(partial);
		Check(Contains(text, R"("enabled":true)"), "an existing value survives the extension");
		Check(Contains(text, R"("toggleKey":112)"), "and so does an existing number");
		Check(Contains(text, R"("somebodyElsesKey":7)"), "and an unknown key is left alone");
		Check(Contains(text, R"("fontSize":18)"), "a missing key appears with its default");
		Check(Contains(text, R"("language":"en")"), "including a missing choice");
		Check(Contains(text, R"("Beta":{"enabled":true})"), "and a whole missing block");
	}

	Check(Settings::GetUInt32("Menu/toggleKey") == 112, "the loaded value is the file's");
	Check(Settings::GetDouble("Menu/fontSize") == 18.0, "and the added one is the default");

	// A complete file is not rewritten. Its timestamp is the evidence: churning
	// it on every launch would risk losing it to a crash mid-write.
	{
		const auto complete = root / "complete.json";
		Settings::Init(complete);

		const auto first = std::filesystem::last_write_time(complete);
		std::filesystem::last_write_time(complete, first - std::chrono::seconds{ 30 });
		const auto marked = std::filesystem::last_write_time(complete);

		Settings::Init(complete);
		Check(
			std::filesystem::last_write_time(complete) == marked,
			"a complete file is left untouched on the next start");
	}

	// A file that is not JSON is left alone rather than overwritten - a typo
	// must not cost the player their settings - and every value falls back to
	// its declared default. Init(X) means "reflect X, and the declared defaults
	// wherever X says nothing", which a broken file says about everything.
	Settings::Init(existing);
	const auto broken = root / "broken.json";
	WriteFile(broken, "{ this is not json");
	Settings::Init(broken);

	Check(ReadFile(broken) == "{ this is not json", "a broken file is not overwritten");
	Check(!Settings::IsEnabled("Alpha"), "and every setting falls back to its default");
	Check(Settings::GetUInt32("Menu/toggleKey") == 35, "the key too");

	// Whole numbers are stored as doubles, so a hand edited file is not
	// confined to the range of a uint32.
	const auto odd = root / "odd.json";
	WriteFile(odd, R"({"Menu":{"toggleKey":-5}})");
	Settings::Init(odd);
	Check(Settings::GetUInt32("Menu/toggleKey") == 0, "a negative key clamps to zero");

	WriteFile(odd, R"({"Menu":{"toggleKey":1e30}})");
	Settings::Init(odd);
	Check(
		Settings::GetUInt32("Menu/toggleKey") == 4294967295u,
		"and one past the top clamps to the maximum");

	Check(!Settings::IsEnabled("Undeclared"), "an undeclared name reads as off");
	Check(Settings::GetDouble("no/such/path") == 0.0, "and an undeclared path as zero");

	std::filesystem::remove_all(root);
	std::printf("%d failure(s)\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
