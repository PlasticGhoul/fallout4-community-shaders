#pragma once

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
		void Draw(bool a_visible, std::uint64_t a_frame) noexcept;

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
		bool _ready{ false };
		bool _refused{ false };
	};
}
