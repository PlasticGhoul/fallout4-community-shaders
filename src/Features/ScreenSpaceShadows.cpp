#include "Features/ScreenSpaceShadows.h"

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
#include <RE/N/NiLight.h>
#include <RE/S/Sky.h>

// RE/S/Sun.h is not self-contained: it names SkyObject, NiBillboardNode,
// BSTriShape, BSShaderAccumulator and NiDirectionalLight without declaring any
// of them, and only ever compiles inside RE/Fallout.h, which includes the world
// in dependency order. Including the four that do have headers pulls a chain
// behind them - NiBillboardNode wants NiNode, and so on - which is a heavy
// price for one pointer.
//
// Only the base class has to be complete. The rest are NiPointer members we
// never dereference through their own type, so a forward declaration is
// enough; what we do read, we read through NiAVObject, which sits at offset
// zero of every one of them. Worth handing back upstream, like B2's two
// findings.
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
#include <memory>
#include <string>

namespace Features
{
	namespace
	{
		constexpr auto kRaymarchFile = "ScreenSpaceShadows/RaymarchCS.hlsl";
		constexpr auto kModulateFile = "ScreenSpaceShadows/Modulate.hlsl";
		constexpr auto kMaskFormat = REX::W32::DXGI_FORMAT_R8_UNORM;

		/// Once a second at 180 fps. Often enough that a stalled phase is found
		/// in the run that caused it, rare enough to cost nothing.
		constexpr std::uint64_t kStallInterval = 180;

		/// Field for field the cbuffer PerFrame of RaymarchCS.hlsl. HLSL packs
		/// a float2 so that it never straddles a sixteen byte boundary, and
		/// this order is chosen so that none of them does - which is why the
		/// two layouts agree without padding anywhere but at the end.
		struct alignas(16) RaymarchConstants
		{
			float lightCoordinate[4];      //  0
			std::int32_t waveOffset[2];    // 16
			float farDepthValue;           // 24
			float nearDepthValue;          // 28
			float invDepthTextureSize[2];  // 32
			float dynamicRes[2];           // 40
			float surfaceThickness;        // 48
			float bilinearThreshold;       // 52
			float shadowContrast;          // 56
			float padding;                 // 60
		};
		static_assert(sizeof(RaymarchConstants) == 64);

		std::filesystem::path ShaderRoot()
		{
			return Util::DataDirectory() / "Shaders" / "FO4";
		}
	}

	void ScreenSpaceShadows::Declare()
	{
		// Off until the sun's place on screen is right. The pass works, the
		// mask is drawn and the modulation lands where it should, but the light
		// coordinate it sweeps from is still wrong, and a feature that is known
		// to draw the wrong thing has no business defaulting to on.
		Settings::DeclareFeature("ScreenSpaceShadows", false)
			.Label("feature.screen_space_shadows.name", "Screen-Space Shadows")
			.Help(
				"feature.screen_space_shadows.help",
				"Contact shadows from the sun, ray marched through the depth buffer. Adds the "
				"fine shadows a shadow map is too coarse to carry.");

		Settings::DeclareSlider("ScreenSpaceShadows/sampleCount", 1.0, 1.0, 4.0)
			.Label("feature.screen_space_shadows.sample_count", "Sample Count")
			.Help(
				"feature.screen_space_shadows.sample_count_help",
				"Multiplier for the number of samples along a ray. Higher reaches further and "
				"costs more. Scales with the render resolution.");

		Settings::DeclareSlider("ScreenSpaceShadows/surfaceThickness", 0.02, 0.005, 0.05)
			.Label("feature.screen_space_shadows.surface_thickness", "Surface Thickness")
			.Help(
				"feature.screen_space_shadows.surface_thickness_help",
				"How thick surfaces are assumed to be. Lower gives thinner, more precise "
				"shadows.");

		Settings::DeclareSlider("ScreenSpaceShadows/bilinearThreshold", 0.02, 0.02, 1.0)
			.Label("feature.screen_space_shadows.bilinear_threshold", "Bilinear Threshold")
			.Help(
				"feature.screen_space_shadows.bilinear_threshold_help",
				"Depth difference at which an edge stops being smoothed across.");

		Settings::DeclareSlider("ScreenSpaceShadows/shadowContrast", 1.0, 0.0, 4.0)
			.Label("feature.screen_space_shadows.shadow_contrast", "Shadow Contrast")
			.Help(
				"feature.screen_space_shadows.shadow_contrast_help",
				"How hard the transition into shadow is. Higher gives sharper edges.");
	}

