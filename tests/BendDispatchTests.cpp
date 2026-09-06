#include "Features/ScreenSpaceShadows/BendDispatch.h"

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

	constexpr int kViewport[2] = { 2560, 1440 };

	bool Near(float a_value, float a_expected)
	{
		const auto difference = a_value > a_expected ? a_value - a_expected : a_expected - a_value;
		return difference < 0.5f;
	}

	// Every dispatch has to ask for work in all three dimensions. One that does
	// not is a sweep the GPU runs for nothing, and the mask keeps a hole where
	// it should have written.
	bool EveryDispatchDoesWork(const Features::Bend::DispatchPlan& a_plan)
	{
		for (int i = 0; i < a_plan.count; ++i) {
			const auto& dispatch = a_plan.dispatches[i];
			if (dispatch.waveCount[0] <= 0 || dispatch.waveCount[1] <= 0 ||
				dispatch.waveCount[2] <= 0) {
				return false;
			}
		}

		return true;
	}
}

int main()
{
	{
		// A directional light straight overhead: w is zero, the light sits at
		// infinity, and the plan still has to be built.
		const float light[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
		const auto plan = Features::Bend::BuildPlan(light, kViewport);

		Check(plan.count >= 1, "an overhead sun produces at least one dispatch");
		Check(plan.count <= 8, "and never more than the eight the list holds");
		Check(EveryDispatchDoesWork(plan), "every dispatch asks for work");
	}

	{
		// Light behind the camera. The fourth coordinate carries the sign, and
		// getting it wrong is the classic way to march the rays backwards.
		const float behind[4] = { 0.0f, 0.0f, -1.0f, 0.0f };
		const auto plan = Features::Bend::BuildPlan(behind, kViewport);

		Check(plan.count >= 1, "a sun behind the camera still produces a plan");
		Check(EveryDispatchDoesWork(plan), "and every dispatch of it asks for work");
	}

	{
		// On screen: the documented case with the most dispatches.
		const float onScreen[4] = { 0.1f, 0.2f, 0.5f, 1.0f };
		const auto plan = Features::Bend::BuildPlan(onScreen, kViewport);

		Check(plan.count >= 1, "an on-screen light produces a plan");
		Check(plan.count <= 8, "within the list's capacity");
		Check(EveryDispatchDoesWork(plan), "and every dispatch of it asks for work");

		Check(
			plan.lightCoordinate[0] > 0.0f && plan.lightCoordinate[0] < 2560.0f,
			"its x coordinate lands inside the viewport");
		Check(
			plan.lightCoordinate[1] > 0.0f && plan.lightCoordinate[1] < 1440.0f,
			"and so does its y coordinate");
	}

	{
		// The light coordinate is in pixels, and x has to follow the width
		// while y follows the height - each on its own. Shrinking both at once
		// cannot tell the two apart: a plan that swapped x and y would pass
		// that just as convincingly. Only the width is halved here.
		const float light[4] = { 0.5f, 0.25f, 0.5f, 1.0f };
		const int narrow[2] = { 1280, 1440 };

		const auto full = Features::Bend::BuildPlan(light, kViewport);
		const auto half = Features::Bend::BuildPlan(light, narrow);

		Check(
			Near(full.lightCoordinate[0], half.lightCoordinate[0] * 2.0f),
			"halving the width halves the light's x coordinate");
		Check(
			Near(full.lightCoordinate[1], half.lightCoordinate[1]),
			"and leaves its y coordinate where it was");
	}

	{
		// A degenerate viewport must not produce a dispatch that would run off
		// the end of a zero-sized texture.
		const float light[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
		const int empty[2] = { 0, 0 };
		const auto plan = Features::Bend::BuildPlan(light, empty);

		Check(plan.count >= 0 && plan.count <= 8, "a zero viewport produces a bounded plan");
	}

	{
		// Same input, same output. The plan is arithmetic; if it ever came to
		// depend on hidden state, this would catch it.
		const float light[4] = { 0.1f, 0.2f, 0.5f, 1.0f };
		const auto first = Features::Bend::BuildPlan(light, kViewport);
		const auto second = Features::Bend::BuildPlan(light, kViewport);

		bool identical = first.count == second.count;
		for (int i = 0; identical && i < first.count; ++i) {
			identical = first.dispatches[i].waveCount[0] == second.dispatches[i].waveCount[0] &&
			            first.dispatches[i].waveCount[1] == second.dispatches[i].waveCount[1] &&
			            first.dispatches[i].waveOffset[0] == second.dispatches[i].waveOffset[0] &&
			            first.dispatches[i].waveOffset[1] == second.dispatches[i].waveOffset[1];
		}

		Check(identical, "the same input builds the same plan");
	}

	std::printf("\n%s\n", g_failures == 0 ? "all checks passed" : "checks failed");
	return g_failures == 0 ? 0 : 1;
}
