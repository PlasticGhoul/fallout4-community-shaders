#pragma once

#include <string_view>

namespace Upscale::Hooks
{
	/// What the DLSS feature does at each hooked point. Every call arrives on
	/// the render thread, inside the engine's frame. The defaults are what
	/// the engine does without us.
	class Handler
	{
	public:
		virtual ~Handler() = default;

		/// After BSGraphics::State::UpdateTemporalData: jitter and the
		/// resolution ratios for this frame.
		virtual void AfterTemporalData() noexcept {}
		/// ImageSpaceEffectTemporalAA::IsActive; false keeps the engine's TAA
		/// out of the chain.
		[[nodiscard]] virtual bool AllowTemporalAA() noexcept { return true; }
		/// After DrawWorld_Imagespace's RenderEffectRange - where DLSS runs.
		virtual void AfterEffectRange() noexcept {}
		/// Before SetUseDynamicResolutionViewportAsDefaultViewport.
		virtual void BeforeDynamicViewportDefault() noexcept {}
		/// Around the deferred prepass and the forward pass (sampler bias).
		virtual void AroundWorldPass(bool a_before) noexcept { static_cast<void>(a_before); }
		/// Around the deferred composite (proxy targets, full depth).
		virtual void AroundComposite(bool a_before) noexcept { static_cast<void>(a_before); }
		/// Around the lens flare (full depth).
		virtual void AroundLensFlare(bool a_before) noexcept { static_cast<void>(a_before); }
		/// After BSImagespaceShaderSSLRRaytracing has set its technique up.
		virtual void AfterReflectionsSetup() noexcept {}
	};

	/// A second listener on three of the same sites, for frame generation:
	/// independent of the handler so that either feature runs without the
	/// other. Called after the handler at every site.
	class Tap
	{
	public:
		virtual ~Tap() = default;

		/// After UpdateTemporalData: the frame's rendering begins.
		virtual void AfterTemporalData() noexcept {}
		/// After RenderEffectRange: depth and motion vectors are still the
		/// frame's own - DLSS reads them here.
		virtual void AfterEffectRange() noexcept {}
		/// After SetUseDynamicResolutionViewportAsDefaultViewport: post-
		/// processing is done and the HUD not yet drawn (doodlum takes his
		/// HUD-less picture here, after the original call).
		virtual void AfterDynamicViewportDefault(bool a_enabled) noexcept { static_cast<void>(a_enabled); }
	};

	/// Verifies and installs every hook; called once from F4SEPlugin_Load,
	/// after the runtime check. All or nothing.
	[[nodiscard]] bool Install() noexcept;
	[[nodiscard]] bool Installed() noexcept;
	/// The site that refused, for the feature's refusal line.
	[[nodiscard]] std::string_view FailedSite() noexcept;

	/// nullptr passes everything through.
	void SetHandler(Handler* a_handler) noexcept;

	/// nullptr silences it.
	void SetTap(Tap* a_tap) noexcept;

	/// The engine's own SetUseDynamicResolutionViewportAsDefaultViewport, for
	/// the handler to call.
	void SetDynamicResolutionViewport(bool a_enabled) noexcept;

	/// The engine adjusts its dynamic resolution ratios each frame from its
	/// own heuristics; while we set them, that call goes to an empty function
	/// instead (doodlum nops it). Toggled from the render thread only.
	void SkipDynamicResolutionUpdate(bool a_skip) noexcept;
}
