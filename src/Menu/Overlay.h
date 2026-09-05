#pragma once

#include "Menu/MousePointer.h"
#include "Menu/PerformancePanel.h"
#include "Menu/SettingsPanel.h"

#include <cstdint>

namespace Menu
{
	/// Sets ImGui up against the live device on first use, and draws one frame
	/// when asked to.
	///
	/// Everything here runs on the render thread, for the same reason
	/// subproject C kept its D3D calls there: it is the one thread we can be
	/// sure about.
	class Overlay
	{
	public:
		/// Idempotent. Returns whether ImGui is ready; a failure is logged once
		/// and never retried, because a setup that failed will fail again.
		[[nodiscard]] bool EnsureReady() noexcept;

		/// One ImGui frame. Draws nothing but the frame itself when a_visible
		/// is false, so that ImGui keeps its input state consistent.
		///
		/// a_pointer overrides what the win32 backend read from the system
		/// cursor, and is only used while visible. See MousePointer for why the
		/// system cursor cannot be trusted here.
		///
		/// Returns whether the player asked to close the overlay from inside
		/// it. Acted on by the caller rather than here, so that a click and the
		/// toggle key take the same path through the gate.
		[[nodiscard]] bool Draw(
			bool a_visible,
			const PanelContext& a_panel,
			const PerformanceContext& a_performance,
			MousePointer::Point a_pointer) noexcept;

		[[nodiscard]] void* Window() const noexcept { return _window; }

	private:
		[[nodiscard]] bool BindBackBuffer() noexcept;

		// void* rather than the D3D types: this header is included from
		// MenuSystem.cpp, and imgui_impl_dx11.h brings forward declarations of
		// its own. The two type worlds meet in Overlay.cpp and nowhere else.
		void* _window{ nullptr };
		void* _renderTarget{ nullptr };

		std::uint32_t _width{ 0 };
		std::uint32_t _height{ 0 };

		// Zero until the first frame, so the theme is applied once even if the
		// setting already holds the reference size.
		float _appliedFontSize{ 0.0f };
		bool _ready{ false };
		bool _refused{ false };
	};
}