	bool ScreenSpaceShadows::Setup()
	{
		_reportedNoSun = false;
		_reportedNoTargets = false;
		_reportedStall = false;
		_everFired = false;
		_draws = 0;
		_frames = 0;
		_lastHits = Render::FramePhaseHits();

		if (!Render::InitFullscreenPass()) {
			return false;
		}

		if (!CompileModulate()) {
			return false;
		}

		if (!EnsureSampler() || !EnsureBlendState()) {
			return false;
		}

		if (!_constants.Create(sizeof(RaymarchConstants), "FO4CS_CB_ScreenSpaceShadows")) {
			return false;
		}

		// The mask is sized from the light target, and that exists by now:
		// Setup runs from Present. A resolution change is answered in Draw.
		if (!EnsureMask()) {
			return false;
		}

		if (!CompileRaymarch(ScaledSampleCount())) {
			return false;
		}

		// The check that the slot constants point where we think, read back out
		// of the names B2's inventory wrote.
		Render::Targets::LogSlots();

		_phase = Render::SubscribeFramePhase("ScreenSpaceShadows", [this] { Draw(); });
		if (_phase == Render::PhaseDispatcher::kNoToken) {
			REX::ERROR("ScreenSpaceShadows: the frame phase has no room left");
			return false;
		}

		return true;
	}

	void ScreenSpaceShadows::Frame()
	{
		++_frames;

		// Recompiling on the render thread, without a watcher thread of its
		// own: compiling costs milliseconds and only happens on a change. The
		// thread ImagespaceTint keeps carries the bug D2 found, where a file
		// missing on the first try empties the watch for the rest of the
		// session.
		if (_watch.Poll()) {
			REX::INFO("ScreenSpaceShadows: shader changed, recompiling");
			static_cast<void>(CompileRaymarch(_compiledSampleCount));
		}

		const auto wanted = ScaledSampleCount();
		if (wanted != _compiledSampleCount) {
			static_cast<void>(CompileRaymarch(wanted));
		}

		// A hit count that stops moving means the vtable entry has been
		// overwritten - see the ordering note in FramePhase.h. There is no
		// other symptom, so it is worth asking once a second.
		//
		// Only once it has moved at all, though. Before the first hit, silence
		// is the normal state: in the main menu and on a loading screen the
		// composite legitimately does not run, and the first version of this
		// check reported a stall there every time. A diagnostic that cannot
		// tell "not started" from "stopped" is worse than none, because it
		// teaches whoever reads the log to ignore it.
		if (_frames % kStallInterval == 0) {
			const auto hits = Render::FramePhaseHits();

			if (hits != _lastHits) {
				_everFired = true;
				_reportedStall = false;
			} else if (_everFired && !_reportedStall) {
				REX::ERROR(
					"ScreenSpaceShadows: the frame phase fired {} times and has now stopped "
					"for {} frames, something has taken the vtable entry back",
					hits,
					kStallInterval);
				_reportedStall = true;
			}

			_lastHits = hits;
		}
	}

	void ScreenSpaceShadows::Shutdown()
	{
		// First, so that no callback can be in flight while what it uses is
		// released. Both run on the render thread - this one from Present by
		// way of Registry::Tick, the callback from SetupTechnique - so they
		// cannot overlap, and unsubscribing before releasing is enough. No lock
		// is needed, and that is the assumption the design rests on.
		Render::UnsubscribeFramePhase(_phase);
		_phase = Render::PhaseDispatcher::kNoToken;

		if (_raymarch != nullptr) {
			_raymarch->Release();
			_raymarch = nullptr;
		}
		if (_modulate != nullptr) {
			_modulate->Release();
			_modulate = nullptr;
		}
		if (_pointBorder != nullptr) {
			_pointBorder->Release();
			_pointBorder = nullptr;
		}
		if (_multiply != nullptr) {
			_multiply->Release();
			_multiply = nullptr;
		}

		_mask.Release();
		_constants.Release();
		_compiledSampleCount = 0;

		// The shared full-screen vertex shader is deliberately not released: it
		// belongs to the process, not to this feature.
	}

