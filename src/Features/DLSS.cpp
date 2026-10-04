#include "Features/DLSS.h"

#include "Render/Camera.h"
#include "Render/FramePhase.h"
#include "Render/Profiler.h"
#include "Render/Renderer.h"
#include "Render/Resolution.h"
#include "Render/SwapChainHook.h"
#include "Render/Targets.h"
#include "Settings/Settings.h"
#include "Util/GamePaths.h"

#include <RE/B/BSGraphics.h>
#include <RE/I/ImageSpaceEffect.h>
#include <RE/I/ImageSpaceManager.h>
#include <RE/M/Main.h>
#include <RE/N/NiCamera.h>
#include <RE/U/UI.h>

#include <REX/W32/KERNEL32.h>

#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <string>
#include <vector>

namespace Features
{
	namespace
	{
		constexpr std::uint64_t kLogInterval = 180;

		/// FO4_RT_029, R16G16_FLOAT, full resolution: the engine's motion
		/// vectors, written by the g-buffer prepass and the sky.
		constexpr std::size_t kMotionVectors = 29;

		/// FO4_RT_039, R32_FLOAT with twelve mips: the linear depth chain the
		/// engine's later effects read; FullDepth rewrites mip 0 at display
		/// size (doodlum's kMainDepthMips).
		constexpr std::size_t kLinearDepth = 39;

		/// RT_029 holds uv units (doodlum hands Streamline a scale of 1, and
		/// Streamline wants [-1, 1] uv); NGX wants render pixels, so the scale
		/// is the render size times these. Settled by the first run on
		/// 2026-10-04: still ~1e-6, walking 0.001 to 0.012 (3 to 30 pixels a
		/// frame at 1440p), and with these signs nothing smeared or shook.
		constexpr float kMotionScaleX = 1.0f;
		constexpr float kMotionScaleY = 1.0f;

		/// The engine's TAA switch; doodlum turns it on so that the engine
		/// produces jitter and motion vectors at all.
		constexpr std::uint64_t kEnableTAA = 2704658;

		bool* TAAFlag() noexcept
		{
			return reinterpret_cast<bool*>(REL::ID(kEnableTAA).address());
		}

		RE::ImageSpaceEffect* FXAAEffect() noexcept
		{
			auto* const manager = RE::ImageSpaceManager::GetSingleton();
			if (manager == nullptr) {
				return nullptr;
			}
			const auto index = static_cast<std::uint32_t>(RE::ImageSpaceManager::ImageSpaceEffectEnum::EFFECT_SHADER_FXAA);
			return index < manager->effectList.size() ? manager->effectList[index] : nullptr;
		}

		/// IEEE 754 binary16 to float, for the R16G16_FLOAT probe.
		float HalfToFloat(std::uint16_t a_half) noexcept
		{
			const auto sign = (a_half >> 15) & 0x1u;
			const auto exponent = (a_half >> 10) & 0x1Fu;
			const auto mantissa = a_half & 0x3FFu;
			float value = 0.0f;
			if (exponent == 0) {
				value = std::ldexp(static_cast<float>(mantissa), -24);
			} else if (exponent == 31) {
				value = mantissa == 0 ? 65504.0f : 0.0f;
			} else {
				value = std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f, static_cast<int>(exponent) - 15);
			}
			return sign != 0 ? -value : value;
		}

		/// Another upscaler in the process would fight over the same TAA
		/// switch and the same targets.
		const wchar_t* ForeignUpscaler() noexcept
		{
			for (const auto* module : { L"Upscaling.dll", L"OptiScaler.dll", L"nvngx.dll" }) {
				if (REX::W32::GetModuleHandleW(module) != nullptr) {
					return module;
				}
			}
			return nullptr;
		}

		bool EnbLoaded() noexcept
		{
			const auto d3d11 = REX::W32::GetModuleHandleW(L"d3d11.dll");
			return d3d11 != nullptr && REX::W32::GetProcAddress(d3d11, "ENBGetSDKVersion") != nullptr;
		}

