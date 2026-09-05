#include "Render/PassStatistics.h"

#include <algorithm>
#include <cmath>

namespace Render
{
	void PassStatistics::Push(float a_ms) noexcept
	{
		_history[_head] = a_ms;
		_head = (_head + 1) % kHistorySize;
		if (_count < kHistorySize) {
			++_count;
		}

		_last = a_ms;
	}

	void PassStatistics::Clear() noexcept
	{
		_head = 0;
		_count = 0;
		_last = 0.0f;
	}

	float PassStatistics::Average() const noexcept
	{
		if (_count == 0) {
			return 0.0f;
		}

		float total = 0.0f;
		for (std::size_t i = 0; i < _count; ++i) {
			total += Sample(i);
		}

		return total / static_cast<float>(_count);
	}

	float PassStatistics::Percentile(float a_p) const noexcept
	{
		if (_count == 0) {
			return 0.0f;
		}

		// Sorted on demand rather than kept sorted: this runs once per pass per
		// drawn frame, over at most three hundred floats, and a second ordered
		// structure would have to be kept right on every push.
		std::array<float, kHistorySize> sorted{};
		for (std::size_t i = 0; i < _count; ++i) {
			sorted[i] = Sample(i);
		}
		std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(_count));

		const float clamped = std::clamp(a_p, 0.0f, 100.0f);
		const float rank = clamped / 100.0f * static_cast<float>(_count - 1);
		const auto lower = static_cast<std::size_t>(std::floor(rank));
		const auto upper = std::min(lower + 1, _count - 1);
		const float fraction = rank - static_cast<float>(lower);

		return sorted[lower] + (sorted[upper] - sorted[lower]) * fraction;
	}

	float PassStatistics::Sample(std::size_t a_index) const noexcept
	{
		if (a_index >= _count) {
			return 0.0f;
		}

		// _head points at the next slot to write, so the oldest sample sits
		// _count places behind it.
		const std::size_t oldest = (_head + kHistorySize - _count) % kHistorySize;
		return _history[(oldest + a_index) % kHistorySize];
	}

	std::size_t PassTable::Find(std::string_view a_name) noexcept
	{
		if (const auto found = _index.find(std::string{ a_name }); found != _index.end()) {
			return found->second;
		}

		if (_entries.size() >= kMaxPasses) {
			return kNoPass;
		}

		_entries.push_back(Entry{ std::string{ a_name } });
		const auto index = _entries.size() - 1;
		_index.emplace(_entries.back().name, index);
		return index;
	}

	void PassTable::Sample(
		std::size_t a_index,
		std::uint32_t a_depth,
		float a_gpuMs,
		float a_cpuMs,
		std::uint64_t a_frame) noexcept
	{
		if (a_index >= _entries.size()) {
			return;
		}

		auto& entry = _entries[a_index];
		entry.depth = a_depth;
		entry.gpu.Push(a_gpuMs);
		entry.cpu.Push(a_cpuMs);
		entry.lastFrame = a_frame;

		RebuildResults();
	}

	void PassTable::Retire(std::uint64_t a_frame) noexcept
	{
		const auto before = _entries.size();
		std::erase_if(_entries, [&](const Entry& a_entry) {
			return a_frame > a_entry.lastFrame &&
			       a_frame - a_entry.lastFrame > kRetireFrames;
		});

		if (_entries.size() == before) {
			return;
		}

		RebuildIndex();
		RebuildResults();
	}

	void PassTable::Clear() noexcept
	{
		_entries.clear();
		_index.clear();
		_results.clear();
	}

	void PassTable::RebuildIndex() noexcept
	{
		_index.clear();
		for (std::size_t i = 0; i < _entries.size(); ++i) {
			_index.emplace(_entries[i].name, i);
		}
	}

	void PassTable::RebuildResults() noexcept
	{
		_results.clear();
		_results.reserve(_entries.size());

		for (const auto& entry : _entries) {
			_results.push_back(PassResult{
				entry.name,
				entry.depth,
				entry.gpu.Last(),
				entry.cpu.Last(),
				entry.gpu.Average(),
				entry.gpu.Percentile(95.0f),
				entry.gpu.Percentile(99.0f),
				std::addressof(entry.gpu) });
		}
	}
}
