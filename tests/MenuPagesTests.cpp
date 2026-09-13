#include "Menu/PageList.h"

#include <array>
#include <cstdio>
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
	using Menu::PageKind;
	using Menu::PageList;

	// Fresh: the two fixed pages, General open.
	{
		PageList pages;
		Check(pages.Pages().size() == 2, "a fresh list has two pages");
		Check(pages.Pages()[0].kind == PageKind::kGeneral && pages.Pages()[0].name == "General",
			"the first is General");
		Check(pages.Pages()[1].kind == PageKind::kPerformance && pages.Pages()[1].name == "Performance",
			"the second is Performance");
		Check(pages.Selected().kind == PageKind::kGeneral, "General is open");
	}

	// Features follow the fixed pages, in the order given.
	{
		PageList pages;
		constexpr std::array<std::string_view, 2> names{ "A", "B" };
		pages.SetFeatures(names);
		Check(pages.Pages().size() == 4, "two features make four pages");
		Check(pages.Pages()[2].kind == PageKind::kFeature && pages.Pages()[2].name == "A",
			"the third is feature A");
		Check(pages.Pages()[3].name == "B", "the fourth is feature B");
		Check(pages.Selected().name == "General", "adding features leaves General open");
	}

	// Selecting by name, and refusing a name that is no page.
	{
		PageList pages;
		constexpr std::array<std::string_view, 2> names{ "A", "B" };
		pages.SetFeatures(names);
		Check(pages.Select("B"), "selecting B succeeds");
		Check(pages.Selected().name == "B", "and B is open");
		Check(!pages.Select("C"), "selecting an unknown name fails");
		Check(pages.Selected().name == "B", "and leaves B open");
		Check(pages.Select("Performance"), "the fixed pages select by their key");
		Check(pages.Selected().kind == PageKind::kPerformance, "and Performance is open");
		Check(pages.Select("General"), "General selects by its key");
		Check(pages.Selected().kind == PageKind::kGeneral, "and General is open");
	}

	// The selection survives a feature list that still holds it, and falls
	// back to General when it does not.
	{
		PageList pages;
		constexpr std::array<std::string_view, 2> two{ "A", "B" };
		constexpr std::array<std::string_view, 1> one{ "A" };
		pages.SetFeatures(two);
		pages.Select("A");
		pages.SetFeatures(two);
		Check(pages.Selected().name == "A", "a selection that still exists survives SetFeatures");
		pages.Select("B");
		pages.SetFeatures(one);
		Check(pages.Selected().kind == PageKind::kGeneral, "a selection that vanished falls back to General");
		Check(pages.Pages().size() == 3, "and the list shrank to three");
	}

	// Which page a settings block belongs to.
	{
		PageList pages;
		constexpr std::array<std::string_view, 1> names{ "A" };
		pages.SetFeatures(names);
		Check(pages.PageOf("Performance").kind == PageKind::kPerformance, "the Performance block is the performance page");
		Check(pages.PageOf("A").kind == PageKind::kFeature && pages.PageOf("A").name == "A", "a feature's block is its page");
		Check(pages.PageOf("Menu").kind == PageKind::kGeneral, "the Menu block is General");
		Check(pages.PageOf("Unknown").kind == PageKind::kGeneral, "an unknown block is General");
		Check(pages.PageOf("General").kind == PageKind::kGeneral, "a block called General is General, by the fallback");
	}

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
