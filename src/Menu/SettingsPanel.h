#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

namespace Menu
{
	/// What the panel needs from around it, handed in rather than reached for:
	/// the panel draws, and knows neither the gate nor the frame counter.
	struct PanelContext
	{
		std::uint64_t frame{ 0 };

		/// Starts taking the next key press for the setting at this path. The
		/// binding button of that setting, and only that one, says so until a
		/// key arrives. The path travels with the request because the capture
		/// itself is one for the whole overlay: without it, every key button
		/// showed the prompt at once and the key went to the toggle setting
		/// whichever button had been pressed - found in F3, when a third key
		/// setting arrived.
		std::function<void(std::string_view)> armCapture;
		std::function<bool(std::string_view)> isCapturing;
	};

	/// One ImGui window, drawn from the settings schema and the feature
	/// registry. Returns whether the player asked to close it - acted on by the
	/// caller, so that the button and the toggle key take the same path through
	/// the gate.
	[[nodiscard]] bool DrawSettingsPanel(const PanelContext& a_context);
}
