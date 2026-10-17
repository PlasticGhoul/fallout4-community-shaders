#pragma once

namespace Menu
{
	/// Declares the overlay's settings. Called once, from kGameDataReady,
	/// before Features::StartSystem loads the file.
	void StartSystem() noexcept;

	/// The one entry per frame, from Present, on the render thread.
	void TickSystem() noexcept;

	/// Whether the overlay is open, as of the last tick. Frame generation
	/// switches itself off while it is: generated frames of a menu are input
	/// latency and nothing else.
	[[nodiscard]] bool IsOverlayOpen() noexcept;
}
