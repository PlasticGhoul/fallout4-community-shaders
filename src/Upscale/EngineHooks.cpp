#include "Upscale/EngineHooks.h"

#include "Render/VTablePatch.h"
#include "Upscale/CallSite.h"

#include <RE/B/BSGraphics.h>
#include <REL/THook.h>

#include <array>
#include <atomic>
#include <cstring>
#include <memory>

namespace Upscale
{
	bool AttachLensFlare(void** a_original, void* a_replacement) noexcept;
}

namespace Upscale::Hooks
{
	namespace
	{
		/// One call site: the function that holds it, the offset of the E8,
		/// and the function the call must reach. Verified offline on 1.11.240
		/// (F5.5a spec, finding 1); a mismatch means another plugin got there
		/// first or the game changed, and either way nothing is written.
		struct Site
		{
			const char* name;
			std::uint64_t function;
			std::ptrdiff_t offset;
			std::uint64_t callee;
		};

		constexpr Site kTemporalData{ "State::UpdateTemporalData", 2318286, 0x38C, 2277095 };
		constexpr Site kEffectRange{ "Imagespace RenderEffectRange", 2318322, 0x83, 2316593 };
		constexpr Site kViewportDefault{ "SetUseDynamicResolutionViewportAsDefaultViewport", 2318322, 0xC5, 2277194 };
		constexpr Site kPrePass{ "PreUI DeferredPrePass", 2318321, 0x2E3, 2318301 };
		constexpr Site kForward{ "PreUI Forward", 2318321, 0x3A6, 2318315 };
		constexpr Site kDynamicUpdate{ "UpdateDynamicResolution", 2318321, 0x29F, 2277195 };
		constexpr Site kComposite{ "DeferredComposite RenderPassImmediately", 2318313, 0x915, 2318699 };
		constexpr Site kReflections{ "SSLRRaytracing BeginTechnique", 2317302, 0x1C, 2318876 };
		constexpr std::uint64_t kLensFlare = 2317547;
		constexpr std::size_t kTemporalAAIsActive = 8;

		using Single = REL::THook<void(void*)>;
		using EffectRangeHook = REL::THook<void(void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t)>;
		using ViewportHook = REL::THook<void(void*, bool)>;
		using CompositeHook = REL::THook<void(void*, std::uint32_t, bool)>;

		std::atomic<Handler*> g_handler{ nullptr };
		bool g_installed{ false };
		std::string_view g_failed{};

		std::unique_ptr<Single> g_temporalData;
		std::unique_ptr<EffectRangeHook> g_effectRange;
		std::unique_ptr<ViewportHook> g_viewportDefault;
		std::unique_ptr<Single> g_prePass;
		std::unique_ptr<Single> g_forward;
		std::unique_ptr<Single> g_dynamicUpdate;
		std::unique_ptr<CompositeHook> g_composite;
		std::unique_ptr<EffectRangeHook> g_reflections;
		Render::VTablePatch g_temporalAA;
		void* g_lensFlare{ nullptr };

		Handler* Current() noexcept
		{
			return g_handler.load(std::memory_order_acquire);
		}

		std::atomic<Tap*> g_tap{ nullptr };

		Tap* CurrentTap() noexcept
		{
			return g_tap.load(std::memory_order_acquire);
		}

		bool Verify(const Site& a_site) noexcept
		{
			const auto address = REL::ID(a_site.function).address() + a_site.offset;
			std::array<std::uint8_t, 5> bytes{};
			std::memcpy(bytes.data(), reinterpret_cast<const void*>(address), bytes.size());
			const auto target = CallTarget(bytes, address);
			if (!target || *target != REL::ID(a_site.callee).address()) {
				REX::ERROR("upscale hooks: {} does not call what 1.11.240 calls there; nothing installed", a_site.name);
				g_failed = a_site.name;
				return false;
			}
			return true;
		}

		void TemporalData(void* a_state)
		{
			(*g_temporalData)(a_state);
			if (auto* const handler = Current()) {
				handler->AfterTemporalData();
			}
			if (auto* const tap = CurrentTap()) {
				tap->AfterTemporalData();
			}
		}

		void EffectRange(void* a_self, std::uint32_t a_1, std::uint32_t a_2, std::uint32_t a_3, std::uint32_t a_4)
		{
			(*g_effectRange)(a_self, a_1, a_2, a_3, a_4);
			if (auto* const handler = Current()) {
				handler->AfterEffectRange();
			}
			if (auto* const tap = CurrentTap()) {
				tap->AfterEffectRange();
			}
		}

		void ViewportDefault(void* a_self, bool a_enabled)
		{
			if (auto* const handler = Current()) {
				handler->BeforeDynamicViewportDefault();
			}
			(*g_viewportDefault)(a_self, a_enabled);
			if (auto* const tap = CurrentTap()) {
				tap->AfterDynamicViewportDefault(a_enabled);
			}
		}

		void PrePass(void* a_drawWorld)
		{
			auto* const handler = Current();
			if (handler != nullptr) {
				handler->AroundWorldPass(true);
			}
			(*g_prePass)(a_drawWorld);
			if (handler != nullptr) {
				handler->AroundWorldPass(false);
			}
		}

		void Forward(void* a_drawWorld)
		{
			auto* const handler = Current();
			if (handler != nullptr) {
				handler->AroundWorldPass(true);
			}
			(*g_forward)(a_drawWorld);
			if (handler != nullptr) {
				handler->AroundWorldPass(false);
			}
		}

