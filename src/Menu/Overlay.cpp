#include "Menu/Overlay.h"

#include "Render/Renderer.h"

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
		// a fight with no end.
		io.MouseDrawCursor = true;

		// No imgui.ini. The game's working directory is not ours to write in,
		// and the overlay has no layout worth remembering yet.
		io.IniFilename = nullptr;

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

	void Overlay::Draw(bool a_visible, std::uint64_t a_frame, MousePointer::Point a_pointer) noexcept
	{
		if (!_ready) {
			return;
		}

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();

		// After the backend, deliberately: its NewFrame posts the system
		// cursor's position, and this has to be the last word on where the
		// pointer is. NewFrame below is what consumes the queue.
		if (a_visible) {
			ImGui::GetIO().AddMousePosEvent(a_pointer.x, a_pointer.y);
		}

		ImGui::NewFrame();

		if (a_visible) {
			ImGui::SetNextWindowSize(ImVec2{ 380.0f, 0.0f }, ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Community Shaders")) {
				ImGui::Text("Frame %llu", static_cast<unsigned long long>(a_frame));
				ImGui::Separator();
				ImGui::TextUnformatted("The feature list arrives with subproject E2.");
			}
			ImGui::End();
		}

		// Rendered even while invisible, so that ImGui keeps writing its input
		// state forward and does not open with a frame from the past.
		ImGui::Render();

		if (a_visible && BindBackBuffer()) {
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		}
	}
}