		std::string Narrow(const wchar_t* a_text)
		{
			std::string text;
			for (const auto* c = a_text; *c != L'\0'; ++c) {
				text.push_back(static_cast<char>(*c));
			}
			return text;
		}
	}

	void DLSS::Declare()
	{
		Settings::DeclareFeature("DLSS", false)
			.Label("feature.dlss.name", "DLSS")
			.Help(
				"feature.dlss.help",
				"NVIDIA DLSS replaces the game's temporal anti-aliasing: DLAA at full resolution, or "
				"upscaling from a lower one for more frames. Uses NVIDIA DLSS technology.");
		Settings::DeclareChoice(
			"DLSS/mode",
			"quality",
			std::vector<std::string>{ "dlaa", "quality", "balanced", "performance", "ultra_performance" })
			.Label("feature.dlss.mode", "Mode")
			.Help("feature.dlss.mode_help", "DLAA keeps the full resolution; the others render lower and upscale.");
		Settings::DeclareChoice("DLSS/preset", "default", std::vector<std::string>{ "default", "k", "l", "m" })
			.Label("feature.dlss.preset", "Preset")
			.Help(
				"feature.dlss.preset_help",
				"The DLSS model. Default lets DLSS choose; K is DLSS 4, L and M are DLSS 4.5.");
		Settings::DeclareSlider("DLSS/sharpness", 0.0, 0.0, 1.0)
			.Label("feature.dlss.sharpness", "Sharpness")
			.Help("feature.dlss.sharpness_help", "Contrast adaptive sharpening after DLSS. 0 is off.");
	}

	bool DLSS::Setup()
	{
		_frames = 0;
		_evaluations = 0;
		_failedEvaluations = 0;
		_resets = 0;
		_reset = true;
		_wasNative = true;
		_haveCamera = false;
		_active = false;

		if (!Upscale::Hooks::Installed()) {
			REX::ERROR("DLSS: the upscaling hooks are not installed ({})", Upscale::Hooks::FailedSite());
			return false;
		}
		if (const auto* const foreign = ForeignUpscaler(); foreign != nullptr) {
			REX::ERROR("DLSS: another upscaler is loaded ({}); one at a time", Narrow(foreign));
			return false;
		}
		_enb = EnbLoaded();
		if (_enb) {
			REX::INFO("DLSS: ENB is loaded, DLAA only");
		}

		if (!_ngx.Init(Render::GetDevice(), Util::PluginDataDirectory())) {
			REX::ERROR("DLSS: {}", _ngx.Refusal());
			return false;
		}
		if (!_passes.Compile()) {
			return false;
		}
		if (!_motionProbe.Create(REX::W32::DXGI_FORMAT_R16G16_FLOAT, 4, "FO4CS_PROBE_Motion")) {
			return false;
		}

		_taaWas = *TAAFlag();
		*TAAFlag() = true;
		if (auto* const fxaa = FXAAEffect(); fxaa != nullptr) {
			_fxaaWas = fxaa->isActive;
			fxaa->isActive = false;
		}
		_engineSwitched = true;

		_phaseTokens[0] = Render::SubscribeFramePhase(Render::Phase::kBeforeComposite, "DLSS/ProbeBeforeComposite", [this] { LogPhase("kBeforeComposite"); });
		_phaseTokens[1] = Render::SubscribeFramePhase(Render::Phase::kComposite, "DLSS/ProbeComposite", [this] { LogPhase("kComposite"); });
		_phaseTokens[2] = Render::SubscribeFramePhase(Render::Phase::kAfterOpaque, "DLSS/ProbeAfterOpaque", [this] { LogPhase("kAfterOpaque"); });

		Upscale::Hooks::SetHandler(this);
		_handlerSet = true;
		REX::INFO("DLSS: running, TAA was {}, FXAA was {}", _taaWas, _fxaaWas);
		return true;
	}

	bool DLSS::MenuForcesNative() const noexcept
	{
		auto* const ui = RE::UI::GetSingleton();
		return ui != nullptr && (ui->GetMenuOpen("PipboyMenu") || ui->GetMenuOpen("ExamineMenu"));
	}

	bool DLSS::TakeCameraCut() noexcept
	{
		auto* const world = RE::Main::WorldRootCamera();
		if (world == nullptr) {
			return false;
		}
		const auto& translate = world->GetWorldTransform().translate;
		const auto camera = Render::FogCameraFromMatrix(world->worldToCam, translate.z);
		const auto right = camera.rightOverScaleX;
		const auto halfWidth = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
		const Upscale::CameraState current{
			{ translate.x, translate.y, translate.z },
			{ camera.forward[0], camera.forward[1], camera.forward[2] },
			2.0 * std::atan(halfWidth) * 180.0 / 3.14159265358979323846,
		};
		const bool cut = _haveCamera && Upscale::IsCameraCut(_camera, current);
		_camera = current;
		_haveCamera = true;
		return cut;
	}

	void DLSS::AfterTemporalData() noexcept
	{
		auto* const state = RE::BSGraphics::State::GetSingleton();
		if (state == nullptr) {
			return;
		}
		const Render::Extent display{ state->screenWidth, state->screenHeight };
		const bool native = MenuForcesNative();
		_active = !native;
		if (_wasNative && _active) {
			// Back from a menu that rendered natively: the history is stale.
			_reset = true;
		}
		_wasNative = native;

		const auto chosen = Upscale::ModeFromName(Settings::GetString("DLSS/mode")).value_or(Upscale::Mode::kQuality);
		// ENB does not survive the dynamic resolution; with it, DLAA only.
		_mode = _enb ? Upscale::Mode::kDLAA : chosen;
		// Stand-ins and samplers follow the chosen mode, not the menu: the
		// Pip-Boy renders natively for a moment, and rebuilding twelve
		// targets on each opening would be a hitch each time (doodlum keeps
		// them the same way).
		const bool modeUpscales = _mode != Upscale::Mode::kDLAA;
		_display = display;
		auto render = modeUpscales ? _ngx.OptimalRenderSize(display, _mode).value_or(Upscale::RenderSize(display, _mode)) : display;
		const auto ratioX = static_cast<float>(render.width) / static_cast<float>(display.width);
		const auto ratioY = static_cast<float>(render.height) / static_cast<float>(display.height);
		if (!_proxies.Update(ratioX, ratioY) ||
			!_proxies.UpdateSamplers(modeUpscales ? Upscale::MipBias(render, display) : 0.0f)) {
			render = display;
		}
		const bool wasUpscaling = _upscaling;
		_upscaling = _active && modeUpscales && render != display;
		_render = _upscaling ? render : display;
		if (_upscaling != wasUpscaling) {
			_reset = true;
		}
		Upscale::Hooks::SkipDynamicResolutionUpdate(_upscaling);
		SetRatios(_upscaling ? ratioX : 1.0f, _upscaling ? ratioY : 1.0f);
		Render::Resolution::Publish(_display, _render);
		if (!_active) {
			state->offsetX = 0.0f;
			state->offsetY = 0.0f;
			return;
		}

		const auto phases = Upscale::JitterPhaseCount(_render, _display);
		_jitter = Upscale::JitterOffset(state->frameCount, phases);
		const auto offset = Upscale::ToEngineOffset(_jitter, _display);
		state->offsetX = offset.x;
		state->offsetY = offset.y;
		if (TakeCameraCut()) {
			_reset = true;
		}
	}

	bool DLSS::AllowTemporalAA() noexcept
	{
		return !_active;
	}

	void DLSS::SetRatios(float a_x, float a_y) noexcept
	{
		auto* const manager = RE::BSGraphics::RenderTargetManager::GetSingleton();
		if (manager == nullptr) {
			return;
		}
		// Only the ratios, as doodlum sets them; whether the engine counts
		// its dynamic resolution as active stays its own business.
		manager->SetDynamicResolutionState(a_x, a_y, manager->IsDynamicResolutionCurrentlyActivated());
		_ratioX = a_x;
		_ratioY = a_y;
	}

	void DLSS::OverrideDepth() noexcept
	{
		auto* const data = RE::BSGraphics::GetRendererData();
		auto* const context = Render::GetContext();
		if (_depthOverridden || data == nullptr || context == nullptr) {
			return;
		}
		auto& depth = data->depthStencilTargets[Render::Targets::kSceneDepth];
		// Once a frame, at the first override: the composite comes before
		// the effects, and both see the same depth.
		const auto frame = Render::FrameCount();
		if (_fullDepthFrame != frame) {
			_fullDepthFrame = frame;
			static_cast<void>(_passes.EnsureTextures(_render, _display));
			_passes.FullDepth(*context, depth.srViewDepth, Render::Targets::RenderTargetUAV(kLinearDepth), _render, _display);
		}
		if (_passes.FullDepthSRV() == nullptr) {
			return;
		}
		_savedDepthView = depth.srViewDepth;
		depth.srViewDepth = _passes.FullDepthSRV();
		_depthOverridden = true;
		Render::Resolution::SetDepthFull(true);
	}

	void DLSS::ResetDepth() noexcept
	{
		auto* const data = RE::BSGraphics::GetRendererData();
		if (!_depthOverridden || data == nullptr) {
			return;
		}
		data->depthStencilTargets[Render::Targets::kSceneDepth].srViewDepth = _savedDepthView;
		_savedDepthView = nullptr;
		_depthOverridden = false;
		Render::Resolution::SetDepthFull(false);
	}

	void DLSS::BeforeDynamicViewportDefault() noexcept
	{
		if (_upscaling) {
			ResetDepth();
		}
	}

	void DLSS::AroundWorldPass(bool a_before) noexcept
	{
		if (!_upscaling) {
			return;
		}
		if (a_before) {
			_proxies.BiasSamplers();
		} else {
			_proxies.RestoreSamplers();
		}
	}

	void DLSS::AroundComposite(bool a_before) noexcept
	{
		auto* const context = Render::GetContext();
		if (!_upscaling || context == nullptr) {
			return;
		}
		// doodlum's DrawWorld_DeferredComposite_RenderPassImmediately: the
		// composite reads stand-ins of the render size at a ratio of one.
		if (a_before) {
			++_composites;
			_proxies.Swap(*context);
			OverrideDepth();
			const auto x = _ratioX;
			const auto y = _ratioY;
			SetRatios(1.0f, 1.0f);
			_ratioX = x;
			_ratioY = y;
			Upscale::Hooks::SetDynamicResolutionViewport(false);
		} else {
			ResetDepth();
			_proxies.Restore(*context);
			Upscale::Hooks::SetDynamicResolutionViewport(true);
			SetRatios(_ratioX, _ratioY);
		}
	}

	void DLSS::AroundLensFlare(bool a_before) noexcept
	{
		if (!_upscaling) {
			return;
		}
		if (a_before) {
			++_lensFlares;
			OverrideDepth();
		} else {
			ResetDepth();
		}
	}

	void DLSS::AfterReflectionsSetup() noexcept
	{
		auto* const context = Render::GetContext();
		if (_upscaling && context != nullptr && _passes.Reflections() != nullptr) {
			context->PSSetShader(_passes.Reflections(), nullptr, 0);
			++_reflections;
		}
	}

	void DLSS::AfterEffectRange() noexcept
	{
		if (!_active) {
			return;
		}
		auto* const context = Render::GetContext();
		auto* const scene = Render::Targets::RenderTargetTexture(Render::Targets::kSceneHDR);
		auto* const sceneUAV = Render::Targets::RenderTargetUAV(Render::Targets::kSceneHDR);
		auto* const motion = Render::Targets::RenderTargetSRV(kMotionVectors);
		auto* const motionTexture = Render::Targets::RenderTargetTexture(kMotionVectors);
		auto* const depthSRV = Render::Targets::DepthSRV(Render::Targets::kSceneDepth);
		auto* const depth = Render::Targets::DepthTexture(Render::Targets::kSceneDepth);
		if (context == nullptr || scene == nullptr || sceneUAV == nullptr || motion == nullptr || depthSRV == nullptr || depth == nullptr) {
			return;
		}

		const Render::PassScope scope{ "DLSS/Evaluate" };
		if (_upscaling) {
			// From here on the picture is at display size (doodlum's
			// DrawWorld_Imagespace_RenderEffectRange).
			Upscale::Hooks::SetDynamicResolutionViewport(false);
		}
		context->OMSetRenderTargets(0, nullptr, nullptr);
		if (!_passes.EnsureTextures(_render, _display) || !_ngx.Ensure(*context, _render, _display, _mode, _preset)) {
			++_failedEvaluations;
			return;
		}

		if (motionTexture != nullptr) {
			_motionProbe.Capture(*context, reinterpret_cast<REX::W32::ID3D11Resource*>(motionTexture), _render.width, _render.height, Render::FrameCount());
		}
		_passes.DilateMotionVectors(*context, motion, depthSRV, _render, _display);
		_passes.CopyColor(*context, reinterpret_cast<REX::W32::ID3D11Resource*>(scene), _render);

		const Upscale::EvaluateInputs inputs{
			_passes.ColorInput(),
			reinterpret_cast<REX::W32::ID3D11Resource*>(scene),
			reinterpret_cast<REX::W32::ID3D11Resource*>(depth),
			_passes.MotionVectors(),
			_render,
			-_jitter.x,
			-_jitter.y,
			kMotionScaleX * static_cast<float>(_render.width),
			kMotionScaleY * static_cast<float>(_render.height),
			_reset,
		};
		if (_ngx.Evaluate(*context, inputs)) {
			++_evaluations;
		} else {
			++_failedEvaluations;
		}
		if (_reset) {
			++_resets;
			_reset = false;
		}

		const auto sharpness = static_cast<float>(Settings::GetDouble("DLSS/sharpness"));
		if (sharpness > 0.0f) {
			_passes.Sharpen(*context, reinterpret_cast<REX::W32::ID3D11Resource*>(scene), sceneUAV, sharpness, _display);
		}
		if (_upscaling) {
			OverrideDepth();
		}
	}

	void DLSS::LogMotionProbe() noexcept
	{
		auto* const context = Render::GetContext();
		if (context == nullptr) {
			return;
		}
		std::array<std::uint8_t, Render::TexelProbe::kTexels * 4> bytes{};
		if (!_motionProbe.Read(*context, Render::FrameCount(), bytes)) {
			return;
		}
		float minimum[2]{ 1e30f, 1e30f };
		float maximum[2]{ -1e30f, -1e30f };
		double sum[2]{};
		for (std::size_t i = 0; i < Render::TexelProbe::kTexels; ++i) {
			for (std::size_t c = 0; c < 2; ++c) {
				std::uint16_t half = 0;
				std::memcpy(std::addressof(half), bytes.data() + i * 4 + c * 2, sizeof(half));
				const auto value = HalfToFloat(half);
				minimum[c] = std::min(minimum[c], value);
				maximum[c] = std::max(maximum[c], value);
				sum[c] += value;
			}
		}
		const auto count = static_cast<double>(Render::TexelProbe::kTexels);
		REX::INFO(
			"dlss probe: motion vectors x [{:.6f} {:.6f} mean {:.6f}] y [{:.6f} {:.6f} mean {:.6f}]",
			minimum[0], maximum[0], sum[0] / count, minimum[1], maximum[1], sum[1] / count);
	}

	void DLSS::LogPhase(const char* a_name) noexcept
	{
		const auto index = a_name[1] == 'B' ? 0 : (a_name[1] == 'C' ? 1 : 2);
		if (!_phaseDue[index]) {
			return;
		}
		_phaseDue[index] = false;
		auto* const context = Render::GetContext();
		if (context == nullptr) {
			return;
		}
		const auto sizeOf = [](REX::W32::ID3D11Texture2D* a_texture) {
			if (a_texture == nullptr) {
				return std::string{ "none" };
			}
			REX::W32::D3D11_TEXTURE2D_DESC desc{};
			a_texture->GetDesc(std::addressof(desc));
			return std::format("{}x{}", desc.width, desc.height);
		};
		std::uint32_t count = 1;
		REX::W32::D3D11_VIEWPORT viewport{};
		context->RSGetViewports(std::addressof(count), std::addressof(viewport));
		std::string depth{ "none" };
		if (auto* const view = Render::Targets::DepthSRV(Render::Targets::kSceneDepth); view != nullptr) {
			REX::W32::ID3D11Resource* resource = nullptr;
			view->GetResource(std::addressof(resource));
			if (resource != nullptr) {
				depth = sizeOf(reinterpret_cast<REX::W32::ID3D11Texture2D*>(resource));
				resource->Release();
			}
		}
		REX::INFO(
			"dlss phase {}: viewport {:.0f}x{:.0f}, RT_004 {}, RT_028 {} (rtv {}), RT_058 {}, depth {} full {}",
			a_name,
			count > 0 ? viewport.width : 0.0f,
			count > 0 ? viewport.height : 0.0f,
			sizeOf(Render::Targets::RenderTargetTexture(Render::Targets::kSceneHDR)),
			sizeOf(Render::Targets::RenderTargetTexture(28)),
			Render::Targets::RenderTargetView(28) != nullptr,
			sizeOf(Render::Targets::RenderTargetTexture(Render::Targets::kLightDiffuse)),
			depth,
			Render::Resolution::Current().depthFull);
	}

	void DLSS::Frame()
	{
		if (_passes.PollReload()) {
			_reset = true;
		}
		_preset = Upscale::PresetFromName(Settings::GetString("DLSS/preset")).value_or(Upscale::Preset::kDefault);

		++_frames;
		if (_frames % kLogInterval == 0) {
			REX::INFO(
				"dlss: mode {} {}x{} -> {}x{}, jitter [{:.3f} {:.3f}], {} evaluation(s), {} failed, {} reset(s), native {}, "
				"composite {}, lens flare {}, reflections {}",
				Settings::GetString("DLSS/mode"),
				_render.width, _render.height, _display.width, _display.height,
				_jitter.x, _jitter.y, _evaluations, _failedEvaluations, _resets, !_active,
				_composites, _lensFlares, _reflections);
			_composites = 0;
			_lensFlares = 0;
			_reflections = 0;
			LogMotionProbe();
			for (auto& due : _phaseDue) {
				due = true;
			}
			_evaluations = 0;
			_failedEvaluations = 0;
			_resets = 0;
		}
	}

	void DLSS::Shutdown()
	{
		// First, before anything a hook could touch goes away.
		if (_handlerSet) {
			Upscale::Hooks::SetHandler(nullptr);
			_handlerSet = false;
		}
		Render::UnsubscribeFramePhase(Render::Phase::kBeforeComposite, _phaseTokens[0]);
		Render::UnsubscribeFramePhase(Render::Phase::kComposite, _phaseTokens[1]);
		Render::UnsubscribeFramePhase(Render::Phase::kAfterOpaque, _phaseTokens[2]);
		for (auto& token : _phaseTokens) {
			token = Render::PhaseDispatcher::kNoToken;
		}
		if (auto* const state = RE::BSGraphics::State::GetSingleton(); state != nullptr) {
			state->offsetX = 0.0f;
			state->offsetY = 0.0f;
		}
		if (_engineSwitched) {
			*TAAFlag() = _taaWas;
			if (auto* const fxaa = FXAAEffect(); fxaa != nullptr) {
				fxaa->isActive = _fxaaWas;
			}
			_engineSwitched = false;
		}
		_active = false;
		ResetDepth();
		Upscale::Hooks::SkipDynamicResolutionUpdate(false);
		SetRatios(1.0f, 1.0f);
		_upscaling = false;
		_proxies.Release(Render::GetContext());
		Render::Resolution::Withdraw();
		_passes.Release();
		_motionProbe.Release();
		_ngx.Shutdown();
	}
}
