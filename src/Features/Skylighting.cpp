#include "Features/Skylighting.h"

#include "Features/Skylighting/OcclusionProjection.h"
#include "Render/Camera.h"
#include "Render/DebugName.h"
#include "Render/DrawHook.h"
#include "Render/FramePhase.h"
#include "Render/FullscreenPass.h"
#include "Render/Profiler.h"
#include "Render/Renderer.h"
#include "Render/StateGuard.h"
#include "Render/SwapChainHook.h"
#include "Render/Targets.h"
#include "Render/TechniqueTracker.h"
#include "Settings/Settings.h"
#include "Shader/ShaderCompiler.h"
#include "Shader/ShaderSource.h"
#include "Util/GamePaths.h"

#include <RE/B/BSGraphics.h>
#include <RE/M/Main.h>
#include <RE/N/NiCamera.h>
#include <RE/S/Sky.h>

#include <REX/W32/DXGI.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace Features
{
	namespace
	{
		constexpr auto kPassShaderFile = "Skylighting/Skylighting.hlsl";
		constexpr auto kUpdateShaderFile = "Skylighting/UpdateProbes.hlsl";
		constexpr std::uint64_t kLogInterval = 180;
		constexpr double kPi = 3.14159265358979323846;
		/// Any seed does; this one is not zero, so that frame zero already
		/// has a direction off the axis.
		constexpr double kSequenceSeed = 0.37;

		/// From the probe run of 2026-09-13: the shadow casters' vertex stage
		/// binds its per-view constants in slot 12, 752 bytes, four 4x4
		/// matrices in rows - the light camera's rotation, its orthographic
		/// projection, their product, the transposed rotation. Which of them
		/// the vertex shader reads is not known, so the product is written
		/// over both the projection and the product, and an identity over
		/// both rotations; whichever pair it multiplies, the answer is ours.
		constexpr std::uint32_t kViewConstantSlot = 12;
		constexpr std::size_t kRotationOffset = 0;
		constexpr std::size_t kProjectionOffset = 64;
		constexpr std::size_t kViewProjectionOffset = 128;
		constexpr std::size_t kInverseRotationOffset = 192;
		constexpr std::size_t kMatricesEnd = 256;

		/// The grid's cells are relative to the eye, so its cube is centred on
		/// the origin. The casters' positions are absolute world coordinates -
		/// the object matrices the probe dumped carry translations around
		/// ninety thousand - so their cube is centred on the eye. The probe
		/// had read the engine's constant projection as camera relative
		/// geometry; the first acceptance run drew nothing, which is what an
		/// absolute scene clipped against a cube at the origin looks like.
		constexpr double kGridCentre[3]{ 0.0, 0.0, 0.0 };

		/// RT_028 read as white in every channel; which of them the composite
		/// takes is not settled, so all three get the factor.
		constexpr std::uint8_t kAmbientWriteMask = static_cast<std::uint8_t>(REX::W32::D3D11_COLOR_WRITE_ENABLE_ALL);

		/// The half resolution target the composite reads its ambient
		/// occlusion from.
		constexpr std::size_t kAmbientOcclusionSlot = 28;

		/// Field for field the cbuffer PerFrame of both skylighting shaders.
		struct alignas(16) Constants
		{
			float occlusionViewProj[16];
			float occlusionDir[4];
			float posOffset[4];
			std::uint32_t arrayOrigin[4];
			std::int32_t validMargin[4];
			float cameraForward[4];
			float cameraRight[4];
			float cameraUp[4];
			float params[4];
		};
		static_assert(sizeof(Constants) == 64 + 8 * 16);

		std::filesystem::path ShaderRoot()
		{
			return Util::DataDirectory() / "Shaders" / "FO4";
		}

		float DebugViewIndex(std::string_view a_choice) noexcept
		{
			if (a_choice == "visibility") {
				return 1.0f;
			}
			if (a_choice == "occlusion") {
				return 2.0f;
			}
			if (a_choice == "grid") {
				return 3.0f;
			}
			return 0.0f;
		}

		void WriteIdentity(std::uint8_t* a_at) noexcept
		{
			const float identity[16]{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
			std::memcpy(a_at, identity, sizeof(identity));
		}
	}

	void Skylighting::Declare()
	{
		Settings::DeclareFeature("Skylighting", true)
			.Label("feature.skylighting.name", "Skylighting")
			.Help(
				"feature.skylighting.help",
				"Darkens the ambient light where the sky is hidden - under overhangs, in "
				"trenches, beneath trees. Taken from the shadow casters the game draws.");
		Settings::DeclareSlider("Skylighting/maxZenith", 90.0, 30.0, 90.0)
			.Label("feature.skylighting.max_zenith", "Max Zenith")
			.Help(
				"feature.skylighting.max_zenith_help",
				"How far from straight up the sky is sampled, in degrees. Lower keeps light out "
				"of narrow gaps.");
		Settings::DeclareSlider("Skylighting/minVisibility", 0.1, 0.0, 1.0)
			.Label("feature.skylighting.min_visibility", "Min Visibility")
			.Help(
				"feature.skylighting.min_visibility_help",
				"How dark a fully hidden spot gets. 0 is black, 1 switches the effect off.");
		Settings::DeclareChoice(
			"Skylighting/debugView",
			"off",
			std::vector<std::string>{ "off", "visibility", "occlusion", "grid" })
			.Label("feature.skylighting.debug_view", "Debug View")
			.Help(
				"feature.skylighting.debug_view_help",
				"Shows the pass instead of the picture: the factor, this frame's depth map "
				"from the sky, or the grid as seen from straight above.");
	}

	bool Skylighting::Setup()
	{
		_frames = 0;
		_castersSeen = 0;
		_castersRepeated = 0;
		_dispatches = 0;
		_passes = 0;
		_captureFrame = 0;
		_captureReady = false;
		_repeating = false;
		_surrounding = false;
		_reportedSmallBuffer = false;
		_reportedIndoors = false;
		_gridValid = false;
		_wasOutdoors = false;

		const auto utilityIndex = Render::ClassIndexOf("BSUtilityShader");
		if (!utilityIndex.has_value()) {
			REX::ERROR("Skylighting: BSUtilityShader is not among the shader classes");
			return false;
		}
		_casters = Render::TechniqueFilter{ *utilityIndex, Render::kAnyTechnique };

		if (!Render::InitFullscreenPass() || !Compile() || !EnsureStates()) {
			return false;
		}
		if (!_constants.Create(sizeof(Constants), "FO4CS_CB_Skylighting")) {
			return false;
		}
		if (!_occlusion.Create(SkyOcclusion::kOcclusionMapSize, "FO4CS_DS_SkyOcclusion")) {
			return false;
		}
		if (!_probes.Create(
				SkyOcclusion::kGridSize[0], SkyOcclusion::kGridSize[1], SkyOcclusion::kGridSize[2],
				REX::W32::DXGI_FORMAT_R16G16B16A16_FLOAT, "FO4CS_TEX3D_SkyProbes") ||
			!_accumFrames.Create(
				SkyOcclusion::kGridSize[0], SkyOcclusion::kGridSize[1], SkyOcclusion::kGridSize[2],
				REX::W32::DXGI_FORMAT_R8_UINT, "FO4CS_TEX3D_SkyAccum")) {
			return false;
		}
		ResetGrid();

		_probePhase = Render::SubscribeFramePhase(
			Render::Phase::kBeforeComposite, "Skylighting/Probes", [this] { UpdateProbes(); });
		_compositePhase = Render::SubscribeFramePhase(
			Render::Phase::kComposite, "Skylighting/Draw", [this] { Draw(); });
		_debugPhase = Render::SubscribeFramePhase(
			Render::Phase::kAfterOpaque, "Skylighting/Debug", [this] { DrawDebug(); });
		if (_probePhase == Render::PhaseDispatcher::kNoToken ||
			_compositePhase == Render::PhaseDispatcher::kNoToken ||
			_debugPhase == Render::PhaseDispatcher::kNoToken) {
			REX::ERROR("Skylighting: a frame phase has no room left");
			return false;
		}
		if (!Render::AddDrawObserver(this)) {
			return false;
		}
		REX::INFO(
			"Skylighting: repeating BSUtilityShader casters into a {} depth map, grid {}x{}x{}",
			SkyOcclusion::kOcclusionMapSize,
			SkyOcclusion::kGridSize[0],
			SkyOcclusion::kGridSize[1],
			SkyOcclusion::kGridSize[2]);
		return true;
	}

	void Skylighting::Frame()
	{
		if (_watch.Poll()) {
			REX::INFO("Skylighting: shader changed, recompiling");
			static_cast<void>(Compile());
		}

		const bool outdoors = Outdoors();
		if (outdoors && !_wasOutdoors) {
			auto* const context = Render::GetContext();
			if (context != nullptr) {
				ResetGrid();
			}
			_reportedIndoors = false;
		} else if (!outdoors && !_reportedIndoors) {
			REX::INFO("Skylighting: indoors, standing down");
			_reportedIndoors = true;
		}
		_wasOutdoors = outdoors;

		++_frames;
		if (_frames % kLogInterval == 0) {
			REX::INFO(
				"skylighting: {} caster(s) seen, {} repeated, {} dispatch(es), {} pass(es) in {} frames, direction [{:.3f} {:.3f} {:.3f}], cell [{} {} {}], margin [{} {} {}], composite technique 0x{:04X}",
				_castersSeen,
				_castersRepeated,
				_dispatches,
				_passes,
				kLogInterval,
				_direction[0],
				_direction[1],
				_direction[2],
				_previousCell[0],
				_previousCell[1],
				_previousCell[2],
				_window.validMargin[0],
				_window.validMargin[1],
				_window.validMargin[2],
				Render::FramePhaseLastTechnique(Render::Phase::kComposite));
			_castersSeen = 0;
			_castersRepeated = 0;
			_dispatches = 0;
			_passes = 0;
		}
	}

	void Skylighting::Shutdown()
	{
		// First, before anything it could touch goes away.
		Render::RemoveDrawObserver(this);
		Render::UnsubscribeFramePhase(Render::Phase::kBeforeComposite, _probePhase);
		Render::UnsubscribeFramePhase(Render::Phase::kComposite, _compositePhase);
		Render::UnsubscribeFramePhase(Render::Phase::kAfterOpaque, _debugPhase);
		_probePhase = _compositePhase = _debugPhase = Render::PhaseDispatcher::kNoToken;
		ReleaseSaved();

		for (auto** object : { reinterpret_cast<REX::W32::IUnknown**>(std::addressof(_update)),
				 reinterpret_cast<REX::W32::IUnknown**>(std::addressof(_shader)),
				 reinterpret_cast<REX::W32::IUnknown**>(std::addressof(_comparison)),
				 reinterpret_cast<REX::W32::IUnknown**>(std::addressof(_linearClamp)),
				 reinterpret_cast<REX::W32::IUnknown**>(std::addressof(_multiply)),
				 reinterpret_cast<REX::W32::IUnknown**>(std::addressof(_plain)),
				 reinterpret_cast<REX::W32::IUnknown**>(std::addressof(_depthState)),
				 reinterpret_cast<REX::W32::IUnknown**>(std::addressof(_viewConstants)) }) {
			if (*object != nullptr) {
				(*object)->Release();
				*object = nullptr;
			}
		}
		_viewImage.clear();
		_occlusion.Release();
		_probes.Release();
		_accumFrames.Release();
		_constants.Release();
	}

	bool Skylighting::Outdoors() const noexcept
	{
		const auto* const sky = RE::Sky::GetSingleton();
		return sky != nullptr && sky->mode.get() == RE::Sky::Mode::kFull;
	}

	void Skylighting::ResetGrid() noexcept
	{
		auto* const context = Render::GetContext();
		if (context != nullptr) {
			_accumFrames.ClearUint(*context, 0);
		}
		// Everything counts as new until the first window is computed.
		_gridValid = false;
	}

	// ---------------------------------------------------------------- capture

	bool Skylighting::Wants(const Render::CurrentTechnique& a_current) const noexcept
	{
		return !_repeating && _casters.Matches(a_current);
	}

	bool Skylighting::EnsureViewCopy(REX::W32::ID3D11DeviceContext& a_context, REX::W32::ID3D11Buffer* a_engine) noexcept
	{
		REX::W32::D3D11_BUFFER_DESC desc{};
		a_engine->GetDesc(std::addressof(desc));
		if (desc.byteWidth < kMatricesEnd) {
			if (!_reportedSmallBuffer) {
				REX::ERROR("Skylighting: the view constants are {} bytes, four matrices need {}", desc.byteWidth, kMatricesEnd);
				_reportedSmallBuffer = true;
			}
			return false;
		}
		if (_viewConstants != nullptr && _viewImage.size() == desc.byteWidth) {
			return true;
		}

		// Once: the engine's bytes, read through a staging buffer with a
		// blocking map. A stall at the first caster of a session; everything
		// after it is our own copy, and the probe saw the buffer hold still
		// for minutes.
		auto* const device = Render::GetDevice();
		if (device == nullptr) {
			return false;
		}
		REX::W32::D3D11_BUFFER_DESC staging = desc;
		staging.usage = REX::W32::D3D11_USAGE_STAGING;
		staging.bindFlags = 0;
		staging.cpuAccessFlags = REX::W32::D3D11_CPU_ACCESS_READ;
		staging.miscFlags = 0;
		REX::W32::ID3D11Buffer* copy = nullptr;
		if (device->CreateBuffer(std::addressof(staging), nullptr, std::addressof(copy)) < 0) {
			return false;
		}
		a_context.CopyResource(copy, a_engine);
		REX::W32::D3D11_MAPPED_SUBRESOURCE mapped{};
		const bool read = a_context.Map(copy, 0, REX::W32::D3D11_MAP_READ, 0, std::addressof(mapped)) >= 0;
		if (read) {
			const auto* const bytes = static_cast<const std::uint8_t*>(mapped.data);
			_viewImage.assign(bytes, bytes + desc.byteWidth);
			a_context.Unmap(copy, 0);
		}
		copy->Release();
		if (!read) {
			return false;
		}

		REX::W32::D3D11_BUFFER_DESC ours = desc;
		ours.usage = REX::W32::D3D11_USAGE_DEFAULT;
		ours.bindFlags = REX::W32::D3D11_BIND_CONSTANT_BUFFER;
		ours.cpuAccessFlags = 0;
		ours.miscFlags = 0;
		if (_viewConstants != nullptr) {
			_viewConstants->Release();
			_viewConstants = nullptr;
		}
		if (device->CreateBuffer(std::addressof(ours), nullptr, std::addressof(_viewConstants)) < 0) {
			REX::ERROR("Skylighting: our copy of the view constants could not be created");
			return false;
		}
		static_cast<void>(Render::SetDebugName(reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_viewConstants), "FO4CS_CB_SkyOcclusionView"));
		REX::INFO("Skylighting: view constants are {} bytes in slot {}", desc.byteWidth, kViewConstantSlot);
		return true;
	}

	bool Skylighting::BeginFrameCapture(REX::W32::ID3D11DeviceContext& a_context) noexcept
	{
		// Once a frame, at the first caster: this frame's direction, the
		// matrix, our copy of the view constants, a clean depth map.
		const auto frame = Render::FrameCount();
		if (_captureFrame == frame) {
			return _captureReady;
		}
		_captureFrame = frame;
		_captureReady = false;

		if (!Outdoors()) {
			return false;
		}
		auto* const world = RE::Main::WorldRootCamera();
		if (world == nullptr) {
			return false;
		}
		const auto& eye = world->GetWorldTransform().translate;
		_eye[0] = eye.x;
		_eye[1] = eye.y;
		_eye[2] = eye.z;

		const auto maxZenith = Settings::GetDouble("Skylighting/maxZenith") * kPi / 180.0;
		SkyOcclusion::SkyDirection(static_cast<std::uint32_t>(frame), kSequenceSeed, maxZenith, _direction);
		SkyOcclusion::OcclusionMatrix(kGridCentre, _direction, _matrix);
		SkyOcclusion::OcclusionMatrix(_eye, _direction, _captureMatrix);

		REX::W32::ID3D11Buffer* engine = nullptr;
		a_context.VSGetConstantBuffers(kViewConstantSlot, 1, std::addressof(engine));
		if (engine == nullptr) {
			return false;
		}
		const bool ready = EnsureViewCopy(a_context, engine);
		engine->Release();
		if (!ready) {
			return false;
		}

		float rows[16]{};
		for (int i = 0; i < 16; ++i) {
			rows[i] = static_cast<float>(_captureMatrix[i]);
		}
		WriteIdentity(_viewImage.data() + kRotationOffset);
		std::memcpy(_viewImage.data() + kProjectionOffset, rows, sizeof(rows));
		std::memcpy(_viewImage.data() + kViewProjectionOffset, rows, sizeof(rows));
		WriteIdentity(_viewImage.data() + kInverseRotationOffset);
		a_context.UpdateSubresource(_viewConstants, 0, nullptr, _viewImage.data(), 0, 0);
		_occlusion.Clear(a_context, 1.0f);
		_captureReady = true;
		return true;
	}

	void Skylighting::BeforeDraw(REX::W32::ID3D11DeviceContext& a_context, const Render::DrawCall&) noexcept
	{
		_surrounding = false;
		REX::W32::ID3D11DepthStencilView* dsv = nullptr;
		a_context.OMGetRenderTargets(kSavedTargets, _savedTargets, std::addressof(dsv));
		const bool shadowCaster = dsv != nullptr && dsv == Render::Targets::DepthStencilView(Render::Targets::kShadowMap);
		if (!shadowCaster || !_occlusion.Valid() || !BeginFrameCapture(a_context)) {
			if (dsv != nullptr) {
				dsv->Release();
			}
			ReleaseSaved();
			return;
		}
		++_castersSeen;
		_savedDepth = dsv;
		a_context.VSGetConstantBuffers(kViewConstantSlot, 1, std::addressof(_savedView));
		a_context.OMGetDepthStencilState(std::addressof(_savedDepthState), std::addressof(_savedStencilRef));

		// Once: what the engine draws its casters with, for the record. The
		// depth function decides whether a map cleared to one could take
		// anything at all.
		if (!_reportedStates) {
			_reportedStates = true;
			REX::W32::D3D11_DEPTH_STENCIL_DESC depthDesc{};
			if (_savedDepthState != nullptr) {
				_savedDepthState->GetDesc(std::addressof(depthDesc));
			}
			REX::W32::ID3D11RasterizerState* rasterizer = nullptr;
			a_context.RSGetState(std::addressof(rasterizer));
			REX::W32::D3D11_RASTERIZER_DESC rasterDesc{};
			if (rasterizer != nullptr) {
				rasterizer->GetDesc(std::addressof(rasterDesc));
				rasterizer->Release();
			}
			REX::INFO(
				"Skylighting: casters draw with depth enable {} func {} write {} stencil {}, cull {} bias {} slope {:.3f} clamp {:.3f} depth clip {}",
				depthDesc.depthEnable,
				static_cast<int>(depthDesc.depthFunc),
				static_cast<int>(depthDesc.depthWriteMask),
				depthDesc.stencilEnable,
				static_cast<int>(rasterDesc.cullMode),
				rasterDesc.depthBias,
				rasterDesc.slopeScaledDepthBias,
				rasterDesc.depthBiasClamp,
				rasterDesc.depthClipEnable);
		}
		_savedViewportCount = kSavedRects;
		a_context.RSGetViewports(std::addressof(_savedViewportCount), _savedViewports);
		_savedScissorCount = kSavedRects;
		a_context.RSGetScissorRects(std::addressof(_savedScissorCount), _savedScissors);
		_surrounding = true;
	}

	void Skylighting::AfterDraw(REX::W32::ID3D11DeviceContext& a_context, const Render::DrawCall& a_call) noexcept
	{
		if (!_surrounding) {
			return;
		}
		_surrounding = false;

		// Our depth map, no colour target, our view constants, the whole map
		// as viewport and scissor; the engine's rasterizer and depth state
		// stay - the casters carry their own bias, and that is wanted.
		const auto size = static_cast<float>(_occlusion.Size());
		const REX::W32::D3D11_VIEWPORT viewport{ 0.0f, 0.0f, size, size, 0.0f, 1.0f };
		const REX::W32::D3D11_RECT scissor{ 0, 0, static_cast<std::int32_t>(_occlusion.Size()), static_cast<std::int32_t>(_occlusion.Size()) };
		auto* const dsv = _occlusion.DSV();
		a_context.OMSetRenderTargets(0, nullptr, dsv);
		// Our own depth test: less or equal against a map cleared to one,
		// whatever direction the engine's shadow depth runs in.
		a_context.OMSetDepthStencilState(_depthState, 0);
		a_context.RSSetViewports(1, std::addressof(viewport));
		a_context.RSSetScissorRects(1, std::addressof(scissor));
		a_context.VSSetConstantBuffers(kViewConstantSlot, 1, std::addressof(_viewConstants));

		_repeating = true;
		a_call.Repeat(a_context);
		_repeating = false;
		++_castersRepeated;

		a_context.VSSetConstantBuffers(kViewConstantSlot, 1, std::addressof(_savedView));
		a_context.OMSetDepthStencilState(_savedDepthState, _savedStencilRef);
		a_context.OMSetRenderTargets(kSavedTargets, _savedTargets, _savedDepth);
		a_context.RSSetViewports(_savedViewportCount, _savedViewports);
		a_context.RSSetScissorRects(_savedScissorCount, _savedScissors);
		ReleaseSaved();
	}

	void Skylighting::ReleaseSaved() noexcept
	{
		for (auto*& target : _savedTargets) {
			if (target != nullptr) {
				target->Release();
				target = nullptr;
			}
		}
		if (_savedDepth != nullptr) {
			_savedDepth->Release();
			_savedDepth = nullptr;
		}
		if (_savedView != nullptr) {
			_savedView->Release();
			_savedView = nullptr;
		}
		if (_savedDepthState != nullptr) {
			_savedDepthState->Release();
			_savedDepthState = nullptr;
		}
	}

	// ------------------------------------------------------------- grid, pass

	bool Skylighting::FillConstants(float a_debugView) noexcept
	{
		auto* const world = RE::Main::WorldRootCamera();
		if (world == nullptr) {
			return false;
		}
		float matrix[16]{};
		std::memcpy(matrix, world->worldToCam, sizeof(matrix));
		if (!Render::IsPlausibleViewProjection(matrix)) {
			return false;
		}
		const auto camera = Render::FogCameraFromMatrix(world->worldToCam, world->GetWorldTransform().translate.z);

		Constants data{};
		for (int i = 0; i < 16; ++i) {
			data.occlusionViewProj[i] = static_cast<float>(_matrix[i]);
		}
		for (int i = 0; i < 3; ++i) {
			data.occlusionDir[i] = static_cast<float>(_direction[i]);
			data.posOffset[i] = static_cast<float>(_window.cellOrigin[i]);
			data.arrayOrigin[i] = static_cast<std::uint32_t>(_window.arrayOrigin[i]);
			data.validMargin[i] = _window.validMargin[i];
		}

		std::memcpy(data.cameraForward, camera.forward, sizeof(camera.forward));
		std::memcpy(data.cameraRight, camera.rightOverScaleX, sizeof(camera.rightOverScaleX));
		data.cameraRight[3] = camera.near;
		std::memcpy(data.cameraUp, camera.upOverScaleY, sizeof(camera.upOverScaleY));
		data.cameraUp[3] = a_debugView;
		data.params[0] = static_cast<float>(Settings::GetDouble("Skylighting/minVisibility"));

		// Full resolution from the HDR target's description; the depth buffer
		// has the same size, and the pass runs at RT_028's half of it.
		auto* const sceneTexture = Render::Targets::RenderTargetTexture(Render::Targets::kSceneHDR);
		if (sceneTexture == nullptr) {
			return false;
		}
		REX::W32::D3D11_TEXTURE2D_DESC sceneDesc{};
		sceneTexture->GetDesc(std::addressof(sceneDesc));
		data.params[2] = static_cast<float>(sceneDesc.width);
		data.params[3] = static_cast<float>(sceneDesc.height);
		return _constants.Update(std::addressof(data), sizeof(data));
	}

	void Skylighting::UpdateProbes()
	{
		auto* const context = Render::GetContext();
		if (context == nullptr || _update == nullptr || !_captureReady || _captureFrame != Render::FrameCount()) {
			return;
		}

		// The window once a frame, here, because this is what moves the ring
		// buffer; the pass later in the frame reads the same one. After a
		// reset every cell is new: a margin wider than the grid leaves
		// nothing valid.
		_window = SkyOcclusion::WindowFor(_eye, _previousCell);
		if (!_gridValid) {
			for (auto& margin : _window.validMargin) {
				margin = 1000;
			}
			_gridValid = true;
		}
		if (!FillConstants(0.0f)) {
			return;
		}

		// Compute bindings are set anew by the engine before it uses them;
		// ours are cleared after the dispatch so that nothing of the grid
		// stays bound as both a resource and a target.
		auto* const buffer = _constants.Buffer();
		auto* const occlusion = _occlusion.SRV();
		REX::W32::ID3D11UnorderedAccessView* uavs[2]{ _probes.UAV(), _accumFrames.UAV() };
		context->CSSetShader(_update, nullptr, 0);
		context->CSSetShaderResources(0, 1, std::addressof(occlusion));
		context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
		context->CSSetSamplers(0, 1, std::addressof(_comparison));
		context->CSSetConstantBuffers(1, 1, std::addressof(buffer));
		context->Dispatch(
			(SkyOcclusion::kGridSize[0] + 7) / 8,
			(SkyOcclusion::kGridSize[1] + 7) / 8,
			SkyOcclusion::kGridSize[2]);
		++_dispatches;

		REX::W32::ID3D11UnorderedAccessView* noUAVs[2]{ nullptr, nullptr };
		REX::W32::ID3D11ShaderResourceView* noSRV = nullptr;
		context->CSSetUnorderedAccessViews(0, 2, noUAVs, nullptr);
		context->CSSetShaderResources(0, 1, std::addressof(noSRV));
		context->CSSetShader(nullptr, nullptr, 0);
	}

	void Skylighting::DrawPass(
		REX::W32::ID3D11DeviceContext& a_context,
		REX::W32::ID3D11RenderTargetView* a_target,
		REX::W32::ID3D11BlendState* a_blend,
		float a_debugView)
	{
		if (!FillConstants(a_debugView)) {
			return;
		}
		auto* const depth = Render::Targets::DepthSRV(Render::Targets::kSceneDepth);
		if (depth == nullptr) {
			return;
		}

		// The viewport from the target, not from the screen: RT_028 is half
		// resolution, the HDR target full.
		REX::W32::ID3D11Resource* resource = nullptr;
		a_target->GetResource(std::addressof(resource));
		if (resource == nullptr) {
			return;
		}
		REX::W32::D3D11_TEXTURE2D_DESC desc{};
		reinterpret_cast<REX::W32::ID3D11Texture2D*>(resource)->GetDesc(std::addressof(desc));
		resource->Release();
		const REX::W32::D3D11_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(desc.width), static_cast<float>(desc.height), 0.0f, 1.0f };

		const Render::StateGuard guard;
		a_context.OMSetRenderTargets(0, nullptr, nullptr);
		auto* const buffer = _constants.Buffer();
		const float blendFactor[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
		a_context.OMSetRenderTargets(1, std::addressof(a_target), nullptr);
		a_context.OMSetBlendState(a_blend, blendFactor, 0xFFFFFFFFu);
		a_context.RSSetViewports(1, std::addressof(viewport));
		REX::W32::ID3D11ShaderResourceView* resources[3]{ _probes.SRV(), depth, _occlusion.SRV() };
		a_context.PSSetShaderResources(0, 3, resources);
		a_context.PSSetSamplers(0, 1, std::addressof(_linearClamp));
		a_context.PSSetConstantBuffers(1, 1, std::addressof(buffer));
		a_context.PSSetShader(_shader, nullptr, 0);
		Render::DrawFullscreen();
		++_passes;

		// The guard puts two pixel resources back; the third is ours alone.
		REX::W32::ID3D11ShaderResourceView* none = nullptr;
		a_context.PSSetShaderResources(2, 1, std::addressof(none));
	}

	void Skylighting::Draw()
	{
		auto* const context = Render::GetContext();
		if (context == nullptr || _shader == nullptr || !_gridValid || !Outdoors()) {
			return;
		}
		if (DebugViewIndex(Settings::GetString("Skylighting/debugView")) != 0.0f) {
			return;
		}
		auto* const target = Render::Targets::RenderTargetView(kAmbientOcclusionSlot);
		if (target == nullptr) {
			return;
		}
		const Render::PassScope scope{ "Skylighting/Shadow" };
		DrawPass(*context, target, _multiply, 0.0f);
	}

	void Skylighting::DrawDebug()
	{
		auto* const context = Render::GetContext();
		if (context == nullptr || _shader == nullptr || !_gridValid || !Outdoors()) {
			return;
		}
		const auto debugView = DebugViewIndex(Settings::GetString("Skylighting/debugView"));
		if (debugView == 0.0f) {
			return;
		}
		auto* const target = Render::Targets::RenderTargetView(Render::Targets::kSceneHDR);
		if (target == nullptr) {
			return;
		}
		DrawPass(*context, target, _plain, debugView);
	}

	// ------------------------------------------------------------- resources

	bool Skylighting::Compile()
	{
		std::vector<std::filesystem::path> watched;
		bool ok = true;

		const auto passSource = Shader::LoadSource(ShaderRoot(), kPassShaderFile);
		if (!passSource) {
			watched.push_back(ShaderRoot() / kPassShaderFile);
			REX::ERROR("Skylighting: {}", passSource.error());
			ok = false;
		} else {
			watched.insert(watched.end(), passSource->files.begin(), passSource->files.end());
		}
		const auto updateSource = Shader::LoadSource(ShaderRoot(), kUpdateShaderFile);
		if (!updateSource) {
			watched.push_back(ShaderRoot() / kUpdateShaderFile);
			REX::ERROR("Skylighting: {}", updateSource.error());
			ok = false;
		} else {
			watched.insert(watched.end(), updateSource->files.begin(), updateSource->files.end());
		}
		_watch.Reset(watched);
		if (!ok) {
			return false;
		}

		auto* const device = Render::GetDevice();
		if (device == nullptr) {
			return false;
		}

		const auto pass = Shader::CompilePixelShader(passSource->text, "Skylighting.hlsl", "main");
		if (!pass.Succeeded()) {
			REX::ERROR("Skylighting: {}", pass.diagnostics);
			return false;
		}
		REX::W32::ID3D11PixelShader* pixel = nullptr;
		if (device->CreatePixelShader(pass.bytecode.data(), pass.bytecode.size(), nullptr, std::addressof(pixel)) < 0) {
			REX::ERROR("Skylighting: the pass shader could not be created");
			return false;
		}

		const auto update = Shader::CompileComputeShader(updateSource->text, "UpdateProbes.hlsl", "main");
		if (!update.Succeeded()) {
			REX::ERROR("Skylighting: {}", update.diagnostics);
			pixel->Release();
			return false;
		}
		REX::W32::ID3D11ComputeShader* compute = nullptr;
		if (device->CreateComputeShader(update.bytecode.data(), update.bytecode.size(), nullptr, std::addressof(compute)) < 0) {
			REX::ERROR("Skylighting: the update shader could not be created");
			pixel->Release();
			return false;
		}

		if (_shader != nullptr) {
			_shader->Release();
		}
		if (_update != nullptr) {
			_update->Release();
		}
		_shader = pixel;
		_update = compute;
		static_cast<void>(Render::SetDebugName(reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_shader), "FO4CS_PS_Skylighting"));
		static_cast<void>(Render::SetDebugName(reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_update), "FO4CS_CS_SkylightingProbes"));
		REX::INFO("Skylighting: shaders compiled");
		return true;
	}

	bool Skylighting::EnsureStates()
	{
		auto* const device = Render::GetDevice();
		if (device == nullptr) {
			return false;
		}
		if (_multiply == nullptr) {
			// dest * src into the channels the composite reads, the state F2
			// and F4 multiply their light with.
			REX::W32::D3D11_BLEND_DESC desc{};
			auto& target = desc.renderTarget[0];
			target.blendEnable = true;
			target.srcBlend = REX::W32::D3D11_BLEND_ZERO;
			target.destBlend = REX::W32::D3D11_BLEND_SRC_COLOR;
			target.blendOp = REX::W32::D3D11_BLEND_OP_ADD;
			target.srcBlendAlpha = REX::W32::D3D11_BLEND_ZERO;
			target.destBlendAlpha = REX::W32::D3D11_BLEND_ONE;
			target.blendOpAlpha = REX::W32::D3D11_BLEND_OP_ADD;
			target.renderTargetWriteMask = kAmbientWriteMask;
			if (device->CreateBlendState(std::addressof(desc), std::addressof(_multiply)) < 0) {
				REX::ERROR("Skylighting: the multiply blend state could not be created");
				return false;
			}
		}
		if (_plain == nullptr) {
			REX::W32::D3D11_BLEND_DESC desc{};
			auto& target = desc.renderTarget[0];
			target.blendEnable = false;
			target.renderTargetWriteMask = static_cast<std::uint8_t>(REX::W32::D3D11_COLOR_WRITE_ENABLE_ALL);
			if (device->CreateBlendState(std::addressof(desc), std::addressof(_plain)) < 0) {
				REX::ERROR("Skylighting: the plain blend state could not be created");
				return false;
			}
		}
		if (_depthState == nullptr) {
			// Depth only, nearer wins, no stencil: the map is cleared to one
			// and the comparison sampler reads it as less or equal.
			REX::W32::D3D11_DEPTH_STENCIL_DESC desc{};
			desc.depthEnable = true;
			desc.depthWriteMask = REX::W32::D3D11_DEPTH_WRITE_MASK_ALL;
			desc.depthFunc = REX::W32::D3D11_COMPARISON_LESS_EQUAL;
			desc.stencilEnable = false;
			if (device->CreateDepthStencilState(std::addressof(desc), std::addressof(_depthState)) < 0) {
				REX::ERROR("Skylighting: the depth state could not be created");
				return false;
			}
		}
		if (_comparison == nullptr) {
			// depth <= reference: 1 where the cell centre is nearer the sky
			// than what the casters drew there.
			REX::W32::D3D11_SAMPLER_DESC desc{};
			desc.filter = REX::W32::D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
			desc.addressU = REX::W32::D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.addressV = REX::W32::D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.addressW = REX::W32::D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.comparisonFunc = REX::W32::D3D11_COMPARISON_LESS_EQUAL;
			desc.maxLOD = 3.402823466e+38f;
			if (device->CreateSamplerState(std::addressof(desc), std::addressof(_comparison)) < 0) {
				REX::ERROR("Skylighting: the comparison sampler could not be created");
				return false;
			}
		}
		if (_linearClamp == nullptr) {
			REX::W32::D3D11_SAMPLER_DESC desc{};
			desc.filter = REX::W32::D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			desc.addressU = REX::W32::D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.addressV = REX::W32::D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.addressW = REX::W32::D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.maxLOD = 3.402823466e+38f;
			if (device->CreateSamplerState(std::addressof(desc), std::addressof(_linearClamp)) < 0) {
				REX::ERROR("Skylighting: the sampler could not be created");
				return false;
			}
		}
		return true;
	}
}
