#pragma once

namespace Menu
{
	/// The size the theme's metrics are written for. A font of this size scales
	/// them by exactly one.
	inline constexpr float kReferenceFontSize = 18.0f;

	/// Applies our style to the current ImGui context, scaled to a_fontSize.
	///
	/// Rebuilds the style from scratch every time, so calling it again after a
	/// size change is correct rather than cumulative.
	void ApplyTheme(float a_fontSize) noexcept;
}
