#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace Render
{
	/// Who gets called once per frame, at a fixed point inside the frame.
	///
	/// Deliberately knows neither D3D nor the engine. F1 measured the cost of
	/// the other arrangement: reaching for the profiler from inside
	/// FeatureRegistry tore up a state machine D1 had built to be testable
	/// without a game. The vtable patch that drives this lives in FramePhase,
	/// on the other side of that line.
	///
	/// Subscribers sit in a fixed array rather than a vector because a callback
	/// may subscribe or unsubscribe while it is running. Nothing moves, so no
	/// std::function is ever relocated or destroyed while it is executing.
	class PhaseDispatcher
	{
	public:
		using Token = std::uint32_t;

		/// Never handed out. What Subscribe returns when the cap is reached.
		static constexpr Token kNoToken = 0;

		/// Ten features would already be a lot; sixteen is room to spare
		/// without making the array worth thinking about.
		static constexpr std::size_t kMaxSubscribers = 16;

		/// The name is for logging, and for the profiler pass FramePhase wraps
		/// around the callback. Returns kNoToken when every slot is taken.
		Token Subscribe(std::string_view a_name, std::function<void()> a_callback);

		/// Takes effect immediately, including for a subscriber that has not
		/// run yet in a dispatch already underway. Removing the callback that
		/// is currently executing defers the destruction of its std::function
		/// until the dispatch is over.
		void Unsubscribe(Token a_token) noexcept;

		[[nodiscard]] std::size_t Count() const noexcept;

		/// Runs every subscriber, in subscription order, but only when a_frame
		/// differs from the frame last dispatched. Returns whether it ran.
		///
		/// A subscriber added while this is running waits for the next frame:
		/// how many are to run is fixed before the first callback.
		bool Dispatch(std::uint64_t a_frame);

	private:
		struct Entry
		{
			Token token{ kNoToken };
			std::string name;
			std::function<void()> callback;
			bool active{ false };

			/// Unsubscribed during a dispatch: no longer run, but its callback
			/// is not cleared until the dispatch ends.
			bool retiring{ false };
		};

		void Retire(Entry& a_entry) noexcept;

		std::array<Entry, kMaxSubscribers> _entries{};
		Token _nextToken{ 1 };
		std::uint64_t _lastFrame{ 0 };

		/// Separate from _lastFrame, because frame zero is a frame like any
		/// other and must not read as "nothing dispatched yet".
		bool _dispatched{ false };
		bool _running{ false };
	};
}
