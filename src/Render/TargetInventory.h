#pragma once

namespace Render
{
	/// Walks the engine's three render target arrays once: reads each texture's
	/// D3D description, gives every non-null object a debug name so capture
	/// tools show it, and logs a table.
	///
	/// Reads and labels; never changes how the engine renders.
	void RunTargetInventory() noexcept;
}
