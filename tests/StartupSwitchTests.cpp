#include "Bridge/StartupSwitch.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

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

	constexpr std::string_view noNeural = "; the settings file has no NeuralRendering block";
	const auto off = ParseStartupSwitch(R"({"FrameGeneration":{"enabled":false}})");
	Check(!off.on && off.reason == std::string{ "FrameGeneration/enabled is false" } + std::string{ noNeural },
		"false keeps it off and says so; a file from before F5.5c");

	Check(ParseStartupSwitch(R"({"DLSS":{"enabled":true}})").reason ==
			  std::string{ "the settings file has no FrameGeneration block" } + std::string{ noNeural },
		"a file written before F5.5b has no block");
	Check(ParseStartupSwitch(R"({"FrameGeneration":{"reflex":"on"}})").reason ==
			  std::string{ "FrameGeneration/enabled is missing" } + std::string{ noNeural },
		"a block without the switch");
	Check(ParseStartupSwitch(R"({"FrameGeneration":true})").reason ==
			  std::string{ "FrameGeneration/enabled is missing" } + std::string{ noNeural },
		"a block that is not an object");
	Check(ParseStartupSwitch(R"({"FrameGeneration":{"enabled":1.0}})").reason ==
			  std::string{ "FrameGeneration/enabled is not true or false" } + std::string{ noNeural },
		"a number is not a switch");

	Check(ParseStartupSwitch(R"({"NeuralRendering":{"enabled":true}})").on, "NR alone switches the bridge on");
	Check(ParseStartupSwitch(R"({"FrameGeneration":{"enabled":false},"NeuralRendering":{"enabled":true}})").on, "either is enough");
	Check(ParseStartupSwitch(R"({"FrameGeneration":{"enabled":true},"NeuralRendering":{"enabled":1.0}})").on,
		"a broken NR value does not stop FG");
	const auto bothOff = ParseStartupSwitch(R"({"FrameGeneration":{"enabled":false},"NeuralRendering":{"enabled":false}})");
	Check(!bothOff.on && bothOff.reason == "FrameGeneration/enabled is false; NeuralRendering/enabled is false", "both off, both named");
	Check(ParseStartupSwitch(R"({"FrameGeneration":{"enabled":false},"NeuralRendering":{"enabled":"yes"}})").reason ==
			  "FrameGeneration/enabled is false; NeuralRendering/enabled is not true or false",
		"NR value not a boolean");
	Check(ParseStartupSwitch(R"({"NeuralRendering":{"enabled":true}})").reason.empty(), "on means no reason");
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
