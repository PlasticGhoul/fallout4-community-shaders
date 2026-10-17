#pragma once

#include "Bridge/CameraConstants.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Bridge
{
	struct StartupConfig
	{
		bool switchOn{ false };
		std::string switchReason;
		/// Data/F4SE/Plugins/CommunityShadersFO4; Streamline's files are in its
		/// Streamline subdirectory.
		std::filesystem::path pluginDirectory;
	};

	/// Patches Fallout4.exe's import of D3D11CreateDeviceAndSwapChain. Once,
	/// from F4SEPlugin_Load, after the runtime check.
	[[nodiscard]] bool InstallDeviceHook(const StartupConfig& a_config) noexcept;

	/// True while the game holds our swap chain.
	[[nodiscard]] bool Active() noexcept;
	/// Why the bridge is not there; empty while it is, and before the game
	/// created its device.
	[[nodiscard]] std::string Refusal();

	/// D3D11 objects of the slot this frame writes. REX::W32 and the Windows
	/// SDK declare the same interfaces with the same layout; void* carries
	/// them across without either side seeing the other's headers.
	struct InputSlot
	{
		void* hudless{ nullptr };   // ID3D11Texture2D*, back buffer format and size
		void* depthUAV{ nullptr };  // ID3D11UnorderedAccessView*, R32_FLOAT
		void* motion{ nullptr };    // ID3D11Texture2D*, R16G16_FLOAT
		std::uint32_t width{ 0 };
		std::uint32_t height{ 0 };
	};
	[[nodiscard]] bool CurrentSlot(InputSlot& a_slot) noexcept;
	/// The proxy's private D3D11 back buffer (ID3D11Texture2D*), for the probe.
	[[nodiscard]] void* BackBuffer() noexcept;

	/// Depth, motion and camera of this frame are in the slot.
	struct FrameInputs
	{
		Constants constants{};
		std::uint32_t depthWidth{ 0 };
		std::uint32_t depthHeight{ 0 };
		std::uint32_t motionWidth{ 0 };
		std::uint32_t motionHeight{ 0 };
	};
	void SubmitFrameInputs(const FrameInputs& a_inputs) noexcept;
	/// The picture before the HUD is in the slot.
	void SubmitHudless() noexcept;

	/// Applied at the next Present, on the presenting thread.
	void SetGeneration(bool a_on) noexcept;
	/// 1 low latency, 2 low latency with boost.
	void SetReflexMode(std::uint32_t a_mode) noexcept;
	void SetSplitView(bool a_on) noexcept;

	/// From the prolog of PollInputDevices, once per game frame: frame token,
	/// Reflex sleep, simulation start. In loading screens on another thread
	/// than Present.
	void OnSimulationStart() noexcept;
	/// From UpdateTemporalData: the frame's rendering begins.
	void OnRenderStart() noexcept;

	struct Status
	{
		bool generating{ false };
		double renderedPerSecond{ 0.0 };
		double shownPerSecond{ 0.0 };
		std::uint32_t dlssgStatus{ 0 };
		std::uint32_t minWidthOrHeight{ 0 };
		std::uint32_t width{ 0 };
		std::uint32_t height{ 0 };
		bool vsyncSupported{ false };
	};
	[[nodiscard]] Status CurrentStatus() noexcept;
}
