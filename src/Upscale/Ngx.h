#pragma once

#include "Render/Extent.h"
#include "Upscale/UpscaleMath.h"

#include <REX/W32/D3D11.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

struct NVSDK_NGX_Parameter;
struct NVSDK_NGX_Handle;

namespace Upscale
{
	enum class Preset : std::uint8_t
	{
		kDefault,
		kK,
		kL,
		kM,
	};

	/// The names DLSS/preset stores.
	[[nodiscard]] std::optional<Preset> PresetFromName(std::string_view a_name) noexcept;

	struct EvaluateInputs
	{
		REX::W32::ID3D11Resource* color;          // render size, ours
		REX::W32::ID3D11Resource* output;         // display size, RT_004
		REX::W32::ID3D11Resource* depth;          // DS_002 texture
		REX::W32::ID3D11Resource* motionVectors;  // render size, dilated
		Render::Extent render;
		float jitterX;  // render pixels, as NGX takes them
		float jitterY;
		float motionScaleX;
		float motionScaleY;
		bool reset;
	};

	/// A thin layer over the DLSS SDK 310.9.1 on D3D11: one DLSS object for
	/// one combination of sizes, mode and preset, recreated when any of them
	/// changes. Every failure reaches the log with its NVSDK_NGX_Result.
	class Ngx
	{
	public:
		Ngx() = default;
		~Ngx() = default;

		Ngx(const Ngx&) = delete;
		Ngx& operator=(const Ngx&) = delete;

		/// Starts NGX on the engine's device and asks whether DLSS can run.
		/// a_dllDirectory is where nvngx_dlss.dll is looked for.
		[[nodiscard]] bool Init(REX::W32::ID3D11Device* a_device, const std::filesystem::path& a_dllDirectory) noexcept;
		[[nodiscard]] bool Available() const noexcept { return _available; }
		/// Why Init said no, for the feature's refusal line.
		[[nodiscard]] std::string_view Refusal() const noexcept { return _refusal; }

		[[nodiscard]] std::optional<Render::Extent> OptimalRenderSize(Render::Extent a_display, Mode a_mode) noexcept;
		[[nodiscard]] bool Ensure(REX::W32::ID3D11DeviceContext& a_context, Render::Extent a_render, Render::Extent a_display, Mode a_mode, Preset a_preset) noexcept;
		[[nodiscard]] bool Evaluate(REX::W32::ID3D11DeviceContext& a_context, const EvaluateInputs& a_inputs) noexcept;
		/// Idempotent.
		void Shutdown() noexcept;

	private:
		REX::W32::ID3D11Device* _device{ nullptr };
		NVSDK_NGX_Parameter* _parameters{ nullptr };
		NVSDK_NGX_Handle* _feature{ nullptr };
		Render::Extent _render{};
		Render::Extent _display{};
		Mode _mode{ Mode::kDLAA };
		Preset _preset{ Preset::kDefault };
		bool _available{ false };
		std::string _refusal;
		std::wstring _dllDirectory;
	};
}
