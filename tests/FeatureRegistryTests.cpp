#include "Feature/FeatureRegistry.h"

#include <cstdio>
#include <set>
#include <stdexcept>
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

	// Records the order in which the fakes are set up and shut down, so the
	// reverse teardown can be checked rather than assumed.
	std::vector<std::string> g_order;

	class FakeFeature : public Features::Feature
	{
	public:
		FakeFeature(std::string a_name, bool a_setupSucceeds, bool a_frameThrows) :
			_name(std::move(a_name)),
			_setupSucceeds(a_setupSucceeds),
			_frameThrows(a_frameThrows)
		{}

		[[nodiscard]] std::string_view Name() const override { return _name; }

		[[nodiscard]] bool Setup() override
		{
			++setups;
			g_order.push_back("setup:" + _name);
			return _setupSucceeds;
		}

		void Frame() override
		{
			++frames;
			if (_frameThrows) {
				throw std::runtime_error("deliberate");
			}
		}

		void Shutdown() override
		{
			++shutdowns;
			g_order.push_back("shutdown:" + _name);
		}

		int setups = 0;
		int frames = 0;
		int shutdowns = 0;

	private:
		std::string _name;
		bool _setupSucceeds;
		bool _frameThrows;
	};

	Features::EnabledQuery Only(std::set<std::string> a_enabled)
	{
		return [enabled = std::move(a_enabled)](std::string_view a_name) {
			return enabled.contains(std::string{ a_name });
		};
	}
}

int main()
{
	// A feature that is on runs; one that is off does not.
	{
		Features::Registry registry;
		auto* const alpha = new FakeFeature{ "alpha", true, false };
		auto* const beta = new FakeFeature{ "beta", true, false };
		registry.Register(std::unique_ptr<Features::Feature>{ alpha });
		registry.Register(std::unique_ptr<Features::Feature>{ beta });

		registry.Tick(Only({ "alpha" }));

		Check(alpha->setups == 1, "an enabled feature is set up");
		Check(alpha->frames == 1, "and gets a frame");
		Check(beta->setups == 0, "a disabled feature is not set up");
		Check(beta->frames == 0, "and gets no frame");
		Check(
			registry.StateOf("alpha") == Features::State::kRunning,
			"the enabled one reports running");
		Check(registry.StateOf("beta") == Features::State::kOff, "the disabled one reports off");

		// Toggling one must leave the other alone.
		registry.Tick(Only({ "beta" }));

		Check(alpha->shutdowns == 1, "turning a feature off shuts it down");
		Check(beta->setups == 1, "turning the other on sets it up");
		Check(alpha->frames == 1, "the stopped one gets no further frames");
	}

	// Teardown runs in reverse registration order.
	{
		g_order.clear();
		Features::Registry registry;
		registry.Register(std::make_unique<FakeFeature>("first", true, false));
		registry.Register(std::make_unique<FakeFeature>("second", true, false));

		registry.Tick(Only({ "first", "second" }));
		registry.Tick(Only({}));

		const std::vector<std::string> expected{
			"setup:first", "setup:second", "shutdown:second", "shutdown:first"
		};
		Check(g_order == expected, "setup runs in order and teardown in reverse");
	}

	// A refused feature is not retried every frame.
	{
		Features::Registry registry;
		auto* const broken = new FakeFeature{ "broken", false, false };
		registry.Register(std::unique_ptr<Features::Feature>{ broken });

		registry.Tick(Only({ "broken" }));
		registry.Tick(Only({ "broken" }));
		registry.Tick(Only({ "broken" }));

		Check(broken->setups == 1, "a refused feature is set up only once");
		Check(broken->frames == 0, "and never gets a frame");
		Check(registry.StateOf("broken") == Features::State::kRefused, "and reports as refused");
		Check(broken->shutdowns == 1, "a failed setup is still shut down");

		// A settings change is what earns it another go.
		registry.ClearRefusals();
		registry.Tick(Only({ "broken" }));

		Check(broken->setups == 2, "clearing refusals lets it try again");
	}

	// A feature that throws from Frame is shut down, not left running.
	{
		Features::Registry registry;
		auto* const thrower = new FakeFeature{ "thrower", true, true };
		auto* const bystander = new FakeFeature{ "bystander", true, false };
		registry.Register(std::unique_ptr<Features::Feature>{ thrower });
		registry.Register(std::unique_ptr<Features::Feature>{ bystander });

		registry.Tick(Only({ "thrower", "bystander" }));

		Check(thrower->shutdowns == 1, "a throwing feature is shut down");
		Check(registry.StateOf("thrower") == Features::State::kRefused, "and reports as refused");
		Check(bystander->frames == 1, "a throwing feature does not stop its neighbour");

		registry.Tick(Only({ "thrower", "bystander" }));

		Check(thrower->frames == 1, "and gets no further frames");
		Check(bystander->frames == 2, "while the neighbour keeps running");
	}

	std::printf("%d failure(s)\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
