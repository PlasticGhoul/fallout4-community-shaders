#pragma once

#include "Render/PassStatistics.h"

#include <REX/W32/D3D11.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Render
{
	/// CPU and GPU time per named pass, through D3D11 timestamp queries.
	///
	/// A frame is Present to Present, because that is the one point per frame
	/// where we hold control. The frame figure is therefore wall time and
	/// includes a vsync wait if one is pending; a pass of our own is exact,
	/// because its timestamps sit immediately around our own calls.
	///
	/// Everything here belongs to the render thread. Nothing locks, because
	/// nothing else touches it.
	class Profiler
	{
	public:
		/// A slot is reused when the write cursor comes round to it again, so
		/// the ring length is the grace a result gets: six slots give five
		/// frames, about 28 ms at 180 fps.
		///
		/// Three was the obvious number and it was wrong. Measured in the first
		/// run: 4664 of 9600 frames were dropped because their results had not
		/// come back within the two frames three slots allow. A GPU runs one to
		/// three frames behind and the driver may queue more, so the ring has to
		/// outlast that rather than match it.
		static constexpr std::size_t kFrameLatency = 6;

		[[nodiscard]] static Profiler& GetSingleton() noexcept;

		/// Idempotent. A failure is logged once and never retried: creating a
		/// query that failed once will fail again.
		bool Initialize() noexcept;
		void Release() noexcept;

		/// Whether queries are being issued at all - false while refused, and
		/// false while the setting is off.
		[[nodiscard]] bool IsMeasuring() const noexcept { return _measuring && _ready; }
		void SetMeasuring(bool a_measuring) noexcept;

		void BeginFrame() noexcept;
		void EndFrame() noexcept;

		/// Reads back whatever is ready, without ever blocking the render
		/// thread, and moves the histories forward.
		void Collect() noexcept;

		void BeginPass(std::string_view a_name) noexcept;
		void EndPass() noexcept;

		[[nodiscard]] std::span<const PassResult> Results() const noexcept
		{
			return _passes.Results();
		}

		[[nodiscard]] float FrameGpuMs() const noexcept { return _frameGpuMs; }
		[[nodiscard]] float FrameCpuMs() const noexcept { return _frameCpuMs; }
		[[nodiscard]] std::uint64_t DiscardedFrames() const noexcept { return _discarded; }

		/// Writes the current histories to the log, in the format that goes into
		/// the roadmap.
		void LogSnapshot() const noexcept;

	private:
		struct Timing
		{
			std::string name;
			std::uint32_t depth{ 0 };
			REX::W32::ID3D11Query* begin{ nullptr };
			REX::W32::ID3D11Query* end{ nullptr };
			std::int64_t cpuBegin{ 0 };
			float cpuMs{ 0.0f };

			/// Whether a RenderDoc region was opened for it, so that exactly the
			/// ones that opened are closed again.
			bool marked{ false };
		};

		struct Slot
		{
			REX::W32::ID3D11Query* disjoint{ nullptr };

			// The name rather than the index: a retire between issuing and
			// collecting shifts every index behind the hole, and the sample
			// would land on another pass.
			std::vector<Timing> timings;
			std::uint64_t frame{ 0 };
			bool inFlight{ false };
		};

		[[nodiscard]] bool CreateSlot(Slot& a_slot) noexcept;
		void ReleaseSlot(Slot& a_slot) noexcept;
		[[nodiscard]] REX::W32::ID3D11Query* AcquireTimestamp() noexcept;
		void RecycleTimings(Slot& a_slot) noexcept;
		void CollectSlot(Slot& a_slot) noexcept;

		std::array<Slot, kFrameLatency> _slots{};
		std::vector<REX::W32::ID3D11Query*> _spare;

		PassTable _passes;

		/// Indices into the current slot's timings, innermost last.
		std::vector<std::size_t> _open;

		std::uint64_t _frame{ 0 };
		std::uint64_t _discarded{ 0 };
		float _frameGpuMs{ 0.0f };
		float _frameCpuMs{ 0.0f };
		double _ticksToMs{ 0.0 };

		std::size_t _writeSlot{ 0 };
		bool _ready{ false };
		bool _refused{ false };
		bool _measuring{ true };
		bool _frameOpen{ false };
	};

	/// Opens a pass on construction and closes it on destruction.
	///
	/// The RenderDoc region belongs to the profiler rather than to this class,
	/// because the feature registry opens its passes through two function
	/// pointers rather than through a scope - and a region only half the passes
	/// carry would be worse than none.
	class PassScope
	{
	public:
		explicit PassScope(std::string_view a_name) noexcept;
		~PassScope() noexcept;

		PassScope(const PassScope&) = delete;
		PassScope& operator=(const PassScope&) = delete;
	};
}
