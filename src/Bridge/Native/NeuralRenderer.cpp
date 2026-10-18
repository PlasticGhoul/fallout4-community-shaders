#include "Bridge/Native/NeuralRenderer.h"

#include "Bridge/Log.h"
#include "Bridge/Native/Presenter.h"
#include "Bridge/Native/Streamline.h"

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs_dlssnr.h>

#include <format>

namespace Bridge::Native
{
	namespace
	{
		/// The plugin's own NGX project (F5.5a); NVIDIA asks engines without an
		/// NVIDIA-issued id for one of their own and ENGINE_TYPE_CUSTOM.
		constexpr const char* kProjectId = "99023707-327d-45dc-ba45-0c47e8445542";
		constexpr const char* kEngineVersion = "Fallout 4 1.11.240";
		constexpr std::uint32_t kMeanOver = 180;
		constexpr UINT kDescriptorsPerSlot = 4;

		std::string Code(NVSDK_NGX_Result a_result)
		{
			return std::format("0x{:08X}", static_cast<std::uint32_t>(a_result));
		}

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

		ComPtr<ID3D12Resource> CreateTexture(ID3D12Device* a_device, DXGI_FORMAT a_format, UINT a_width, UINT a_height) noexcept
		{
			D3D12_HEAP_PROPERTIES heap{};
			heap.Type = D3D12_HEAP_TYPE_DEFAULT;
			D3D12_RESOURCE_DESC desc{};
			desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			desc.Width = a_width;
			desc.Height = a_height;
			desc.DepthOrArraySize = 1;
			desc.MipLevels = 1;
			desc.Format = a_format;
			desc.SampleDesc.Count = 1;
			desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			ComPtr<ID3D12Resource> texture;
			if (FAILED(a_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&texture)))) {
				return nullptr;
			}
			return texture;
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
	}

	NeuralRenderer& TheNeuralRenderer() noexcept
	{
		static NeuralRenderer renderer;
		return renderer;
	}

	std::string NeuralRenderer::Fail(std::string a_reason) noexcept
	{
		_startResult = std::move(a_reason);
		return _startResult;
	}

	std::string NeuralRenderer::Start(const std::filesystem::path& a_dllDirectory, std::span<const std::uint8_t> a_composite) noexcept
	{
		if (_startTried) {
			return _startResult;
		}
		_startTried = true;

		auto& backend = TheBackend();
		if (!backend.device || !backend.queue) {
			return Fail("the bridge has no D3D12 device");
		}
		if (!std::filesystem::exists(a_dllDirectory / L"nvngx_dlssnr.dll")) {
			return Fail(std::format("nvngx_dlssnr.dll not found in {} - is the NeuralRendering archive installed?", Narrow(a_dllDirectory.wstring())));
		}

		const std::wstring directory = a_dllDirectory.wstring();
		const wchar_t* paths[]{ directory.c_str() };
		NVSDK_NGX_FeatureCommonInfo info{};
		info.PathListInfo.Path = paths;
		info.PathListInfo.Length = 1;
		const auto init = NVSDK_NGX_D3D12_Init_with_ProjectID(
			kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, kEngineVersion, directory.c_str(), backend.device.Get(), &info, NVSDK_NGX_Version_API);
		if (NVSDK_NGX_FAILED(init)) {
			return Fail(std::format("NGX did not start on D3D12 ({})", Code(init)));
		}
		const auto capability = NVSDK_NGX_D3D12_GetCapabilityParameters(&_parameters);
		if (NVSDK_NGX_FAILED(capability) || _parameters == nullptr) {
			return Fail(std::format("NGX capability parameters not readable on D3D12 ({})", Code(capability)));
		}
		int needsDriver = 0;
		unsigned int minMajor = 0;
		unsigned int minMinor = 0;
		int available = 0;
		int featureInit = 0;
		NVSDK_NGX_Parameter_GetI(_parameters, NVSDK_NGX_Parameter_DLSSNR_NeedsUpdatedDriver, &needsDriver);
		NVSDK_NGX_Parameter_GetUI(_parameters, NVSDK_NGX_Parameter_DLSSNR_MinDriverVersionMajor, &minMajor);
		NVSDK_NGX_Parameter_GetUI(_parameters, NVSDK_NGX_Parameter_DLSSNR_MinDriverVersionMinor, &minMinor);
		NVSDK_NGX_Parameter_GetI(_parameters, NVSDK_NGX_Parameter_DLSSNR_Available, &available);
		NVSDK_NGX_Parameter_GetI(_parameters, NVSDK_NGX_Parameter_DLSSNR_FeatureInitResult, &featureInit);
		if (needsDriver != 0) {
			return Fail(std::format("DLSS Neural Rendering needs driver {}.{} or newer", minMajor, minMinor));
		}
		if (available == 0) {
			return Fail(std::format("DLSS Neural Rendering is not available on this GPU (FeatureInitResult 0x{:08X})", static_cast<std::uint32_t>(featureInit)));
		}

		if (!CreatePipeline(a_composite)) {
			return _startResult;
		}
		auto* const device = backend.device.Get();
		if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_createAllocator))) ||
			FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _createAllocator.Get(), nullptr, IID_PPV_ARGS(&_createList))) ||
			FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_createFence)))) {
			return Fail("the list for creating the NR feature could not be created");
		}
		_createList->Close();
		_createEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (_createEvent == nullptr) {
			return Fail("CreateEvent for the NR feature failed");
		}

		D3D12_QUERY_HEAP_DESC queryDesc{};
		queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		queryDesc.Count = 2 * SlotRing::kSlots;
		D3D12_HEAP_PROPERTIES readbackHeap{};
		readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
		D3D12_RESOURCE_DESC readbackDesc{};
		readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		readbackDesc.Width = sizeof(std::uint64_t) * 2 * SlotRing::kSlots;
		readbackDesc.Height = 1;
		readbackDesc.DepthOrArraySize = 1;
		readbackDesc.MipLevels = 1;
		readbackDesc.SampleDesc.Count = 1;
		readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		void* mapped = nullptr;
		if (FAILED(device->CreateQueryHeap(&queryDesc, IID_PPV_ARGS(&_timestamps))) ||
			FAILED(device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&_timestampReadback))) ||
			FAILED(_timestampReadback->Map(0, nullptr, &mapped)) || FAILED(backend.queue->GetTimestampFrequency(&_timestampFrequency))) {
			return Fail("the NR timestamps could not be set up");
		}
		// A readback buffer may stay mapped; it is read only after the slot's
		// release fence has passed.
		_timestampData = static_cast<const std::uint64_t*>(mapped);

		Log::Info(std::format("neural rendering: NGX up on D3D12, DLSS-NR available (snippet from {})", Narrow(directory)));
		_started.store(true, std::memory_order_release);
		return Fail({});
	}

	bool NeuralRenderer::CreatePipeline(std::span<const std::uint8_t> a_composite) noexcept
	{
		auto* const device = TheBackend().device.Get();
		D3D12_DESCRIPTOR_RANGE ranges[2]{};
		ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[0].NumDescriptors = 3;
		ranges[0].BaseShaderRegister = 0;
		ranges[0].OffsetInDescriptorsFromTableStart = 0;
		ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		ranges[1].NumDescriptors = 1;
		ranges[1].BaseShaderRegister = 0;
		ranges[1].OffsetInDescriptorsFromTableStart = 3;
		D3D12_ROOT_PARAMETER parameters[2]{};
		parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		parameters[0].Constants.ShaderRegister = 0;
		parameters[0].Constants.Num32BitValues = 4;
		parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		parameters[1].DescriptorTable.NumDescriptorRanges = 2;
		parameters[1].DescriptorTable.pDescriptorRanges = ranges;
		parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		D3D12_ROOT_SIGNATURE_DESC rootDesc{};
		rootDesc.NumParameters = 2;
		rootDesc.pParameters = parameters;

		ComPtr<ID3DBlob> serialized;
		ComPtr<ID3DBlob> errors;
		HRESULT result = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
		if (FAILED(result)) {
			static_cast<void>(Fail(std::format("the NR composite root signature did not serialise (0x{:08X})", static_cast<std::uint32_t>(result))));
			return false;
		}
		result = device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&_rootSignature));
		if (FAILED(result)) {
			static_cast<void>(Fail(std::format("CreateRootSignature for the NR composite failed (0x{:08X})", static_cast<std::uint32_t>(result))));
			return false;
		}
		D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
		pipelineDesc.pRootSignature = _rootSignature.Get();
		pipelineDesc.CS.pShaderBytecode = a_composite.data();
		pipelineDesc.CS.BytecodeLength = a_composite.size();
		result = device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&_pipeline));
		if (FAILED(result)) {
			static_cast<void>(Fail(std::format("CreateComputePipelineState for the NR composite failed (0x{:08X})", static_cast<std::uint32_t>(result))));
			return false;
		}
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.NumDescriptors = kDescriptorsPerSlot * SlotRing::kSlots;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		result = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_descriptors));
		if (FAILED(result)) {
			static_cast<void>(Fail(std::format("CreateDescriptorHeap for the NR composite failed (0x{:08X})", static_cast<std::uint32_t>(result))));
			return false;
		}
		_descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		return true;
	}

	void NeuralRenderer::SetOptions(const NeuralOptions& a_options, bool a_wanted) noexcept
	{
		const std::scoped_lock lock{ _optionsLock };
		_options = a_options;
		_wanted = a_wanted;
	}

	bool NeuralRenderer::Wanted() const noexcept
	{
		const std::scoped_lock lock{ _optionsLock };
		return _wanted && Started();
	}

	void NeuralRenderer::Transition(ID3D12GraphicsCommandList* a_list, ID3D12Resource* a_resource, D3D12_RESOURCE_STATES& a_state, D3D12_RESOURCE_STATES a_target) noexcept
	{
		if (a_state != a_target) {
			Barrier(a_list, a_resource, a_state, a_target);
			a_state = a_target;
		}
	}

	bool NeuralRenderer::EnsureFeature(const NeuralFrame& a_frame, std::uint32_t a_preset, std::uint64_t a_doneValue) noexcept
	{
		const auto format = StripSrgb(a_frame.hudless->GetDesc().Format);
		if (_feature != nullptr && _width == a_frame.width && _height == a_frame.height && _preset == a_preset && _format == format) {
			return true;
		}
		// A failed creation is not retried every frame (it stalls the CPU on
		// the queue each time), and after a timeout the create allocator stays
		// untouched until its list has run.
		const NeuralFeatureKey key{ a_frame.width, a_frame.height, a_preset, static_cast<std::uint32_t>(format) };
		if (!_gate.MayCreate(key, _createFence->GetCompletedValue())) {
			return false;
		}
		Retire(a_doneValue);

		auto& backend = TheBackend();
		auto* const device = backend.device.Get();
		_output = CreateTexture(device, format, a_frame.width, a_frame.height);
		_composed = CreateTexture(device, format, a_frame.width, a_frame.height);
		if (!_output || !_composed) {
			Log::Error(std::format("neural rendering: the {}x{} textures could not be created", a_frame.width, a_frame.height));
			_output.Reset();
			_composed.Reset();
			_gate.Failed(key);
			return false;
		}
		_outputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		_composedState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

		NVSDK_NGX_Parameter_SetUI(_parameters, NVSDK_NGX_Parameter_DLSSNR_Width, a_frame.width);
		NVSDK_NGX_Parameter_SetUI(_parameters, NVSDK_NGX_Parameter_DLSSNR_Height, a_frame.height);
		NVSDK_NGX_Parameter_SetUI(_parameters, NVSDK_NGX_Parameter_DLSSNR_Hint_Render_Preset, a_preset);
		_createAllocator->Reset();
		_createList->Reset(_createAllocator.Get(), nullptr);
		NVSDK_NGX_Handle* feature = nullptr;
		const auto created = NVSDK_NGX_D3D12_CreateFeature(_createList.Get(), NVSDK_NGX_Feature_DLSSNR, _parameters, &feature);
		_createList->Close();
		ID3D12CommandList* const lists[]{ _createList.Get() };
		backend.queue->ExecuteCommandLists(1, lists);
		const auto value = ++_createValue;
		backend.queue->Signal(_createFence.Get(), value);
		_gate.Submitted(value);
		if (_createFence->GetCompletedValue() < value) {
			_createFence->SetEventOnCompletion(value, _createEvent);
			if (WaitForSingleObject(_createEvent, 5000) != WAIT_OBJECT_0) {
				Log::Error("neural rendering: creating the feature did not finish within 5 s");
				if (feature != nullptr) {
					_retired.push_back(Retired{ UINT64_MAX, feature, nullptr, nullptr });
				}
				_output.Reset();
				_composed.Reset();
				_gate.Failed(key);
				return false;
			}
		}
		if (NVSDK_NGX_FAILED(created) || feature == nullptr) {
			Log::Error(std::format("neural rendering: the feature for {}x{}, preset {} could not be created ({})", a_frame.width, a_frame.height, a_preset, Code(created)));
			_output.Reset();
			_composed.Reset();
			_gate.Failed(key);
			return false;
		}
		_gate.Succeeded();
		_feature = feature;
		_width = a_frame.width;
		_height = a_frame.height;
		_preset = a_preset;
		_format = format;
		_lastFailure = 0;

		unsigned long long bytes = 0;
		void* stats = nullptr;
		NVSDK_NGX_Parameter_GetVoidPointer(_parameters, NVSDK_NGX_Parameter_DLSSNRGetStatsCallback, &stats);
		if (stats != nullptr && NVSDK_NGX_SUCCEED(reinterpret_cast<PFN_NVSDK_NGX_DLSSNR_GetStatsCallback>(stats)(_parameters))) {
			NVSDK_NGX_Parameter_GetULL(_parameters, NVSDK_NGX_Parameter_SizeInBytes, &bytes);
		}
		Log::Info(std::format("neural rendering: feature created, {}x{}, preset {}, {:.0f} MB", _width, _height, _preset, static_cast<double>(bytes) / 1048576.0));
		return true;
	}

	void NeuralRenderer::ReadTimestamps(std::size_t a_slot) noexcept
	{
		if (!_timestampPending[a_slot] || _timestampData == nullptr || _timestampFrequency == 0) {
			return;
		}
		_timestampPending[a_slot] = false;
		const auto begin = _timestampData[2 * a_slot];
		const auto end = _timestampData[2 * a_slot + 1];
		if (end <= begin) {
			return;
		}
		_sumMs += static_cast<double>(end - begin) * 1000.0 / static_cast<double>(_timestampFrequency);
		if (++_samples == kMeanOver) {
			_meanMs.store(_sumMs / kMeanOver, std::memory_order_relaxed);
			_sumMs = 0.0;
			_samples = 0;
		}
	}

	bool NeuralRenderer::Record(ID3D12GraphicsCommandList* a_list, const NeuralFrame& a_frame, std::uint64_t a_doneValue) noexcept
	{
		NeuralOptions options{};
		{
			const std::scoped_lock lock{ _optionsLock };
			if (!_wanted) {
				return false;
			}
			options = _options;
		}
		if (!Started() || a_frame.final == nullptr || a_frame.hudless == nullptr || a_frame.depth == nullptr || a_frame.motion == nullptr ||
			a_frame.slot >= SlotRing::kSlots || !EnsureFeature(a_frame, options.preset, a_doneValue)) {
			return false;
		}
		// The slot is reusable only once its release fence passed, so its
		// timestamps from the round before are complete.
		ReadTimestamps(a_frame.slot);

		ID3D12Resource* const inputs[]{ a_frame.final, a_frame.hudless, a_frame.depth, a_frame.motion };
		for (auto* const input : inputs) {
			Barrier(a_list, input, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		}
		Transition(a_list, _output.Get(), _outputState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		Transition(a_list, _composed.Get(), _composedState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		const auto first = static_cast<UINT>(2 * a_frame.slot);
		a_list->EndQuery(_timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, first);

		auto* const p = _parameters;
		NVSDK_NGX_Parameter_SetD3d12Resource(p, NVSDK_NGX_Parameter_DLSSNR_Color, a_frame.hudless);
		NVSDK_NGX_Parameter_SetD3d12Resource(p, NVSDK_NGX_Parameter_DLSSNR_Output, _output.Get());
		NVSDK_NGX_Parameter_SetD3d12Resource(p, NVSDK_NGX_Parameter_DLSSNR_MVec, a_frame.motion);
		NVSDK_NGX_Parameter_SetD3d12Resource(p, NVSDK_NGX_Parameter_DLSSNR_Depth, a_frame.depth);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSSNR_ColorSubrectBaseX, 0);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSSNR_ColorSubrectBaseY, 0);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSSNR_ColorSubrectWidth, a_frame.width);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSSNR_ColorSubrectHeight, a_frame.height);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSSNR_OutputSubrectBaseX, 0);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSSNR_OutputSubrectBaseY, 0);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSSNR_OutputSubrectWidth, a_frame.width);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSSNR_OutputSubrectHeight, a_frame.height);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSSNR_MVecSubrectBaseX, 0);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSSNR_MVecSubrectBaseY, 0);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSSNR_MVecSubrectWidth, a_frame.motionWidth);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSSNR_MVecSubrectHeight, a_frame.motionHeight);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSSNR_DepthSubrectBaseX, 0);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSSNR_DepthSubrectBaseY, 0);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSSNR_DepthSubrectWidth, a_frame.motionWidth);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSSNR_DepthSubrectHeight, a_frame.motionHeight);
		// The engine's vectors are in uv units; NGX wants pixels of the
		// vectors' own size - the scale DLSS SR uses (F5.5a, DLSS.cpp).
		NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_Parameter_DLSSNR_MVecScaleX, static_cast<float>(a_frame.motionWidth));
		NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_Parameter_DLSSNR_MVecScaleY, static_cast<float>(a_frame.motionHeight));
		NVSDK_NGX_Parameter_SetI(p, NVSDK_NGX_Parameter_DLSSNR_DepthInverted, 0);
		NVSDK_NGX_Parameter_SetI(p, NVSDK_NGX_Parameter_DLSSNR_Reset, a_frame.reset ? 1 : 0);
		NVSDK_NGX_Parameter_SetI(p, NVSDK_NGX_Parameter_DLSSNR_Enabled, 1);
		NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_Parameter_DLSSNR_Intensity, options.intensity);
		NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_Parameter_DLSSNR_LocalToneStrength, options.tone);
		NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_Parameter_DLSSNR_LocalStructureStrength, options.structure);
		NVSDK_NGX_Parameter_SetF(p, NVSDK_NGX_Parameter_DLSSNR_SkinStructureStrength, options.skin);
		NVSDK_NGX_Parameter_SetUI(p, NVSDK_NGX_Parameter_DLSSNR_Style, options.style);
		NVSDK_NGX_Parameter_SetI(p, NVSDK_NGX_Parameter_DLSSNR_UseAutoMask, options.autoMask ? 1 : 0);
		NVSDK_NGX_Parameter_SetI(p, NVSDK_NGX_Parameter_DLSSNR_MaskBlendMode, static_cast<int>(options.maskBlendMode));
		const auto evaluated = NVSDK_NGX_D3D12_EvaluateFeature_C(a_list, _feature, p, nullptr);

		const bool ok = NVSDK_NGX_SUCCEED(evaluated);
		if (ok) {
			// NGX sets its own heaps and root signature; ours go back on.
			Transition(a_list, _output.Get(), _outputState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			const auto base = static_cast<UINT>(a_frame.slot) * kDescriptorsPerSlot;
			auto cpu = _descriptors->GetCPUDescriptorHandleForHeapStart();
			cpu.ptr += static_cast<SIZE_T>(base) * _descriptorSize;
			auto* const device = TheBackend().device.Get();
			ID3D12Resource* const sources[]{ a_frame.final, a_frame.hudless, _output.Get() };
			for (auto* const source : sources) {
				D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
				srv.Format = source->GetDesc().Format;
				srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				srv.Texture2D.MipLevels = 1;
				device->CreateShaderResourceView(source, &srv, cpu);
				cpu.ptr += _descriptorSize;
			}
			D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
			uav.Format = _format;
			uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			device->CreateUnorderedAccessView(_composed.Get(), nullptr, &uav, cpu);

			ID3D12DescriptorHeap* const heaps[]{ _descriptors.Get() };
			a_list->SetDescriptorHeaps(1, heaps);
			a_list->SetComputeRootSignature(_rootSignature.Get());
			const UINT constants[4]{ a_frame.width, a_frame.height, static_cast<UINT>(options.view), 0 };
			a_list->SetComputeRoot32BitConstants(0, 4, constants, 0);
			auto gpu = _descriptors->GetGPUDescriptorHandleForHeapStart();
			gpu.ptr += static_cast<UINT64>(base) * _descriptorSize;
			a_list->SetComputeRootDescriptorTable(1, gpu);
			a_list->SetPipelineState(_pipeline.Get());
			a_list->Dispatch((a_frame.width + 7) / 8, (a_frame.height + 7) / 8, 1);
			Transition(a_list, _composed.Get(), _composedState, D3D12_RESOURCE_STATE_COPY_SOURCE);

			a_list->EndQuery(_timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, first + 1);
			a_list->ResolveQueryData(_timestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, first, 2, _timestampReadback.Get(), sizeof(std::uint64_t) * first);
			_timestampPending[a_frame.slot] = true;
		} else if (static_cast<std::uint32_t>(evaluated) != _lastFailure) {
			_lastFailure = static_cast<std::uint32_t>(evaluated);
			Log::Error(std::format("neural rendering: evaluate failed ({}); presenting without it", Code(evaluated)));
		}

		for (auto* const input : inputs) {
			Barrier(a_list, input, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
		}
		return ok;
	}

	void NeuralRenderer::Retire(std::uint64_t a_doneValue) noexcept
	{
		if (_feature == nullptr && !_output && !_composed) {
			return;
		}
		if (_feature != nullptr) {
			Log::Info(std::format("neural rendering: feature for {}x{}, preset {} released", _width, _height, _preset));
		}
		_retired.push_back(Retired{ a_doneValue, _feature, std::move(_output), std::move(_composed) });
		_feature = nullptr;
		_output.Reset();
		_composed.Reset();
		_width = 0;
		_height = 0;
	}

	void NeuralRenderer::Collect(std::uint64_t a_completedValue) noexcept
	{
		std::erase_if(_retired, [&](Retired& a_retired) {
			if (a_retired.value > a_completedValue) {
				return false;
			}
			if (a_retired.feature != nullptr) {
				NVSDK_NGX_D3D12_ReleaseFeature(a_retired.feature);
			}
			return true;
		});
	}
}
