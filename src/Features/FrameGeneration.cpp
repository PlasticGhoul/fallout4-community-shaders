#include "Features/FrameGeneration.h"

#include "Bridge/SimulationHook.h"
#include "Menu/MenuSystem.h"
#include "Render/Camera.h"
#include "Render/DebugName.h"
#include "Render/Profiler.h"
#include "Render/Renderer.h"
#include "Render/Resolution.h"
#include "Render/Targets.h"
#include "Settings/Settings.h"
#include "Shader/ShaderCompiler.h"
#include "Shader/ShaderSource.h"
#include "Util/GamePaths.h"

#include <RE/B/BSGraphics.h>
#include <RE/M/Main.h>
#include <RE/N/NiCamera.h>
#include <RE/U/UI.h>

#include <cmath>
#include <format>
#include <vector>

namespace Features
{
	namespace
	{
		constexpr std::uint64_t kLogInterval = 180;

		/// FO4_RT_029, R16G16_FLOAT: the engine's motion vectors, in uv units.
		constexpr std::size_t kMotionVectors = 29;

		/// The engine's camera planes; DLSS reads the same two floats.
		constexpr std::uint64_t kCameraNear = 2712882;
		constexpr std::uint64_t kCameraFar = 2712883;
		/// The engine's TAA switch: without it the engine writes no motion
		/// vectors (F5.5a); DLSS forces it on while it runs.
		constexpr std::uint64_t kEnableTAA = 2704658;

		/// Field for field the cbuffer CopyDepth of CopyDepth.hlsl.
		struct CopyDepthConstants
		{
			std::uint32_t outputSize[2];
			std::uint32_t inputSize[2];
		};
		static_assert(sizeof(CopyDepthConstants) == 16);

		std::uint32_t Groups(std::uint32_t a_pixels) noexcept
		{
			return (a_pixels + 7) / 8;
		}
	}

	void FrameGeneration::Declare()
	{
		Settings::DeclareFeature("FrameGeneration", false)
			.Label("feature.framegeneration.name", "Frame Generation")
			.Help(
				"feature.framegeneration.help",
				"NVIDIA DLSS Frame Generation adds a generated frame after every rendered one for smoother motion, with "
				"NVIDIA Reflex keeping the delay down. Needs borderless windowed mode; switching it on takes effect at the "
				"next game start. Uses NVIDIA DLSS and Reflex technology.");
		Settings::DeclareChoice("FrameGeneration/reflex", "on", std::vector<std::string>{ "on", "boost" })
			.Label("feature.framegeneration.reflex", "Reflex")
			.Help("feature.framegeneration.reflex_help", "Low latency mode, or with boost, which also keeps the GPU clock up.");
		Settings::DeclareChoice("FrameGeneration/view", "off", std::vector<std::string>{ "off", "split" })
			.Label("feature.framegeneration.view", "Debug view")
			.Help(
				"feature.framegeneration.view_help",
				"Split shows the picture frame generation sees on the left half: it must have no HUD there.");
	}

