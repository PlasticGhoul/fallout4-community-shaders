#pragma once

struct ImFont;

namespace Menu::Fonts
{
	/// Loads the shipped family once, into the current ImGui context. Safe to
	/// call repeatedly; only the first call does anything.
	///
	/// A missing file is not fatal: ImGui keeps its built-in font, one line
	/// says so, and the overlay is ugly but usable. Must run before the first
	/// frame, because that is when ImGui builds its atlas.
	void Load() noexcept;

	/// Null until Load found the file. Null is ImGui's way of saying "keep the
	/// current font", so a caller needs no branch of its own.
	[[nodiscard]] ImFont* Body() noexcept;
	[[nodiscard]] ImFont* Heading() noexcept;
}