	void ScreenSpaceShadows::Draw()
	{
		// Counted before anything can refuse, because it is what paces the
		// periodic logging. Counting it after the first early return meant a
		// frame that gave up left the counter at zero, and "every 180th" became
		// "every one" - the last run wrote 920 kB of the same two lines.
		++_draws;

		const auto* const sky = RE::Sky::GetSingleton();
		if (sky == nullptr || sky->mode.get() != RE::Sky::Mode::kFull ||
			sky->sun == nullptr || sky->sun->light.get() == nullptr) {
			if (!_reportedNoSun) {
				REX::INFO("ScreenSpaceShadows: no sun, standing down");
				_reportedNoSun = true;
			}
			return;
		}
		_reportedNoSun = false;

		auto* const depth = Render::Targets::DepthSRV(Render::Targets::kSceneDepth);
		auto* const diffuse = Render::Targets::RenderTargetView(Render::Targets::kLightDiffuse);
		auto* const specular = Render::Targets::RenderTargetView(Render::Targets::kLightSpecular);

		if (depth == nullptr || diffuse == nullptr || specular == nullptr) {
			if (!_reportedNoTargets) {
				REX::ERROR(
					"ScreenSpaceShadows: standing down, depth {}, diffuse {}, specular {}",
					depth != nullptr ? "ok" : "missing",
					diffuse != nullptr ? "ok" : "missing",
					specular != nullptr ? "ok" : "missing");
				_reportedNoTargets = true;
			}
			return;
		}
		_reportedNoTargets = false;

		if (!EnsureMask() || _raymarch == nullptr || _modulate == nullptr) {
			return;
		}

		auto* const context = Render::GetContext();
		if (context == nullptr) {
			return;
		}

		RaymarchGeometry geometry{};
		if (!BuildGeometry(*sky, geometry)) {
			return;
		}

		const Render::StateGuard guard;

		// Unbind first: DS_002 still hangs on the output merger as a depth
		// stencil view, and binding it as a shader resource at the same time
		// would have D3D11 quietly drop one of the two.
		context->OMSetRenderTargets(0, nullptr, nullptr);

		{
			const Render::PassScope scope{ "ScreenSpaceShadows/RayMarch" };
			RayMarch(*context, depth, geometry);
		}

		{
			const Render::PassScope scope{ "ScreenSpaceShadows/Modulate" };
			Modulate(*context, diffuse, specular);
		}
	}

