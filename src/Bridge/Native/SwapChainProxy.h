#pragma once

#include "Bridge/Native/NativeSdk.h"
#include "Bridge/Native/Presenter.h"

#include <atomic>
#include <memory>

namespace Bridge::Native
{
	/// What the game holds as its IDXGISwapChain. Only that interface: asking
	/// for IDXGISwapChain1 or later gets E_NOINTERFACE rather than this object
	/// under a vtable it does not have - both templates return themselves for
	/// any interface the real swap chain knows, which crashes whoever calls a
	/// method beyond the first version. Our own Present hook (Render::
	/// InstallSwapChainHook) patches this class's vtable like any other.
	class SwapChainProxy final : public IDXGISwapChain
	{
	public:
		explicit SwapChainProxy(std::unique_ptr<Presenter> a_presenter) noexcept;

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID a_iid, void** a_object) override;
		ULONG STDMETHODCALLTYPE AddRef() override;
		ULONG STDMETHODCALLTYPE Release() override;

		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID a_name, UINT a_size, const void* a_data) override;
		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID a_name, const IUnknown* a_unknown) override;
		HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID a_name, UINT* a_size, void* a_data) override;
		HRESULT STDMETHODCALLTYPE GetParent(REFIID a_iid, void** a_parent) override;

		HRESULT STDMETHODCALLTYPE GetDevice(REFIID a_iid, void** a_device) override;

		HRESULT STDMETHODCALLTYPE Present(UINT a_syncInterval, UINT a_flags) override;
		HRESULT STDMETHODCALLTYPE GetBuffer(UINT a_buffer, REFIID a_iid, void** a_surface) override;
		HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL a_fullscreen, IDXGIOutput* a_target) override;
		HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* a_fullscreen, IDXGIOutput** a_target) override;
		HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* a_desc) override;
		HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT a_count, UINT a_width, UINT a_height, DXGI_FORMAT a_format, UINT a_flags) override;
		HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* a_parameters) override;
		HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** a_output) override;
		HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* a_statistics) override;
		HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* a_count) override;

	private:
		~SwapChainProxy();

		std::atomic<ULONG> _references{ 1 };
		std::unique_ptr<Presenter> _presenter;
	};

	/// The presenter of the swap chain the game holds now; null when none.
	[[nodiscard]] Presenter* ActivePresenter() noexcept;
}
