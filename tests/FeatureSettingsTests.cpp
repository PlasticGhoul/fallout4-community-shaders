#include "Feature/FeatureSettings.h"

#include <chrono>
#include <cstdio>
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

	// Whitespace-insensitive, because the exact indentation of the generated
	// file is not what this test is about.
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
	const auto root = std::filesystem::temp_directory_path() / "fo4cs-settings-tests";
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);

	// Declared before Init, because REX registers a setting with its store at
	// construction and Init is what walks that registration.
	Features::Settings::DeclareFeature("alpha", false);
	Features::Settings::DeclareFeature("beta", true);
	Features::Settings::DeclareFeature("gamma", true);

	const auto good = root / "good.json";
	WriteFile(good, R"({"alpha":{"enabled":true},"beta":{"enabled":false}})");
	Features::Settings::Init(good);

	Check(Features::Settings::IsEnabled("alpha"), "the file turns a default-off feature on");
	Check(!Features::Settings::IsEnabled("beta"), "the file turns a default-on feature off");
	Check(
		Features::Settings::IsEnabled("gamma"),
		"a feature the file never mentions keeps its default");
	Check(!Features::Settings::IsEnabled("delta"), "an undeclared name reads as off");

	// A broken file must not wipe what is already loaded.
	const auto broken = root / "broken.json";
	WriteFile(broken, "{ this is not json");
	Features::Settings::Init(broken);

	Check(Features::Settings::IsEnabled("alpha"), "a broken file leaves alpha as it was");
	Check(!Features::Settings::IsEnabled("beta"), "a broken file leaves beta as it was");

	// A file that is not there is written, because REX can only ever overwrite
	// keys that already exist and would otherwise never produce one.
	const auto absent = root / "absent.json";
	Features::Settings::Init(absent);

	Check(std::filesystem::exists(absent), "a missing file is written");

	const auto written = ReadFile(absent);
	Check(
		Contains(written, R"("alpha":{"enabled":false})"),
		"and holds the declared default, not the value loaded a moment ago");
	Check(
		Contains(written, R"("beta":{"enabled":true})") &&
			Contains(written, R"("gamma":{"enabled":true})"),
		"and holds every declared feature");
	Check(!Features::Settings::IsEnabled("alpha"), "the written file is what gets loaded");

	// A setting that is not a feature switch, and not a bool.
	Features::Settings::DeclareUInt32("Menu/toggleKey", 35);
	Features::Settings::DeclareBool("Menu/verbose", true);

	// Two segments are the contract; anything else is refused rather than
	// silently addressing a top level key.
	Features::Settings::DeclareUInt32("noSlash", 7);
	Features::Settings::DeclareUInt32("too/many/segments", 8);

	const auto mixed = root / "mixed.json";
	Features::Settings::Init(mixed);

	Check(std::filesystem::exists(mixed), "a mixed default file is written");

	const auto mixedText = ReadFile(mixed);
	Check(
		Contains(mixedText, R"("Menu":{"toggleKey":35,"verbose":true})"),
		"and groups both Menu settings into one block");
	Check(
		Contains(mixedText, R"("alpha":{"enabled":false})"),
		"and still writes the feature blocks");
	Check(Features::Settings::GetUInt32("Menu/toggleKey") == 35, "a uint32 reads back");
	Check(Features::Settings::GetBool("Menu/verbose"), "and so does a bool");
	Check(Features::Settings::GetUInt32("noSlash") == 0, "a path without a slash was refused");
	Check(
		Features::Settings::GetUInt32("too/many/segments") == 0,
		"and so was one with too many");

	WriteFile(mixed, R"({"Menu":{"toggleKey":112,"verbose":false}})");
	Features::Settings::Init(mixed);

	Check(Features::Settings::GetUInt32("Menu/toggleKey") == 112, "the file overrides the uint32");
	Check(!Features::Settings::GetBool("Menu/verbose"), "and the bool");

	// A whole number setting is stored as a double, because glaze only ever
	// holds a JSON number as one. What a hand edited file can put there is
	// therefore not confined to the range of a uint32.
	WriteFile(mixed, R"({"Menu":{"toggleKey":-5,"verbose":false}})");
	Features::Settings::Init(mixed);
	Check(Features::Settings::GetUInt32("Menu/toggleKey") == 0, "a negative key clamps to zero");

	WriteFile(mixed, R"({"Menu":{"toggleKey":1e30,"verbose":false}})");
	Features::Settings::Init(mixed);
	Check(
		Features::Settings::GetUInt32("Menu/toggleKey") == 4294967295u,
		"and one past the top clamps to the maximum");

	WriteFile(mixed, R"({"Menu":{"toggleKey":112.7,"verbose":false}})");
	Features::Settings::Init(mixed);
	Check(Features::Settings::GetUInt32("Menu/toggleKey") == 112, "a fractional key truncates");

	// The watch reports a change exactly once.
	Features::Settings::Init(good);
	Check(!Features::Settings::ReloadIfChanged(), "no change right after Init");

	const auto stamp = std::filesystem::last_write_time(good);
	std::filesystem::last_write_time(good, stamp + std::chrono::seconds{ 5 });

	Check(Features::Settings::ReloadIfChanged(), "a touched file is reported");
	Check(!Features::Settings::ReloadIfChanged(), "and not reported twice");

	std::filesystem::remove_all(root);
	std::printf("%d failure(s)\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
