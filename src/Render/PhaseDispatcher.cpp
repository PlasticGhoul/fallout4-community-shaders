#include "Render/PhaseDispatcher.h"

namespace Render
{
	PhaseDispatcher::Token PhaseDispatcher::Subscribe(
		std::string_view a_name,
		std::function<void()> a_callback)
	{
		for (auto& entry : _entries) {
			if (entry.active) {
				continue;
			}

			entry.token = _nextToken++;
			entry.name.assign(a_name);
			entry.callback = std::move(a_callback);
			entry.active = true;
			entry.retiring = false;
			return entry.token;
		}

		return kNoToken;
	}

	void PhaseDispatcher::Unsubscribe(Token a_token) noexcept
	{
		if (a_token == kNoToken) {
			return;
		}

		for (auto& entry : _entries) {
			if (entry.active && entry.token == a_token) {
				Retire(entry);
				return;
			}
		}
	}

	void PhaseDispatcher::Retire(Entry& a_entry) noexcept
	{
		a_entry.active = false;

		if (_running) {
			// The callback may be the one on the stack right now. Clearing it
			// here would destroy a std::function in the middle of its call.
			a_entry.retiring = true;
			return;
		}

		a_entry.callback = nullptr;
		a_entry.name.clear();
		a_entry.token = kNoToken;
	}

	std::size_t PhaseDispatcher::Count() const noexcept
	{
		std::size_t count = 0;
		for (const auto& entry : _entries) {
			if (entry.active) {
				++count;
			}
		}

		return count;
	}

	bool PhaseDispatcher::Dispatch(std::uint64_t a_frame)
	{
		if (_dispatched && _lastFrame == a_frame) {
			return false;
		}

		_lastFrame = a_frame;
		_dispatched = true;

		// Settled before the first callback, so that one which subscribes from
		// inside the dispatch waits for the next frame instead of running in a
		// slot that happens to lie ahead of the cursor.
		const auto planned = Count();
		if (planned == 0) {
			return false;
		}

		_running = true;

		std::size_t ran = 0;
		for (auto& entry : _entries) {
			if (ran == planned) {
				break;
			}

			// A subscriber retired earlier in this very dispatch is no longer
			// active, so it is skipped rather than run.
			if (entry.active && entry.callback) {
				entry.callback();
				++ran;
			}
		}

		_running = false;

		for (auto& entry : _entries) {
			if (entry.retiring) {
				entry.retiring = false;
				entry.callback = nullptr;
				entry.name.clear();
				entry.token = kNoToken;
			}
		}

		return true;
	}
}
