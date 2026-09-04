#include "Menu/MousePointer.h"

#include "Menu/Win32.h"

namespace Menu
{
	namespace
	{
		// The window in screen coordinates, which is what the cursor is
		// expressed in. Borderless full screen makes these the same numbers as
		// the client rectangle, but a windowed game does not, and the parking
		// spot has to be the window's middle either way.
		bool WindowBounds(void* a_window, REX::W32::RECT& a_bounds) noexcept
		{
			if (a_window == nullptr) {
				return false;
			}
			return REX::W32::GetWindowRect(static_cast<REX::W32::HWND>(a_window), std::addressof(a_bounds));
		}

		float Clamp(float a_value, float a_low, float a_high) noexcept
		{
			if (a_value < a_low) {
				return a_low;
			}
			if (a_value > a_high) {
				return a_high;
			}
			return a_value;
		}
	}

	void MousePointer::Acquire(void* a_window) noexcept
	{
		if (_active) {
			return;
		}

		_window = a_window;

		REX::W32::POINT entry{};
		if (Win32::GetCursorPos(std::addressof(entry)) != 0) {
			_entryX = entry.x;
			_entryY = entry.y;
		}

		REX::W32::RECT bounds{};
		if (WindowBounds(_window, bounds)) {
			// Start where the cursor already is, so the pointer does not appear
			// somewhere the player was not looking.
			_pointer.x = static_cast<float>(entry.x - bounds.x1);
			_pointer.y = static_cast<float>(entry.y - bounds.y1);
		}

		_active = true;
	}

	MousePointer::Point MousePointer::Update() noexcept
	{
		if (!_active) {
			return _pointer;
		}

		REX::W32::RECT bounds{};
		if (!WindowBounds(_window, bounds)) {
			return _pointer;
		}

		const auto width = bounds.x2 - bounds.x1;
		const auto height = bounds.y2 - bounds.y1;
		if (width <= 0 || height <= 0) {
			return _pointer;
		}

		const auto parkX = bounds.x1 + width / 2;
		const auto parkY = bounds.y1 + height / 2;

		// Only while the game is in front. Parking the cursor otherwise would
		// pull the mouse out of whatever the player alt-tabbed to, and no
		// overlay is worth that.
		if (Win32::GetForegroundWindow() != static_cast<REX::W32::HWND>(_window)) {
			return _pointer;
		}

		REX::W32::POINT now{};
		if (Win32::GetCursorPos(std::addressof(now)) != 0) {
			_pointer.x += static_cast<float>(now.x - parkX);
			_pointer.y += static_cast<float>(now.y - parkY);

			_pointer.x = Clamp(_pointer.x, 0.0f, static_cast<float>(width - 1));
			_pointer.y = Clamp(_pointer.y, 0.0f, static_cast<float>(height - 1));
		}

		Win32::SetCursorPos(parkX, parkY);
		return _pointer;
	}

	void MousePointer::Release() noexcept
	{
		if (!_active) {
			return;
		}

		Win32::SetCursorPos(_entryX, _entryY);
		_active = false;
	}
}
