#include "Feature/FeatureSystem.h"

#include "Feature/FeatureRegistry.h"
#include "Features/FrameCounter.h"
#include "Features/ImagespaceTint.h"
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

			// Registered last so that it is torn down first: teardown runs in
			// reverse, and the one that writes into engine memory should be the
			// one that gives it back soonest.
			TheRegistry().Register(std::make_unique<ImagespaceTint>());
		}
	}

	void StartSystem() noexcept
	{
		RegisterAll();

		// Declared before Init, because a REX setting registers with its store
		// at construction and Init is what walks that registration.
		Settings::DeclareFeature("FrameCounter", false);
		Settings::DeclareFeature("ImagespaceTint", true);

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
