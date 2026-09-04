#include "Settings/Settings.h"

#include <cstdio>
#include <string>
#include <vector>

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
	Settings::DeclareFeature("Alpha", true).Label("feature.alpha.name", "Alpha");
	Settings::DeclareBool("Alpha/verbose", false)
		.Label("setting.alpha.verbose", "Verbose")
		.Help("setting.alpha.verbose.help", "Writes more to the log.");
	Settings::DeclareSlider("Menu/fontSize", 18.0, 12.0, 32.0)
		.Label("setting.menu.font_size", "Font size");
	Settings::DeclareKey("Menu/toggleKey", 0x23).Label("setting.menu.toggle_key", "Toggle key");
	Settings::DeclareChoice("Menu/language", "en", { "en", "de" })
		.Label("setting.menu.language", "Language");

	// Two segments are the contract; anything else is refused rather than
	// silently addressing a top level key with a slash in its name.
	Settings::DeclareBool("noSlash", true);
	Settings::DeclareBool("too/many/segments", true);
	Settings::DeclareBool("/leadingEmpty", true);
	Settings::DeclareBool("trailingEmpty/", true);

	// Blocks come back in declaration order, not sorted.
	{
		std::vector<std::string> blocks;
		Settings::ForEachBlock(
			[&blocks](std::string_view a_block) { blocks.emplace_back(a_block); });

		Check(blocks.size() == 2, "two blocks were declared");
		Check(blocks.size() == 2 && blocks[0] == "Alpha", "Alpha came first");
		Check(blocks.size() == 2 && blocks[1] == "Menu", "Menu came second");
	}

	// A malformed path never becomes a block.
	{
		bool sawJunk = false;
		Settings::ForEachBlock([&sawJunk](std::string_view a_block) {
			if (a_block == "noSlash" || a_block == "too" || a_block.empty() ||
				a_block == "trailingEmpty") {
				sawJunk = true;
			}
		});
		Check(!sawJunk, "no malformed path became a block");
	}

	// The feature switch is marked, and it is the only one in its block.
	{
		int entries = 0;
		int switches = 0;
		Settings::ForEachEntry("Alpha", [&](const Settings::Entry& a_entry) {
			++entries;
			if (a_entry.isFeatureSwitch) {
				++switches;
				Check(a_entry.key == "enabled", "the switch is the enabled key");
				Check(a_entry.defaultBool, "and carries its declared default");
			}
		});
		Check(entries == 2, "Alpha has two entries");
		Check(switches == 1, "exactly one of them is the feature switch");
	}

	// Kinds and metadata survive the declaration.
	{
		Settings::ForEachEntry("Menu", [](const Settings::Entry& a_entry) {
			if (a_entry.key == "fontSize") {
				Check(a_entry.kind == Settings::Kind::kSlider, "fontSize is a slider");
				Check(a_entry.min == 12.0 && a_entry.max == 32.0, "with its declared bounds");
				Check(a_entry.defaultNumber == 18.0, "and its declared default");
				Check(a_entry.labelText == "Font size", "and its English label");
				Check(a_entry.labelKey == "setting.menu.font_size", "and its translation key");
			} else if (a_entry.key == "toggleKey") {
				Check(a_entry.kind == Settings::Kind::kKey, "toggleKey is a key");
				Check(a_entry.defaultNumber == 35.0, "stored as a double");
			} else if (a_entry.key == "language") {
				Check(a_entry.kind == Settings::Kind::kChoice, "language is a choice");
				Check(a_entry.choices.size() == 2, "with two choices");
				Check(a_entry.defaultChoice == "en", "and its declared default");
			}
		});
	}

	// Entries within a block come back in declaration order too.
	{
		std::vector<std::string> keys;
		Settings::ForEachEntry(
			"Menu",
			[&keys](const Settings::Entry& a_entry) { keys.emplace_back(a_entry.key); });

		Check(keys.size() == 3, "Menu has three entries");
		Check(keys.size() == 3 && keys[0] == "fontSize", "fontSize was declared first");
		Check(keys.size() == 3 && keys[1] == "toggleKey", "toggleKey second");
		Check(keys.size() == 3 && keys[2] == "language", "language third");
	}

	// Help is optional and empty when it was never given.
	{
		Settings::ForEachEntry("Alpha", [](const Settings::Entry& a_entry) {
			if (a_entry.key == "verbose") {
				Check(a_entry.helpText == "Writes more to the log.", "help is carried through");
			} else if (a_entry.key == "enabled") {
				Check(a_entry.helpText.empty(), "and is empty when never given");
			}
		});
	}

	// The first declaration of a path wins; a second is ignored rather than
	// overwriting a label somebody else already set.
	{
		Settings::DeclareBool("Alpha/verbose", true).Label("other.key", "Other");

		Settings::ForEachEntry("Alpha", [](const Settings::Entry& a_entry) {
			if (a_entry.key == "verbose") {
				Check(!a_entry.defaultBool, "a second declaration does not change the default");
				Check(a_entry.labelText == "Verbose", "nor the label");
			}
		});
	}

	std::printf("%d failure(s)\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
