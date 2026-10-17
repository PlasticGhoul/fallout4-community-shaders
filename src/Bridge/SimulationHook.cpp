#include "Bridge/SimulationHook.h"

#include "Bridge/Bridge.h"
#include "Bridge/PrologDetour.h"

#include <memory>

namespace Bridge
{
	namespace
	{
		/// Hooked at its prolog, as doodlum does (fo4test d510212, "Reduce
		/// input latency"). The first attempt hooked one of its three call
		/// sites, Main::OnIdle + 0x8E5, and the run of 2026-10-17 showed that
		/// site is not taken once per frame in the game: not one frame got a
		/// token. Which caller runs changes with the place - outdoors
		/// 2228927+0x13, indoors OnIdle+0x8E5, loading 2227631+0x21E - and
		/// every frame has exactly one of them (runs of 2026-10-17).
		constexpr std::uint64_t kPollInputDevices = 2268382;

		using Poll_t = void (*)(void*, float);
		void* g_original{ nullptr };
		bool g_installed{ false };

		void Poll(void* a_manager, float a_delta)
		{
			OnSimulationStart();
			reinterpret_cast<Poll_t>(g_original)(a_manager, a_delta);
		}
	}

	bool InstallSimulationHook() noexcept
	{
		g_original = reinterpret_cast<void*>(REL::ID(kPollInputDevices).address());
		if (!AttachPrologDetour(std::addressof(g_original), reinterpret_cast<void*>(&Poll))) {
			REX::ERROR("bridge: PollInputDevices could not be detoured; no frame start for Reflex");
			return false;
		}
		g_installed = true;
		return true;
	}

	bool SimulationHookInstalled() noexcept
	{
		return g_installed;
	}
}
