#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

namespace Bridge
{
	/// Streamline frame indices from the start of the game's frame (the
	/// prolog of PollInputDevices) to where that frame is rendered and
	/// presented. Both usually run on one thread, in loading screens not, and
	/// their order within one loop iteration is not known: a locked queue
	/// serves either order and either thread, a single variable neither. The
	/// index, not the token pointer, travels:
	/// slGetNewFrameToken gives the same frame's token back for its index
	/// (ProgrammingGuide.md 2.10).
	class FrameTokenQueue
	{
	public:
		/// Streamline expects no more than three frames in flight
		/// (sl_core_api.h, slGetNewFrameToken).
		static constexpr std::size_t kInFlight = 3;

		void Push(std::uint32_t a_index) noexcept;
		[[nodiscard]] std::optional<std::uint32_t> Pop() noexcept;
		[[nodiscard]] std::uint64_t Dropped() const noexcept;
		void Clear() noexcept;

	private:
		mutable std::mutex _lock;
		std::deque<std::uint32_t> _indices;
		std::uint64_t _dropped{ 0 };
	};
}