	bool FrameGeneration::CompileShader() noexcept
	{
		constexpr const char* kFile = "FrameGeneration/CopyDepth.hlsl";
		auto* const device = Render::GetDevice();
		if (device == nullptr) {
			REX::ERROR("FrameGeneration: no device");
			return false;
		}
		const auto text = Shader::LoadSource(Util::DataDirectory() / "Shaders" / "FO4", kFile);
		if (!text) {
			REX::ERROR("FrameGeneration: {}", text.error());
			return false;
		}
		const auto result = Shader::CompileComputeShader(text->text, kFile, "main");
		if (!result.Succeeded()) {
			REX::ERROR("FrameGeneration: {}", result.diagnostics);
			return false;
		}
		if (device->CreateComputeShader(result.bytecode.data(), result.bytecode.size(), nullptr, std::addressof(_copyDepth)) < 0) {
			REX::ERROR("FrameGeneration: CreateComputeShader failed");
			return false;
		}
		static_cast<void>(Render::SetDebugName(reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_copyDepth), "FO4CS_CS_FrameGenerationCopyDepth"));
		return true;
	}

	bool FrameGeneration::Setup()
	{
		_frames = 0;
		_captures = 0;
		_hudless = 0;
		_refusedDepthMotion = 0;
		_refusedCamera = 0;
		_refusedHudless = 0;
		_generating = false;
		_blocker = "<start>";
		_reset = true;
		_havePrevious = false;
		_haveCamera = false;

		if (!Bridge::Active()) {
			REX::ERROR("FrameGeneration: no bridge - {}", Bridge::Refusal());
			return false;
		}
		if (!Upscale::Hooks::Installed()) {
			REX::ERROR("FrameGeneration: the engine hooks are not installed ({})", Upscale::Hooks::FailedSite());
			return false;
		}
		if (!Bridge::SimulationHookInstalled()) {
			REX::ERROR("FrameGeneration: no frame start for Reflex (PollInputDevices detour missing)");
			return false;
		}
		if (!CompileShader()) {
			return false;
		}
		if (!_constants.Create(sizeof(CopyDepthConstants), "FO4CS_CB_FrameGenerationCopyDepth")) {
			return false;
		}
		Upscale::Hooks::SetTap(this);
		_tapSet = true;
		REX::INFO("FrameGeneration: running");
		return true;
	}

	const char* FrameGeneration::Blocker() const noexcept
	{
		auto* const ui = RE::UI::GetSingleton();
		if (ui != nullptr && ui->GetMenuOpen("LoadingMenu")) {
			return "loading screen";
		}
		if (ui != nullptr && ui->GetMenuOpen("PipboyMenu")) {
			return "Pip-Boy";
		}
		if (auto* const main = RE::Main::GetSingleton(); main != nullptr && main->inMenuMode) {
			return "a game menu is open";
		}
		if (Menu::IsOverlayOpen()) {
			return "the overlay is open";
		}
		if (!*reinterpret_cast<const bool*>(REL::ID(kEnableTAA).address())) {
			return "the game's TAA is off, so it writes no motion vectors";
		}
		const auto status = Bridge::CurrentStatus();
		if (status.minWidthOrHeight != 0 && (status.width < status.minWidthOrHeight || status.height < status.minWidthOrHeight)) {
			return "the picture is smaller than DLSS-G accepts";
		}
		return nullptr;
	}

	void FrameGeneration::Frame()
	{
		++_frames;
		const char* const blocker = Blocker();
		const bool generate = blocker == nullptr;
		if (generate && !_generating) {
			// History from before an off stretch is not this frame's.
			_reset = true;
		}
		_generating = generate;
		Bridge::SetGeneration(generate);
		Bridge::SetReflexMode(Settings::GetString("FrameGeneration/reflex") == "boost" ? 2u : 1u);
		Bridge::SetSplitView(Settings::GetString("FrameGeneration/view") == "split");

		const std::string reason = blocker != nullptr ? blocker : "";
		if (reason != _blocker) {
			REX::INFO("FrameGeneration: {}", reason.empty() ? std::string{ "generating" } : std::format("off - {}", reason));
			_blocker = reason;
		}

		if (_frames % kLogInterval == 0) {
			const auto status = Bridge::CurrentStatus();
			REX::INFO(
				"frame generation: rendered {:.0f}/s, shown {:.0f}/s, {}, DLSS-G status 0x{:X}, captures {} depth+motion / {} "
				"hud-less, refused {} depth+motion / {} camera / {} hud-less",
				status.renderedPerSecond, status.shownPerSecond, status.generating ? "generating" : "not generating", status.dlssgStatus,
				_captures, _hudless, _refusedDepthMotion, _refusedCamera, _refusedHudless);
			_captures = 0;
			_hudless = 0;
			_refusedDepthMotion = 0;
			_refusedCamera = 0;
			_refusedHudless = 0;
		}
	}

	void FrameGeneration::AfterTemporalData() noexcept
	{
		Bridge::OnRenderStart();
	}

	void FrameGeneration::AfterEffectRange() noexcept
	{
		if (_generating) {
			const Render::PassScope pass{ "FrameGeneration/Capture"sv };
			CaptureDepthAndMotion();
		}
	}

	void FrameGeneration::CaptureDepthAndMotion() noexcept
	{
		Bridge::InputSlot slot{};
		auto* const context = Render::GetContext();
		auto* const motion = Render::Targets::RenderTargetTexture(kMotionVectors);
		auto* const depth = Render::Targets::DepthSRV(Render::Targets::kSceneDepth);
		if (!Bridge::CurrentSlot(slot) || context == nullptr || motion == nullptr || depth == nullptr || _copyDepth == nullptr) {
			++_refusedDepthMotion;
			return;
		}
		const auto frame = Render::Resolution::Current();
		const auto depthExtent = Render::Resolution::DepthExtent(frame);
		const auto motionExtent = frame.render;

		REX::W32::D3D11_TEXTURE2D_DESC motionDesc{};
		motion->GetDesc(std::addressof(motionDesc));
		if (motionDesc.format != REX::W32::DXGI_FORMAT_R16G16_FLOAT || motionExtent.width > slot.width || motionExtent.height > slot.height ||
			depthExtent.width > slot.width || depthExtent.height > slot.height) {
			++_refusedDepthMotion;
			return;
		}

		const REX::W32::D3D11_BOX box{ 0, 0, 0, motionExtent.width, motionExtent.height, 1 };
		context->CopySubresourceRegion(static_cast<REX::W32::ID3D11Resource*>(slot.motion), 0, 0, 0, 0, motion, 0, std::addressof(box));

		// Depth goes out at the motion vectors' size: DLSS-G takes the two as
		// one pair, and while DLSS upscales, the depth bound here is DLSS's
		// display-size one (DLSS::AfterEffectRange runs first).
		const CopyDepthConstants constants{ { motionExtent.width, motionExtent.height }, { depthExtent.width, depthExtent.height } };
		static_cast<void>(_constants.Update(std::addressof(constants), sizeof(constants)));
		auto* const buffer = _constants.Buffer();
		auto* uav = static_cast<REX::W32::ID3D11UnorderedAccessView*>(slot.depthUAV);
		context->CSSetShaderResources(0, 1, std::addressof(depth));
		context->CSSetUnorderedAccessViews(0, 1, std::addressof(uav), nullptr);
		context->CSSetConstantBuffers(0, 1, std::addressof(buffer));
		context->CSSetShader(_copyDepth, nullptr, 0);
		context->Dispatch(Groups(motionExtent.width), Groups(motionExtent.height), 1);
		REX::W32::ID3D11ShaderResourceView* noView = nullptr;
		REX::W32::ID3D11UnorderedAccessView* noUAV = nullptr;
		REX::W32::ID3D11Buffer* noBuffer = nullptr;
		context->CSSetShaderResources(0, 1, std::addressof(noView));
		context->CSSetUnorderedAccessViews(0, 1, std::addressof(noUAV), nullptr);
		context->CSSetConstantBuffers(0, 1, std::addressof(noBuffer));
		context->CSSetShader(nullptr, nullptr, 0);

		++_captures;
		SubmitCamera(motionExtent.width, motionExtent.height, motionExtent.width, motionExtent.height);
	}

	bool FrameGeneration::TakeCameraCut(const float (&a_worldToCam)[4][4], const float (&a_position)[3]) noexcept
	{
		const auto camera = Render::FogCameraFromMatrix(a_worldToCam, a_position[2]);
		const auto right = camera.rightOverScaleX;
		const auto halfWidth = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
		const Upscale::CameraState current{
			{ a_position[0], a_position[1], a_position[2] },
			{ camera.forward[0], camera.forward[1], camera.forward[2] },
			2.0 * std::atan(halfWidth) * 180.0 / 3.14159265358979323846,
		};
		const bool cut = _haveCamera && Upscale::IsCameraCut(_camera, current);
		_camera = current;
		_haveCamera = true;
		return cut;
	}

	void FrameGeneration::SubmitCamera(std::uint32_t a_depthWidth, std::uint32_t a_depthHeight, std::uint32_t a_motionWidth, std::uint32_t a_motionHeight) noexcept
	{
		auto* const world = RE::Main::WorldRootCamera();
		auto* const state = RE::BSGraphics::State::GetSingleton();
		if (world == nullptr || state == nullptr) {
			return;
		}
		const auto& translate = world->GetWorldTransform().translate;
		const float position[3]{ translate.x, translate.y, translate.z };

		Bridge::CameraFrame current{};
		for (std::size_t i = 0; i < 4; ++i) {
			for (std::size_t j = 0; j < 4; ++j) {
				current.worldToClip[i][j] = world->worldToCam[i][j];
			}
		}
		current.position = { position[0], position[1], position[2] };

		if (TakeCameraCut(world->worldToCam, position)) {
			_reset = true;
		}

		const auto nearPlane = *reinterpret_cast<const float*>(REL::ID(kCameraNear).address());
		const auto farPlane = *reinterpret_cast<const float*>(REL::ID(kCameraFar).address());
		const auto jitter = Bridge::JitterFromEngineOffset(state->offsetX, state->offsetY, state->screenWidth, state->screenHeight);
		// After a reset the frame before is not this one's history.
		const auto* const previous = _havePrevious && !_reset ? &_previous : nullptr;
		const auto built = Bridge::BuildConstants(current, previous, nearPlane, farPlane, jitter, _reset);
		_previous = current;
		_havePrevious = true;
		if (!built) {
			++_refusedCamera;
			return;
		}
		Bridge::FrameInputs inputs{};
		inputs.constants = *built;
		inputs.depthWidth = a_depthWidth;
		inputs.depthHeight = a_depthHeight;
		inputs.motionWidth = a_motionWidth;
		inputs.motionHeight = a_motionHeight;
		Bridge::SubmitFrameInputs(inputs);
		_reset = false;
	}

	void FrameGeneration::AfterDynamicViewportDefault(bool a_enabled) noexcept
	{
		// doodlum takes the picture at the call that switches the dynamic
		// viewport off; the other calls of this site are not the end of
		// post-processing.
		if (a_enabled || !_generating) {
			return;
		}
		const Render::PassScope pass{ "FrameGeneration/Hudless"sv };
		// Straight from the proxy's back buffer: that is what the game got
		// from GetBuffer(0), and what doodlum reads as renderTargets[0]. In
		// Fallout 4 RT_000's texture is null at this point (run of 2026-10-17).
		auto* const backBuffer = static_cast<REX::W32::ID3D11Texture2D*>(Bridge::BackBuffer());
		Bridge::InputSlot slot{};
		auto* const context = Render::GetContext();
		if (!Bridge::CurrentSlot(slot) || context == nullptr || backBuffer == nullptr) {
			++_refusedHudless;
			return;
		}
		auto* const hudless = static_cast<REX::W32::ID3D11Texture2D*>(slot.hudless);
		context->CopyResource(hudless, backBuffer);
		Bridge::SubmitHudless();
		++_hudless;
	}

	void FrameGeneration::Shutdown()
	{
		Bridge::SetGeneration(false);
		Bridge::SetSplitView(false);
		// Boost keeps the GPU clock up; that must not outlive the switch.
		Bridge::SetReflexMode(1);
		if (_tapSet) {
			Upscale::Hooks::SetTap(nullptr);
			_tapSet = false;
		}
		if (_copyDepth != nullptr) {
			_copyDepth->Release();
			_copyDepth = nullptr;
		}
		_constants.Release();
		_generating = false;
	}
}
