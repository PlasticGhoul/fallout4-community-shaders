#include "Bridge/NeuralDecision.h"

#include <algorithm>
#include <cmath>

namespace Bridge
{
	namespace
	{
		constexpr std::uint32_t kPresetA = 1;  // NVSDK_NGX_DLSSNR_Hint_Render_Preset_A
		constexpr std::uint32_t kPresetB = 2;  // NVSDK_NGX_DLSSNR_Hint_Render_Preset_B
		constexpr std::uint32_t kBlendNone = 0;
		constexpr std::uint32_t kBlendMin = 2;

		float Unit(double a_value, float a_default) noexcept
		{
			if (std::isnan(a_value)) {
				return a_default;
			}
			return static_cast<float>(std::clamp(a_value, 0.0, 1.0));
		}
	}

	NeuralOptions MakeNeuralOptions(const NeuralSettings& a_settings) noexcept
	{
		const NeuralOptions defaults{};
		NeuralOptions options{};
		options.preset = a_settings.model == "original" ? kPresetA : kPresetB;
		if (a_settings.style == "2") {
			options.style = 1;
		} else if (a_settings.style == "3") {
			options.style = 2;
		}
		if (a_settings.view == "split") {
			options.view = NeuralView::kSplit;
		} else if (a_settings.view == "mask") {
			options.view = NeuralView::kMask;
		}
		options.intensity = Unit(a_settings.intensity, defaults.intensity);
		options.structure = Unit(a_settings.structure, defaults.structure);
		options.tone = Unit(a_settings.tone, defaults.tone);
		options.skin = Unit(a_settings.skin, defaults.skin);
		options.autoMask = a_settings.characters;
		options.maskBlendMode = a_settings.characters ? kBlendMin : kBlendNone;
		return options;
	}

	const char* NeuralBlocker(const NeuralFacts& a_facts) noexcept
	{
		if (a_facts.loadingScreen) {
			return "loading screen";
		}
		if (a_facts.pipboy) {
			return "Pip-Boy";
		}
		if (a_facts.menuMode) {
			return "a game menu is open";
		}
		if (!a_facts.taaOn) {
			return "the game's TAA is off, so it writes no motion vectors";
		}
		return nullptr;
	}
}