	bool ScreenSpaceShadows::BuildGeometry(const RE::Sky& a_sky, RaymarchGeometry& a_out)
	{
		// NiDirectionalLight is forward declared only in commonlibf4, so no
		// method of it can be called. NiLight is its base, fully declared, and
		// sits at offset zero - the whole chain down to NiAVObject is single
		// inheritance.
		const auto* const light = reinterpret_cast<const RE::NiLight*>(a_sky.sun->light.get());
		const auto& rotate = light->GetWorldRotate();

		// Where the engine draws the sun. Read through NiAVObject, which sits
		// at offset zero of NiBillboardNode - its own type is forward declared
		// only, the same trick as the light above.
		const auto* const node =
			reinterpret_cast<const RE::NiAVObject*>(a_sky.sun->sunBaseNode.get());
		if (node == nullptr) {
			return false;
		}

		const auto& sunPosition = node->GetWorldTranslate();

		// Logged once a second rather than once a session. The first version
		// wrote it a single time and caught the one moment where it was
		// meaningless - an identity rotation and a sun at the origin, because
		// the world had not been placed yet. One sample of a value that
		// changes is not a measurement.
		if (_draws % kStallInterval == 0) {
			// All twelve floats, not the nine a 3x3 would have. Two readings of
			// this structure have now missed: the column this code takes gives
			// a direction with no vertical component at all, and rows one and
			// two read as an exact identity that cannot be orthonormal beside
			// row zero. Once an assumption about a layout is wrong twice, the
			// answer is a raw dump rather than a third guess - the lesson C
			// paid for. NiMatrix3 is NiPoint4 entry[3], so the fourth column
			// is in here as well.
			const auto& transform = light->GetWorldTransform();
			REX::INFO(
				"sun light rotate raw: "
				"[{:.4f} {:.4f} {:.4f} {:.4f}] [{:.4f} {:.4f} {:.4f} {:.4f}] "
				"[{:.4f} {:.4f} {:.4f} {:.4f}] translate [{:.1f} {:.1f} {:.1f}] scale {:.3f}",
				rotate.entry[0][0], rotate.entry[0][1], rotate.entry[0][2], rotate.entry[0][3],
				rotate.entry[1][0], rotate.entry[1][1], rotate.entry[1][2], rotate.entry[1][3],
				rotate.entry[2][0], rotate.entry[2][1], rotate.entry[2][2], rotate.entry[2][3],
				transform.translate.x,
				transform.translate.y,
				transform.translate.z,
				transform.scale);

			// Same trick as the light: read the node through NiAVObject, which
			// is at offset zero of NiBillboardNode. Its own type is forward
			// declared only. The position is the fallback answer if the
			// rotation above turns out not to carry the direction.
			REX::INFO(
				"sun node at [{:.1f} {:.1f} {:.1f}]",
				sunPosition.x,
				sunPosition.y,
				sunPosition.z);

			// Once per session is enough for this one: the projection changes
			// only with the field of view, and what is being asked of it -
			// which end of the depth buffer is near - does not change at all.
			if (!_loggedDirection) {
				_loggedDirection = true;
				Render::LogProjection();
			}
		}

		// Row zero, and only row zero. The engine does not keep a rotation here
		// at all: rows one and two read as an untouched identity in every
		// sample, while row zero is a unit vector that drifts with the time of
		// day and tilts about 38 degrees downward - a sun direction. Taking a
		// column instead mixed row zero's second entry with the 1.0 of the
		// identity below it and produced a direction lying exactly in the
		// horizontal plane, which is both wrong and the grazing angle Bend
		// names as its own worst case for edge artefacts.
		const float direction[3] = {
			rotate.entry[0][0],
			rotate.entry[0][1],
			rotate.entry[0][2]
		};

		const auto length = std::sqrt(
			direction[0] * direction[0] +
			direction[1] * direction[1] +
			direction[2] * direction[2]);

		if (length < 1.0e-4f) {
			return false;
		}

		// Bend wants the direction towards the light, and w zero because a
		// directional light sits at infinity.
		const float light4[4] = {
			-direction[0] / length,
			-direction[1] / length,
			-direction[2] / length,
			0.0f
		};

		const int viewport[2] = {
			static_cast<int>(_mask.Width()),
			static_cast<int>(_mask.Height())
		};

		// The sun's own billboard, projected through the world camera's
		// worldToCam. That is where the engine draws the sun, so it is where
		// the sun is on screen - by construction rather than by a chain of
		// assumptions about axes and fields of view. Bend takes a point as
		// readily as a direction, and at this distance the difference does not
		// arise.
		const float sunPoint[3]{ sunPosition.x, sunPosition.y, sunPosition.z };
		const auto clip = Render::ProjectPoint(sunPoint);
		if (!clip) {
			return false;
		}

		for (int column = 0; column < 4; ++column) {
			a_out.lightProjection[column] = (*clip)[column];
		}

		a_out.plan = Bend::BuildPlan(a_out.lightProjection, viewport);

		// One line a second while the feature is young. The pixel position is
		// what says the projection is built right: with the sun on screen it
		// has to land where the sun is seen.
		if (_draws % kStallInterval == 0) {
			REX::INFO(
				"towards sun [{:.3f} {:.3f} {:.3f}], elevation {:.1f} deg, "
				"at pixel [{:.0f} {:.0f}], w {:.4f}, {} dispatch(es)",
				light4[0],
				light4[1],
				light4[2],
				std::asin(light4[2]) * 57.2957795f,
				a_out.plan.lightCoordinate[0],
				a_out.plan.lightCoordinate[1],
				a_out.lightProjection[3],
				a_out.plan.count);
		}

		return a_out.plan.count > 0;
	}

