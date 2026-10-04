#include "Render/Resolution.h"

// The arithmetic alone, so that the host test links without the engine; the
// state a frame publishes lives in ResolutionState.cpp.
namespace Render::Resolution
{
	Extent DepthExtent(const Frame& a_frame) noexcept
	{
		return a_frame.depthFull ? a_frame.display : a_frame.render;
	}

	Extent RenderPart(const Frame& a_frame, Extent a_texture) noexcept
	{
		if (a_frame.display.width == 0 || a_frame.display.height == 0) {
			return a_texture;
		}
		return Extent{
			static_cast<std::uint32_t>(static_cast<std::uint64_t>(a_texture.width) * a_frame.render.width / a_frame.display.width),
			static_cast<std::uint32_t>(static_cast<std::uint64_t>(a_texture.height) * a_frame.render.height / a_frame.display.height),
		};
	}

	std::array<float, 2> RenderToTexture(const Frame& a_frame, Extent a_texture) noexcept
	{
		if (a_texture.width == 0 || a_texture.height == 0) {
			return { 1.0f, 1.0f };
		}
		return {
			static_cast<float>(a_frame.render.width) / static_cast<float>(a_texture.width),
			static_cast<float>(a_frame.render.height) / static_cast<float>(a_texture.height),
		};
	}
}
