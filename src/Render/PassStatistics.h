#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Render
{
	/// A pass's timings over the recent past, as a ring of samples.
	///
	/// Deliberately free of D3D: this is the half of the profiler that
	/// computes, and computing is the half that can be tested without a game.
	class PassStatistics
	{
	public:
		/// Three hundred samples is about 1.7 seconds at 180 fps and five at
		/// 60. Long enough for a p99 to mean something, short enough that
		/// walking into a new cell shows up rather than being averaged away.
		static constexpr std::size_t kHistorySize = 300;

		void Push(float a_ms) noexcept;
		void Clear() noexcept;

		[[nodiscard]] float Last() const noexcept { return _last; }
		[[nodiscard]] std::size_t Count() const noexcept { return _count; }

		[[nodiscard]] float Average() const noexcept;

		/// Interpolated, with a_p in [0, 100]. Zero when there is nothing to
		/// take a percentile of.
		[[nodiscard]] float Percentile(float a_p) const noexcept;

		/// Oldest first, which is the order a plot wants. Zero past the end.
		[[nodiscard]] float Sample(std::size_t a_index) const noexcept;

	private:
		std::array<float, kHistorySize> _history{};
		std::size_t _head{ 0 };
		std::size_t _count{ 0 };
		float _last{ 0.0f };
	};

	/// What one pass looked like in the frame just collected.
	struct PassResult
	{
		std::string name;
		std::uint32_t depth{ 0 };

		float gpuMs{ 0.0f };
		float cpuMs{ 0.0f };
		float avgMs{ 0.0f };
		float p95Ms{ 0.0f };
		float p99Ms{ 0.0f };

		/// The GPU history, for the plot. Owned by the table, valid until the
		/// next Retire.
		const PassStatistics* history{ nullptr };
	};

	/// Every pass the profiler has seen lately, in the order it first saw them.
	///
	/// Order matters: passes open in the same order every frame, so first-seen
	/// order is tree order, and the table reads as the tree it is.
	class PassTable
	{
	public:
		static constexpr std::size_t kMaxPasses = 128;
		static constexpr std::size_t kNoPass = static_cast<std::size_t>(-1);

		/// A pass that has not been sampled for this many frames leaves the
		/// table, so a switched-off feature stops showing its last number.
		static constexpr std::uint64_t kRetireFrames = 60;

		/// The slot for a name, creating it if there is room. kNoPass once the
		/// cap is reached, which is refused once and logged by the caller.
		[[nodiscard]] std::size_t Find(std::string_view a_name) noexcept;

		void Sample(
			std::size_t a_index,
			std::uint32_t a_depth,
			float a_gpuMs,
			float a_cpuMs,
			std::uint64_t a_frame) noexcept;

		void Retire(std::uint64_t a_frame) noexcept;
		void Clear() noexcept;

		[[nodiscard]] std::span<const PassResult> Results() const noexcept { return _results; }

	private:
		struct Entry
		{
			std::string name;
			std::uint32_t depth{ 0 };
			PassStatistics gpu;
			PassStatistics cpu;
			std::uint64_t lastFrame{ 0 };
		};

		/// Repoints the name index at the current positions. Erasing shifts
		/// everything behind the hole, so the map has to be rebuilt rather than
		/// patched.
		void RebuildIndex() noexcept;
		void RebuildResults() noexcept;

		std::vector<Entry> _entries;
		std::unordered_map<std::string, std::size_t> _index;
		std::vector<PassResult> _results;
	};
}
