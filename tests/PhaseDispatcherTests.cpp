#include "Render/PhaseDispatcher.h"

#include <cstdio>
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

	// Written through globals rather than through captures, so that the test
	// itself never reads a capture of a closure that may just have been
	// destroyed - which is the very thing it is trying to detect.
	std::string g_order;
	Render::PhaseDispatcher* g_dispatcher = nullptr;
	Render::PhaseDispatcher::Token g_self = Render::PhaseDispatcher::kNoToken;

	/// Leaves a mark when the closure that owns it is destroyed. Captured by
	/// value, so it lives exactly as long as the std::function does.
	struct Sentinel
	{
		Sentinel() = default;
		Sentinel(const Sentinel&) = default;
		Sentinel& operator=(const Sentinel&) = delete;

		~Sentinel() { g_order += "d"; }
	};
}

int main()
{
	{
		Render::PhaseDispatcher dispatcher;
		Check(dispatcher.Count() == 0, "a fresh dispatcher has no subscribers");
		Check(!dispatcher.Dispatch(1), "and dispatching to nobody does not run");
	}

	{
		Render::PhaseDispatcher dispatcher;
		int runs = 0;
		const auto token = dispatcher.Subscribe("counter", [&runs] { ++runs; });

		Check(token != Render::PhaseDispatcher::kNoToken, "subscribing hands out a token");
		Check(dispatcher.Count() == 1, "and counts the subscriber");

		Check(dispatcher.Dispatch(1), "the first call of a frame dispatches");
		Check(runs == 1, "and runs the subscriber once");

		Check(!dispatcher.Dispatch(1), "the second call of the same frame does not");
		Check(runs == 1, "and does not run it again");

		Check(dispatcher.Dispatch(2), "the first call of the next frame does");
		Check(runs == 2, "and runs it a second time");
	}

	{
		// Frame zero is a legitimate frame number, not "nothing seen yet".
		Render::PhaseDispatcher dispatcher;
		int runs = 0;
		static_cast<void>(dispatcher.Subscribe("counter", [&runs] { ++runs; }));

		Check(dispatcher.Dispatch(0), "frame zero dispatches");
		Check(!dispatcher.Dispatch(0), "and does not dispatch twice");
		Check(runs == 1, "so the subscriber ran exactly once");
	}

	{
		Render::PhaseDispatcher dispatcher;
		int runs = 0;
		const auto token = dispatcher.Subscribe("counter", [&runs] { ++runs; });

		static_cast<void>(dispatcher.Dispatch(1));
		dispatcher.Unsubscribe(token);

		Check(dispatcher.Count() == 0, "unsubscribing removes the subscriber");

		static_cast<void>(dispatcher.Dispatch(2));
		Check(runs == 1, "and it does not run again");
	}

	{
		Render::PhaseDispatcher dispatcher;
		std::string order;
		static_cast<void>(dispatcher.Subscribe("first", [&order] { order += "a"; }));
		static_cast<void>(dispatcher.Subscribe("second", [&order] { order += "b"; }));

		static_cast<void>(dispatcher.Dispatch(1));
		Check(order == "ab", "subscribers run in subscription order");
	}

	{
		// The one that would crash a naive implementation: a subscriber that
		// removes itself while it is the thing being executed.
		Render::PhaseDispatcher dispatcher;
		int runs = 0;
		Render::PhaseDispatcher::Token self = Render::PhaseDispatcher::kNoToken;
		self = dispatcher.Subscribe("suicide", [&] {
			++runs;
			dispatcher.Unsubscribe(self);
		});

		static_cast<void>(dispatcher.Dispatch(1));
		Check(runs == 1, "a self-removing subscriber runs once");
		Check(dispatcher.Count() == 0, "and is gone afterwards");

		static_cast<void>(dispatcher.Dispatch(2));
		Check(runs == 1, "and stays gone");
	}

	{
		// A self-removing subscriber must keep its callback alive until the
		// dispatch is over. Clearing it inside Unsubscribe would destroy the
		// std::function in the middle of its own call - undefined, and with a
		// small by-reference closure quietly harmless, which is why "it did not
		// crash" proves nothing. The order of the marks does.
		Render::PhaseDispatcher dispatcher;
		g_dispatcher = std::addressof(dispatcher);

		g_self = dispatcher.Subscribe("sentinel", [sentinel = Sentinel{}] {
			static_cast<void>(sentinel);
			g_order += "a";
			g_dispatcher->Unsubscribe(g_self);
			g_order += "b";
		});

		// Copies made while the std::function was being built have left marks
		// of their own; only what happens from here counts.
		g_order.clear();
		static_cast<void>(dispatcher.Dispatch(1));

		Check(g_order == "abd", "a self-removing callback outlives its own call");

		g_dispatcher = nullptr;
	}

	{
		// Removing a later subscriber from an earlier one must take effect in
		// this very dispatch, not only in the next.
		Render::PhaseDispatcher dispatcher;
		int second = 0;
		Render::PhaseDispatcher::Token later = Render::PhaseDispatcher::kNoToken;
		static_cast<void>(dispatcher.Subscribe("first", [&] { dispatcher.Unsubscribe(later); }));
		later = dispatcher.Subscribe("second", [&second] { ++second; });

		static_cast<void>(dispatcher.Dispatch(1));
		Check(second == 0, "a subscriber removed during dispatch does not run");
	}

	{
		// Subscribing from inside a dispatch must not run the newcomer in the
		// same frame, and must not disturb the one running.
		Render::PhaseDispatcher dispatcher;
		int newcomer = 0;
		static_cast<void>(dispatcher.Subscribe("adder", [&] {
			static_cast<void>(dispatcher.Subscribe("newcomer", [&newcomer] { ++newcomer; }));
		}));

		static_cast<void>(dispatcher.Dispatch(1));
		Check(newcomer == 0, "a subscriber added during dispatch waits for the next frame");

		static_cast<void>(dispatcher.Dispatch(2));
		Check(newcomer == 1, "and runs then");
	}

	{
		Render::PhaseDispatcher dispatcher;
		bool allTaken = true;
		for (std::size_t i = 0; i < Render::PhaseDispatcher::kMaxSubscribers; ++i) {
			allTaken = allTaken &&
			           dispatcher.Subscribe("filler", [] {}) != Render::PhaseDispatcher::kNoToken;
		}

		Check(allTaken, "every slot up to the cap is handed out");
		Check(
			dispatcher.Subscribe("one too many", [] {}) == Render::PhaseDispatcher::kNoToken,
			"the cap refuses the next subscriber");
	}

	{
		Render::PhaseDispatcher dispatcher;
		dispatcher.Unsubscribe(Render::PhaseDispatcher::kNoToken);
		dispatcher.Unsubscribe(4711);
		Check(dispatcher.Count() == 0, "unsubscribing an unknown token is harmless");
	}

	std::printf("\n%s\n", g_failures == 0 ? "all checks passed" : "checks failed");
	return g_failures == 0 ? 0 : 1;
}
