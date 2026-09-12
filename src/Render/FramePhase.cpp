#include "Render/FramePhase.h"

#include "Render/Profiler.h"
#include "Render/SwapChainHook.h"
#include "Render/VTablePatch.h"
#include "Shader/ShaderCatalog.h"
#include "Util/ObjectRTTI.h"

#include <array>
#include <string>
#include <utility>

namespace Render
{
	namespace
	{
		// bool SetupTechnique(std::uint32_t), slot 02 of the BSShader vtable.
		// The slot numbers commonlibf4 gives BSShader are right even though its
		// data offsets are not - every field after shaderType sits 0x78 too
		// low, measured in subproject C. Nothing here reads a field, and the
		// self pointer is passed on untouched, so void* is the honest type.
		using SetupTechniqueFn = bool (*)(void*, std::uint32_t);

		constexpr std::size_t kSetupTechniqueSlot = 2;
		constexpr auto kPhaseCount = static_cast<std::size_t>(Phase::kCount);

		struct Anchor
		{
			/// As the RTTI spells it, and as Shader::ShaderClasses lists it.
			const char* className;
			VTablePatch patch;
			void* original{ nullptr };
			PhaseDispatcher dispatcher;
			std::uint64_t hits{ 0 };
			bool installed{ false };
		};

		// kAfterOpaque names the class FrameTrace is expected to find first
		// after composite and sky; the first trace run of F3 confirms or
		// corrects it. A different measurement changes this string and
		// nothing else.
		std::array<Anchor, kPhaseCount> g_anchors{ {
			{ "BSDFCompositeShader" },
			{ "BSEffectShader" },
		} };

		template <std::size_t N>
		bool ThunkSetupTechnique(void* a_self, std::uint32_t a_pass) noexcept
		{
			auto& anchor = g_anchors[N];
			if (anchor.dispatcher.Dispatch(FrameCount())) {
				++anchor.hits;
			}
			return reinterpret_cast<SetupTechniqueFn>(anchor.original)(a_self, a_pass);
		}

		template <std::size_t... I>
		constexpr std::array<SetupTechniqueFn, sizeof...(I)> MakeThunks(std::index_sequence<I...>)
		{
			return { &ThunkSetupTechnique<I>... };
		}

		constexpr auto kThunks = MakeThunks(std::make_index_sequence<kPhaseCount>{});

		std::uintptr_t VTableOf(std::string_view a_className) noexcept
		{
			for (const auto& shaderClass : Shader::ShaderClasses()) {
				if (shaderClass.className == a_className) {
					return shaderClass.vtable;
				}
			}
			return 0;
		}

		bool InstallOne(std::size_t a_index) noexcept
		{
			auto& anchor = g_anchors[a_index];
			if (anchor.installed) {
				return true;
			}

			auto** const table = reinterpret_cast<void**>(VTableOf(anchor.className));
			if (table == nullptr) {
				REX::ERROR("frame phase: no vtable address for {}", anchor.className);
				return false;
			}

			// The table has to be the one the id promised, and it has to be a
			// primary table, before an entry of it is touched. Finding out
			// afterwards is not an option: the first call through a wrongly
			// patched entry ends the process, and the locator sits in front
			// of the table anyway.
			const auto identity = Util::DescribeVTable(table);
			if (!identity.has_value() ||
				identity->className != anchor.className ||
				identity->subobjectOffset != 0) {
				REX::ERROR(
					"frame phase: the vtable id for {} names {} at +0x{:X}, leaving it alone",
					anchor.className,
					identity.has_value() ? identity->className : std::string{ "nothing" },
					identity.has_value() ? identity->subobjectOffset : 0);
				return false;
			}

			if (!anchor.patch.InstallAtTable(
					table, kSetupTechniqueSlot, reinterpret_cast<void*>(kThunks[a_index]))) {
				REX::ERROR("frame phase: could not patch {}::SetupTechnique", anchor.className);
				return false;
			}

			anchor.original = anchor.patch.Original();
			anchor.installed = true;
			REX::INFO(
				"frame phase {} installed on {}, chaining to {}",
				a_index,
				anchor.className,
				anchor.original);
			return true;
		}

		bool InRange(Phase a_phase) noexcept
		{
			return static_cast<std::size_t>(a_phase) < kPhaseCount;
		}
	}

	bool InstallFramePhase() noexcept
	{
		bool before = false;
		for (std::size_t i = 0; i < kPhaseCount; ++i) {
			const bool ok = InstallOne(i);
			if (i == static_cast<std::size_t>(Phase::kBeforeComposite)) {
				before = ok;
			}
		}
		return before;
	}

	PhaseDispatcher::Token SubscribeFramePhase(
		Phase a_phase,
		std::string_view a_name,
		std::function<void()> a_callback)
	{
		if (!InRange(a_phase)) {
			return PhaseDispatcher::kNoToken;
		}

		// The scope is opened here rather than by the caller, so that every
		// subscriber is measured and not only the ones that remembered to ask.
		std::string name{ a_name };
		return g_anchors[static_cast<std::size_t>(a_phase)].dispatcher.Subscribe(
			a_name,
			[name = std::move(name), callback = std::move(a_callback)] {
				const PassScope scope{ name };
				callback();
			});
	}

	void UnsubscribeFramePhase(Phase a_phase, PhaseDispatcher::Token a_token) noexcept
	{
		if (InRange(a_phase)) {
			g_anchors[static_cast<std::size_t>(a_phase)].dispatcher.Unsubscribe(a_token);
		}
	}

	std::uint64_t FramePhaseHits(Phase a_phase) noexcept
	{
		return InRange(a_phase) ? g_anchors[static_cast<std::size_t>(a_phase)].hits : 0;
	}
}