	void ScreenSpaceShadows::RayMarch(
		REX::W32::ID3D11DeviceContext& a_context,
		REX::W32::ID3D11ShaderResourceView* a_depth,
		const RaymarchGeometry& a_geometry)
	{
		auto* const uav = _mask.UAV();
		auto* const buffer = _constants.Buffer();
		auto* const sampler = _pointBorder;

		// White is "nothing in the way", and the mask has to start there every
		// frame. Bend's sweep writes only the pixels its dispatch quadrants
		// cover; whatever it does not reach keeps what the texture held, which
		// on a fresh one is zero - fully shadowed. Left out, it shows as a
		// stable dark band around the edges of the picture, thickest where the
		// quadrants reach least. The reference implementation clears here too,
		// and this is the line of it that went missing in translation.
		const float unshadowed[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
		a_context.ClearUnorderedAccessViewFloat(uav, unshadowed);

		a_context.CSSetShader(_raymarch, nullptr, 0);
		a_context.CSSetShaderResources(0, 1, std::addressof(a_depth));
		a_context.CSSetUnorderedAccessViews(0, 1, std::addressof(uav), nullptr);
		a_context.CSSetSamplers(0, 1, std::addressof(sampler));
		a_context.CSSetConstantBuffers(1, 1, std::addressof(buffer));

		// Read fresh every frame: there is no change notification, and whoever
		// caches a setting has to refresh it himself.
		const auto thickness =
			static_cast<float>(Settings::GetDouble("ScreenSpaceShadows/surfaceThickness"));
		const auto threshold =
			static_cast<float>(Settings::GetDouble("ScreenSpaceShadows/bilinearThreshold"));
		const auto contrast =
			static_cast<float>(Settings::GetDouble("ScreenSpaceShadows/shadowContrast"));

		for (int i = 0; i < a_geometry.plan.count; ++i) {
			const auto& dispatch = a_geometry.plan.dispatches[i];

			RaymarchConstants data{};
			data.lightCoordinate[0] = a_geometry.plan.lightCoordinate[0];
			data.lightCoordinate[1] = a_geometry.plan.lightCoordinate[1];
			data.lightCoordinate[2] = a_geometry.plan.lightCoordinate[2];
			data.lightCoordinate[3] = a_geometry.plan.lightCoordinate[3];
			data.waveOffset[0] = dispatch.waveOffset[0];
			data.waveOffset[1] = dispatch.waveOffset[1];

			// Which end of the range is near is unestablished for Fallout 4. An
			// empty mask means swapping these two, and nothing else.
			data.farDepthValue = 1.0f;
			data.nearDepthValue = 0.0f;

			data.invDepthTextureSize[0] = 1.0f / static_cast<float>(_mask.Width());
			data.invDepthTextureSize[1] = 1.0f / static_cast<float>(_mask.Height());

			// Dynamic resolution is not read yet; the mask and the depth are
			// the same size, so one is the honest value.
			data.dynamicRes[0] = 1.0f;
			data.dynamicRes[1] = 1.0f;

			data.surfaceThickness = thickness;
			data.bilinearThreshold = threshold;
			data.shadowContrast = contrast;

			static_cast<void>(_constants.Update(std::addressof(data), sizeof(data)));

			a_context.Dispatch(
				static_cast<std::uint32_t>(dispatch.waveCount[0]),
				static_cast<std::uint32_t>(dispatch.waveCount[1]),
				static_cast<std::uint32_t>(dispatch.waveCount[2]));
		}
	}

	void ScreenSpaceShadows::Modulate(
		REX::W32::ID3D11DeviceContext& a_context,
		REX::W32::ID3D11RenderTargetView* a_diffuse,
		REX::W32::ID3D11RenderTargetView* a_specular)
	{
		// Let go of the mask as a UAV before taking it as an SRV: D3D11 would
		// otherwise unbind one of the two without saying so.
		REX::W32::ID3D11UnorderedAccessView* noUAV[1]{ nullptr };
		REX::W32::ID3D11ShaderResourceView* noSRV[1]{ nullptr };
		a_context.CSSetUnorderedAccessViews(0, 1, noUAV, nullptr);
		a_context.CSSetShaderResources(0, 1, noSRV);
		a_context.CSSetShader(nullptr, nullptr, 0);

		// No depth view: we are only multiplying, and DS_002 is about to be
		// bound for writing again by the composite.
		REX::W32::ID3D11RenderTargetView* targets[2]{ a_diffuse, a_specular };
		a_context.OMSetRenderTargets(2, targets, nullptr);

		const float blendFactor[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
		a_context.OMSetBlendState(_multiply, blendFactor, 0xFFFFFFFFu);

		auto* const mask = _mask.SRV();
		a_context.PSSetShaderResources(0, 1, std::addressof(mask));
		a_context.PSSetShader(_modulate, nullptr, 0);

		// The viewport is left alone: it already covers the whole picture,
		// because the engine is about to draw into these same targets.
		Render::DrawFullscreen();
	}

	std::uint32_t ScreenSpaceShadows::ScaledSampleCount() const
	{
		const auto multiplier = Settings::GetDouble("ScreenSpaceShadows/sampleCount");

		const auto width = static_cast<double>(_mask.Width());
		const auto height = static_cast<double>(_mask.Height());
		if (width <= 0.0 || height <= 0.0) {
			return 64;
		}

		// Scaled against 1920x1080 so the rays reach as far across the picture
		// at any resolution, then quantised to eights: a dynamic resolution
		// that wobbles by a few pixels must not recompile the shader.
		const auto scale = std::sqrt((width * height) / (1920.0 * 1080.0));
		const auto raw = static_cast<std::uint32_t>(std::lround(multiplier * 60.0 * scale));

		return std::max<std::uint32_t>(((raw + 7u) / 8u) * 8u, 8u);
	}

	bool ScreenSpaceShadows::CompileRaymarch(std::uint32_t a_sampleCount)
	{
		const auto source = Shader::LoadSource(ShaderRoot(), kRaymarchFile);

		// Reset the watch even when loading failed. ImagespaceTint does not,
		// which is the bug D2 found: a file missing on the first try leaves the
		// watch empty for the rest of the session, and hot reload never works
		// again.
		if (!source) {
			const std::filesystem::path only[] = { ShaderRoot() / kRaymarchFile };
			_watch.Reset(only);
			REX::ERROR("ScreenSpaceShadows: {}", source.error());
			return false;
		}

		_watch.Reset(source->files);

		const Shader::ShaderDefine defines[] = {
			{ "SAMPLE_COUNT", std::to_string(a_sampleCount) }
		};

		const auto compiled =
			Shader::CompileComputeShader(source->text, "RaymarchCS.hlsl", "main", defines);

		if (!compiled.Succeeded()) {
			// The previous shader stays in place. A bad edit dims nothing and
			// costs no frame; the compiler's own text, with file and line, goes
			// to the log for whoever made it.
			REX::ERROR("ScreenSpaceShadows: {}", compiled.diagnostics);
			return false;
		}

		auto* const device = Render::GetDevice();
		REX::W32::ID3D11ComputeShader* shader = nullptr;
		if (device == nullptr ||
			device->CreateComputeShader(
				compiled.bytecode.data(),
				compiled.bytecode.size(),
				nullptr,
				std::addressof(shader)) < 0) {
			REX::ERROR("ScreenSpaceShadows: the raymarch shader could not be created");
			return false;
		}

		// Swapped in only once the new one exists, so a failure never leaves
		// the feature without a shader.
		if (_raymarch != nullptr) {
			_raymarch->Release();
		}
		_raymarch = shader;
		_compiledSampleCount = a_sampleCount;

		static_cast<void>(Render::SetDebugName(
			reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_raymarch),
			"FO4CS_CS_ScreenSpaceShadows"));

		REX::INFO("ScreenSpaceShadows: raymarch compiled for {} samples", a_sampleCount);
		return true;
	}

	bool ScreenSpaceShadows::CompileModulate()
	{
		const auto source = Shader::LoadSource(ShaderRoot(), kModulateFile);
		if (!source) {
			REX::ERROR("ScreenSpaceShadows: {}", source.error());
			return false;
		}

		const auto compiled =
			Shader::CompilePixelShader(source->text, "Modulate.hlsl", "main");
		if (!compiled.Succeeded()) {
			REX::ERROR("ScreenSpaceShadows: {}", compiled.diagnostics);
			return false;
		}

		auto* const device = Render::GetDevice();
		if (device == nullptr ||
			device->CreatePixelShader(
				compiled.bytecode.data(),
				compiled.bytecode.size(),
				nullptr,
				std::addressof(_modulate)) < 0) {
			REX::ERROR("ScreenSpaceShadows: the modulate shader could not be created");
			return false;
		}

		static_cast<void>(Render::SetDebugName(
			reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_modulate),
			"FO4CS_PS_ScreenSpaceShadowsModulate"));

		// Deliberately not under the file watch. It does not change with the
		// sample count, and a second watch for one file is apparatus without
		// return.
		return true;
	}

