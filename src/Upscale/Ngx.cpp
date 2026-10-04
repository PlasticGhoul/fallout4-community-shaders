#include "Upscale/Ngx.h"

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_d3d.h>

#include <format>

namespace Upscale
{
	namespace
	{
		/// NVIDIA asks engines without an NVIDIA-issued id to describe
		/// themselves with a project id of their own and ENGINE_TYPE_CUSTOM.
		/// Generated once; NGX files its own settings and logs under it.
		constexpr const char* kProjectId = "99023707-327d-45dc-ba45-0c47e8445542";
		constexpr const char* kEngineVersion = "Fallout 4 1.11.240";

		NVSDK_NGX_PerfQuality_Value ToQuality(Mode a_mode) noexcept
		{
			switch (a_mode) {
			case Mode::kQuality:
				return NVSDK_NGX_PerfQuality_Value_MaxQuality;
			case Mode::kBalanced:
				return NVSDK_NGX_PerfQuality_Value_Balanced;
			case Mode::kPerformance:
				return NVSDK_NGX_PerfQuality_Value_MaxPerf;
			case Mode::kUltraPerformance:
				return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
			case Mode::kDLAA:
			default:
				return NVSDK_NGX_PerfQuality_Value_DLAA;
			}
		}

		unsigned int ToPreset(Preset a_preset) noexcept
		{
			switch (a_preset) {
			case Preset::kK:
				return NVSDK_NGX_DLSS_Hint_Render_Preset_K;
			case Preset::kL:
				return NVSDK_NGX_DLSS_Hint_Render_Preset_L;
			case Preset::kM:
				return NVSDK_NGX_DLSS_Hint_Render_Preset_M;
			case Preset::kDefault:
			default:
				return NVSDK_NGX_DLSS_Hint_Render_Preset_Default;
			}
		}

		std::string Describe(NVSDK_NGX_Result a_result)
		{
			return std::format("0x{:08X}", static_cast<std::uint32_t>(a_result));
		}
	}

	std::optional<Preset> PresetFromName(std::string_view a_name) noexcept
	{
		if (a_name == "default") {
			return Preset::kDefault;
		}
		if (a_name == "k") {
			return Preset::kK;
		}
		if (a_name == "l") {
			return Preset::kL;
		}
		if (a_name == "m") {
			return Preset::kM;
		}
		return std::nullopt;
	}

	bool Ngx::Init(REX::W32::ID3D11Device* a_device, const std::filesystem::path& a_dllDirectory) noexcept
	{
		_device = a_device;
		_dllDirectory = a_dllDirectory.wstring();
		const wchar_t* paths[]{ _dllDirectory.c_str() };
		NVSDK_NGX_FeatureCommonInfo info{};
		info.PathListInfo.Path = paths;
		info.PathListInfo.Length = 1;

		const auto init = NVSDK_NGX_D3D11_Init_with_ProjectID(
			kProjectId,
			NVSDK_NGX_ENGINE_TYPE_CUSTOM,
			kEngineVersion,
			_dllDirectory.c_str(),
			reinterpret_cast<::ID3D11Device*>(a_device),
			std::addressof(info),
			NVSDK_NGX_Version_API);
		if (NVSDK_NGX_FAILED(init)) {
			_device = nullptr;
			_refusal = std::format("NGX did not start ({}): no NVIDIA GPU, or a driver older than 616.64", Describe(init));
			return false;
		}
		const auto capability = NVSDK_NGX_D3D11_GetCapabilityParameters(std::addressof(_parameters));
		if (NVSDK_NGX_FAILED(capability) || _parameters == nullptr) {
			_refusal = std::format("NGX capability parameters not readable ({})", Describe(capability));
			return false;
		}
		int available = 0;
		int needsDriver = 0;
		NVSDK_NGX_Parameter_GetI(_parameters, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, std::addressof(needsDriver));
		NVSDK_NGX_Parameter_GetI(_parameters, NVSDK_NGX_Parameter_SuperSampling_Available, std::addressof(available));
		if (needsDriver != 0) {
			_refusal = "DLSS needs a newer driver";
			return false;
		}
		if (available == 0) {
			_refusal = std::format("DLSS not available - is nvngx_dlss.dll in {}?", a_dllDirectory.string());
			return false;
		}
		_available = true;
		return true;
	}

	std::optional<Render::Extent> Ngx::OptimalRenderSize(Render::Extent a_display, Mode a_mode) noexcept
	{
		if (!_available) {
			return std::nullopt;
		}
		unsigned int width = 0;
		unsigned int height = 0;
		unsigned int maxWidth = 0;
		unsigned int maxHeight = 0;
		unsigned int minWidth = 0;
		unsigned int minHeight = 0;
		float sharpness = 0.0f;
		const auto result = NGX_DLSS_GET_OPTIMAL_SETTINGS(
			_parameters,
			a_display.width,
			a_display.height,
			ToQuality(a_mode),
			std::addressof(width),
			std::addressof(height),
			std::addressof(maxWidth),
			std::addressof(maxHeight),
			std::addressof(minWidth),
			std::addressof(minHeight),
			std::addressof(sharpness));
		if (NVSDK_NGX_FAILED(result) || width == 0 || height == 0) {
			return std::nullopt;
		}
		return Render::Extent{ width, height };
	}

