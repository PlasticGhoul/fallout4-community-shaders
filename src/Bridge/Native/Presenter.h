#pragma once

#include "Bridge/Bridge.h"
#include "Bridge/FrameTokenQueue.h"
#include "Bridge/Native/NativeSdk.h"
#include "Bridge/PresentStats.h"
#include "Bridge/SlotRing.h"

#include <array>
#include <cstdint>
#include <string>

namespace Bridge::Native
{
	/// What lives for the process, created with the first bridged device and
	/// reused should the game create its device again: Streamline is bound to
	/// one device, and the proxies it hands out are its own to free.
	struct Backend
	{
		ComPtr<ID3D12Device> device;
		ComPtr<ID3D12CommandQueue> queue;     // created through Streamline's device proxy
		ComPtr<IDXGIFactory5> nativeFactory;  // for what Streamline does not intercept
		IDXGIFactory2* factory{ nullptr };    // Streamline's proxy, never released by us
		LUID adapter{};
		bool tearing{ false };
	};
	[[nodiscard]] Backend& TheBackend() noexcept;

	struct SharedTexture
	{
		ComPtr<ID3D11Texture2D> texture11;
		ComPtr<ID3D11UnorderedAccessView> uav11;
		ComPtr<ID3D12Resource> resource12;
	};

	struct Slot
	{
		SharedTexture final;
		SharedTexture hudless;
		SharedTexture depth;
		SharedTexture motion;
	};

	/// The D3D12 side of one swap chain of the game's. Every call arrives on
	/// the thread that presents, which in Fallout 4 is the main thread.
	class Presenter
	{
	public:
		Presenter() = default;
		~Presenter();
		Presenter(const Presenter&) = delete;
		Presenter& operator=(const Presenter&) = delete;

		[[nodiscard]] bool Create(ID3D11Device* a_device, IDXGIAdapter* a_adapter, const DXGI_SWAP_CHAIN_DESC& a_desc) noexcept;
		void Destroy() noexcept;
		[[nodiscard]] const std::string& Failure() const noexcept { return _failure; }

		[[nodiscard]] HRESULT Present(UINT a_syncInterval, UINT a_flags) noexcept;
		[[nodiscard]] HRESULT ResizeBuffers(UINT a_count, UINT a_width, UINT a_height, DXGI_FORMAT a_format, UINT a_flags) noexcept;

		[[nodiscard]] ID3D11Device* Device11() const noexcept { return _device11.Get(); }
		[[nodiscard]] ID3D11Texture2D* BackBuffer() const noexcept { return _backBuffer.Get(); }
		[[nodiscard]] IDXGISwapChain1* Native() const noexcept { return _native; }
		[[nodiscard]] DXGI_SWAP_CHAIN_DESC Describe() const noexcept;

		void OnSimulationStart() noexcept;
		void OnRenderStart() noexcept;
		[[nodiscard]] bool CurrentSlot(InputSlot& a_slot) noexcept;
		void SubmitFrameInputs(const FrameInputs& a_inputs) noexcept;
		void SubmitHudless() noexcept { _hudlessThisFrame = true; }
		void SetGeneration(bool a_on) noexcept { _generationWanted = a_on; }
		void SetReflexMode(std::uint32_t a_mode) noexcept;
		void SetSplitView(bool a_on) noexcept { _splitView = a_on; }
		[[nodiscard]] Status CurrentStatus() const noexcept;

	private:
		bool Fail(std::string a_reason, HRESULT a_result = S_OK) noexcept;
		bool EnsureBackend(IDXGIAdapter* a_adapter) noexcept;
		bool CreateSwapChain(UINT a_width, UINT a_height) noexcept;
		bool CreateTextures(UINT a_width, UINT a_height) noexcept;
		bool CreateShared(const D3D11_TEXTURE2D_DESC& a_desc, SharedTexture& a_texture) noexcept;
		bool AcquireBuffers() noexcept;
		void ReleaseBuffers() noexcept;
		void WaitForQueueIdle() noexcept;
		void WaitSlotOnD3D11() noexcept;
		void ApplyOptions(bool a_wanted) noexcept;
		void Tag(const sl::FrameToken& a_token, bool a_generate) noexcept;
		void Mark(sl::PCLMarker a_marker, const sl::FrameToken& a_token) noexcept;
		sl::FrameToken* TakeRenderToken() noexcept;
		void PollState() noexcept;
		void LogSecond() noexcept;

		std::string _failure;
		DXGI_SWAP_CHAIN_DESC _desc{};
		HWND _window{ nullptr };
		UINT _width{ 0 };
		UINT _height{ 0 };
		DXGI_FORMAT _swapFormat{ DXGI_FORMAT_UNKNOWN };
		UINT _swapFlags{ 0 };

		ComPtr<ID3D11Device5> _device11;
		ComPtr<ID3D11DeviceContext4> _context11;
		ComPtr<ID3D11Fence> _fence11;
		ComPtr<ID3D12Fence> _fence12;
		HANDLE _event{ nullptr };
		std::uint64_t _fenceValue{ 0 };
		std::uint64_t _waitedFor{ 0 };

		std::array<ComPtr<ID3D12CommandAllocator>, SlotRing::kSlots> _allocators;
		std::array<ComPtr<ID3D12GraphicsCommandList>, SlotRing::kSlots> _lists;
		ComPtr<IDXGISwapChain3> _swapChain;   // Streamline's proxy
		IDXGISwapChain1* _native{ nullptr };  // what Streamline does not intercept
		std::array<ComPtr<ID3D12Resource>, 2> _buffers;

		ComPtr<ID3D11Texture2D> _backBuffer;
		std::array<Slot, SlotRing::kSlots> _slots;
		SlotRing _ring;

		FrameTokenQueue _tokens;
		sl::FrameToken* _renderToken{ nullptr };
		sl::ViewportHandle _viewport{ 0u };

		FrameInputs _inputs{};
		bool _inputsThisFrame{ false };
		bool _hudlessThisFrame{ false };
		bool _generationWanted{ false };
		bool _generationApplied{ false };
		bool _optionsSet{ false };
		bool _splitView{ false };
		std::uint32_t _reflexMode{ 0 };

		PresentStats _stats;
		std::uint32_t _dlssgStatus{ 0 };
		std::uint32_t _minWidthOrHeight{ 0 };
		bool _vsyncSupported{ false };

		std::uint64_t _presents{ 0 };
		std::uint64_t _cpuWaits{ 0 };
		std::uint64_t _blockingWaits{ 0 };
		std::uint64_t _framesWithoutToken{ 0 };
		DWORD _simulationThread{ 0 };
		std::uint32_t _loggedStatus{ UINT32_MAX };
	};
}
