#pragma once

namespace Shader
{
	/// Starts the watcher thread. Called once, from kGameDataReady.
	void StartPipeline() noexcept;

	/// Called from Present, on the render thread. Runs the catalog on the first
	/// frames, picks up freshly compiled bytecode, and guards the slot.
	void TickPipeline() noexcept;
}
