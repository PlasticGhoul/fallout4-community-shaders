#include "Features/TargetView.h"

#include "Render/DebugName.h"
#include "Render/FramePhase.h"
#include "Render/FullscreenPass.h"
#include "Render/Profiler.h"
#include "Render/Renderer.h"
#include "Render/StateGuard.h"
#include "Render/Targets.h"
#include "Settings/Settings.h"
#include "Shader/ShaderCompiler.h"
#include "Shader/ShaderSource.h"
#include "Util/GamePaths.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace Features
{
	namespace
	{
		constexpr auto kShaderFile = "TargetView/TargetView.hlsl";
		constexpr std::uint64_t kLogInterval = 180;

		std::filesystem::path ShaderRoot()
		{
			return Util::DataDirectory() / "Shaders" / "FO4";
		}

		/// "RT_028" -> 28. The slot is the number in the name, and the name is
		/// what a capture and the inventory show, so nothing is translated.
		std::size_t SlotOf(const std::string& a_choice)
		{
			if (a_choice.size() != 6 || !a_choice.starts_with("RT_")) {
				return 0;
			}
			std::size_t slot = 0;
			for (const char digit : a_choice.substr(3)) {
				if (digit < '0' || digit > '9') {
					return 0;
				}
				slot = slot * 10 + static_cast<std::size_t>(digit - '0');
			}
			return slot;
		}

		float ChannelOf(const std::string& a_choice)
		{
			if (a_choice == "r") {
				return 1.0f;
			}
			if (a_choice == "g") {
				return 2.0f;
			}
			if (a_choice == "b") {
				return 3.0f;
			}
			if (a_choice == "a") {
				return 4.0f;
			}
			return 0.0f;
		}
	}

	void TargetView::Declare()
	{
		Settings::DeclareFeature("TargetView", false)
			.Label("feature.target_view.name", "Target View")
			.Help(
				"feature.target_view.help",
				"Lays one of the engine's render targets over the picture. A tool for finding "
				"out what a pass leaves behind; off unless you are looking.");
		Settings::DeclareChoice(
			"TargetView/target",
			"RT_028",
			std::vector<std::string>{ "RT_028", "RT_009", "RT_003", "RT_020", "RT_022", "RT_023", "RT_024", "RT_057", "RT_039" })
			.Label("feature.target_view.target", "Target")
			.Help("feature.target_view.target_help", "Which render target to show, by its slot.");
		Settings::DeclareChoice(
			"TargetView/channel",
			"rgb",
			std::vector<std::string>{ "rgb", "r", "g", "b", "a" })
			.Label("feature.target_view.channel", "Channel")
			.Help("feature.target_view.channel_help", "All three colour channels, or one of the four as grey.");
		// The HDR target is tone mapped after us: a value of 1 reads as
		// white, and a target that is mostly 1 - an occlusion map - washes
		// out. The probe of F5 saw RT_028 as nothing but glare.
		Settings::DeclareChoice(
			"TargetView/exposure",
			"1",
			std::vector<std::string>{ "1", "0.25", "0.05" })
			.Label("feature.target_view.exposure", "Exposure")
			.Help("feature.target_view.exposure_help", "Scales the target before it is shown, for targets that sit near white.");
	}

	bool TargetView::Setup()
	{
		_draws = 0;
		if (!Render::InitFullscreenPass() || !Compile() || !EnsureStates()) {
			return false;
		}
		if (!_constants.Create(16, "FO4CS_CB_TargetView")) {
			return false;
		}
		_phase = Render::SubscribeFramePhase(
			Render::Phase::kAfterOpaque, "TargetView/Show", [this] { Draw(); });
		if (_phase == Render::PhaseDispatcher::kNoToken) {
			REX::ERROR("TargetView: the frame phase has no room left");
			return false;
		}
		REX::INFO("TargetView: running");
		return true;
	}

	void TargetView::Frame()
	{
		if (_watch.Poll()) {
			REX::INFO("TargetView: shader changed, recompiling");
			static_cast<void>(Compile());
		}
	}

	void TargetView::Shutdown()
	{
		Render::UnsubscribeFramePhase(Render::Phase::kAfterOpaque, _phase);
		_phase = Render::PhaseDispatcher::kNoToken;
		if (_shader != nullptr) {
			_shader->Release();
			_shader = nullptr;
		}
		if (_pointClamp != nullptr) {
			_pointClamp->Release();
			_pointClamp = nullptr;
		}
		if (_plain != nullptr) {
			_plain->Release();
			_plain = nullptr;
		}
		_constants.Release();
	}

	void TargetView::Draw()
	{
		++_draws;
		const auto choice = Settings::GetString("TargetView/target");
		const auto slot = SlotOf(choice);
		auto* const source = Render::Targets::RenderTargetSRV(slot);
		auto* const scene = Render::Targets::RenderTargetView(Render::Targets::kSceneHDR);
		auto* const context = Render::GetContext();
		if (source == nullptr || scene == nullptr || context == nullptr || _shader == nullptr) {
			if (_draws % kLogInterval == 0) {
				REX::INFO(
					"TargetView: nothing drawn, slot {} source {}, scene {}",
					slot,
					source != nullptr ? "ok" : "missing",
					scene != nullptr ? "ok" : "missing");
			}
			return;
		}
		const auto exposureChoice = Settings::GetString("TargetView/exposure");
		const float exposure = exposureChoice == "0.25" ? 0.25f : exposureChoice == "0.05" ? 0.05f :
		                                                                                     1.0f;
		const float params[4]{ ChannelOf(Settings::GetString("TargetView/channel")), exposure, 0.0f, 0.0f };

		const Render::StateGuard guard;
		const Render::PassScope scope{ "TargetView/Draw" };
		context->OMSetRenderTargets(0, nullptr, nullptr);
		static_cast<void>(_constants.Update(params, sizeof(params)));
		auto* const buffer = _constants.Buffer();
		const float blendFactor[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
		context->OMSetRenderTargets(1, std::addressof(scene), nullptr);
		context->OMSetBlendState(_plain, blendFactor, 0xFFFFFFFFu);
		context->PSSetShaderResources(0, 1, std::addressof(source));
		context->PSSetSamplers(0, 1, std::addressof(_pointClamp));
		context->PSSetConstantBuffers(1, 1, std::addressof(buffer));
		context->PSSetShader(_shader, nullptr, 0);
		Render::DrawFullscreen();
	}

	bool TargetView::Compile()
	{
		const auto source = Shader::LoadSource(ShaderRoot(), kShaderFile);
		if (!source) {
			const std::filesystem::path only[] = { ShaderRoot() / kShaderFile };
			_watch.Reset(only);
			REX::ERROR("TargetView: {}", source.error());
			return false;
		}
		_watch.Reset(source->files);
		const auto compiled = Shader::CompilePixelShader(source->text, "TargetView.hlsl", "main");
		if (!compiled.Succeeded()) {
			REX::ERROR("TargetView: {}", compiled.diagnostics);
			return false;
		}
		auto* const device = Render::GetDevice();
		REX::W32::ID3D11PixelShader* shader = nullptr;
		if (device == nullptr ||
			device->CreatePixelShader(compiled.bytecode.data(), compiled.bytecode.size(), nullptr, std::addressof(shader)) < 0) {
			REX::ERROR("TargetView: the shader could not be created");
			return false;
		}
		if (_shader != nullptr) {
			_shader->Release();
		}
		_shader = shader;
		static_cast<void>(Render::SetDebugName(reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_shader), "FO4CS_PS_TargetView"));
		REX::INFO("TargetView: shader compiled");
		return true;
	}

	bool TargetView::EnsureStates()
	{
		auto* const device = Render::GetDevice();
		if (device == nullptr) {
			return false;
		}
		if (_plain == nullptr) {
			// Overwrite: the target replaces the picture, that is the point.
			REX::W32::D3D11_BLEND_DESC desc{};
			auto& target = desc.renderTarget[0];
			target.blendEnable = false;
			target.renderTargetWriteMask = static_cast<std::uint8_t>(REX::W32::D3D11_COLOR_WRITE_ENABLE_ALL);
			if (device->CreateBlendState(std::addressof(desc), std::addressof(_plain)) < 0) {
				REX::ERROR("TargetView: the blend state could not be created");
				return false;
			}
		}
		if (_pointClamp == nullptr) {
			// Point: a half resolution target is shown as its pixels are,
			// not smoothed into something it is not.
			REX::W32::D3D11_SAMPLER_DESC desc{};
			desc.filter = REX::W32::D3D11_FILTER_MIN_MAG_MIP_POINT;
			desc.addressU = REX::W32::D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.addressV = REX::W32::D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.addressW = REX::W32::D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.maxLOD = 3.402823466e+38f;
			if (device->CreateSamplerState(std::addressof(desc), std::addressof(_pointClamp)) < 0) {
				REX::ERROR("TargetView: the sampler could not be created");
				return false;
			}
		}
		return true;
	}
}
