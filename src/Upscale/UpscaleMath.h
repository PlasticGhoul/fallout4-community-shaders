#pragma once

#include "Render/Extent.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace Upscale
{
	enum class Mode : std::uint8_t
	{
		kDLAA,
		kQuality,
		kBalanced,
		kPerformance,
		kUltraPerformance,
	};

	/// The names DLSS/mode stores.
	[[nodiscard]] std::optional<Mode> ModeFromName(std::string_view a_name) noexcept;

	/// Render size over display size, per axis - NVIDIA's DLSS ratios. In the
	/// game the optimal settings NGX reports win; this is what is used when
	/// that query fails, and what the tests pin.
	[[nodiscard]] double ModeScale(Mode a_mode) noexcept;
	[[nodiscard]] Render::Extent RenderSize(Render::Extent a_display, Mode a_mode) noexcept;

	/// FSR's rule, which doodlum's upscaler uses for DLSS as well: eight
	/// phases times the square of the upscale ratio, truncated.
	[[nodiscard]] std::uint32_t JitterPhaseCount(Render::Extent a_render, Render::Extent a_display) noexcept;

	/// Radical inverse of a_index (from 1) in a_base.
	[[nodiscard]] double Halton(std::uint32_t a_index, std::uint32_t a_base) noexcept;

	/// In render pixels, in [-0.5, 0.5): Halton 2 and 3 at index
	/// (frame mod phases) + 1, minus one half.
	struct Jitter
	{
		float x;
		float y;
	};
	[[nodiscard]] Jitter JitterOffset(std::uint32_t a_frame, std::uint32_t a_phases) noexcept;

	/// What BSGraphics::State::offsetX/Y take: NDC, x negated (doodlum,
	/// fo4test upscaler). a_reference is the size the engine relates the
	/// offset to; doodlum passes the display size even when upscaling, and the
	/// first probe run decides whether that holds on 1.11.240.
	struct EngineOffset
	{
		float x;
		float y;
	};
	[[nodiscard]] EngineOffset ToEngineOffset(Jitter a_jitter, Render::Extent a_reference) noexcept;

	/// NVIDIA's recommendation for DLSS: log2(render / display) - 1.
	[[nodiscard]] float MipBias(Render::Extent a_render, Render::Extent a_display) noexcept;
}
