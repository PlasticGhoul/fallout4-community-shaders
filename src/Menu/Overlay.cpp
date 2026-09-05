#include "Menu/Overlay.h"

#include "Menu/Fonts.h"
#include "Menu/Theme.h"
#include "Render/Renderer.h"
#include "Settings/Settings.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

namespace Menu
{
	namespace
	{
		// The two type worlds meet here and nowhere else. REX::W32 and the
		// Windows SDK declare the same COM objects under different names; the
		// object behind the pointer is one and the same.
		template <class T, class U>
		T* Reinterpret(U* a_pointer) noexcept
		{
			return reinterpret_cast<T*>(a_pointer);
		}
	}

	bool Overlay::EnsureReady() noexcept
	{
		if (_ready) {
			return true;
		}
		if (_refused) {
			return false;
		}

		auto* const device = Render::GetDevice();
		auto* const context = Render::GetContext();
		auto* const swapChain = Render::GetSwapChain();

		if (device == nullptr || context == nullptr || swapChain == nullptr) {
			REX::ERROR("no renderer, the overlay stays off");
			_refused = true;
			return false;
		}

		REX::W32::DXGI_SWAP_CHAIN_DESC desc{};
		if (swapChain->GetDesc(std::addressof(desc)) < 0 || desc.outputWindow == nullptr) {
			REX::ERROR("no output window, the overlay stays off");
			_refused = true;
			return false;
		}

		_window = desc.outputWindow;

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		auto& io = ImGui::GetIO();

		// Our own cursor rather than the system one: the game hides and warps
		// the system cursor for its own purposes, and fighting it over that is
		// a fight with no end. Switched per frame in Draw, not left on: since
		// F1 there is something drawn while the overlay is closed, and a
		// pointer painted over the game would be drawn into it.
		io.MouseDrawCursor = false;

		// No imgui.ini. The game's working directory is not ours to write in,
		// and the overlay has no layout worth remembering yet.
		io.IniFilename = nullptr;

		// Before the backends: adding a font after the atlas is built would
		// mean rebuilding it, and there is no reason to do that once a frame
		// has been drawn.
		Fonts::Load();

		if (!ImGui_ImplWin32_Init(_window) ||
			!ImGui_ImplDX11_Init(
				Reinterpret<ID3D11Device>(device),
				Reinterpret<ID3D11DeviceContext>(context))) {
			REX::ERROR("ImGui refused the device, the overlay stays off");
			ImGui::DestroyContext();
			_refused = true;
			return false;
		}

		REX::INFO("overlay ready, window {}, ImGui {}", _window, ImGui::GetVersion());

		_ready = true;
		return true;
	}

	bool Overlay::BindBackBuffer() noexcept
	{
		auto* const swapChain = Render::GetSwapChain();
		auto* const context = Render::GetContext();
		auto* const device = Render::GetDevice();
		if (swapChain == nullptr || context == nullptr || device == nullptr) {
			return false;
		}

		REX::W32::DXGI_SWAP_CHAIN_DESC desc{};
		if (swapChain->GetDesc(std::addressof(desc)) < 0) {
			return false;
		}

		// Held across frames, but thrown away when the buffer changed size: a
		// resolution change or a switch to fullscreen leaves the old view
		// pointing at a buffer that no longer exists.
		const auto width = desc.bufferDesc.width;
		const auto height = desc.bufferDesc.height;
		if (_renderTarget != nullptr && (width != _width || height != _height)) {
			Reinterpret<REX::W32::ID3D11RenderTargetView>(_renderTarget)->Release();
			_renderTarget = nullptr;
		}

		if (_renderTarget == nullptr) {
			REX::W32::ID3D11Texture2D* backBuffer = nullptr;
			if (swapChain->GetBuffer(
					0,
					REX::W32::IID_ID3D11Texture2D,
					reinterpret_cast<void**>(std::addressof(backBuffer))) < 0 ||
				backBuffer == nullptr) {
				return false;
			}

			REX::W32::ID3D11RenderTargetView* view = nullptr;
			const auto hr = device->CreateRenderTargetView(backBuffer, nullptr, std::addressof(view));
			backBuffer->Release();

			if (hr < 0 || view == nullptr) {
				return false;
			}

			_renderTarget = view;
			_width = width;
			_height = height;
		}

		auto* view = Reinterpret<REX::W32::ID3D11RenderTargetView>(_renderTarget);

		// ImGui draws into whatever is bound, and what that is at Present time
		// is nobody's promise. Bind the back buffer ourselves.
		context->OMSetRenderTargets(1, std::addressof(view), nullptr);
		return true;
	}

	bool Overlay::Draw(
		bool a_visible,
		const PanelContext& a_panel,
		const PerformanceContext& a_performance,
		MousePointer::Point a_pointer) noexcept
	{
		if (!_ready) {
			return false;
		}

		bool closeWanted = false;

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();

		// Only while the overlay is open. ImGui paints this cursor into the same
		// draw data as everything else, so leaving it on would put a pointer on
		// screen next to the small display, over a game the player is aiming in.
		ImGui::GetIO().MouseDrawCursor = a_visible;

		// After the backend, deliberately: its NewFrame posts the system
		// cursor's position, and this has to be the last word on where the
		// pointer is. NewFrame below is what consumes the queue.
		if (a_visible) {
			ImGui::GetIO().AddMousePosEvent(a_pointer.x, a_pointer.y);
		}

		ImGui::NewFrame();

		// Read every frame rather than cached: there is no change notification,
		// so whoever caches a setting has to refresh it themselves. The theme
		// is only rebuilt when the size actually moved.
		const auto fontSize = static_cast<float>(Settings::GetDouble("Menu/fontSize"));
		if (fontSize != _appliedFontSize) {
			ApplyTheme(fontSize);
			_appliedFontSize = fontSize;
		}

		// Pushed around the whole frame, not around the window: a tooltip or a
		// popup opens outside it and would otherwise be drawn in a different
		// size than the thing it belongs to.
		ImGui::PushFont(Fonts::Body(), fontSize);

		bool drewHud = false;
		if (a_visible) {
			closeWanted = DrawSettingsPanel(a_panel);
			static_cast<void>(DrawPerformancePanel(a_performance, Detail::kFull));
		} else if (a_performance.hud) {
			drewHud = DrawPerformancePanel(a_performance, Detail::kCompact);
		}

		ImGui::PopFont();

		// Rendered even while invisible, so that ImGui keeps writing its input
		// state forward and does not open with a frame from the past.
		ImGui::Render();

		// Also when only the small display was drawn. Before F1 nothing outside
		// the overlay drew anything, so the draw data was built every frame and
		// thrown away; now there is something in it worth handing over.
		if ((a_visible || drewHud) && BindBackBuffer()) {
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		}

		return closeWanted;
	}
}
