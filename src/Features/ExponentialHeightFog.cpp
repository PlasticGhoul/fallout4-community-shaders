#include "Features/ExponentialHeightFog.h"

#include "Render/Camera.h"
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

#include <RE/B/BSGraphics.h>
#include <RE/M/Main.h>
#include <RE/N/NiCamera.h>
#include <RE/N/NiLight.h>
#include <RE/S/Sky.h>

// RE/S/Sun.h is not self-contained; see ScreenSpaceShadows.cpp for the
// account. Only NiLight is read through the pointers it declares.
#include <RE/S/SkyObject.h>

namespace RE
{
	class BSShaderAccumulator;
	class BSTriShape;
	class NiBillboardNode;
	class NiDirectionalLight;
}

#include <RE/S/Sun.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace Features
{
	namespace
	{
		constexpr auto kShaderFile = "ExponentialHeightFog/Fog.hlsl";
		constexpr std::uint64_t kLogInterval = 180;

		/// Field for field the cbuffer PerFrame of Fog.hlsl: thirteen float4.
		struct alignas(16) FogConstants
		{
			float cameraForward[4];
			float cameraRight[4];
			float cameraUp[4];
			float sunDirection[4];
			float sunColor[4];
			float fogRange[4];
			float fogHeightRange[4];
			float fogNearLow[4];
			float fogNearHigh[4];
			float fogFarLow[4];
			float fogFarHigh[4];
			float params[4];
			float screen[4];
		};
		static_assert(sizeof(FogConstants) == 13 * 16);

		std::filesystem::path ShaderRoot()
		{
			return Util::DataDirectory() / "Shaders" / "FO4";
		}
	}

	void ExponentialHeightFog::Declare()
	{
		Settings::DeclareFeature("ExponentialHeightFog", true)
			.Label("feature.exponential_height_fog.name", "Exponential Height Fog")
			.Help(
				"feature.exponential_height_fog.help",
				"Fog that lies in low ground and thins with height, drawn over the finished "
				"scene in the game's own fog colour, with the sun glowing through it.");

		// 0.03, not the template's 0.005: at Sanctuary the camera stands
		// almost eight thousand units above a fog height of zero, which the
		// falloff turns into a third of the density, and the ground in view is
		// five to twenty thousand units away. The template's value gives under
		// one percent of cover there - the first run showed no difference
		// switching it on and off. This gives a few percent, visibly a layer.
		Settings::DeclareSlider("ExponentialHeightFog/density", 0.03, 0.0, 0.2)
			.Label("feature.exponential_height_fog.density", "Density")
			.Help("feature.exponential_height_fog.density_help", "How thick the fog is at its base.");

		Settings::DeclareSlider("ExponentialHeightFog/height", 0.0, -22000.0, 22000.0)
			.Label("feature.exponential_height_fog.height", "Height")
			.Help(
				"feature.exponential_height_fog.height_help",
				"World height above which the fog thins. Sanctuary lies at about 7900.");

		Settings::DeclareSlider("ExponentialHeightFog/heightFalloff", 0.2, 0.001, 2.0)
			.Label("feature.exponential_height_fog.height_falloff", "Height Falloff")
			.Help(
				"feature.exponential_height_fog.height_falloff_help",
				"How quickly the fog thins with height.");

		Settings::DeclareSlider("ExponentialHeightFog/startDistance", 0.0, 0.0, 100000.0)
			.Label("feature.exponential_height_fog.start_distance", "Start Distance")
			.Help(
				"feature.exponential_height_fog.start_distance_help",
				"Distance from the camera kept free of fog.");

		Settings::DeclareSlider("ExponentialHeightFog/sunInscattering", 1.0, 0.0, 10.0)
			.Label("feature.exponential_height_fog.sun_inscattering", "Sun Inscattering")
			.Help(
				"feature.exponential_height_fog.sun_inscattering_help",
				"How strongly the sun glows through the fog.");

		Settings::DeclareSlider("ExponentialHeightFog/sunAnisotropy", 0.2, -0.99, 0.99)
			.Label("feature.exponential_height_fog.sun_anisotropy", "Sun Anisotropy")
			.Help(
				"feature.exponential_height_fog.sun_anisotropy_help",
				"How tightly the glow gathers around the sun. Positive is forward scattering.");

		Settings::DeclareSlider("ExponentialHeightFog/vanillaFog", 1.0, 0.0, 1.0)
			.Label("feature.exponential_height_fog.vanilla_fog", "Vanilla Fog")
			.Help(
				"feature.exponential_height_fog.vanilla_fog_help",
				"How much of the game's own fog remains. One leaves it untouched.");
	}

	bool ExponentialHeightFog::Setup()
	{
		_reportedNoSun = false;
		_reportedNoTargets = false;
		_reportedNoCamera = false;
		_draws = 0;

		if (!Render::InitFullscreenPass()) {
			return false;
		}
		if (!Compile()) {
			return false;
		}
		if (!EnsureBlendState()) {
			return false;
		}
		if (!_constants.Create(sizeof(FogConstants), "FO4CS_CB_ExponentialHeightFog")) {
			return false;
		}

		_phase = Render::SubscribeFramePhase(
			Render::Phase::kAfterOpaque, "ExponentialHeightFog/Draw", [this] { Draw(); });
		if (_phase == Render::PhaseDispatcher::kNoToken) {
			REX::ERROR("ExponentialHeightFog: the frame phase has no room left");
			return false;
		}

		return true;
	}

	void ExponentialHeightFog::Frame()
	{
		if (_watch.Poll()) {
			REX::INFO("ExponentialHeightFog: shader changed, recompiling");
			static_cast<void>(Compile());
		}

		// Written from Present, it survives to the composite with one frame
		// of delay: measured on 2026-09-12, the first outcome the spec named.
		ApplyVanillaFog();
	}

	void ExponentialHeightFog::Shutdown()
	{
		Render::UnsubscribeFramePhase(Render::Phase::kAfterOpaque, _phase);
		_phase = Render::PhaseDispatcher::kNoToken;

		RestoreVanillaFog();

		if (_shader != nullptr) {
			_shader->Release();
			_shader = nullptr;
		}
		if (_alpha != nullptr) {
			_alpha->Release();
			_alpha = nullptr;
		}
		_constants.Release();
	}

	void ExponentialHeightFog::Draw()
	{
		++_draws;

		const auto* const sky = RE::Sky::GetSingleton();
		if (sky == nullptr || sky->mode.get() != RE::Sky::Mode::kFull ||
			sky->sun == nullptr || sky->sun->light.get() == nullptr) {
			if (!_reportedNoSun) {
				REX::INFO("ExponentialHeightFog: no sun, standing down");
				_reportedNoSun = true;
			}
			return;
		}
		_reportedNoSun = false;

		auto* const depth = Render::Targets::DepthSRV(Render::Targets::kSceneDepth);
		auto* const scene = Render::Targets::RenderTargetView(Render::Targets::kSceneHDR);
		auto* const sceneTexture = Render::Targets::RenderTargetTexture(Render::Targets::kSceneHDR);
		if (depth == nullptr || scene == nullptr || sceneTexture == nullptr) {
			if (!_reportedNoTargets) {
				REX::ERROR(
					"ExponentialHeightFog: standing down, depth {}, scene {}",
					depth != nullptr ? "ok" : "missing",
					scene != nullptr ? "ok" : "missing");
				_reportedNoTargets = true;
			}
			return;
		}
		_reportedNoTargets = false;

		auto* const world = RE::Main::WorldRootCamera();
		const auto* const state = RE::BSGraphics::State::GetSingleton();
		if (world == nullptr || state == nullptr) {
			if (!_reportedNoCamera) {
				REX::ERROR("ExponentialHeightFog: no world camera or renderer state");
				_reportedNoCamera = true;
			}
			return;
		}
		_reportedNoCamera = false;

		auto* const context = Render::GetContext();
		if (context == nullptr || _shader == nullptr) {
			return;
		}

		float matrix[16]{};
		std::memcpy(matrix, world->worldToCam, sizeof(matrix));
		if (!Render::IsPlausibleViewProjection(matrix)) {
			return;
		}

		const auto camera = Render::FogCameraFromMatrix(
			world->worldToCam, world->GetWorldTransform().translate.z);

		// Row zero of the light's rotation is the direction the light travels;
		// negated it points at the sun. Measured in F2.
		const auto* const light = reinterpret_cast<const RE::NiLight*>(sky->sun->light.get());
		const auto& rotate = light->GetWorldRotate();
		float towardsSun[3]{ -rotate.entry[0][0], -rotate.entry[0][1], -rotate.entry[0][2] };
		const auto length = std::sqrt(
			towardsSun[0] * towardsSun[0] + towardsSun[1] * towardsSun[1] + towardsSun[2] * towardsSun[2]);
		if (length < 1.0e-4f) {
			return;
		}
		for (auto& component : towardsSun) {
			component /= length;
		}

		REX::W32::D3D11_TEXTURE2D_DESC desc{};
		sceneTexture->GetDesc(std::addressof(desc));

		const auto& fog = state->fogState;

		FogConstants data{};
		data.cameraForward[0] = camera.forward[0];
		data.cameraForward[1] = camera.forward[1];
		data.cameraForward[2] = camera.forward[2];
		data.cameraForward[3] = camera.height;
		data.cameraRight[0] = camera.rightOverScaleX[0];
		data.cameraRight[1] = camera.rightOverScaleX[1];
		data.cameraRight[2] = camera.rightOverScaleX[2];
		data.cameraRight[3] = camera.near;
		data.cameraUp[0] = camera.upOverScaleY[0];
		data.cameraUp[1] = camera.upOverScaleY[1];
		data.cameraUp[2] = camera.upOverScaleY[2];
		// Free since the sky stopped having a distance of its own; the shader
		// caps ground and sky alike at the game's far fog distance.
		data.cameraUp[3] = 0.0f;
		data.sunDirection[0] = towardsSun[0];
		data.sunDirection[1] = towardsSun[1];
		data.sunDirection[2] = towardsSun[2];
		data.sunDirection[3] = static_cast<float>(Settings::GetDouble("ExponentialHeightFog/sunInscattering"));
		data.sunColor[0] = light->diff.r;
		data.sunColor[1] = light->diff.g;
		data.sunColor[2] = light->diff.b;
		data.sunColor[3] = static_cast<float>(Settings::GetDouble("ExponentialHeightFog/sunAnisotropy"));

		// rangeData holds raw distances, near and far, not the ramp
		// coefficients the composite's constant buffer carries - the trace of
		// 2026-09-12 read [1600 250000 64 15000]. The shader builds the ramp.
		// The clamp is the engine's own, not the one we may have dimmed.
		data.fogRange[0] = fog.rangeData.x;
		data.fogRange[1] = fog.rangeData.y;
		data.fogRange[2] = fog.power;
		data.fogRange[3] = VanillaClamp(fog.clamp);
		data.fogHeightRange[0] = fog.highLowRangeData.x;
		data.fogHeightRange[1] = fog.highLowRangeData.y;
		data.fogHeightRange[2] = fog.highLowRangeData.z;
		data.fogHeightRange[3] = fog.highLowRangeData.w;
		data.fogNearLow[0] = fog.nearLowColor.r;
		data.fogNearLow[1] = fog.nearLowColor.g;
		data.fogNearLow[2] = fog.nearLowColor.b;
		data.fogNearHigh[0] = fog.nearHighColor.r;
		data.fogNearHigh[1] = fog.nearHighColor.g;
		data.fogNearHigh[2] = fog.nearHighColor.b;
		data.fogFarLow[0] = fog.farLowColor.r;
		data.fogFarLow[1] = fog.farLowColor.g;
		data.fogFarLow[2] = fog.farLowColor.b;
		data.fogFarHigh[0] = fog.farHighColor.r;
		data.fogFarHigh[1] = fog.farHighColor.g;
		data.fogFarHigh[2] = fog.farHighColor.b;
		data.params[0] = static_cast<float>(Settings::GetDouble("ExponentialHeightFog/density"));
		data.params[1] = static_cast<float>(Settings::GetDouble("ExponentialHeightFog/height"));
		data.params[2] = static_cast<float>(Settings::GetDouble("ExponentialHeightFog/heightFalloff"));
		data.params[3] = static_cast<float>(Settings::GetDouble("ExponentialHeightFog/startDistance"));
		data.screen[0] = static_cast<float>(desc.width);
		data.screen[1] = static_cast<float>(desc.height);
		data.screen[2] = 1.0f / static_cast<float>(desc.width);
		data.screen[3] = 1.0f / static_cast<float>(desc.height);

		if (_draws % kLogInterval == 0) {
			REX::INFO(
				"fog: camera height {:.1f}, near {:.2f}, forward [{:.3f} {:.3f} {:.3f}], "
				"vanilla clamp {:.3f} (engine {:.3f}), range [{:.0f} {:.0f}], nearLow [{:.3f} {:.3f} {:.3f}], "
				"sun [{:.3f} {:.3f} {:.3f}] colour [{:.3f} {:.3f} {:.3f}]",
				camera.height, camera.near,
				camera.forward[0], camera.forward[1], camera.forward[2],
				fog.clamp, data.fogRange[3],
				fog.rangeData.x, fog.rangeData.y,
				fog.nearLowColor.r, fog.nearLowColor.g, fog.nearLowColor.b,
				towardsSun[0], towardsSun[1], towardsSun[2],
				light->diff.r, light->diff.g, light->diff.b);
		}

		const Render::StateGuard guard;
		const Render::PassScope scope{ "ExponentialHeightFog/Fog" };

		// Unbind first: DS_002 may still hang on the output merger as the
		// depth view of whatever drew last, and we read it now.
		context->OMSetRenderTargets(0, nullptr, nullptr);

		static_cast<void>(_constants.Update(std::addressof(data), sizeof(data)));
		auto* const buffer = _constants.Buffer();

		context->OMSetRenderTargets(1, std::addressof(scene), nullptr);
		const float blendFactor[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
		context->OMSetBlendState(_alpha, blendFactor, 0xFFFFFFFFu);
		context->PSSetShaderResources(0, 1, std::addressof(depth));
		context->PSSetConstantBuffers(1, 1, std::addressof(buffer));
		context->PSSetShader(_shader, nullptr, 0);

		Render::DrawFullscreen();
	}

	bool ExponentialHeightFog::Compile()
	{
		const auto source = Shader::LoadSource(ShaderRoot(), kShaderFile);
		if (!source) {
			const std::filesystem::path only[] = { ShaderRoot() / kShaderFile };
			_watch.Reset(only);
			REX::ERROR("ExponentialHeightFog: {}", source.error());
			return false;
		}
		_watch.Reset(source->files);

		const auto compiled = Shader::CompilePixelShader(source->text, "Fog.hlsl", "main");
		if (!compiled.Succeeded()) {
			REX::ERROR("ExponentialHeightFog: {}", compiled.diagnostics);
			return false;
		}

		auto* const device = Render::GetDevice();
		REX::W32::ID3D11PixelShader* shader = nullptr;
		if (device == nullptr ||
			device->CreatePixelShader(
				compiled.bytecode.data(), compiled.bytecode.size(), nullptr, std::addressof(shader)) < 0) {
			REX::ERROR("ExponentialHeightFog: the fog shader could not be created");
			return false;
		}

		if (_shader != nullptr) {
			_shader->Release();
		}
		_shader = shader;
		static_cast<void>(Render::SetDebugName(
			reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_shader), "FO4CS_PS_ExponentialHeightFog"));
		REX::INFO("ExponentialHeightFog: fog shader compiled");
		return true;
	}

	bool ExponentialHeightFog::EnsureBlendState()
	{
		if (_alpha != nullptr) {
			return true;
		}
		auto* const device = Render::GetDevice();
		if (device == nullptr) {
			return false;
		}

		// dest = src * a + dest * (1 - a) for colour; alpha is left as the
		// target had it. R11G11B10 carries none, but a target that did would
		// keep its own.
		REX::W32::D3D11_BLEND_DESC desc{};
		desc.alphaToCoverageEnable = false;
		desc.independentBlendEnable = false;
		auto& target = desc.renderTarget[0];
		target.blendEnable = true;
		target.srcBlend = REX::W32::D3D11_BLEND_SRC_ALPHA;
		target.destBlend = REX::W32::D3D11_BLEND_INV_SRC_ALPHA;
		target.blendOp = REX::W32::D3D11_BLEND_OP_ADD;
		target.srcBlendAlpha = REX::W32::D3D11_BLEND_ZERO;
		target.destBlendAlpha = REX::W32::D3D11_BLEND_ONE;
		target.blendOpAlpha = REX::W32::D3D11_BLEND_OP_ADD;
		target.renderTargetWriteMask = static_cast<std::uint8_t>(REX::W32::D3D11_COLOR_WRITE_ENABLE_ALL);

		if (device->CreateBlendState(std::addressof(desc), std::addressof(_alpha)) < 0) {
			REX::ERROR("ExponentialHeightFog: the alpha blend state could not be created");
			return false;
		}
		return true;
	}

	void ExponentialHeightFog::ApplyVanillaFog()
	{
		auto* const state = RE::BSGraphics::State::GetSingleton();
		if (state == nullptr) {
			return;
		}

		const auto amount = static_cast<float>(
			std::clamp(Settings::GetDouble("ExponentialHeightFog/vanillaFog"), 0.0, 1.0));
		float& clamp = state->fogState.clamp;

		if (amount >= 1.0f) {
			RestoreVanillaFog();
			return;
		}

		// Whose value is this? Ours from last frame means the engine did not
		// refresh it, and the original stands; anything else is a fresh
		// original. Without this the value halves every frame.
		if (!_clampOverridden || clamp != _clampWritten) {
			_clampOriginal = clamp;
		}
		_clampWritten = _clampOriginal * amount;
		clamp = _clampWritten;
		_clampOverridden = true;
	}

	void ExponentialHeightFog::RestoreVanillaFog() noexcept
	{
		if (!_clampOverridden) {
			return;
		}
		if (auto* const state = RE::BSGraphics::State::GetSingleton(); state != nullptr) {
			// Only if it is still ours: the engine may have moved on, and
			// writing an old original over a new value would be our own bug.
			if (state->fogState.clamp == _clampWritten) {
				state->fogState.clamp = _clampOriginal;
			}
		}
		_clampOverridden = false;
	}

	float ExponentialHeightFog::VanillaClamp(float a_current) const noexcept
	{
		return _clampOverridden && a_current == _clampWritten ? _clampOriginal : a_current;
	}
}
