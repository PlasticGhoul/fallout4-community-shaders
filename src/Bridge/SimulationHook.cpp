#include "Bridge/SimulationHook.h"

#include "Bridge/Bridge.h"
#include "Bridge/PrologDetour.h"

#include <intrin.h>

#include <array>
#include <atomic>
#include <format>
#include <memory>

namespace Bridge
{
	namespace
	{
		/// Hooked at its prolog, as doodlum does (fo4test d510212, "Reduce
		/// input latency"). The first attempt hooked one of its three call
		/// sites, Main::OnIdle + 0x8E5, and the run of 2026-10-17 showed that
		/// site is not taken once per frame in the game: not one frame got a
		/// token. The callers below are the three E8 sites found offline on
		/// 1.11.240; the report counts which of them actually run.
		constexpr std::uint64_t kPollInputDevices = 2268382;

		struct Caller
		{
			const char* name;
			std::uint64_t function;
			std::ptrdiff_t offset;
		};
		constexpr std::array<Caller, 3> kCallers{ {
			{ "2227631+0x21E", 2227631, 0x21E },
			{ "Main::OnIdle+0x8E5", 2228917, 0x8E5 },
			{ "2228927+0x13", 2228927, 0x13 },
		} };

		using Poll_t = void (*)(void*, float);
		void* g_original{ nullptr };
		bool g_installed{ false };
		std::array<std::uintptr_t, kCallers.size()> g_returns{};
		std::array<std::atomic<std::uint64_t>, kCallers.size() + 1> g_counts{};

		void Poll(void* a_manager, float a_delta)
		{
			const auto from = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			std::size_t slot = kCallers.size();
			for (std::size_t i = 0; i < kCallers.size(); ++i) {
				if (g_returns[i] == from) {
					slot = i;
					break;
				}
			}
			g_counts[slot].fetch_add(1, std::memory_order_relaxed);
			OnSimulationStart();
			reinterpret_cast<Poll_t>(g_original)(a_manager, a_delta);
		}
	}

	bool InstallSimulationHook() noexcept
	{
		for (std::size_t i = 0; i < kCallers.size(); ++i) {
			// The return address is the byte after the five-byte call.
			g_returns[i] = REL::ID(kCallers[i].function).address() + kCallers[i].offset + 5;
		}
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

	std::string TakeSimulationReport()
	{
		std::string report;
		for (std::size_t i = 0; i <= kCallers.size(); ++i) {
			const auto count = g_counts[i].exchange(0, std::memory_order_relaxed);
			report += std::format("{}{} x{}", i == 0 ? "" : ", ", i < kCallers.size() ? kCallers[i].name : "other", count);
		}
		return report;
	}
}
