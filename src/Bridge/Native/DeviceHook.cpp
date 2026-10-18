#include "Bridge/Bridge.h"

#include "Bridge/Decision.h"
#include "Bridge/Log.h"
#include "Bridge/Native/NativeSdk.h"
#include "Bridge/Native/NeuralRenderer.h"
#include "Bridge/Native/Presenter.h"
#include "Bridge/Native/Streamline.h"
#include "Bridge/Native/SwapChainProxy.h"

#include <cstring>
#include <format>
#include <iterator>
#include <mutex>

namespace Bridge
{
	namespace
	{
		using Create_t = HRESULT(WINAPI*)(
			IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT,
			const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

		StartupConfig g_config;
		Create_t g_original{ nullptr };
		std::mutex g_refusalLock;
		std::string g_refusal;

		void SetRefusal(std::string a_reason)
		{
			const std::scoped_lock lock{ g_refusalLock };
			g_refusal = std::move(a_reason);
		}

		/// Fallout4.exe's import slot for a function of a DLL. Matches by name
		/// through the original thunks; where a binder dropped those, by the
		/// address the slot already holds.
		void** FindImport(HMODULE a_module, const char* a_dll, const char* a_function) noexcept
		{
			auto* const base = reinterpret_cast<std::uint8_t*>(a_module);
			const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
			const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
			const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
			if (directory.VirtualAddress == 0) {
				return nullptr;
			}
			for (auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress); descriptor->Name != 0; ++descriptor) {
				if (_stricmp(reinterpret_cast<const char*>(base + descriptor->Name), a_dll) != 0) {
					continue;
				}
				auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
				if (descriptor->OriginalFirstThunk != 0) {
					const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
					for (; names->u1.AddressOfData != 0; ++names, ++slots) {
						if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) {
							continue;
						}
						const auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
						if (std::strcmp(reinterpret_cast<const char*>(byName->Name), a_function) == 0) {
							return reinterpret_cast<void**>(&slots->u1.Function);
						}
					}
					continue;
				}
				const auto exported = reinterpret_cast<ULONG_PTR>(GetProcAddress(GetModuleHandleA(a_dll), a_function));
				for (; slots->u1.Function != 0; ++slots) {
					if (slots->u1.Function == exported) {
						return reinterpret_cast<void**>(&slots->u1.Function);
					}
				}
			}
			return nullptr;
		}

