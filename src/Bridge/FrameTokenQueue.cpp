#include "Bridge/FrameTokenQueue.h"

namespace Bridge
{
	void FrameTokenQueue::Push(std::uint32_t a_index) noexcept
	{
		const std::scoped_lock lock{ _lock };
		_indices.push_back(a_index);
		while (_indices.size() > kInFlight) {
			_indices.pop_front();
			++_dropped;
		}
	}

	std::optional<std::uint32_t> FrameTokenQueue::Pop() noexcept
	{
		const std::scoped_lock lock{ _lock };
		if (_indices.empty()) {
			return std::nullopt;
		}
		const auto index = _indices.front();
		_indices.pop_front();
		return index;
	}

	std::uint64_t FrameTokenQueue::Dropped() const noexcept
	{
		const std::scoped_lock lock{ _lock };
		return _dropped;
	}

	void FrameTokenQueue::Clear() noexcept
	{
		const std::scoped_lock lock{ _lock };
		_indices.clear();
	}
}
