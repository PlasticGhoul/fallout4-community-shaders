#pragma once

#include "Bridge/Native/NativeSdk.h"

#include <filesystem>
#include <string>

namespace Bridge::Native
{
	/// sl.interposer.dll, loaded by hand: the plugin links neither it nor
	/// Streamline's helpers, which would need slGetFeatureFunction at link
	/// time (ProgrammingGuideManualHooking.md 1.1). Lives for the process:
	/// Streamline works with exactly one device, and slShutdown would have to
	/// come before that device goes - neither happens while the game runs.
	class Streamline
	{
	public:
		/// Verifies NVIDIA's signature, loads the interposer, runs slInit with
		/// manual hooking on D3D12. Idempotent.
		[[nodiscard]] bool Load(const std::filesystem::path& a_directory) noexcept;
		[[nodiscard]] bool SupportsFrameGeneration(IDXGIAdapter* a_adapter) noexcept;
		/// slSetD3DDevice, then the feature functions, which exist only after it.
		[[nodiscard]] bool BindDevice(ID3D12Device* a_device) noexcept;

		[[nodiscard]] const std::string& Refusal() const noexcept { return _refusal; }

		PFun_slSetTagForFrame* setTagForFrame{};
		PFun_slSetConstants* setConstants{};
		PFun_slGetNewFrameToken* getNewFrameToken{};
		PFun_slUpgradeInterface* upgradeInterface{};
		PFun_slGetNativeInterface* getNativeInterface{};
		PFun_slDLSSGGetState* dlssgGetState{};
		PFun_slDLSSGSetOptions* dlssgSetOptions{};
		PFun_slReflexSetOptions* reflexSetOptions{};
		PFun_slReflexSleep* reflexSleep{};
		PFun_slPCLSetMarker* pclSetMarker{};

	private:
		bool Fail(std::string a_reason) noexcept;

		HMODULE _module{ nullptr };
		bool _initialised{ false };
		std::wstring _pluginPath;
		std::string _refusal;
		PFun_slInit* _init{};
		PFun_slIsFeatureSupported* _isFeatureSupported{};
		PFun_slSetD3DDevice* _setD3DDevice{};
		PFun_slGetFeatureFunction* _getFeatureFunction{};
	};

	[[nodiscard]] Streamline& TheStreamline() noexcept;

	/// UTF-16 path or text to the log's UTF-8.
	[[nodiscard]] std::string Narrow(const std::wstring& a_text);
}
