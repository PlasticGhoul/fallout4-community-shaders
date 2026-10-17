#include "Bridge/Native/SwapChainProxy.h"

#include "Bridge/Log.h"

namespace Bridge::Native
{
	namespace
	{
		std::atomic<Presenter*> g_active{ nullptr };
	}

	Presenter* ActivePresenter() noexcept
	{
		return g_active.load(std::memory_order_acquire);
	}

	SwapChainProxy::SwapChainProxy(std::unique_ptr<Presenter> a_presenter) noexcept :
		_presenter(std::move(a_presenter))
	{
		g_active.store(_presenter.get(), std::memory_order_release);
	}

	SwapChainProxy::~SwapChainProxy()
	{
		g_active.store(nullptr, std::memory_order_release);
		_presenter->Destroy();
		Log::Info("bridge: the game released its swap chain");
	}

	HRESULT SwapChainProxy::QueryInterface(REFIID a_iid, void** a_object)
	{
		if (a_object == nullptr) {
			return E_POINTER;
		}
		if (a_iid == __uuidof(IUnknown) || a_iid == __uuidof(IDXGIObject) || a_iid == __uuidof(IDXGIDeviceSubObject) ||
			a_iid == __uuidof(IDXGISwapChain)) {
			*a_object = static_cast<IDXGISwapChain*>(this);
			AddRef();
			return S_OK;
		}
		*a_object = nullptr;
		return E_NOINTERFACE;
	}

	ULONG SwapChainProxy::AddRef()
	{
		return ++_references;
	}

	ULONG SwapChainProxy::Release()
	{
		const auto left = --_references;
		if (left == 0) {
			delete this;
		}
		return left;
	}

	HRESULT SwapChainProxy::SetPrivateData(REFGUID a_name, UINT a_size, const void* a_data)
	{
		return _presenter->Native()->SetPrivateData(a_name, a_size, a_data);
	}

	HRESULT SwapChainProxy::SetPrivateDataInterface(REFGUID a_name, const IUnknown* a_unknown)
	{
		return _presenter->Native()->SetPrivateDataInterface(a_name, a_unknown);
	}

	HRESULT SwapChainProxy::GetPrivateData(REFGUID a_name, UINT* a_size, void* a_data)
	{
		return _presenter->Native()->GetPrivateData(a_name, a_size, a_data);
	}

	HRESULT SwapChainProxy::GetParent(REFIID a_iid, void** a_parent)
	{
		return _presenter->Native()->GetParent(a_iid, a_parent);
	}

	HRESULT SwapChainProxy::GetDevice(REFIID a_iid, void** a_device)
	{
		// The D3D11 device for every version of ID3D11Device and for
		// IDXGIDevice alike; QueryInterface adds the reference the caller owns.
		return _presenter->Device11()->QueryInterface(a_iid, a_device);
	}

	HRESULT SwapChainProxy::Present(UINT a_syncInterval, UINT a_flags)
	{
		return _presenter->Present(a_syncInterval, a_flags);
	}

	HRESULT SwapChainProxy::GetBuffer(UINT a_buffer, REFIID a_iid, void** a_surface)
	{
		if (a_buffer != 0) {
			return DXGI_ERROR_INVALID_CALL;
		}
		return _presenter->BackBuffer()->QueryInterface(a_iid, a_surface);
	}

	HRESULT SwapChainProxy::SetFullscreenState(BOOL a_fullscreen, IDXGIOutput*)
	{
		return a_fullscreen != FALSE ? DXGI_ERROR_NOT_CURRENTLY_AVAILABLE : S_OK;
	}

	HRESULT SwapChainProxy::GetFullscreenState(BOOL* a_fullscreen, IDXGIOutput** a_target)
	{
		if (a_fullscreen != nullptr) {
			*a_fullscreen = FALSE;
		}
		if (a_target != nullptr) {
			*a_target = nullptr;
		}
		return S_OK;
	}

	HRESULT SwapChainProxy::GetDesc(DXGI_SWAP_CHAIN_DESC* a_desc)
	{
		if (a_desc == nullptr) {
			return E_INVALIDARG;
		}
		*a_desc = _presenter->Describe();
		return S_OK;
	}

	HRESULT SwapChainProxy::ResizeBuffers(UINT a_count, UINT a_width, UINT a_height, DXGI_FORMAT a_format, UINT a_flags)
	{
		return _presenter->ResizeBuffers(a_count, a_width, a_height, a_format, a_flags);
	}

	HRESULT SwapChainProxy::ResizeTarget(const DXGI_MODE_DESC* a_parameters)
	{
		return _presenter->Native()->ResizeTarget(a_parameters);
	}

	HRESULT SwapChainProxy::GetContainingOutput(IDXGIOutput** a_output)
	{
		return _presenter->Native()->GetContainingOutput(a_output);
	}

	HRESULT SwapChainProxy::GetFrameStatistics(DXGI_FRAME_STATISTICS* a_statistics)
	{
		return _presenter->Native()->GetFrameStatistics(a_statistics);
	}

	HRESULT SwapChainProxy::GetLastPresentCount(UINT* a_count)
	{
		return _presenter->Native()->GetLastPresentCount(a_count);
	}
}
