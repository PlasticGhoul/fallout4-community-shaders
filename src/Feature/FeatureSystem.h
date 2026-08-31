#pragma once

namespace Features
{
	/// Registers the features, declares their settings and loads them. Called
	/// once, from kGameDataReady.
	void StartSystem() noexcept;

	/// The one entry per frame, from Present, on the render thread.
	void TickSystem() noexcept;
}
