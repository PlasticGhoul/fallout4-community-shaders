#include "Render/FramePhase.h"

#include "Render/Profiler.h"
#include "Render/SwapChainHook.h"
#include "Render/VTablePatch.h"
#include "Util/ObjectRTTI.h"

#include <RE/B/BSShader.h>

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
		constexpr auto kClassName = "BSDFCompositeShader";

		VTablePatch g_patch;
		void* g_original = nullptr;
		PhaseDispatcher g_dispatcher;
		std::uint64_t g_hits = 0;
		bool g_installed = false;

		bool ThunkSetupTechnique(void* a_self, std::uint32_t a_pass) noexcept
		{
			if (g_dispatcher.Dispatch(FrameCount())) {
				++g_hits;
			}

			return reinterpret_cast<SetupTechniqueFn>(g_original)(a_self, a_pass);
		}
	}

	bool InstallFramePhase() noexcept
	{
		if (g_installed) {
			return true;
		}

		// [0] is the main table; [1] is the second base subobject, 0x70 further
		// on. Both ids exist in 1.11.240, checked offline against
		// version-1-11-240-0.bin before ever being resolved - REL::ID::offset
		// ends the process on an id it does not know.
		auto** const table =
			reinterpret_cast<void**>(RE::VTABLE::BSDFCompositeShader[0].address());

		if (table == nullptr) {
			REX::ERROR("frame phase: no vtable address for {}", kClassName);
			return false;
		}

		// The table has to be the one the id promised, and it has to be a
		// primary table, before an entry of it is touched. Finding out
		// afterwards is not an option: the first call through a wrongly patched
		// entry ends the process, and the locator sits in front of the table
		// anyway.
		const auto identity = Util::DescribeVTable(table);
		if (!identity.has_value() ||
			identity->className != kClassName ||
			identity->subobjectOffset != 0) {
			REX::ERROR(
				"frame phase: the vtable id for {} names {} at +0x{:X}, leaving it alone",
				kClassName,
				identity.has_value() ? identity->className : std::string{ "nothing" },
				identity.has_value() ? identity->subobjectOffset : 0);
			return false;
		}

		if (!g_patch.InstallAtTable(
				table, kSetupTechniqueSlot, reinterpret_cast<void*>(&ThunkSetupTechnique))) {
			REX::ERROR("frame phase: could not patch {}::SetupTechnique", kClassName);
			return false;
		}

		g_original = g_patch.Original();
		g_installed = true;

		REX::INFO("frame phase installed, chaining to {}", g_original);
		return true;
	}

	PhaseDispatcher::Token SubscribeFramePhase(
		std::string_view a_name,
		std::function<void()> a_callback)
	{
		// The scope is opened here rather than by the caller, so that every
		// subscriber is measured and not only the ones that remembered to ask.
		std::string name{ a_name };
		return g_dispatcher.Subscribe(
			a_name,
			[name = std::move(name), callback = std::move(a_callback)] {
				const PassScope scope{ name };
				callback();
			});
	}

	void UnsubscribeFramePhase(PhaseDispatcher::Token a_token) noexcept
	{
		g_dispatcher.Unsubscribe(a_token);
	}

	std::uint64_t FramePhaseHits() noexcept
	{
		return g_hits;
	}
}
