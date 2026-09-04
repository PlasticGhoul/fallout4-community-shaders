#pragma once

namespace Menu
{
	/// Declares the overlay's settings. Called once, from kGameDataReady,
	/// before Features::StartSystem loads the file.
	void StartSystem() noexcept;

	/// The one entry per frame, from Present, on the render thread.
	void TickSystem() noexcept;
}
