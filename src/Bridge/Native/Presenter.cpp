#include "Bridge/Native/Presenter.h"

#include "Bridge/Log.h"
#include "Bridge/Native/NeuralRenderer.h"
#include "Bridge/Native/Streamline.h"

#include <format>
#include <iterator>

namespace Bridge::Native
{
	namespace
	{
		constexpr std::uint64_t kLogInterval = 180;

		/// A flip-model swap chain takes no sRGB back buffer; the copy from the
		/// game's sRGB texture into a UNORM one of the same group is legal.
		DXGI_FORMAT StripSrgb(DXGI_FORMAT a_format) noexcept
		{
			switch (a_format) {
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
				return DXGI_FORMAT_R8G8B8A8_UNORM;
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
				return DXGI_FORMAT_B8G8R8A8_UNORM;
			default:
				return a_format;
			}
		}

		void Barrier(ID3D12GraphicsCommandList* a_list, ID3D12Resource* a_resource, D3D12_RESOURCE_STATES a_before, D3D12_RESOURCE_STATES a_after) noexcept
		{
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = a_resource;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.StateBefore = a_before;
			barrier.Transition.StateAfter = a_after;
			a_list->ResourceBarrier(1, &barrier);
		}

		sl::float4x4 ToStreamline(const Matrix& a_matrix) noexcept
		{
			sl::float4x4 out{};
			for (std::uint32_t row = 0; row < 4; ++row) {
				out.setRow(row, sl::float4(a_matrix[row][0], a_matrix[row][1], a_matrix[row][2], a_matrix[row][3]));
			}
			return out;
		}

		sl::float3 ToStreamline(const std::array<float, 3>& a_vector) noexcept
		{
			return sl::float3(a_vector[0], a_vector[1], a_vector[2]);
		}

		double Seconds() noexcept
		{
			LARGE_INTEGER frequency{};
			LARGE_INTEGER now{};
			QueryPerformanceFrequency(&frequency);
			QueryPerformanceCounter(&now);
			return static_cast<double>(now.QuadPart) / static_cast<double>(frequency.QuadPart);
		}
	}

	Backend& TheBackend() noexcept
	{
		static Backend backend;
		return backend;
	}

	Presenter::~Presenter()
	{
		Destroy();
	}

	bool Presenter::Fail(std::string a_reason, HRESULT a_result) noexcept
	{
		_failure = a_result == S_OK ? std::move(a_reason) : std::format("{} (0x{:08X})", a_reason, static_cast<std::uint32_t>(a_result));
		return false;
	}

