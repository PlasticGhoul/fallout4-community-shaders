#pragma once

#include <cstdint>
#include <string_view>

namespace Bridge
{
	/// What the composite shows: the picture, the picture next to the
	/// original, or the picture with the pixels it left alone tinted.
	enum class NeuralView : std::uint32_t
	{
		kOff = 0,
		kSplit = 1,
		kMask = 2,
	};

	/// DLSS Neural Rendering's values as NGX takes them. Plain types: the
	/// native island reads this header too.
	struct NeuralOptions
	{
		std::uint32_t preset{ 2 };  // NVSDK_NGX_DLSSNR_Hint_Render_Preset_B
		std::uint32_t style{ 0 };
		float intensity{ 1.0f };
		float structure{ 0.7f };
		float tone{ 0.3f };
		float skin{ 0.5f };
		bool autoMask{ true };
		std::uint32_t maskBlendMode{ 2 };  // _Min with the auto mask, _None (0) without, as Remix does
		NeuralView view{ NeuralView::kOff };
	};

	/// The NeuralRendering block as the settings store holds it.
	struct NeuralSettings
	{
		std::string_view model;  // "fast" | "original"
		std::string_view style;  // "1" | "2" | "3"
		std::string_view view;   // "off" | "split" | "mask"
		double intensity;
		double structure;
		double tone;
		double skin;
		bool characters;
	};

	/// Unknown strings and NaN fall back to the defaults; sliders are clamped
	/// to 0..1.
	[[nodiscard]] NeuralOptions MakeNeuralOptions(const NeuralSettings& a_settings) noexcept;

	struct NeuralFacts
	{
		bool loadingScreen;
		bool pipboy;
		bool menuMode;
		bool overlayOpen;
		bool taaOn;
	};

	/// Why NR must not run this frame; nullptr when it may. The open overlay
	/// never blocks it: the sliders are set while watching the picture.
	[[nodiscard]] const char* NeuralBlocker(const NeuralFacts& a_facts) noexcept;
}
