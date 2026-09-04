#include "Menu/WindowHook.h"

#include "Menu/Win32.h"

#include <imgui.h>

// ImGui's own header keeps this declaration inside a '#if 0' so that it does
// not drag in <windows.h>, and tells the caller to copy it. Copying it
// verbatim is not an option here, because <windows.h> is banned in this
// project - so the parameter types are spelled out instead.
//
// They have to be spelled out exactly. The handler has C++ linkage, so its
// mangled name carries its parameter types, and imgui.lib exports
//
//     ?ImGui_ImplWin32_WndProcHandler@@YA_JPEAUHWND__@@I_K_J@Z
//
// PEAUHWND__@@ is a pointer to ::HWND__, the SDK's tag type in the global
// namespace. REX::W32::HWND is REX::W32::HWND__*, a different type that would
// mangle into a symbol that does not exist, and a void* would mangle into
// PEAX. Neither links.
struct HWND__;

extern IMGUI_IMPL_API std::int64_t ImGui_ImplWin32_WndProcHandler(
	HWND__* a_wnd,
	unsigned int a_msg,
	std::uint64_t a_wParam,
	std::int64_t a_lParam);

namespace Menu
{
	namespace
	{
		REX::W32::WNDPROC g_original = nullptr;
		std::function<bool(std::uint32_t)> g_wantsToggle;
		std::function<void()> g_onToggle;
		std::function<bool()> g_isOpen;
		bool g_installed = false;

		bool IsInputMessage(std::uint32_t a_msg) noexcept
		{
			// Keyboard and mouse, the ranges the game and other plugins care
			// about. Everything else - painting, focus, sizing - must reach the
			// window or the game misbehaves in ways that have nothing to do
			// with us.
			return (a_msg >= Win32::WM_KEYFIRST && a_msg <= Win32::WM_UNICHAR) ||
			       (a_msg >= Win32::WM_MOUSEFIRST && a_msg <= Win32::WM_MOUSEHWHEEL);
		}

		std::intptr_t WindowProc(
			REX::W32::HWND a_wnd,
			std::uint32_t a_msg,
			std::uintptr_t a_wParam,
			std::intptr_t a_lParam)
		{
			if (a_msg == Win32::WM_KEYDOWN && g_wantsToggle &&
				g_wantsToggle(static_cast<std::uint32_t>(a_wParam))) {
				g_onToggle();
				return 0;
			}

			if (g_isOpen && g_isOpen()) {
				if (ImGui_ImplWin32_WndProcHandler(
						reinterpret_cast<HWND__*>(a_wnd),
						a_msg,
						a_wParam,
						a_lParam) != 0) {
					return 1;
				}

				// The game does not read its input from messages, but other
				// plugins and overlays do. A message meant for us should not
				// also take effect somewhere else.
				if (IsInputMessage(a_msg)) {
					return 0;
				}
			}

			// SetWindowLongPtrW can report success while returning zero, if the
			// window really had no procedure before. Our procedure is installed
			// by then and there is nothing left to chain to.
			if (g_original == nullptr) {
				return 0;
			}

			return Win32::CallWindowProcW(g_original, a_wnd, a_msg, a_wParam, a_lParam);
		}
	}

	void InstallWindowHook(
		void* a_window,
		std::function<bool(std::uint32_t)> a_wantsToggle,
		std::function<void()> a_onToggle,
		std::function<bool()> a_isOpen) noexcept
	{
		if (g_installed || a_window == nullptr) {
			return;
		}

		g_wantsToggle = std::move(a_wantsToggle);
		g_onToggle = std::move(a_onToggle);
		g_isOpen = std::move(a_isOpen);

		const auto previous = Win32::SetWindowLongPtrW(
			static_cast<REX::W32::HWND>(a_window),
			REX::W32::GWLP_WNDPROC,
			reinterpret_cast<std::intptr_t>(&WindowProc));

		if (previous == 0) {
			REX::ERROR("could not chain the window procedure, the overlay stays off");
			return;
		}

		g_original = reinterpret_cast<REX::W32::WNDPROC>(previous);
		g_installed = true;

		REX::INFO("window procedure chained, original at {}", reinterpret_cast<void*>(previous));
	}
}
