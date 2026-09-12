#include "Render/DrawObserver.h"

#include <cstdio>

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

	Render::CurrentTechnique Current(std::size_t a_class, std::uint32_t a_technique, bool a_valid = true)
	{
		Render::CurrentTechnique current;
		current.classIndex = a_class;
		current.technique = a_technique;
		current.valid = a_valid;
		return current;
	}
}

int main()
{
	{
		// The filter the cloud capture uses: one class, one technique.
		const Render::TechniqueFilter clouds{ 7, 0x0005 };
		Check(clouds.Matches(Current(7, 0x0005)), "the exact pair matches");
		Check(!clouds.Matches(Current(7, 0x0004)), "another technique of the class does not");
		Check(!clouds.Matches(Current(6, 0x0005)), "the same technique id of another class does not");
		Check(!clouds.Matches(Current(7, 0x0005, false)), "nothing matches before the first SetupTechnique");
	}

	{
		// The filter the probe uses: every technique of one class.
		const Render::TechniqueFilter sky{ 7, Render::kAnyTechnique };
		Check(sky.Matches(Current(7, 0x0001)), "any technique of the class matches");
		Check(sky.Matches(Current(7, 0x0005)), "and any other");
		Check(!sky.Matches(Current(8, 0x0005)), "but not another class");
		Check(!sky.Matches(Current(7, 0x0001, false)), "and not an invalid state");
	}

	std::printf("\n%s\n", g_failures == 0 ? "all checks passed" : "checks failed");
	return g_failures == 0 ? 0 : 1;
}