	bool ScreenSpaceShadows::EnsureMask()
	{
		auto* const reference =
			Render::Targets::RenderTargetTexture(Render::Targets::kLightDiffuse);
		if (reference == nullptr) {
			return false;
		}

		REX::W32::D3D11_TEXTURE2D_DESC desc{};
		reference->GetDesc(std::addressof(desc));

		if (_mask.Valid() && _mask.Width() == desc.width && _mask.Height() == desc.height) {
			return true;
		}

		// Sized from the light target rather than from the swap chain: the mask
		// is multiplied onto that target, so those two are the pair that has to
		// agree.
		REX::INFO("ScreenSpaceShadows: mask sized {}x{}", desc.width, desc.height);
		return _mask.Create(desc.width, desc.height, kMaskFormat, "FO4CS_TEX_ScreenSpaceShadows");
	}

	bool ScreenSpaceShadows::EnsureSampler()
	{
		if (_pointBorder != nullptr) {
			return true;
		}

		auto* const device = Render::GetDevice();
		if (device == nullptr) {
			return false;
		}

		REX::W32::D3D11_SAMPLER_DESC desc{};
		desc.filter = REX::W32::D3D11_FILTER_MIN_MAG_MIP_POINT;

		// Border, not clamp: a ray leaving the screen has to read "nothing in
		// the way" rather than whatever the edge pixel holds, or every screen
		// edge grows a shadow of its own.
		desc.addressU = REX::W32::D3D11_TEXTURE_ADDRESS_BORDER;
		desc.addressV = REX::W32::D3D11_TEXTURE_ADDRESS_BORDER;
		desc.addressW = REX::W32::D3D11_TEXTURE_ADDRESS_BORDER;
		desc.maxAnisotropy = 1;
		desc.comparisonFunc = REX::W32::D3D11_COMPARISON_NEVER;
		desc.minLOD = 0.0f;
		desc.maxLOD = 3.402823466e+38f;
		desc.borderColor[0] = 1.0f;
		desc.borderColor[1] = 1.0f;
		desc.borderColor[2] = 1.0f;
		desc.borderColor[3] = 1.0f;

		if (device->CreateSamplerState(std::addressof(desc), std::addressof(_pointBorder)) < 0) {
			REX::ERROR("ScreenSpaceShadows: the point border sampler could not be created");
			return false;
		}

		return true;
	}