	bool Presenter::Create(ID3D11Device* a_device, IDXGIAdapter* a_adapter, const DXGI_SWAP_CHAIN_DESC& a_desc) noexcept
	{
		_desc = a_desc;
		_desc.Windowed = TRUE;
		_window = a_desc.OutputWindow;

		if (FAILED(a_device->QueryInterface(IID_PPV_ARGS(&_device11)))) {
			return Fail("the D3D11 device has no ID3D11Device5 (Windows 10 1703 or newer is needed)");
		}
		ComPtr<ID3D11DeviceContext> context;
		_device11->GetImmediateContext(&context);
		if (FAILED(context.As(&_context11))) {
			return Fail("the immediate context has no ID3D11DeviceContext4");
		}
		if (!EnsureBackend(a_adapter)) {
			return false;
		}
		auto& backend = TheBackend();

		if (!CreateSharedFence(_ready) || !CreateSharedFence(_release)) {
			return false;
		}
		_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (_event == nullptr) {
			return Fail("CreateEvent");
		}

		for (std::size_t i = 0; i < SlotRing::kSlots; ++i) {
			HRESULT result = backend.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_allocators[i]));
			if (FAILED(result)) {
				return Fail("CreateCommandAllocator", result);
			}
			result = backend.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _allocators[i].Get(), nullptr, IID_PPV_ARGS(&_lists[i]));
			if (FAILED(result)) {
				return Fail("CreateCommandList", result);
			}
			_lists[i]->Close();
		}

		UINT width = a_desc.BufferDesc.Width;
		UINT height = a_desc.BufferDesc.Height;
		if (width == 0 || height == 0) {
			RECT client{};
			GetClientRect(_window, &client);
			width = static_cast<UINT>(client.right - client.left);
			height = static_cast<UINT>(client.bottom - client.top);
		}
		if (!CreateSwapChain(width, height) || !CreateTextures(width, height)) {
			return false;
		}
		_usable = true;
		SetReflexMode(1);
		return true;
	}

	bool Presenter::CreateSharedFence(SharedFence& a_fence) noexcept
	{
		HRESULT result = TheBackend().device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&a_fence.fence12));
		if (FAILED(result)) {
			return Fail("CreateFence", result);
		}
		HANDLE handle = nullptr;
		result = TheBackend().device->CreateSharedHandle(a_fence.fence12.Get(), nullptr, GENERIC_ALL, nullptr, &handle);
		if (FAILED(result)) {
			return Fail("CreateSharedHandle for a fence", result);
		}
		result = _device11->OpenSharedFence(handle, IID_PPV_ARGS(&a_fence.fence11));
		CloseHandle(handle);
		if (FAILED(result)) {
			return Fail("OpenSharedFence", result);
		}
		a_fence.value = 0;
		return true;
	}

	bool Presenter::EnsureBackend(IDXGIAdapter* a_adapter) noexcept
	{
		auto& backend = TheBackend();
		DXGI_ADAPTER_DESC description{};
		a_adapter->GetDesc(&description);
		if (backend.device && !backend.queue) {
			// Streamline is bound to that device and cannot be bound again.
			return Fail("an earlier attempt bound Streamline to a device and failed after it");
		}
		if (backend.device) {
			if (backend.adapter.LowPart != description.AdapterLuid.LowPart || backend.adapter.HighPart != description.AdapterLuid.HighPart) {
				return Fail("the game created its device again, on another adapter");
			}
			return true;
		}

		ComPtr<ID3D12Device> device;
		HRESULT result = D3D12CreateDevice(a_adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
		if (FAILED(result)) {
			return Fail("D3D12CreateDevice", result);
		}
		auto& streamline = TheStreamline();
		if (!streamline.BindDevice(device.Get())) {
			return Fail(streamline.Refusal());
		}
		// Kept from here on, whatever fails below: Streamline now holds it.
		backend.device = device;
		backend.adapter = description.AdapterLuid;

		// Through the proxy only to create the queue - the one device call
		// Streamline intercepts (sl_hooks.h, eID3D12Device_CreateCommandQueue).
		ID3D12Device* proxyDevice = device.Get();
		if (streamline.upgradeInterface(reinterpret_cast<void**>(&proxyDevice)) != sl::Result::eOk) {
			return Fail("slUpgradeInterface on the D3D12 device");
		}
		D3D12_COMMAND_QUEUE_DESC queueDesc{};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		ComPtr<ID3D12CommandQueue> queue;
		result = proxyDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue));
		if (FAILED(result)) {
			return Fail("CreateCommandQueue", result);
		}

		ComPtr<IDXGIFactory5> nativeFactory;
		result = CreateDXGIFactory2(0, IID_PPV_ARGS(&nativeFactory));
		if (FAILED(result)) {
			return Fail("CreateDXGIFactory2", result);
		}
		BOOL tearing = FALSE;
		nativeFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing));

		ComPtr<IDXGIFactory2> forProxy;
		result = CreateDXGIFactory2(0, IID_PPV_ARGS(&forProxy));
		if (FAILED(result)) {
			return Fail("CreateDXGIFactory2", result);
		}
		IDXGIFactory2* proxyFactory = forProxy.Detach();
		if (streamline.upgradeInterface(reinterpret_cast<void**>(&proxyFactory)) != sl::Result::eOk) {
			return Fail("slUpgradeInterface on the DXGI factory");
		}

		backend.nativeFactory = nativeFactory;
		backend.factory = proxyFactory;
		backend.tearing = tearing != FALSE;
		// Last: a queue is what marks the backend complete.
		backend.queue = queue;
		Log::Info(std::format("bridge: D3D12 device on {}, tearing {}", Narrow(description.Description), backend.tearing));
		return true;
	}

	bool Presenter::CreateSwapChain(UINT a_width, UINT a_height) noexcept
	{
		auto& backend = TheBackend();
		DXGI_SWAP_CHAIN_DESC1 desc{};
		desc.Width = a_width;
		desc.Height = a_height;
		desc.Format = StripSrgb(_desc.BufferDesc.Format);
		desc.SampleDesc.Count = 1;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = 2;
		desc.Scaling = DXGI_SCALING_STRETCH;
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		// No waitable object and no SetMaximumFrameLatency: Streamline paces
		// (ProgrammingGuideDLSS_G.md).
		desc.Flags = backend.tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

		ComPtr<IDXGISwapChain1> swapChain;
		HRESULT result = backend.factory->CreateSwapChainForHwnd(backend.queue.Get(), _window, &desc, nullptr, nullptr, &swapChain);
		if (FAILED(result)) {
			return Fail("CreateSwapChainForHwnd", result);
		}
		// Exclusive fullscreen is not offered, so Alt+Enter must not reach DXGI.
		backend.nativeFactory->MakeWindowAssociation(_window, DXGI_MWA_NO_ALT_ENTER);
		result = swapChain.As(&_swapChain);
		if (FAILED(result)) {
			return Fail("the swap chain has no IDXGISwapChain3", result);
		}
		if (TheStreamline().getNativeInterface(_swapChain.Get(), reinterpret_cast<void**>(&_native)) != sl::Result::eOk || _native == nullptr) {
			return Fail("slGetNativeInterface on the swap chain");
		}
		_width = a_width;
		_height = a_height;
		_swapFormat = desc.Format;
		_swapFlags = desc.Flags;
		return AcquireBuffers();
	}

	bool Presenter::AcquireBuffers() noexcept
	{
		for (UINT i = 0; i < 2; ++i) {
			// Through the proxy: GetBuffer is how Streamline learns the back buffers.
			const auto result = _swapChain->GetBuffer(i, IID_PPV_ARGS(&_buffers[i]));
			if (FAILED(result)) {
				return Fail("GetBuffer", result);
			}
		}
		return true;
	}

	void Presenter::ReleaseBuffers() noexcept
	{
		for (auto& buffer : _buffers) {
			buffer.Reset();
		}
	}

	bool Presenter::CreateShared(const D3D11_TEXTURE2D_DESC& a_desc, SharedTexture& a_texture) noexcept
	{
		auto desc = a_desc;
		desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
		HRESULT result = _device11->CreateTexture2D(&desc, nullptr, &a_texture.texture11);
		if (FAILED(result)) {
			return Fail("CreateTexture2D for a shared slot", result);
		}
		ComPtr<IDXGIResource1> resource;
		result = a_texture.texture11.As(&resource);
		if (FAILED(result)) {
			return Fail("the shared texture has no IDXGIResource1", result);
		}
		HANDLE handle = nullptr;
		result = resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &handle);
		if (FAILED(result)) {
			return Fail("CreateSharedHandle for a slot texture", result);
		}
		result = TheBackend().device->OpenSharedHandle(handle, IID_PPV_ARGS(&a_texture.resource12));
		CloseHandle(handle);
		if (FAILED(result)) {
			return Fail("OpenSharedHandle for a slot texture", result);
		}
		if ((desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0) {
			result = _device11->CreateUnorderedAccessView(a_texture.texture11.Get(), nullptr, &a_texture.uav11);
			if (FAILED(result)) {
				return Fail("CreateUnorderedAccessView for the depth slot", result);
			}
		}
		return true;
	}

	bool Presenter::CreateTextures(UINT a_width, UINT a_height) noexcept
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = a_width;
		desc.Height = a_height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = _desc.BufferDesc.Format;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		// The game's back buffer: private, never shared, so the game never
		// writes a texture D3D12 is reading (doodlum d510212).
		_backBuffer.Reset();
		HRESULT result = _device11->CreateTexture2D(&desc, nullptr, &_backBuffer);
		if (FAILED(result)) {
			return Fail("CreateTexture2D for the back buffer", result);
		}

		for (auto& slot : _slots) {
			slot = Slot{};
			if (!CreateShared(desc, slot.final) || !CreateShared(desc, slot.hudless)) {
				return false;
			}
			auto depth = desc;
			depth.Format = DXGI_FORMAT_R32_FLOAT;
			depth.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			auto motion = desc;
			motion.Format = DXGI_FORMAT_R16G16_FLOAT;
			if (!CreateShared(depth, slot.depth) || !CreateShared(motion, slot.motion)) {
				return false;
			}
		}
		_ring.Reset();
		_waitedFor = 0;
		return true;
	}

	DXGI_SWAP_CHAIN_DESC Presenter::Describe() const noexcept
	{
		auto desc = _desc;
		desc.BufferDesc.Width = _width;
		desc.BufferDesc.Height = _height;
		return desc;
	}

	bool Presenter::WaitForQueueIdle() noexcept
	{
		auto& backend = TheBackend();
		if (!backend.queue || !_release.fence12) {
			return true;
		}
		const auto value = ++_release.value;
		backend.queue->Signal(_release.fence12.Get(), value);
		if (_release.fence12->GetCompletedValue() >= value) {
			return true;
		}
		_release.fence12->SetEventOnCompletion(value, _event);
		if (WaitForSingleObject(_event, 5000) == WAIT_OBJECT_0) {
			return true;
		}
		Log::Error(std::format(
			"bridge: the D3D12 queue did not drain within 5 s (release fence at {} of {}, ready fence at {} of {})",
			_release.fence12->GetCompletedValue(), value, _ready.fence12->GetCompletedValue(), _ready.value));
		return false;
	}

	void Presenter::WaitSlotOnD3D11() noexcept
	{
		const auto value = _ring.ReleaseValue();
		if (value != 0 && value != _waitedFor) {
			// Counted when D3D12 has not got there yet: only then does the
			// D3D11 queue actually stand still on this wait.
			if (_release.fence12->GetCompletedValue() < value) {
				++_blockingWaits;
			}
			_context11->Wait(_release.fence11.Get(), value);
			_waitedFor = value;
		}
	}

	void Presenter::Mark(sl::PCLMarker a_marker, const sl::FrameToken& a_token) noexcept
	{
		TheStreamline().pclSetMarker(a_marker, a_token);
	}

	void Presenter::OnSimulationStart() noexcept
	{
		auto& streamline = TheStreamline();
		sl::FrameToken* token = nullptr;
		if (streamline.getNewFrameToken(token, nullptr) != sl::Result::eOk || token == nullptr) {
			return;
		}
		// Sleep first, then the marker: Reflex holds the frame back before its
		// simulation begins (ProgrammingGuideReflex.md).
		streamline.reflexSleep(*token);
		Mark(sl::PCLMarker::eSimulationStart, *token);
		_tokens.Push(static_cast<std::uint32_t>(*token));
		_simulationThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
	}

	sl::FrameToken* Presenter::TakeRenderToken() noexcept
	{
		if (_renderToken != nullptr) {
			return _renderToken;
		}
		const auto index = _tokens.Pop();
		if (!index) {
			return nullptr;
		}
		sl::FrameToken* token = nullptr;
		const std::uint32_t value = *index;
		if (TheStreamline().getNewFrameToken(token, &value) != sl::Result::eOk || token == nullptr) {
			return nullptr;
		}
		// The simulation of this frame is over once its rendering begins; the
		// engine shows no closer point (spec, section 5).
		Mark(sl::PCLMarker::eSimulationEnd, *token);
		Mark(sl::PCLMarker::eRenderSubmitStart, *token);
		_renderToken = token;
		return token;
	}

	void Presenter::OnRenderStart() noexcept
	{
		static_cast<void>(TakeRenderToken());
		WaitSlotOnD3D11();
	}

	bool Presenter::CurrentSlot(InputSlot& a_slot) noexcept
	{
		auto& slot = _slots[_ring.Current()];
		if (!_usable || !slot.hudless.texture11 || !slot.depth.uav11 || !slot.motion.texture11) {
			return false;
		}
		WaitSlotOnD3D11();
		a_slot.hudless = slot.hudless.texture11.Get();
		a_slot.depthUAV = slot.depth.uav11.Get();
		a_slot.motion = slot.motion.texture11.Get();
		a_slot.width = _width;
		a_slot.height = _height;
		return true;
	}

	void Presenter::SubmitFrameInputs(const FrameInputs& a_inputs) noexcept
	{
		_inputs = a_inputs;
		_inputsThisFrame = true;
	}

	void Presenter::SetReflexMode(std::uint32_t a_mode) noexcept
	{
		if (a_mode == _reflexMode) {
			return;
		}
		sl::ReflexOptions options{};
		options.mode = a_mode == 2 ? sl::ReflexMode::eLowLatencyWithBoost : sl::ReflexMode::eLowLatency;
		const auto result = TheStreamline().reflexSetOptions(options);
		Log::Info(std::format("bridge: Reflex {} (result {})", a_mode == 2 ? "low latency with boost" : "low latency", static_cast<std::uint32_t>(result)));
		_reflexMode = a_mode;
	}

	void Presenter::ApplyOptions(bool a_wanted) noexcept
	{
		if (_optionsSet && a_wanted == _generationApplied) {
			return;
		}
		sl::DLSSGOptions options{};
		options.mode = a_wanted ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
		options.numFramesToGenerate = 1;
		// Off without freeing: menus switch it off and on all the time
		// (ProgrammingGuideDLSS_G.md, eRetainResourcesWhenOff).
		options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
		const auto result = TheStreamline().dlssgSetOptions(_viewport, options);
		if (result != sl::Result::eOk) {
			Log::Error(std::format("bridge: slDLSSGSetOptions {} failed (result {})", a_wanted ? "on" : "off", static_cast<std::uint32_t>(result)));
		}
		_generationApplied = a_wanted;
		_optionsSet = true;
	}

	void Presenter::Tag(const sl::FrameToken& a_token, bool a_generate) noexcept
	{
		auto& streamline = TheStreamline();
		auto& slot = _slots[_ring.Current()];
		// COMMON: a shared texture decays to it after every submission, and a
		// texture without simultaneous access is promoted from it to the read
		// states Streamline needs.
		sl::Resource depth{ sl::ResourceType::eTex2d, slot.depth.resource12.Get(), static_cast<std::uint32_t>(D3D12_RESOURCE_STATE_COMMON) };
		sl::Resource motion{ sl::ResourceType::eTex2d, slot.motion.resource12.Get(), static_cast<std::uint32_t>(D3D12_RESOURCE_STATE_COMMON) };
		// With NR in the frame, DLSS-G interpolates between neural pictures and
		// takes the HUD from the finished one; Record leaves the picture readable.
		sl::Resource hudless = _neuralApplied ? sl::Resource{ sl::ResourceType::eTex2d, TheNeuralRenderer().Output(), static_cast<std::uint32_t>(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) } : sl::Resource{ sl::ResourceType::eTex2d, slot.hudless.resource12.Get(), static_cast<std::uint32_t>(D3D12_RESOURCE_STATE_COMMON) };
		const sl::Extent depthExtent{ 0, 0, _inputs.depthWidth, _inputs.depthHeight };
		const sl::Extent motionExtent{ 0, 0, _inputs.motionWidth, _inputs.motionHeight };
		const sl::Extent full{ 0, 0, _width, _height };
		// Null resources when nothing is generated: tags of a frame that
		// produced none must not survive into it (ProgrammingGuideDLSS_G.md).
		sl::ResourceTag tags[]{
			sl::ResourceTag{ a_generate ? &depth : nullptr, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &depthExtent },
			sl::ResourceTag{ a_generate ? &motion : nullptr, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &motionExtent },
			sl::ResourceTag{ a_generate ? &hudless : nullptr, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, &full },
		};
		streamline.setTagForFrame(a_token, _viewport, tags, static_cast<std::uint32_t>(std::size(tags)), nullptr);

		if (!a_generate) {
			return;
		}
		const auto& c = _inputs.constants;
		sl::Constants constants{};
		constants.cameraViewToClip = ToStreamline(c.cameraViewToClip);
		constants.clipToCameraView = ToStreamline(c.clipToCameraView);
		constants.clipToLensClip = ToStreamline(Matrix{ { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } } });
		constants.clipToPrevClip = ToStreamline(c.clipToPrevClip);
		constants.prevClipToClip = ToStreamline(c.prevClipToClip);
		constants.jitterOffset = sl::float2(c.jitterOffset[0], c.jitterOffset[1]);
		constants.mvecScale = sl::float2(c.mvecScale[0], c.mvecScale[1]);
		constants.cameraPinholeOffset = sl::float2(0.0f, 0.0f);
		constants.cameraPos = ToStreamline(c.cameraPos);
		constants.cameraUp = ToStreamline(c.cameraUp);
		constants.cameraRight = ToStreamline(c.cameraRight);
		constants.cameraFwd = ToStreamline(c.cameraFwd);
		constants.cameraNear = c.cameraNear;
		constants.cameraFar = c.cameraFar;
		constants.cameraFOV = c.cameraFOV;
		constants.cameraAspectRatio = c.cameraAspectRatio;
		constants.depthInverted = sl::Boolean::eFalse;
		constants.cameraMotionIncluded = sl::Boolean::eTrue;
		constants.motionVectors3D = sl::Boolean::eFalse;
		constants.reset = c.reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		constants.motionVectorsJittered = sl::Boolean::eFalse;
		streamline.setConstants(constants, a_token, _viewport);
	}

	HRESULT Presenter::Present(UINT a_syncInterval, UINT a_flags) noexcept
	{
		auto& backend = TheBackend();
		if (!_usable) {
			// ResizeBuffers logged why; nothing here to copy into or from.
			_inputsThisFrame = false;
			_hudlessThisFrame = false;
			_renderToken = nullptr;
			return E_FAIL;
		}
		auto* const token = TakeRenderToken();
		if (token == nullptr) {
			++_framesWithoutToken;
		}
		auto& slot = _slots[_ring.Current()];

		// D3D11: the finished picture into the slot, then the handover.
		WaitSlotOnD3D11();
		_context11->CopyResource(slot.final.texture11.Get(), _backBuffer.Get());
		const auto ready = ++_ready.value;
		_context11->Signal(_ready.fence11.Get(), ready);
		// Without the game's own Present nothing flushes D3D11's queue any
		// more, and D3D12 would wait for a signal still sitting in it.
		_context11->Flush();
		if (token != nullptr) {
			Mark(sl::PCLMarker::eRenderSubmitEnd, *token);
		}

		backend.queue->Wait(_ready.fence12.Get(), ready);
		if (!_ring.Reusable(_release.fence12->GetCompletedValue())) {
			// D3D12 is three frames behind: the allocator is still in use.
			_release.fence12->SetEventOnCompletion(_ring.ReleaseValue(), _event);
			WaitForSingleObject(_event, INFINITE);
			++_cpuWaits;
		}
		const auto k = _ring.Current();
		auto* const allocator = _allocators[k].Get();
		auto* const list = _lists[k].Get();
		allocator->Reset();
		list->Reset(allocator, nullptr);

		auto& neural = TheNeuralRenderer();
		neural.Collect(_release.fence12->GetCompletedValue());
		if (neural.TakeReleaseRequest()) {
			neural.Retire(_release.value + 1);
		}
		_neuralApplied = false;
		// Only with this frame's own hud-less picture and inputs: an older
		// one in the slot belongs to another frame.
		if (neural.Wanted() && _inputsThisFrame && _hudlessThisFrame) {
			const NeuralFrame frame{
				slot.final.resource12.Get(),
				slot.hudless.resource12.Get(),
				slot.depth.resource12.Get(),
				slot.motion.resource12.Get(),
				_width,
				_height,
				_inputs.motionWidth,
				_inputs.motionHeight,
				_inputs.constants.reset,
				k,
			};
			_neuralApplied = neural.Record(list, frame, _release.value + 1);
		}

		const auto index = _swapChain->GetCurrentBackBufferIndex();
		auto* const target = _buffers[index].Get();
		Barrier(list, target, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
		if (_neuralApplied) {
			list->CopyResource(target, neural.Composed());
		} else {
			list->CopyResource(target, slot.final.resource12.Get());
		}
		if (!_neuralApplied && _splitView && _hudlessThisFrame) {
			D3D12_TEXTURE_COPY_LOCATION destination{};
			destination.pResource = target;
			destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			D3D12_TEXTURE_COPY_LOCATION source{};
			source.pResource = slot.hudless.resource12.Get();
			source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			const D3D12_BOX leftHalf{ 0, 0, 0, _width / 2, _height, 1 };
			list->CopyTextureRegion(&destination, 0, 0, 0, &source, &leftHalf);
		}
		Barrier(list, target, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
		list->Close();
		ID3D12CommandList* const lists[]{ list };
		backend.queue->ExecuteCommandLists(1, lists);

		const bool inputs = _inputsThisFrame && _hudlessThisFrame;
		const bool generate = token != nullptr && _generationWanted && inputs;
		ApplyOptions(generate);
		if (token != nullptr) {
			Tag(*token, generate);
			Mark(sl::PCLMarker::ePresentStart, *token);
		}
		UINT flags = a_flags & ~static_cast<UINT>(DXGI_PRESENT_ALLOW_TEARING);
		if (a_syncInterval == 0 && backend.tearing) {
			flags |= DXGI_PRESENT_ALLOW_TEARING;
		}
		// Streamline clamps the interval to 0 or 1 (ProgrammingGuideDLSS_G.md, VSync).
		const auto result = _swapChain->Present(a_syncInterval > 1 ? 1 : a_syncInterval, flags);
		if (token != nullptr) {
			Mark(sl::PCLMarker::ePresentEnd, *token);
		}

		const auto done = ++_release.value;
		backend.queue->Signal(_release.fence12.Get(), done);
		_ring.Submit(done);

		PollState();
		++_presents;
		if (_presents % kLogInterval == 0) {
			LogSecond();
		}
		_inputsThisFrame = false;
		_hudlessThisFrame = false;
		_renderToken = nullptr;
		return result;
	}

	void Presenter::PollState() noexcept
	{
		sl::DLSSGState state{};
		if (TheStreamline().dlssgGetState(_viewport, state, nullptr) == sl::Result::eOk) {
			_dlssgStatus = static_cast<std::uint32_t>(state.status);
			_minWidthOrHeight = state.minWidthOrHeight;
			_vsyncSupported = state.bIsVsyncSupportAvailable == sl::Boolean::eTrue;
			_stats.Frame(_generationApplied, state.numFramesActuallyPresented);
		} else {
			_stats.Frame(false, 0);
		}
		static_cast<void>(_stats.Sample(Seconds()));
		if (_dlssgStatus != _loggedStatus) {
			Log::Info(std::format("bridge: DLSS-G status 0x{:X}", _dlssgStatus));
			_loggedStatus = _dlssgStatus;
		}
	}

	void Presenter::LogSecond() noexcept
	{
		const auto rates = _stats.Last();
		Log::Info(std::format(
			"bridge: rendered {:.0f}/s, shown {:.0f}/s, generation {}, cpu waits {}, d3d11 waits that blocked {} in the last {} "
			"frames, frames without token {}, tokens dropped {}, simulation thread {}, present thread {}, neural {} {:.2f} ms",
			rates.rendered, rates.shown, _generationApplied ? "on" : "off", _cpuWaits, _blockingWaits, kLogInterval,
			_framesWithoutToken, _tokens.Dropped(), _simulationThread.load(std::memory_order_relaxed), GetCurrentThreadId(),
			_neuralApplied ? "on" : "off", TheNeuralRenderer().GpuMilliseconds()));
		_blockingWaits = 0;
	}

	Status Presenter::CurrentStatus() const noexcept
	{
		const auto rates = _stats.Last();
		return Status{
			_generationApplied, rates.rendered, rates.shown, _dlssgStatus, _minWidthOrHeight, _width, _height, _vsyncSupported,
			_neuralApplied, TheNeuralRenderer().GpuMilliseconds()
		};
	}

	HRESULT Presenter::ResizeBuffers(UINT a_count, UINT a_width, UINT a_height, DXGI_FORMAT a_format, UINT a_flags) noexcept
	{
		static_cast<void>(a_count);
		static_cast<void>(a_flags);
		UINT width = a_width;
		UINT height = a_height;
		if (width == 0 || height == 0) {
			RECT client{};
			GetClientRect(_window, &client);
			width = static_cast<UINT>(client.right - client.left);
			height = static_cast<UINT>(client.bottom - client.top);
		}
		const auto format = a_format == DXGI_FORMAT_UNKNOWN ? _desc.BufferDesc.Format : a_format;
		// Minimised: a window of size zero. Nothing to resize to, and textures
		// of size zero cannot exist; the next real size comes with the restore.
		if (width == 0 || height == 0) {
			return S_OK;
		}
		if (_usable && width == _width && height == _height && format == _desc.BufferDesc.Format) {
			return S_OK;
		}

		// Off before the resize, or Streamline's pacer may deadlock on it
		// (ProgrammingGuideDLSS_G.md).
		ApplyOptions(false);
		if (!WaitForQueueIdle()) {
			// Its buffers may still be in flight; resizing now would free them
			// under the queue.
			return DXGI_ERROR_WAS_STILL_DRAWING;
		}
		// The queue is idle: NR's feature and textures can go now and come
		// back at the new size with the next Record.
		TheNeuralRenderer().Retire(0);
		TheNeuralRenderer().Collect(UINT64_MAX);
		TheNeuralRenderer().ForgetFailures();
		_neuralApplied = false;
		ReleaseBuffers();
		const auto result = _swapChain->ResizeBuffers(2, width, height, StripSrgb(format), _swapFlags);
		if (FAILED(result)) {
			Log::Error(std::format("bridge: ResizeBuffers to {}x{} failed (0x{:08X})", width, height, static_cast<std::uint32_t>(result)));
			if (!AcquireBuffers()) {
				_usable = false;
				Log::Error(std::format("bridge: presenting stops until the next resize: {}", _failure));
			}
			return result;
		}
		_desc.BufferDesc.Format = format;
		_width = width;
		_height = height;
		_swapFormat = StripSrgb(format);
		if (!AcquireBuffers() || !CreateTextures(width, height)) {
			_usable = false;
			Log::Error(std::format("bridge: after the resize to {}x{}, presenting stops until the next resize: {}", width, height, _failure));
			return E_FAIL;
		}
		_usable = true;
		Log::Info(std::format("bridge: resized to {}x{}", width, height));
		return S_OK;
	}

	void Presenter::Destroy() noexcept
	{
		if (_optionsSet) {
			ApplyOptions(false);
		}
		// At the end of the process there is nobody to refuse to; the timeout
		// is logged, and what follows is all that is left to do.
		if (WaitForQueueIdle()) {
			TheNeuralRenderer().Retire(0);
			TheNeuralRenderer().Collect(UINT64_MAX);
		}
		_neuralApplied = false;
		_usable = false;
		ReleaseBuffers();
		_swapChain.Reset();
		_native = nullptr;
		for (auto& slot : _slots) {
			slot = Slot{};
		}
		_backBuffer.Reset();
		for (auto& list : _lists) {
			list.Reset();
		}
		for (auto& allocator : _allocators) {
			allocator.Reset();
		}
		_ready = SharedFence{};
		_release = SharedFence{};
		if (_event != nullptr) {
			CloseHandle(_event);
			_event = nullptr;
		}
		_context11.Reset();
		_device11.Reset();
	}
}
