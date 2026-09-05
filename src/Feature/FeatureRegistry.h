#pragma once

#include "Feature/Feature.h"

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace Features
{
	enum class State
	{
		kOff,
		kRunning,

		/// Enabled, but Setup said no or Frame threw. Held apart from kOff so
		/// that a broken feature is not retried every single frame, which would
		/// fill the log in seconds.
		kRefused
	};

	/// Answers whether a feature should be running. The registry takes the
	/// desired state through this rather than reading settings itself, which is
	/// what makes the whole state machine testable without a game.
	using EnabledQuery = std::function<bool(std::string_view a_name)>;

	/// Called around each running feature's Frame, so that every feature is
	/// measured from one place and none of them has to ask for it.
	///
	/// Two plain function pointers, empty until something installs them, for
	/// the same reason EnabledQuery is a callback: the registry has to build
	/// and run without the renderer, which is what keeps its state machine
	/// testable without a game.
	void SetFrameTiming(void (*a_begin)(std::string_view), void (*a_end)()) noexcept;

	class Registry
	{
	public:
		void Register(std::unique_ptr<Feature> a_feature);

		/// Brings every feature into the state the query asks for, then frames
		/// the running ones. Teardown runs before setup, and in reverse
		/// registration order.
		void Tick(const EnabledQuery& a_query) noexcept;

		/// Lets refused features try again. Called when the settings changed:
		/// that is the moment a refusal deserves reconsidering.
		void ClearRefusals() noexcept;

		/// Every feature, in registration order, with the state it is in. Name
		/// and state only: the menu needs no more, and the registry gives away
		/// nothing it owns.
		void ForEach(
			const std::function<void(std::string_view a_name, State a_state)>& a_visit)
			const noexcept;

		[[nodiscard]] State StateOf(std::string_view a_name) const noexcept;
		[[nodiscard]] std::size_t Count() const noexcept;

	private:
		struct Entry
		{
			std::unique_ptr<Feature> feature;
			State state{ State::kOff };
		};

		void SetupEntry(Entry& a_entry) noexcept;
		void FrameEntry(Entry& a_entry) noexcept;
		void ShutdownEntry(Entry& a_entry) noexcept;

		std::vector<Entry> _entries;
	};

	/// The one the game uses. Tests build their own.
	[[nodiscard]] Registry& TheRegistry() noexcept;
}