	bool ScreenSpaceShadows::EnsureBlendState()
	{
		if (_multiply != nullptr) {
			return true;
		}

		auto* const device = Render::GetDevice();
		if (device == nullptr) {
			return false;
		}

		// dest = dest * src. The card does the multiplication, which is why the
		// pixel shader only hands over the mask: the light targets carry no
		// unordered access view to read the destination through.
		REX::W32::D3D11_BLEND_DESC desc{};
		desc.alphaToCoverageEnable = false;

		// Left off, so the description of target zero governs both targets.
		desc.independentBlendEnable = false;

		auto& target = desc.renderTarget[0];
		target.blendEnable = true;
		target.srcBlend = REX::W32::D3D11_BLEND_ZERO;
		target.destBlend = REX::W32::D3D11_BLEND_SRC_COLOR;
		target.blendOp = REX::W32::D3D11_BLEND_OP_ADD;
		target.srcBlendAlpha = REX::W32::D3D11_BLEND_ZERO;
		target.destBlendAlpha = REX::W32::D3D11_BLEND_SRC_ALPHA;
		target.blendOpAlpha = REX::W32::D3D11_BLEND_OP_ADD;
		target.renderTargetWriteMask =
			static_cast<std::uint8_t>(REX::W32::D3D11_COLOR_WRITE_ENABLE_ALL);

		if (device->CreateBlendState(std::addressof(desc), std::addressof(_multiply)) < 0) {
			REX::ERROR("ScreenSpaceShadows: the multiply blend state could not be created");
			return false;
		}

		return true;
	}
}