	bool Ngx::Ensure(REX::W32::ID3D11DeviceContext& a_context, Render::Extent a_render, Render::Extent a_display, Mode a_mode, Preset a_preset) noexcept
	{
		if (!_available) {
			return false;
		}
		if (_feature != nullptr && _render == a_render && _display == a_display && _mode == a_mode && _preset == a_preset) {
			return true;
		}
		if (_feature != nullptr) {
			NVSDK_NGX_D3D11_ReleaseFeature(_feature);
			_feature = nullptr;
		}

		const auto preset = ToPreset(a_preset);
		NVSDK_NGX_Parameter_SetUI(_parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, preset);
		NVSDK_NGX_Parameter_SetUI(_parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, preset);
		NVSDK_NGX_Parameter_SetUI(_parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, preset);
		NVSDK_NGX_Parameter_SetUI(_parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance, preset);
		NVSDK_NGX_Parameter_SetUI(_parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, preset);

		NVSDK_NGX_DLSS_Create_Params create{};
		create.Feature.InWidth = a_render.width;
		create.Feature.InHeight = a_render.height;
		create.Feature.InTargetWidth = a_display.width;
		create.Feature.InTargetHeight = a_display.height;
		create.Feature.InPerfQualityValue = ToQuality(a_mode);
		// HDR input (R11G11B10 before tonemapping), motion vectors at render
		// size, exposure left to DLSS (F5.5a spec, section 2).
		create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
		                              NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
		                              NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;

		const auto result = NGX_D3D11_CREATE_DLSS_EXT(
			reinterpret_cast<::ID3D11DeviceContext*>(std::addressof(a_context)),
			std::addressof(_feature),
			_parameters,
			std::addressof(create));
		if (NVSDK_NGX_FAILED(result) || _feature == nullptr) {
			REX::ERROR(
				"DLSS: the DLSS object for {}x{} -> {}x{} could not be created ({})",
				a_render.width, a_render.height, a_display.width, a_display.height, Describe(result));
			_feature = nullptr;
			return false;
		}
		_render = a_render;
		_display = a_display;
		_mode = a_mode;
		_preset = a_preset;
		REX::INFO("DLSS: object created, {}x{} -> {}x{}", a_render.width, a_render.height, a_display.width, a_display.height);
		return true;
	}

	bool Ngx::Evaluate(REX::W32::ID3D11DeviceContext& a_context, const EvaluateInputs& a_inputs) noexcept
	{
		if (_feature == nullptr) {
			return false;
		}
		NVSDK_NGX_D3D11_DLSS_Eval_Params eval{};
		eval.Feature.pInColor = reinterpret_cast<::ID3D11Resource*>(a_inputs.color);
		eval.Feature.pInOutput = reinterpret_cast<::ID3D11Resource*>(a_inputs.output);
		eval.pInDepth = reinterpret_cast<::ID3D11Resource*>(a_inputs.depth);
		eval.pInMotionVectors = reinterpret_cast<::ID3D11Resource*>(a_inputs.motionVectors);
		eval.InJitterOffsetX = a_inputs.jitterX;
		eval.InJitterOffsetY = a_inputs.jitterY;
		eval.InRenderSubrectDimensions.Width = a_inputs.render.width;
		eval.InRenderSubrectDimensions.Height = a_inputs.render.height;
		eval.InReset = a_inputs.reset ? 1 : 0;
		eval.InMVScaleX = a_inputs.motionScaleX;
		eval.InMVScaleY = a_inputs.motionScaleY;
		const auto result = NGX_D3D11_EVALUATE_DLSS_EXT(
			reinterpret_cast<::ID3D11DeviceContext*>(std::addressof(a_context)),
			_feature,
			_parameters,
			std::addressof(eval));
		return NVSDK_NGX_SUCCEED(result);
	}

	void Ngx::Shutdown() noexcept
	{
		if (_feature != nullptr) {
			NVSDK_NGX_D3D11_ReleaseFeature(_feature);
			_feature = nullptr;
		}
		if (_parameters != nullptr) {
			NVSDK_NGX_D3D11_DestroyParameters(_parameters);
			_parameters = nullptr;
		}
		if (_device != nullptr) {
			NVSDK_NGX_D3D11_Shutdown1(reinterpret_cast<::ID3D11Device*>(_device));
			_device = nullptr;
		}
		_available = false;
	}
}
