#pragma once

namespace Render
{
	/// The vertex shader every full-screen pass shares: one oversized triangle
	/// built from SV_VertexID, no vertex buffer and no input layout.
	///
	/// Compiled once from Data/Shaders/FO4/Fullscreen.hlsl. Idempotent, and a
	/// failure is logged once and not retried - a shader that would not compile
	/// will not compile the second time either.
	[[nodiscard]] bool InitFullscreenPass() noexcept;

	/// Never called while the game runs. The shader belongs to the process, not
	/// to whichever feature happened to need it first, so no Shutdown releases
	/// it; this exists so that the module can be torn down at all.
	void ReleaseFullscreenPass() noexcept;

	/// Binds the shared vertex shader and a triangle list, then draws three
	/// vertices. The caller owns everything else: the pixel shader, the
	/// targets, the blend state.
	void DrawFullscreen() noexcept;
}
