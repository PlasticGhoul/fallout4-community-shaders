#include "Upscale/UpscaleMath.h"

#include <cmath>

namespace Upscale
{
	std::optional<Mode> ModeFromName(std::string_view a_name) noexcept
	{
		if (a_name == "dlaa") {
			return Mode::kDLAA;
		}
		if (a_name == "quality") {
			return Mode::kQuality;
		}
		if (a_name == "balanced") {
			return Mode::kBalanced;
		}
		if (a_name == "performance") {
			return Mode::kPerformance;
		}
		if (a_name == "ultra_performance") {
			return Mode::kUltraPerformance;
		}
		return std::nullopt;
	}

	double ModeScale(Mode a_mode) noexcept
	{
		switch (a_mode) {
		case Mode::kQuality:
			return 2.0 / 3.0;
		case Mode::kBalanced:
			return 0.58;
		case Mode::kPerformance:
			return 0.5;
		case Mode::kUltraPerformance:
			return 1.0 / 3.0;
		case Mode::kDLAA:
		default:
			return 1.0;
		}
	}

	Render::Extent RenderSize(Render::Extent a_display, Mode a_mode) noexcept
	{
		const auto scale = ModeScale(a_mode);
		return Render::Extent{
			static_cast<std::uint32_t>(std::lround(a_display.width * scale)),
			static_cast<std::uint32_t>(std::lround(a_display.height * scale)),
		};
	}

	std::uint32_t JitterPhaseCount(Render::Extent a_render, Render::Extent a_display) noexcept
	{
		if (a_render.width == 0) {
			return 8;
		}
		const auto ratio = static_cast<double>(a_display.width) / static_cast<double>(a_render.width);
		return static_cast<std::uint32_t>(8.0 * ratio * ratio);
	}

	double Halton(std::uint32_t a_index, std::uint32_t a_base) noexcept
	{
		double fraction = 1.0;
		double result = 0.0;
		while (a_index > 0) {
			fraction /= a_base;
			result += fraction * (a_index % a_base);
			a_index /= a_base;
		}
		return result;
	}

	Jitter JitterOffset(std::uint32_t a_frame, std::uint32_t a_phases) noexcept
	{
		const auto index = (a_phases == 0 ? 0 : a_frame % a_phases) + 1;
		return Jitter{
			static_cast<float>(Halton(index, 2) - 0.5),
			static_cast<float>(Halton(index, 3) - 0.5),
		};
	}

	EngineOffset ToEngineOffset(Jitter a_jitter, Render::Extent a_reference) noexcept
	{
		return EngineOffset{
			2.0f * -a_jitter.x / static_cast<float>(a_reference.width),
			2.0f * a_jitter.y / static_cast<float>(a_reference.height),
		};
	}

	float MipBias(Render::Extent a_render, Render::Extent a_display) noexcept
	{
		const auto ratio = static_cast<double>(a_render.width) / static_cast<double>(a_display.width);
		return static_cast<float>(std::log2(ratio) - 1.0);
	}
}
