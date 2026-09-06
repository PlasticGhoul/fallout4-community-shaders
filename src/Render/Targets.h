#pragma once

#include <REX/W32/D3D11.h>

#include <cstddef>

namespace Render::Targets
{
	/// The engine's own render targets, by their slot in
	/// BSGraphics::RendererData.
	///
	/// The number in the name a capture shows is that slot: the inventory from
	/// B2 labels with std::format("FO4_RT_{:03}", i) over the same index.
	/// Nothing here creates a view - the engine's rtView, srView and
	/// srViewDepth are what we bind, and they are already there.
	///
	/// Only the slots a feature actually uses are named. A complete
	/// RENDER_TARGET enum is the known gap in CommonLibF4 and is not the
	/// business of the feature that happens to need four of them.

	/// FO4_DS_002, R24G8_TYPELESS. The scene depth the world is drawn against.
	inline constexpr std::size_t kSceneDepth = 2;

	/// FO4_RT_058 and FO4_RT_059, R11G11B10_FLOAT. Written by kDFLight and
	/// carried on by kDFComposite.
	inline constexpr std::size_t kLightDiffuse = 58;
	inline constexpr std::size_t kLightSpecular = 59;

	/// FO4_RT_020, R16G16_UNORM. The g-buffer normals, confirmed by the
	/// measuring spike. Unused by F2, named for what comes after it.
	inline constexpr std::size_t kGBufferNormal = 20;

	/// Null for an index out of range or an empty slot.
	///
	/// These are silent on failure by design. They are read once per frame from
	/// a render pass, so a log line here would be a log line per frame; the
	/// caller knows which target it asked for and can say so once. LogSlots is
	/// where the detail lives.
	[[nodiscard]] REX::W32::ID3D11RenderTargetView* RenderTargetView(std::size_t a_slot) noexcept;
	[[nodiscard]] REX::W32::ID3D11ShaderResourceView* RenderTargetSRV(std::size_t a_slot) noexcept;
	[[nodiscard]] REX::W32::ID3D11Texture2D* RenderTargetTexture(std::size_t a_slot) noexcept;

	/// The depth as a shader resource: DepthStencilTarget::srViewDepth, which
	/// hands R24G8_TYPELESS over as R24_UNORM_X8_TYPELESS.
	[[nodiscard]] REX::W32::ID3D11ShaderResourceView* DepthSRV(std::size_t a_slot) noexcept;

	/// Logs the debug name behind each named slot.
	///
	/// This is the check that the constants point where we think. The inventory
	/// ran when the Present hook went in and named every resource, so this
	/// reads back FO4_RT_058 rather than an address - and a constant that is
	/// one off says so in one line instead of as a picture that looks subtly
	/// wrong.
	void LogSlots() noexcept;
}
