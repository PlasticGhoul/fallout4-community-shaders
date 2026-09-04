#include "Menu/MenuGate.h"

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

	struct Counters
	{
		int suppressed = 0;
		int restored = 0;
		bool suppressSucceeds = true;
	};

	// Returned as a prvalue on purpose: Gate holds a std::atomic and is
	// therefore neither copyable nor movable. Guaranteed elision is what makes
	// this compile.
	Menu::Gate MakeGate(Counters& a_counters)
	{
		return Menu::Gate{
			[&a_counters] {
				++a_counters.suppressed;
				return a_counters.suppressSucceeds;
			},
			[&a_counters] { ++a_counters.restored; }
		};
	}
}

int main()
{
	// A press opens, and the game's input is taken away once.
	{
		Counters counters;
		auto gate = MakeGate(counters);
		gate.SetToggleKey(35);

		Check(!gate.IsOpen(), "a fresh gate is closed");

		gate.RequestToggle();
		Check(gate.Tick(), "a request opens it on the next tick");
		Check(counters.suppressed == 1, "and takes the game's input away once");
		Check(counters.restored == 0, "and gives nothing back yet");

		gate.RequestToggle();
		Check(!gate.Tick(), "a second request closes it");
		Check(counters.restored == 1, "and gives the input back once");
		Check(counters.suppressed == 1, "without asking to suppress again");
	}

	// A tick without a request changes nothing.
	{
		Counters counters;
		auto gate = MakeGate(counters);
		gate.SetToggleKey(35);

		Check(!gate.Tick(), "a tick without a request leaves it closed");
		Check(counters.suppressed == 0, "and asks for nothing");
	}

	// Key repeat must not queue up transitions.
	{
		Counters counters;
		auto gate = MakeGate(counters);
		gate.SetToggleKey(35);

		gate.RequestToggle();
		gate.RequestToggle();
		gate.RequestToggle();

		Check(gate.Tick(), "three requests before one tick open it");
		Check(counters.suppressed == 1, "and count as one transition");
		// Tick reports whether the overlay is open, so staying open is a true.
		// The two swallowed requests must not surface as a second transition.
		Check(gate.Tick(), "and the next tick does not close it again");
	}

	// A failed suppression still opens: an overlay without an input layer is
	// worth more than no overlay.
	{
		Counters counters;
		counters.suppressSucceeds = false;
		auto gate = MakeGate(counters);
		gate.SetToggleKey(35);

		gate.RequestToggle();
		Check(gate.Tick(), "the overlay opens even when suppression failed");

		gate.RequestToggle();
		Check(!gate.Tick(), "and closes again");
		Check(counters.restored == 0, "without giving back what it never took");
	}

	// The key is the configured one, and nothing else.
	{
		Counters counters;
		auto gate = MakeGate(counters);
		gate.SetToggleKey(35);

		Check(gate.IsToggleKey(35), "the configured key is recognised");
		Check(!gate.IsToggleKey(36), "another key is not");

		gate.SetToggleKey(112);
		Check(gate.IsToggleKey(112), "and the key can be changed");
		Check(!gate.IsToggleKey(35), "which retires the old one");
	}

	// A key of zero disables the toggle rather than matching every key.
	{
		Counters counters;
		auto gate = MakeGate(counters);
		gate.SetToggleKey(0);

		Check(!gate.IsToggleKey(0), "a toggle key of zero matches nothing");
	}

	std::printf("%d failure(s)\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
