#include "Bridge/SimulationHook.h"

#include "Bridge/Bridge.h"
#include "Upscale/CallSite.h"

#include <REL/THook.h>

#include <array>
#include <cstring>
#include <memory>

namespace Bridge
{
	namespace
	{
		/// Verified offline on 1.11.240 (2026-10-17, against version-1-11-240-0.bin
		/// and Fallout4.exe): E8 at Main::OnIdle + 0x8E5 into PollInputDevices,
		/// one of its three callers. doodlum hooked the callee for the same
		/// reason ("Reduce input latency", fo4test d510212).
		constexpr std::uint64_t kOnIdle = 2228917;
		constexpr std::ptrdiff_t kOffset = 0x8E5;
		constexpr std::uint64_t kPollInputDevices = 2268382;

		using PollHook = REL::THook<void(void*, float)>;
		std::unique_ptr<PollHook> g_poll;
		bool g_installed{ false };

		void Poll(void* a_manager, float a_delta)
		{
			OnSimulationStart();
			(*g_poll)(a_manager, a_delta);
		}
	}

	bool InstallSimulationHook() noexcept
	{
		const auto address = REL::ID(kOnIdle).address() + kOffset;
		std::array<std::uint8_t, 5> bytes{};
		std::memcpy(bytes.data(), reinterpret_cast<const void*>(address), bytes.size());
		const auto target = Upscale::CallTarget(bytes, address);
		if (!target || *target != REL::ID(kPollInputDevices).address()) {
			REX::ERROR("bridge: Main::OnIdle does not call PollInputDevices where 1.11.240 does; no frame start for Reflex");
			return false;
		}
		g_poll = std::make_unique<PollHook>("Main::OnIdle PollInputDevices", REL::ID(kOnIdle), kOffset, &Poll);
		if (!g_poll->Init() || !g_poll->Enable()) {
			REX::ERROR("bridge: the verified PollInputDevices site refused its hook");
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
