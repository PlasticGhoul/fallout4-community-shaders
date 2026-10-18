#include "Bridge/Native/Streamline.h"

#include "Bridge/Log.h"

// Defines its functions in the header, so exactly one translation unit may
// include it - this one. Its warnings are NVIDIA's.
#pragma warning(push, 0)
#include <sl_security.h>
#pragma warning(pop)

#include <format>
#include <iterator>
#include <type_traits>

namespace Bridge::Native
{
	namespace
	{
		/// The project id and engine version NGX knows us by since F5.5a
		/// (src/Upscale/Ngx.cpp); one identity for both halves of DLSS.
		constexpr const char* kProjectId = "99023707-327d-45dc-ba45-0c47e8445542";
		constexpr const char* kEngineVersion = "Fallout 4 1.11.240";

		void OnStreamlineLog(sl::LogType a_type, const char* a_message)
		{
			std::string_view text{ a_message != nullptr ? a_message : "" };
			while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
				text.remove_suffix(1);
			}
			const auto line = std::format("streamline: {}", text);
			switch (a_type) {
			case sl::LogType::eError:
				Log::Error(line);
				break;
			case sl::LogType::eWarn:
				Log::Warn(line);
				break;
			default:
				Log::Info(line);
				break;
			}
		}

		template <class T>
		bool Resolve(HMODULE a_module, const char* a_name, T*& a_function) noexcept
		{
			a_function = reinterpret_cast<T*>(GetProcAddress(a_module, a_name));
			return a_function != nullptr;
		}
	}

	std::string Narrow(const std::wstring& a_text)
	{
		if (a_text.empty()) {
			return {};
		}
		const auto size = WideCharToMultiByte(CP_UTF8, 0, a_text.c_str(), static_cast<int>(a_text.size()), nullptr, 0, nullptr, nullptr);
		std::string out(static_cast<std::size_t>(size), '\0');
		WideCharToMultiByte(CP_UTF8, 0, a_text.c_str(), static_cast<int>(a_text.size()), out.data(), size, nullptr, nullptr);
		return out;
	}

	Streamline& TheStreamline() noexcept
	{
		static Streamline instance;
		return instance;
	}

	bool Streamline::Fail(std::string a_reason) noexcept
	{
		_refusal = std::move(a_reason);
		return false;
	}

	bool Streamline::Load(const std::filesystem::path& a_directory) noexcept
	{
		if (_initialised) {
			return true;
		}
		const auto interposer = a_directory / L"sl.interposer.dll";
		if (!std::filesystem::exists(interposer)) {
			return Fail(std::format("{} is missing - is the FrameGeneration archive installed? Frame Generation and Neural Rendering both need it", Narrow(interposer.wstring())));
		}
		// Full path, as sl_security.h insists: a relative one would verify
		// whatever the search order finds first.
		if (!sl::security::verifyEmbeddedSignature(interposer.c_str())) {
			return Fail("sl.interposer.dll carries no valid NVIDIA signature");
		}
		if (_module == nullptr) {
			_module = LoadLibraryExW(interposer.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
			if (_module == nullptr) {
				return Fail(std::format("sl.interposer.dll did not load (error {})", GetLastError()));
			}
		}

		const bool resolved =
			Resolve(_module, "slInit", _init) &&
			Resolve(_module, "slIsFeatureSupported", _isFeatureSupported) &&
			Resolve(_module, "slSetD3DDevice", _setD3DDevice) &&
			Resolve(_module, "slGetFeatureFunction", _getFeatureFunction) &&
			Resolve(_module, "slSetTagForFrame", setTagForFrame) &&
			Resolve(_module, "slSetConstants", setConstants) &&
			Resolve(_module, "slGetNewFrameToken", getNewFrameToken) &&
			Resolve(_module, "slUpgradeInterface", upgradeInterface) &&
			Resolve(_module, "slGetNativeInterface", getNativeInterface);
		if (!resolved) {
			return Fail("sl.interposer.dll lacks an export this plugin needs");
		}

		_pluginPath = a_directory.wstring();
		const wchar_t* paths[]{ _pluginPath.c_str() };
		const sl::Feature features[]{ sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL };

		sl::Preferences preferences{};
		preferences.showConsole = false;
		preferences.logLevel = sl::LogLevel::eDefault;
		preferences.pathsToPlugins = paths;
		preferences.numPathsToPlugins = 1;
		preferences.logMessageCallback = &OnStreamlineLog;
		// Manual hooking: otherwise Streamline patches the DXGI factory class
		// for the whole process (dxgi.cpp:318), the game's D3D11 path included.
		// No OTA: the files that ship are the files that run.
		preferences.flags = sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eUseManualHooking |
		                    sl::PreferenceFlags::eUseFrameBasedResourceTagging;
		preferences.featuresToLoad = features;
		preferences.numFeaturesToLoad = static_cast<std::uint32_t>(std::size(features));
		preferences.engine = sl::EngineType::eCustom;
		preferences.engineVersion = kEngineVersion;
		preferences.projectId = kProjectId;
		preferences.renderAPI = sl::RenderAPI::eD3D12;

		const auto result = _init(preferences, sl::kSDKVersion);
		if (result != sl::Result::eOk) {
			return Fail(std::format("slInit failed (result {})", static_cast<std::uint32_t>(result)));
		}
		_initialised = true;
		Log::Info("bridge: Streamline 2.14.16 initialised, manual hooking on D3D12");
		return true;
	}

	bool Streamline::SupportsFrameGeneration(IDXGIAdapter* a_adapter) noexcept
	{
		DXGI_ADAPTER_DESC description{};
		if (a_adapter == nullptr || FAILED(a_adapter->GetDesc(&description))) {
			return Fail("the adapter could not be described");
		}
		sl::AdapterInfo adapter{};
		adapter.deviceLUID = reinterpret_cast<std::uint8_t*>(&description.AdapterLuid);
		adapter.deviceLUIDSizeInBytes = sizeof(LUID);
		if (const auto result = _isFeatureSupported(sl::kFeatureDLSS_G, adapter); result != sl::Result::eOk) {
			return Fail(std::format(
				"DLSS Frame Generation is not supported here (result {}) - it needs an RTX 40 or newer, a current driver "
				"and hardware-accelerated GPU scheduling",
				static_cast<std::uint32_t>(result)));
		}
		if (const auto result = _isFeatureSupported(sl::kFeatureReflex, adapter); result != sl::Result::eOk) {
			return Fail(std::format("NVIDIA Reflex is not supported here (result {})", static_cast<std::uint32_t>(result)));
		}
		return true;
	}

	bool Streamline::BindDevice(ID3D12Device* a_device) noexcept
	{
		if (const auto result = _setD3DDevice(a_device); result != sl::Result::eOk) {
			return Fail(std::format("slSetD3DDevice failed (result {})", static_cast<std::uint32_t>(result)));
		}
		const auto feature = [this](sl::Feature a_feature, const char* a_name, auto*& a_function) {
			void* function = nullptr;
			if (_getFeatureFunction(a_feature, a_name, function) != sl::Result::eOk || function == nullptr) {
				return false;
			}
			a_function = reinterpret_cast<std::remove_reference_t<decltype(a_function)>>(function);
			return true;
		};
		const bool bound =
			feature(sl::kFeatureDLSS_G, "slDLSSGGetState", dlssgGetState) &&
			feature(sl::kFeatureDLSS_G, "slDLSSGSetOptions", dlssgSetOptions) &&
			feature(sl::kFeatureReflex, "slReflexSetOptions", reflexSetOptions) &&
			feature(sl::kFeatureReflex, "slReflexSleep", reflexSleep) &&
			feature(sl::kFeaturePCL, "slPCLSetMarker", pclSetMarker);
		if (!bound) {
			return Fail("a DLSS-G, Reflex or PCL function is missing from Streamline's plugins");
		}
		return true;
	}
}
