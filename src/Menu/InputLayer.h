#pragma once

namespace Menu
{
	/// Holds the engine's own input enable layer, the one its menus use.
	///
	/// This is the first call into an engine function in the whole port -
	/// everything before it only read memory. The three address library ids
	/// behind it were measured present for 1.11.240; whether they point at the
	/// functions we take them for is what the acceptance run decides.
	class InputLayer
	{
	public:
		/// Takes the game's input away. Returns whether it worked.
		[[nodiscard]] bool Suppress() noexcept;

		/// Gives it back and releases the layer.
		void Restore() noexcept;

	private:
		void* _layer{ nullptr };
	};
}
