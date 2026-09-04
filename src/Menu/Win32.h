#pragma once

#include <REX/W32/USER32.h>

// REX::W32 declares SetWindowLongPtrA but not the W form, and no
// CallWindowProc at all. user32.lib is already linked PUBLIC by
// commonlib-shared, so only the prototypes are missing.
//
// The W forms on purpose: using the A form on a Unicode window switches its
// message translation to ANSI, which subproject E2 would notice the moment it
// puts a text field on screen.
namespace Menu::Win32
{
	// REX::W32's WM enumeration stops at WM_CHILDACTIVATE (0x0022) and carries
	// no input message at all, which is precisely the half the overlay needs.
	// The VK_ constants next to it are complete and are used from there.
	inline constexpr std::uint32_t WM_KEYDOWN = 0x0100u;
	inline constexpr std::uint32_t WM_KEYFIRST = 0x0100u;
	inline constexpr std::uint32_t WM_UNICHAR = 0x0109u;
	inline constexpr std::uint32_t WM_MOUSEFIRST = 0x0200u;
	inline constexpr std::uint32_t WM_MOUSEHWHEEL = 0x020Eu;

	extern "C"
	{
		std::intptr_t __stdcall SetWindowLongPtrW(
			REX::W32::HWND a_wnd,
			std::int32_t a_index,
			std::intptr_t a_newPtr) noexcept;

		std::intptr_t __stdcall CallWindowProcW(
			REX::W32::WNDPROC a_prev,
			REX::W32::HWND a_wnd,
			std::uint32_t a_msg,
			std::uintptr_t a_wParam,
			std::intptr_t a_lParam) noexcept;

		// The cursor half of USER32, none of which REX::W32 has. Returning
		// BOOL rather than bool on purpose: BOOL is any non-zero value, and a
		// bool return would read only its low byte.
		std::int32_t __stdcall GetCursorPos(REX::W32::POINT* a_point) noexcept;
		std::int32_t __stdcall SetCursorPos(std::int32_t a_x, std::int32_t a_y) noexcept;
		std::int32_t __stdcall ClipCursor(const REX::W32::RECT* a_rect) noexcept;
		std::int32_t __stdcall GetClipCursor(REX::W32::RECT* a_rect) noexcept;

		// Needed to tell whether the game still has the focus. Parking the
		// cursor while it does not would take the mouse away from whatever the
		// player alt-tabbed to.
		REX::W32::HWND __stdcall GetForegroundWindow() noexcept;
	}
}
