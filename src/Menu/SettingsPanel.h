#pragma once

#include <cstdint>
#include <functional>

namespace Menu
{
	/// What the panel needs from around it, handed in rather than reached for:
	/// the panel draws, and knows neither the gate nor the frame counter.
	struct PanelContext
	{
		std::uint64_t frame{ 0 };

		/// Starts taking the next key press. The key binding button says so
		/// until one arrives.
		std::function<void()> armCapture;
		std::function<bool()> isCapturing;
	};

	/// One ImGui window, drawn from the settings schema and the feature
	/// registry. Returns whether the player asked to close it - acted on by the
	/// caller, so that the button and the toggle key take the same path through
	/// the gate.
	[[nodiscard]] bool DrawSettingsPanel(const PanelContext& a_context);
}
