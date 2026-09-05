#include "Feature/FeatureSystem.h"

#include "Feature/FeatureRegistry.h"
#include "Features/FrameCounter.h"
#include "Features/ImagespaceTint.h"
#include "Features/ShaderCensus.h"
#include "Render/Profiler.h"
#include "Settings/Settings.h"

#include <memory>

namespace Features
{
	namespace
	{
		// Registration order is also teardown order, reversed. Keep the cheap
		// and self-contained ones first.
		void RegisterAll()
		{
			TheRegistry().Register(std::make_unique<FrameCounter>());

			// Patches thirteen engine vtables, so it belongs ahead of the one
			// that writes into engine memory: teardown runs in reverse, and the
			// entries should go back after the shader pointer does.
			TheRegistry().Register(std::make_unique<ShaderCensus>());

			// Registered last so that it is torn down first: teardown runs in
			// reverse, and the one that writes into engine memory should be the
			// one that gives it back soonest.
			TheRegistry().Register(std::make_unique<ImagespaceTint>());
		}
	}

	void StartSystem() noexcept
	{
		// Handed in here rather than reached for by the registry: the registry
		// has to stay buildable and testable without the renderer, and this is
		// the first place that knows both.
		SetFrameTiming(
			[](std::string_view a_name) { Render::Profiler::GetSingleton().BeginPass(a_name); },
			[] { Render::Profiler::GetSingleton().EndPass(); });

		// Registering is also declaring: the registry calls Declare on every
		// feature it takes, which has to happen before Init because a REX
		// setting registers with its store at construction and Init is what
		// walks that registration.
		RegisterAll();

		Settings::Init();

		REX::INFO("{} features registered", TheRegistry().Count());
	}

	void TickSystem() noexcept
	{
		// A settings change is also the moment a refused feature deserves
		// another try, so the two belong together. It answers for a change on
		// disk and for one made in the overlay alike.
		if (Settings::ConsumeChanged()) {
			TheRegistry().ClearRefusals();
		}

		TheRegistry().Tick([](std::string_view a_name) { return Settings::IsEnabled(a_name); });
	}
}
