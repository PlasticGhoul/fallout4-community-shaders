#pragma once

#include "Bridge/Native/NativeSdk.h"
#include "Bridge/NeuralCreation.h"
#include "Bridge/NeuralDecision.h"
#include "Bridge/SlotRing.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <vector>

struct NVSDK_NGX_Parameter;
struct NVSDK_NGX_Handle;

namespace Bridge::Native
{
	/// One frame's inputs as they sit in the slot, all in COMMON: a shared
	/// texture decays to it after every submission.
	struct NeuralFrame
	{
		ID3D12Resource* final{ nullptr };
		ID3D12Resource* hudless{ nullptr };
		ID3D12Resource* depth{ nullptr };   // R32_FLOAT
		ID3D12Resource* motion{ nullptr };  // R16G16_FLOAT, uv units
		UINT width{ 0 };                    // display
		UINT height{ 0 };
		UINT motionWidth{ 0 };  // depth and motion
		UINT motionHeight{ 0 };
		bool reset{ false };
		std::size_t slot{ 0 };  // ring index: descriptors and timestamps
	};

	/// DLSS Neural Rendering (NGX feature 18) on the bridge's D3D12 device,
	/// and the composite that puts its picture under the HUD. NGX is started
	/// once and stays up until the process ends; feature and textures follow
	/// the picture's size and the chosen model. Everything but Start and
	/// SetOptions is called from the presenting thread.
	class NeuralRenderer
	{
	public:
		NeuralRenderer() = default;
		NeuralRenderer(const NeuralRenderer&) = delete;
		NeuralRenderer& operator=(const NeuralRenderer&) = delete;

		/// NGX on the bridge's D3D12 device and the composite pipeline; empty
		/// on success, else why not. A second call returns the first answer.
		[[nodiscard]] std::string Start(const std::filesystem::path& a_dllDirectory, std::span<const std::uint8_t> a_composite) noexcept;
		[[nodiscard]] bool Started() const noexcept { return _started.load(std::memory_order_acquire); }

		void SetOptions(const NeuralOptions& a_options, bool a_wanted) noexcept;
		[[nodiscard]] bool Wanted() const noexcept;

		/// Records evaluate and composite into a_list. True: Composed() holds
		/// the picture to present, in COPY_SOURCE. False: nothing usable was
		/// recorded, present the finished picture as without NR.
		[[nodiscard]] bool Record(ID3D12GraphicsCommandList* a_list, const NeuralFrame& a_frame, std::uint64_t a_doneValue) noexcept;
		[[nodiscard]] ID3D12Resource* Composed() const noexcept { return _composed.Get(); }
		/// The neural picture, for DLSS-G's hud-less tag; left in
		/// NON_PIXEL_SHADER_RESOURCE after a Record that returned true.
		[[nodiscard]] ID3D12Resource* Output() const noexcept { return _output.Get(); }

		/// Drops feature and textures; they are freed once the release fence
		/// passes a_doneValue. The next Record creates them anew.
		void Retire(std::uint64_t a_doneValue) noexcept;
		/// Frees what the GPU is past. Every Present.
		void Collect(std::uint64_t a_completedValue) noexcept;
		/// After a resize: a creation that failed may be tried again.
		void ForgetFailures() noexcept { _gate.Forget(); }

		/// From the feature's Shutdown: the next Present retires feature and
		/// textures. Not tied to Wanted(): that is false on every menu frame,
		/// and recreating costs about 0.4 s.
		void RequestRelease() noexcept { _releaseRequested.store(true, std::memory_order_release); }
		[[nodiscard]] bool TakeReleaseRequest() noexcept { return _releaseRequested.exchange(false, std::memory_order_acq_rel); }

		/// Mean GPU time of evaluate plus composite over the last 180 Records.
		[[nodiscard]] double GpuMilliseconds() const noexcept { return _meanMs.load(std::memory_order_relaxed); }

	private:
		struct Retired
		{
			std::uint64_t value{ 0 };
			NVSDK_NGX_Handle* feature{ nullptr };
			ComPtr<ID3D12Resource> output;
			ComPtr<ID3D12Resource> composed;
		};

		[[nodiscard]] std::string Fail(std::string a_reason) noexcept;
		[[nodiscard]] bool CreatePipeline(std::span<const std::uint8_t> a_composite) noexcept;
		[[nodiscard]] bool EnsureFeature(const NeuralFrame& a_frame, std::uint32_t a_preset, std::uint64_t a_doneValue) noexcept;
		void ReadTimestamps(std::size_t a_slot) noexcept;
		void Transition(ID3D12GraphicsCommandList* a_list, ID3D12Resource* a_resource, D3D12_RESOURCE_STATES& a_state, D3D12_RESOURCE_STATES a_target) noexcept;

		std::atomic<bool> _started{ false };
		std::string _startResult;
		bool _startTried{ false };

		mutable std::mutex _optionsLock;
		NeuralOptions _options{};
		bool _wanted{ false };

		NVSDK_NGX_Parameter* _parameters{ nullptr };
		NVSDK_NGX_Handle* _feature{ nullptr };
		UINT _width{ 0 };
		UINT _height{ 0 };
		std::uint32_t _preset{ 0 };
		DXGI_FORMAT _format{ DXGI_FORMAT_UNKNOWN };
		ComPtr<ID3D12Resource> _output;
		ComPtr<ID3D12Resource> _composed;
		D3D12_RESOURCE_STATES _outputState{ D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
		D3D12_RESOURCE_STATES _composedState{ D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
		std::vector<Retired> _retired;
		NeuralCreationGate _gate;
		std::atomic<bool> _releaseRequested{ false };
		std::uint32_t _lastFailure{ 0 };

		// Feature creation runs on a list of its own, fenced before first use:
		// OptiScaler saw hangs creating it inside the frame's list.
		ComPtr<ID3D12CommandAllocator> _createAllocator;
		ComPtr<ID3D12GraphicsCommandList> _createList;
		ComPtr<ID3D12Fence> _createFence;
		std::uint64_t _createValue{ 0 };
		HANDLE _createEvent{ nullptr };

		ComPtr<ID3D12RootSignature> _rootSignature;
		ComPtr<ID3D12PipelineState> _pipeline;
		ComPtr<ID3D12DescriptorHeap> _descriptors;  // four per slot: final, hudless, neural, composed
		UINT _descriptorSize{ 0 };

		ComPtr<ID3D12QueryHeap> _timestamps;  // two per slot
		ComPtr<ID3D12Resource> _timestampReadback;
		const std::uint64_t* _timestampData{ nullptr };
		std::array<bool, SlotRing::kSlots> _timestampPending{};
		std::uint64_t _timestampFrequency{ 0 };
		double _sumMs{ 0.0 };
		std::uint32_t _samples{ 0 };
		std::atomic<double> _meanMs{ 0.0 };
	};

	[[nodiscard]] NeuralRenderer& TheNeuralRenderer() noexcept;
}
