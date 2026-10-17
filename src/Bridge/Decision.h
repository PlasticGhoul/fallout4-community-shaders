#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Bridge
{
	inline constexpr std::uint32_t kVendorNvidia = 0x10DE;

	/// What the device hook knows before it touches Streamline. Streamline's
	/// own refusals (not loadable, not signed, slInit, no DLSS-G on this
	/// adapter) come afterwards, from the hook itself.
	struct DecisionFacts
	{
		bool switchOn{ false };
		std::string_view switchReason;
		bool swapChainRequested{ false };
		bool windowed{ false };
		std::uint32_t vendorId{ 0 };
		bool enb{ false };
		/// Empty when no other frame generation or upscaling module is loaded.
		std::string_view foreignModule;
	};

	struct Verdict
	{
		bool build{ false };
		std::string reason;
	};

	/// The first check that fails names the verdict; the order is the order
	/// a player would fix things in.
	[[nodiscard]] Verdict Decide(const DecisionFacts& a_facts);
}
