#include "Feature/FeatureSystem.h"

#include "Feature/FeatureRegistry.h"
#include "Feature/FeatureSettings.h"
#include "Features/FrameCounter.h"

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
		}
	}

	void StartSystem() noexcept
	{
		RegisterAll();

		// Declared before Init, because a REX setting registers with its store
		// at construction and Init is what walks that registration.
		Settings::DeclareFeature("FrameCounter", false);

		Settings::Init();

		REX::INFO("{} features registered", TheRegistry().Count());
	}

	void TickSystem() noexcept
	{
		// A settings change is also the moment a refused feature deserves
		// another try, so the two belong together.
		if (Settings::ReloadIfChanged()) {
			TheRegistry().ClearRefusals();
		}

		TheRegistry().Tick([](std::string_view a_name) { return Settings::IsEnabled(a_name); });
	}
}