		std::string ModuleOf(const void* a_address)
		{
			HMODULE module = nullptr;
			if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(a_address), &module) == 0) {
				return "an unknown module";
			}
			std::wstring path(MAX_PATH, L'\0');
			const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
			path.resize(length);
			return Native::Narrow(path);
		}

		bool EnbLoaded() noexcept
		{
			const auto d3d11 = GetModuleHandleW(L"d3d11.dll");
			return d3d11 != nullptr && GetProcAddress(d3d11, "ENBGetSDKVersion") != nullptr;
		}

		/// Another module that replaces the swap chain or the TAA chain.
		const char* ForeignModule() noexcept
		{
			for (const auto* name : { "AAAFrameGeneration.dll", "Upscaling.dll", "OptiScaler.dll", "nvngx.dll" }) {
				if (GetModuleHandleA(name) != nullptr) {
					return name;
				}
			}
			return "";
		}

		Native::ComPtr<IDXGIAdapter> ResolveAdapter(IDXGIAdapter* a_adapter) noexcept
		{
			if (a_adapter != nullptr) {
				return a_adapter;
			}
			Native::ComPtr<IDXGIFactory1> factory;
			Native::ComPtr<IDXGIAdapter1> adapter;
			if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumAdapters1(0, &adapter))) {
				return nullptr;
			}
			return adapter;
		}

		void LogDescription(const DXGI_SWAP_CHAIN_DESC* a_desc)
		{
			if (a_desc == nullptr) {
				Log::Info("bridge: the game asks for a device without a swap chain");
				return;
			}
			Log::Info(std::format(
				"bridge: the game asks for {}x{}, format {}, windowed {}, swap effect {}, flags 0x{:X}, {} buffer(s)",
				a_desc->BufferDesc.Width, a_desc->BufferDesc.Height, static_cast<int>(a_desc->BufferDesc.Format), a_desc->Windowed != FALSE,
				static_cast<int>(a_desc->SwapEffect), a_desc->Flags, a_desc->BufferCount));
		}

		HRESULT WINAPI CreateDeviceAndSwapChain(
			IDXGIAdapter* a_adapter,
			D3D_DRIVER_TYPE a_driverType,
			HMODULE a_software,
			UINT a_flags,
			const D3D_FEATURE_LEVEL* a_levels,
			UINT a_levelCount,
			UINT a_sdkVersion,
			const DXGI_SWAP_CHAIN_DESC* a_desc,
			IDXGISwapChain** a_swapChain,
			ID3D11Device** a_device,
			D3D_FEATURE_LEVEL* a_level,
			ID3D11DeviceContext** a_context)
		{
			const auto original = [&]() {
				return g_original(a_adapter, a_driverType, a_software, a_flags, a_levels, a_levelCount, a_sdkVersion, a_desc, a_swapChain,
					a_device, a_level, a_context);
			};
			const auto refuse = [&](std::string a_reason) {
				Log::Info(std::format("bridge: not built - {}", a_reason));
				SetRefusal(std::move(a_reason));
				return original();
			};

			LogDescription(a_desc);
			const auto adapter = ResolveAdapter(a_adapter);
			DXGI_ADAPTER_DESC adapterDesc{};
			if (adapter) {
				adapter->GetDesc(&adapterDesc);
			}
			DecisionFacts facts{};
			facts.switchOn = g_config.switchOn;
			facts.switchReason = g_config.switchReason;
			facts.swapChainRequested = a_desc != nullptr && a_swapChain != nullptr;
			facts.windowed = a_desc != nullptr && a_desc->Windowed != FALSE;
			facts.vendorId = adapterDesc.VendorId;
			facts.enb = EnbLoaded();
			facts.foreignModule = ForeignModule();
			if (const auto verdict = Decide(facts); !verdict.build) {
				return refuse(verdict.reason);
			}
			if (Native::ActivePresenter() != nullptr) {
				return refuse("the game asked for a second swap chain while the first is still alive");
			}

			auto& streamline = Native::TheStreamline();
			if (!streamline.Load(g_config.pluginDirectory / L"Streamline")) {
				return refuse(streamline.Refusal());
			}
			if (!streamline.SupportsFrameGeneration(adapter.Get())) {
				return refuse(streamline.Refusal());
			}

			// 11.1 at least: the shared fence and the context's Signal and Wait
			// need it, and the default list stops at 11.0 (doodlum forces it too).
			static constexpr D3D_FEATURE_LEVEL kLevels[]{ D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
			Native::ComPtr<ID3D11Device> device;
			Native::ComPtr<ID3D11DeviceContext> context;
			D3D_FEATURE_LEVEL level{};
			const auto result = D3D11CreateDevice(a_adapter, a_driverType, a_software, a_flags, kLevels,
				static_cast<UINT>(std::size(kLevels)), a_sdkVersion, &device, &level, &context);
			if (FAILED(result)) {
				return refuse(std::format("D3D11CreateDevice failed (0x{:08X})", static_cast<std::uint32_t>(result)));
			}
			Native::ComPtr<IDXGIDevice> dxgiDevice;
			Native::ComPtr<IDXGIAdapter> deviceAdapter;
			if (FAILED(device.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&deviceAdapter))) {
				return refuse("the D3D11 device does not name its adapter");
			}

			auto presenter = std::make_unique<Native::Presenter>();
			if (!presenter->Create(device.Get(), deviceAdapter.Get(), *a_desc)) {
				auto reason = presenter->Failure();
				presenter->Destroy();
				return refuse(std::move(reason));
			}

			*a_swapChain = new Native::SwapChainProxy(std::move(presenter));
			if (a_device != nullptr) {
				*a_device = device.Detach();
			}
			if (a_context != nullptr) {
				*a_context = context.Detach();
			}
			if (a_level != nullptr) {
				*a_level = level;
			}
			SetRefusal({});
			Log::Info(std::format("bridge: in place, D3D11 feature level 0x{:X}", static_cast<std::uint32_t>(level)));
			return S_OK;
		}
	}

	bool InstallDeviceHook(const StartupConfig& a_config) noexcept
	{
		g_config = a_config;
		if (!a_config.switchOn) {
			// Nothing to decide at device creation, so the game's import stays
			// as it is and no other hook there has us in front of it.
			DecisionFacts facts{};
			facts.switchReason = a_config.switchReason;
			auto reason = Decide(facts).reason;
			Log::Info(std::format("bridge: import not patched - {}", reason));
			SetRefusal(std::move(reason));
			return true;
		}
		SetRefusal("the game has not created its device yet");
		void** const slot = FindImport(GetModuleHandleW(nullptr), "d3d11.dll", "D3D11CreateDeviceAndSwapChain");
		if (slot == nullptr) {
			SetRefusal("Fallout4.exe imports no D3D11CreateDeviceAndSwapChain");
			Log::Error("bridge: Fallout4.exe imports no D3D11CreateDeviceAndSwapChain; frame generation will refuse");
			return false;
		}
		g_original = reinterpret_cast<Create_t>(*slot);
		// Names who was there first: another plugin's hook here would now sit
		// below ours, and is passed by when the bridge is built.
		Log::Info(std::format("bridge: D3D11CreateDeviceAndSwapChain leads into {}; patching the import", ModuleOf(*slot)));
		DWORD previous = 0;
		if (VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &previous) == 0) {
			SetRefusal("the import table could not be made writable");
			return false;
		}
		*slot = reinterpret_cast<void*>(&CreateDeviceAndSwapChain);
		VirtualProtect(slot, sizeof(void*), previous, &previous);
		return true;
	}

	bool Active() noexcept
	{
		return Native::ActivePresenter() != nullptr;
	}

	std::string Refusal()
	{
		const std::scoped_lock lock{ g_refusalLock };
		return g_refusal;
	}

	bool CurrentSlot(InputSlot& a_slot) noexcept
	{
		auto* const presenter = Native::ActivePresenter();
		return presenter != nullptr && presenter->CurrentSlot(a_slot);
	}

	void* BackBuffer() noexcept
	{
		auto* const presenter = Native::ActivePresenter();
		return presenter != nullptr ? presenter->BackBuffer() : nullptr;
	}

	void SubmitFrameInputs(const FrameInputs& a_inputs) noexcept
	{
		if (auto* const presenter = Native::ActivePresenter()) {
			presenter->SubmitFrameInputs(a_inputs);
		}
	}

	void SubmitHudless() noexcept
	{
		if (auto* const presenter = Native::ActivePresenter()) {
			presenter->SubmitHudless();
		}
	}

	void SetGeneration(bool a_on) noexcept
	{
		if (auto* const presenter = Native::ActivePresenter()) {
			presenter->SetGeneration(a_on);
		}
	}

	void SetReflexMode(std::uint32_t a_mode) noexcept
	{
		if (auto* const presenter = Native::ActivePresenter()) {
			presenter->SetReflexMode(a_mode);
		}
	}

	void SetSplitView(bool a_on) noexcept
	{
		if (auto* const presenter = Native::ActivePresenter()) {
			presenter->SetSplitView(a_on);
		}
	}

	std::string StartNeuralRendering(const std::filesystem::path& a_dllDirectory, std::span<const std::uint8_t> a_composite) noexcept
	{
		if (Native::ActivePresenter() == nullptr) {
			return "the bridge is not in place";
		}
		return Native::TheNeuralRenderer().Start(a_dllDirectory, a_composite);
	}

	void SetNeuralRendering(const NeuralOptions& a_options, bool a_wanted) noexcept
	{
		Native::TheNeuralRenderer().SetOptions(a_options, a_wanted);
	}

	void StopNeuralRendering() noexcept
	{
		Native::TheNeuralRenderer().SetOptions({}, false);
		Native::TheNeuralRenderer().RequestRelease();
	}

	void OnSimulationStart() noexcept
	{
		if (auto* const presenter = Native::ActivePresenter()) {
			presenter->OnSimulationStart();
		}
	}

	void OnRenderStart() noexcept
	{
		if (auto* const presenter = Native::ActivePresenter()) {
			presenter->OnRenderStart();
		}
	}

	Status CurrentStatus() noexcept
	{
		auto* const presenter = Native::ActivePresenter();
		return presenter != nullptr ? presenter->CurrentStatus() : Status{};
	}
}
