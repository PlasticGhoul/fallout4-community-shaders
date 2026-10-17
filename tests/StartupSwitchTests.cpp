#include "Bridge/StartupSwitch.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

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
}

int main()
{
	using Bridge::ParseStartupSwitch;

	Check(ParseStartupSwitch(R"({"FrameGeneration":{"enabled":true}})").on, "true switches the bridge on");

	const auto off = ParseStartupSwitch(R"({"FrameGeneration":{"enabled":false}})");
	Check(!off.on && off.reason == "FrameGeneration/enabled is false", "false keeps it off and says so");

	Check(ParseStartupSwitch(R"({"DLSS":{"enabled":true}})").reason == "the settings file has no FrameGeneration block",
		"a file written before F5.5b has no block");
	Check(ParseStartupSwitch(R"({"FrameGeneration":{"reflex":"on"}})").reason == "FrameGeneration/enabled is missing",
		"a block without the switch");
	Check(ParseStartupSwitch(R"({"FrameGeneration":true})").reason == "FrameGeneration/enabled is missing",
		"a block that is not an object");
	Check(ParseStartupSwitch(R"({"FrameGeneration":{"enabled":1.0}})").reason == "FrameGeneration/enabled is not true or false",
		"a number is not a switch");
	Check(ParseStartupSwitch("{ not json").reason == "the settings file is not valid JSON", "broken JSON");
	Check(ParseStartupSwitch("[]").reason == "the settings file is not valid JSON", "an array at the root");
	Check(ParseStartupSwitch(R"({"Menu":{"key":45.0},"FrameGeneration":{"reflex":"boost","enabled":true}})").on,
		"other blocks and keys around it do not matter");

	const auto directory = std::filesystem::temp_directory_path();
	const auto missing = Bridge::ReadStartupSwitch(directory / "fo4cs-no-such-settings.json");
	Check(!missing.on && missing.reason == "no settings file yet", "no file means off");
	Check(Bridge::ReadStartupSwitch({}).reason == "the settings file could not be located", "no path means off");

	const auto file = directory / "fo4cs-startup-switch.json";
	{
		std::ofstream out(file, std::ios::binary | std::ios::trunc);
		out << "{\n    \"FrameGeneration\": {\n        \"enabled\": true\n    }\n}\n";
	}
	Check(Bridge::ReadStartupSwitch(file).on, "the file route reads what the string route reads");
	std::filesystem::remove(file);

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