		// Stands in for the engine's dynamic resolution update while we set
		// the ratios ourselves; the call it replaces passes one pointer.
		void SkipUpdate(void*) {}

		void Composite(void* a_pass, std::uint32_t a_technique, bool a_flag)
		{
			auto* const handler = Current();
			if (handler != nullptr) {
				handler->AroundComposite(true);
			}
			(*g_composite)(a_pass, a_technique, a_flag);
			if (handler != nullptr) {
				handler->AroundComposite(false);
			}
		}

		void Reflections(void* a_shader, std::uint32_t a_1, std::uint32_t a_2, std::uint32_t a_3, std::uint32_t a_4)
		{
			(*g_reflections)(a_shader, a_1, a_2, a_3, a_4);
			if (auto* const handler = Current()) {
				handler->AfterReflectionsSetup();
			}
		}

		void LensFlare(void* a_camera)
		{
			auto* const handler = Current();
			if (handler != nullptr) {
				handler->AroundLensFlare(true);
			}
			reinterpret_cast<void (*)(void*)>(g_lensFlare)(a_camera);
			if (handler != nullptr) {
				handler->AroundLensFlare(false);
			}
		}

		bool TemporalAAIsActive(void* a_effect)
		{
			const auto original = reinterpret_cast<bool (*)(void*)>(g_temporalAA.Original());
			auto* const handler = Current();
			return (handler == nullptr || handler->AllowTemporalAA()) && original(a_effect);
		}

		template <class Hook, class Function>
		bool Make(std::unique_ptr<Hook>& a_hook, const Site& a_site, Function a_function) noexcept
		{
			a_hook = std::make_unique<Hook>(a_site.name, REL::ID(a_site.function), a_site.offset, a_function);
			return a_hook->Init() && a_hook->Enable();
		}
	}

	bool Install() noexcept
	{
		for (const auto* site : { &kTemporalData, &kEffectRange, &kViewportDefault, &kPrePass, &kForward,
				 &kDynamicUpdate, &kComposite, &kReflections }) {
			if (!Verify(*site)) {
				return false;
			}
		}

		if (!Make(g_temporalData, kTemporalData, &TemporalData) ||
			!Make(g_effectRange, kEffectRange, &EffectRange) ||
			!Make(g_viewportDefault, kViewportDefault, &ViewportDefault) ||
			!Make(g_prePass, kPrePass, &PrePass) ||
			!Make(g_forward, kForward, &Forward) ||
			!Make(g_composite, kComposite, &Composite) ||
			!Make(g_reflections, kReflections, &Reflections)) {
			REX::ERROR("upscale hooks: a verified site refused its hook");
			g_failed = "trampoline";
			return false;
		}

		// Built and initialised, but enabled only while the DLSS feature sets
		// the ratios itself.
		g_dynamicUpdate = std::make_unique<Single>(kDynamicUpdate.name, REL::ID(kDynamicUpdate.function), kDynamicUpdate.offset, &SkipUpdate);
		if (!g_dynamicUpdate->Init()) {
			g_failed = kDynamicUpdate.name;
			return false;
		}

		auto** const table = reinterpret_cast<void**>(RE::VTABLE::ImageSpaceEffectTemporalAA[0].address());
		if (!g_temporalAA.InstallAtTable(table, kTemporalAAIsActive, reinterpret_cast<void*>(&TemporalAAIsActive))) {
			REX::ERROR("upscale hooks: ImageSpaceEffectTemporalAA::IsActive could not be patched");
			g_failed = "ImageSpaceEffectTemporalAA::IsActive";
			return false;
		}

		g_lensFlare = reinterpret_cast<void*>(REL::ID(kLensFlare).address());
		if (!AttachLensFlare(std::addressof(g_lensFlare), reinterpret_cast<void*>(&LensFlare))) {
			REX::ERROR("upscale hooks: the lens flare prolog could not be detoured");
			g_failed = "BSImagespaceShaderLensFlare (Detours)";
			return false;
		}

		g_installed = true;
		REX::INFO("upscale hooks: eight call sites, one vtable entry and one prolog in place");
		return true;
	}

	bool Installed() noexcept
	{
		return g_installed;
	}

	std::string_view FailedSite() noexcept
	{
		return g_failed;
	}

	void SetHandler(Handler* a_handler) noexcept
	{
		g_handler.store(a_handler, std::memory_order_release);
	}

	void SetTap(Tap* a_tap) noexcept
	{
		g_tap.store(a_tap, std::memory_order_release);
	}

	void SetDynamicResolutionViewport(bool a_enabled) noexcept
	{
		// Always on the render target manager singleton, as doodlum calls it.
		// The first version took the object from the RenderEffectRange hook's
		// first argument instead - another object - and the flag never
		// changed: the effects after DLSS kept the render-size viewport and
		// drew the full picture into its corner (second run, 2026-10-04).
		auto* const manager = RE::BSGraphics::RenderTargetManager::GetSingleton();
		if (g_viewportDefault && manager != nullptr) {
			(*g_viewportDefault)(manager, a_enabled);
		}
	}

	void SkipDynamicResolutionUpdate(bool a_skip) noexcept
	{
		// Called every frame; only a change rewrites the five bytes, which
		// lifts the page protection each time.
		static bool skipping = false;
		if (!g_dynamicUpdate || skipping == a_skip) {
			return;
		}
		skipping = a_skip;
		if (a_skip) {
			static_cast<void>(g_dynamicUpdate->Enable());
		} else {
			static_cast<void>(g_dynamicUpdate->Disable());
		}
	}
}
