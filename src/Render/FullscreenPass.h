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

	/// Binds the shared vertex shader, a triangle list, a rasteriser state
	/// that culls nothing and clips to no scissor, and a depth-stencil state
	/// that tests neither - then draws three vertices. The caller owns
	/// everything else: the pixel shader, the targets, the blend state.
	///
	/// The two states are set here rather than inherited because what is
	/// bound when a feature runs is whatever the engine's last draw left: a
	/// light volume's culling would swallow the triangle, and a stencil test
	/// would cut holes in it. Both were inherited in the first version of F2
	/// and happened to work, which is not the same as being right. The
	/// caller's StateGuard puts the engine's own back.
	void DrawFullscreen() noexcept;
}
