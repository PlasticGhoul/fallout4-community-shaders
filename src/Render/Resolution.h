#pragma once

#include "Render/Extent.h"

#include <array>

namespace Render::Resolution
{
	/// The two sizes of a frame, and which depth is bound. With DLSS
	/// upscaling the world is drawn into the top left render rectangle of the
	/// full size targets; during the composite and the lens flare the DLSS
	/// feature binds a depth at display size instead. Both depths are the same
	/// texture size, so the description cannot tell them apart - this can.
	struct Frame
	{
		Extent display;
		Extent render;
		bool depthFull{ false };
	};

	/// The pixels of the depth texture that hold the picture.
	[[nodiscard]] Extent DepthExtent(const Frame& a_frame) noexcept;

	/// The factor that takes a uv over the render rectangle into a texture of
	/// size a_texture: render / texture per axis, one for a texture that is
	/// the render rectangle itself.
	[[nodiscard]] std::array<float, 2> RenderToTexture(const Frame& a_frame, Extent a_texture) noexcept;

	/// This frame. Without DLSS both sizes are those of RT_004 and nothing
	/// is swapped.
	[[nodiscard]] Frame Current() noexcept;

	/// Called by the DLSS feature at the start of every frame it runs.
	void Publish(Extent a_display, Extent a_render) noexcept;
	void SetDepthFull(bool a_full) noexcept;
	/// Called by the DLSS feature when it stops.
	void Withdraw() noexcept;
}
