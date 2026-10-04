#include "Features/Skylighting.h"

#include "Features/Skylighting/OcclusionProjection.h"
#include "Render/Camera.h"
#include "Render/DebugName.h"
#include "Render/DrawHook.h"
#include "Render/FramePhase.h"
#include "Render/FullscreenPass.h"
#include "Render/Profiler.h"
#include "Render/Renderer.h"
#include "Render/Resolution.h"
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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <iterator>
#include <span>
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

		/// Read off the disassembly of the casters' vertex shaders (Shaders011
		/// .fxp, techniques 0xC01A, 0xC01E, 0xC09A, 0xA0C01A, 2026-09-13):
		///
		///     clip = cb12[8..11] * (World(cb2[0..3]), translation - cb12[35]) * pos
		///     clip.z = max(clip.z, 0)
		///
		/// Slot 12 is the engine's per-view buffer, 752 bytes, the Skyrim
		/// PerFrame layout: the view projection in rows at byte 128, the
		/// camera position adjust at byte 560. Nothing else in it positions
		/// a vertex; the rotation at 0, the projection at 64 and the inverse
		/// at 192 are not read by these shaders. The clamp is the engine's
		/// shadow pancaking: what lies before the near plane lands at depth
		/// zero, not outside the map.
		constexpr std::uint32_t kViewConstantSlot = 12;
		constexpr std::uint32_t kViewProjectionOffset = 128;
		constexpr std::uint32_t kPosAdjustOffset = 560;
		/// The shaders declare cb12 with 36 registers; anything shorter cannot
		/// be the buffer they read.
		constexpr std::uint32_t kViewConstantsNeeded = 576;
		/// Our two patches, staged in one small default buffer: the matrix,
		/// then the eye.
		constexpr std::uint32_t kPatchMatrixOffset = 0;
		constexpr std::uint32_t kPatchEyeOffset = 64;
		constexpr std::uint32_t kPatchBytes = 80;

		/// The depth slots the tally tells apart; DS_010 is the last one the
		/// inventory lists.
		constexpr std::size_t kDepthSlotsScanned = 16;
		constexpr std::size_t kNoDepthSlot = 255;
		constexpr std::size_t kNoView = Render::Targets::kDepthStencilViews;

		/// Which of the shadow map's four views a depth view is, kNoView for
		/// none. Every one of them takes casters: view 0 held only the near
		/// cascade, some 1700 units across, against a shadow distance of
		/// twenty thousand.
		std::size_t ShadowMapView(const REX::W32::ID3D11DepthStencilView* a_dsv) noexcept
		{
			if (a_dsv == nullptr) {
				return kNoView;
			}
			for (std::size_t view = 0; view < Render::Targets::kDepthStencilViews; ++view) {
				if (Render::Targets::DepthStencilView(Render::Targets::kShadowMap, view) == a_dsv) {
					return view;
				}
			}
			return kNoView;
		}

		/// What a caster draws, as one number for CaptureBucket: its vertex
		/// and index buffers and its draw arguments. The same object comes
		/// with the same key frame after frame, whatever the order; the
		/// casters that read their vertices from structured buffers have
		/// no vertex buffer and differ by their arguments.
		std::uint64_t DrawKey(REX::W32::ID3D11DeviceContext& a_context, const Render::DrawCall& a_call) noexcept
		{
			REX::W32::ID3D11Buffer* vertices = nullptr;
			std::uint32_t stride = 0;
			std::uint32_t offset = 0;
			a_context.IAGetVertexBuffers(0, 1, std::addressof(vertices), std::addressof(stride), std::addressof(offset));
			REX::W32::ID3D11Buffer* indices = nullptr;
			REX::W32::DXGI_FORMAT format{};
			std::uint32_t indexOffset = 0;
			a_context.IAGetIndexBuffer(std::addressof(indices), std::addressof(format), std::addressof(indexOffset));

			std::uint64_t key = 0;
			const auto fold = [&key](std::uint64_t a_value) { key = key * 0x100000001B3ull ^ a_value; };
			fold(reinterpret_cast<std::uintptr_t>(vertices));
			fold(offset);
			fold(reinterpret_cast<std::uintptr_t>(indices));
			fold(indexOffset);
			fold(a_call.count);
			fold(a_call.start);
			fold(static_cast<std::uint32_t>(a_call.baseVertex));
			fold(a_call.startInstance);
			fold(a_call.instanceCount);
			if (vertices != nullptr) {
				vertices->Release();
			}
			if (indices != nullptr) {
				indices->Release();
			}
			return key;
		}

		/// Once: the shadow map's texture and what each of its four views
		/// covers - an array slice per cascade is the expectation.
		void LogShadowMapViews() noexcept
		{
			std::string views;
			for (std::size_t view = 0; view < Render::Targets::kDepthStencilViews; ++view) {
				auto* const dsv = Render::Targets::DepthStencilView(Render::Targets::kShadowMap, view);
				if (dsv == nullptr) {
					views += std::format(" [{} empty]", view);
					continue;
				}
				REX::W32::D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
				dsv->GetDesc(std::addressof(desc));
				views += std::format(
					" [{} dimension {} format {} slices {}+{}]",
					view,
					static_cast<int>(desc.viewDimension),
					static_cast<int>(desc.format),
					desc.texture2DArray.firstArraySlice,
					desc.texture2DArray.arraySize);
			}
			REX::INFO("Skylighting: shadow map views:{}", views);
		}

		/// The vertex shader subtracts the position adjust from every object
		/// translation before it applies the view projection. With the eye
		/// written there, the casters arrive relative to the eye - the frame
		/// the grid's cells live in - and one projection, its cube centred on
		/// the origin, serves the capture and the lookup alike.
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

		/// A default usage buffer with no CPU access, the shape both ends of a
		/// GPU copy want: the engine's per-view constants are mirrored into
		/// one, the patches staged in another.
		REX::W32::ID3D11Buffer* CreateDefaultBuffer(std::uint32_t a_bytes, const char* a_debugName) noexcept
		{
			auto* const device = Render::GetDevice();
			if (device == nullptr) {
				return nullptr;
			}
			REX::W32::D3D11_BUFFER_DESC desc{};
			desc.byteWidth = a_bytes;
			desc.usage = REX::W32::D3D11_USAGE_DEFAULT;
			desc.bindFlags = REX::W32::D3D11_BIND_CONSTANT_BUFFER;
			REX::W32::ID3D11Buffer* buffer = nullptr;
			if (device->CreateBuffer(std::addressof(desc), nullptr, std::addressof(buffer)) < 0) {
				REX::ERROR("Skylighting: buffer {} ({} bytes) could not be created", a_debugName, a_bytes);
				return nullptr;
			}
			static_cast<void>(Render::SetDebugName(reinterpret_cast<REX::W32::ID3D11DeviceChild*>(buffer), a_debugName));
			return buffer;
		}

		/// One channel of an R11G11B10_FLOAT texel: five exponent bits, six
		/// (or five for blue) mantissa bits, no sign.
		float SmallFloat(std::uint32_t a_bits, std::uint32_t a_mantissaBits) noexcept
		{
			const auto mantissa = a_bits & ((1u << a_mantissaBits) - 1u);
			const auto exponent = (a_bits >> a_mantissaBits) & 0x1Fu;
			const float scale = static_cast<float>(1u << a_mantissaBits);
			if (exponent == 0) {
				return std::ldexp(static_cast<float>(mantissa) / scale, -14);
			}
			if (exponent == 31) {
				return mantissa == 0 ? 65504.0f : 0.0f;
			}
			return std::ldexp(1.0f + static_cast<float>(mantissa) / scale, static_cast<int>(exponent) - 15);
		}

		struct Range
		{
			float min{ 1e30f };
			float max{ -1e30f };
			float mean{ 0.0f };
		};

		Range RangeOf(std::span<const float> a_values) noexcept
		{
			Range range{};
			double sum = 0.0;
			for (const float value : a_values) {
				range.min = std::min(range.min, value);
				range.max = std::max(range.max, value);
				sum += value;
			}
			range.mean = a_values.empty() ? 0.0f : static_cast<float>(sum / static_cast<double>(a_values.size()));
			return range;
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
		_capturePhase = SkyOcclusion::kNoCapturePhase;
		_lastCapturedFrame = 0;
		_cycles = 0;
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
		_patch = CreateDefaultBuffer(static_cast<std::uint32_t>(kPatchBytes), "FO4CS_CB_SkyOcclusionPatch");
		if (_patch == nullptr) {
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

		if (!_ambientProbe.Create(REX::W32::DXGI_FORMAT_R8G8B8A8_UNORM, 4, "FO4CS_PROBE_Ambient") ||
			!_sceneProbe.Create(REX::W32::DXGI_FORMAT_R11G11B10_FLOAT, 4, "FO4CS_PROBE_Scene")) {
			return false;
		}

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
			ResetGrid();
			_reportedIndoors = false;
		} else if (!outdoors && !_reportedIndoors) {
			REX::INFO("Skylighting: indoors, standing down");
			_reportedIndoors = true;
		}
		_wasOutdoors = outdoors;

		++_frames;
		if (_frames % kLogInterval == 0) {
			if (auto* const context = Render::GetContext(); context != nullptr) {
				LogProbes(*context, Render::FrameCount());
			}
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

			std::string byDepth;
			for (const auto& [key, count] : _utilityByDepth) {
				const auto slot = key / Render::Targets::kDepthStencilViews;
				const auto view = key % Render::Targets::kDepthStencilViews;
				byDepth += slot == kNoDepthSlot ? std::format(" other x{}", count) : std::format(" DS_{:03}/{} x{}", slot, view, count);
			}
			REX::INFO(
				"skylighting tally: casters per frame {}..{}, frames with a second view buffer {}, utility draws by depth target:{}",
				_castersPerFrameMin == UINT32_MAX ? 0 : _castersPerFrameMin,
				_castersPerFrameMax,
				_framesWithSecondView,
				byDepth);
			_utilityByDepth.clear();
			_castersPerFrameMin = UINT32_MAX;
			_castersPerFrameMax = 0;
			_framesWithSecondView = 0;
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
		ReleaseUnbiased();

		for (auto** object : { reinterpret_cast<REX::W32::IUnknown**>(std::addressof(_update)),
				 reinterpret_cast<REX::W32::IUnknown**>(std::addressof(_shader)),
				 reinterpret_cast<REX::W32::IUnknown**>(std::addressof(_comparison)),
				 reinterpret_cast<REX::W32::IUnknown**>(std::addressof(_linearClamp)),
				 reinterpret_cast<REX::W32::IUnknown**>(std::addressof(_multiply)),
				 reinterpret_cast<REX::W32::IUnknown**>(std::addressof(_plain)),
				 reinterpret_cast<REX::W32::IUnknown**>(std::addressof(_depthState)),
				 reinterpret_cast<REX::W32::IUnknown**>(std::addressof(_viewConstants)),
				 reinterpret_cast<REX::W32::IUnknown**>(std::addressof(_patch)) }) {
			if (*object != nullptr) {
				(*object)->Release();
				*object = nullptr;
			}
		}
		_viewConstantsBytes = 0;
		_occlusion.Release();
		_probes.Release();
		_accumFrames.Release();
		_constants.Release();
		_ambientProbe.Release();
		_sceneProbe.Release();
	}

	void Skylighting::LogProbes(REX::W32::ID3D11DeviceContext& a_context, std::uint64_t a_frame) noexcept
	{
		std::array<std::uint8_t, Render::TexelProbe::kTexels * 4> bytes{};
		std::array<float, Render::TexelProbe::kTexels> values{};

		if (_ambientProbe.Read(a_context, a_frame, bytes)) {
			std::array<float, Render::TexelProbe::kTexels> red{};
			std::array<float, Render::TexelProbe::kTexels> alpha{};
			for (std::size_t i = 0; i < values.size(); ++i) {
				red[i] = bytes[i * 4 + 0] / 255.0f;
				values[i] = bytes[i * 4 + 1] / 255.0f;
				alpha[i] = bytes[i * 4 + 3] / 255.0f;
			}
			const auto r = RangeOf(red);
			const auto g = RangeOf(values);
			const auto a = RangeOf(alpha);
			REX::INFO("skylighting probe: RT_028 before the multiply r [{:.3f} {:.3f} mean {:.3f}] g [{:.3f} {:.3f}] a [{:.3f} {:.3f}]", r.min, r.max, r.mean, g.min, g.max, a.min, a.max);
		}

		if (_sceneProbe.Read(a_context, a_frame, bytes)) {
			std::array<float, Render::TexelProbe::kTexels> green{};
			for (std::size_t i = 0; i < values.size(); ++i) {
				std::uint32_t packed = 0;
				std::memcpy(std::addressof(packed), bytes.data() + i * 4, sizeof(packed));
				values[i] = SmallFloat(packed & 0x7FFu, 6);
				green[i] = SmallFloat((packed >> 11) & 0x7FFu, 6);
			}
			const auto r = RangeOf(values);
			const auto g = RangeOf(green);
			REX::INFO("skylighting probe: scene HDR r [{:.4f} {:.4f} mean {:.4f}] g [{:.4f} {:.4f} mean {:.4f}]", r.min, r.max, r.mean, g.min, g.max, g.mean);
		}
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

	bool Skylighting::EnsureViewBuffer(REX::W32::ID3D11Buffer* a_engine) noexcept
	{
		REX::W32::D3D11_BUFFER_DESC desc{};
		a_engine->GetDesc(std::addressof(desc));
		if (desc.byteWidth < kViewConstantsNeeded) {
			if (!_reportedSmallBuffer) {
				REX::ERROR("Skylighting: the view constants are {} bytes, the casters' vertex shaders read {}", desc.byteWidth, kViewConstantsNeeded);
				_reportedSmallBuffer = true;
			}
			return false;
		}
		if (_viewConstants != nullptr && _viewConstantsBytes == desc.byteWidth) {
			return true;
		}
		if (_viewConstants != nullptr) {
			_viewConstants->Release();
			_viewConstants = nullptr;
		}
		_viewConstants = CreateDefaultBuffer(desc.byteWidth, "FO4CS_CB_SkyOcclusionView");
		if (_viewConstants == nullptr) {
			return false;
		}
		_viewConstantsBytes = desc.byteWidth;
		REX::INFO(
			"Skylighting: view constants are {} bytes in slot {}; the view projection at byte {} and the position adjust at byte {} become ours",
			desc.byteWidth,
			kViewConstantSlot,
			kViewProjectionOffset,
			kPosAdjustOffset);
		return true;
	}

	bool Skylighting::BeginFrameCapture(REX::W32::ID3D11DeviceContext& a_context) noexcept
	{
		// Once a frame, at the first caster: the engine's bytes with our two
		// patches. Once a cycle, at its first frame: the direction, the
		// matrix, the eye, a clean depth map.
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

		REX::W32::ID3D11Buffer* engine = nullptr;
		a_context.VSGetConstantBuffers(kViewConstantSlot, 1, std::addressof(engine));
		if (engine == nullptr) {
			return false;
		}
		if (!EnsureViewBuffer(engine)) {
			engine->Release();
			return false;
		}

		// A frame that did not get here broke the cycle; the next one that
		// does starts a new map.
		_capturePhase = SkyOcclusion::NextCapturePhase(_capturePhase, _lastCapturedFrame, frame);
		_lastCapturedFrame = frame;
		const bool cycleStart = _capturePhase == 0;
		if (cycleStart) {
			const auto& eye = world->GetWorldTransform().translate;
			_eye[0] = eye.x;
			_eye[1] = eye.y;
			_eye[2] = eye.z;
			const auto maxZenith = Settings::GetDouble("Skylighting/maxZenith") * kPi / 180.0;
			SkyOcclusion::SkyDirection(_cycles++, kSequenceSeed, maxZenith, _direction);
			SkyOcclusion::OcclusionMatrix(kGridCentre, _direction, _matrix);
		}

		// The engine's bytes of this very frame, so that nothing in the
		// buffer is stale - the first copy of a session was the main menu's
		// spot light, and it stayed for the whole session. Then the two
		// patches, copied in on the GPU: a constant buffer takes no partial
		// UpdateSubresource, but it does take a sixteen byte aligned copy.
		a_context.CopyResource(_viewConstants, engine);
		alignas(16) std::uint8_t patch[kPatchBytes]{};
		float rows[16]{};
		for (int i = 0; i < 16; ++i) {
			rows[i] = static_cast<float>(_matrix[i]);
		}
		std::memcpy(patch + kPatchMatrixOffset, rows, sizeof(rows));
		const float adjust[4]{ static_cast<float>(_eye[0]), static_cast<float>(_eye[1]), static_cast<float>(_eye[2]), 0.0f };
		std::memcpy(patch + kPatchEyeOffset, adjust, sizeof(adjust));
		a_context.UpdateSubresource(_patch, 0, nullptr, patch, 0, 0);
		const REX::W32::D3D11_BOX matrixBox{ kPatchMatrixOffset, 0, 0, kPatchMatrixOffset + static_cast<std::uint32_t>(sizeof(rows)), 1, 1 };
		a_context.CopySubresourceRegion(_viewConstants, 0, kViewProjectionOffset, 0, 0, _patch, 0, std::addressof(matrixBox));
		const REX::W32::D3D11_BOX eyeBox{ kPatchEyeOffset, 0, 0, kPatchEyeOffset + static_cast<std::uint32_t>(sizeof(adjust)), 1, 1 };
		a_context.CopySubresourceRegion(_viewConstants, 0, kPosAdjustOffset, 0, 0, _patch, 0, std::addressof(eyeBox));

		engine->Release();

		if (cycleStart) {
			_occlusion.Clear(a_context, 1.0f);
		}
		_captureReady = true;
		return true;
	}

	void Skylighting::BeforeDraw(REX::W32::ID3D11DeviceContext& a_context, const Render::DrawCall& a_call) noexcept
	{
		_surrounding = false;
		// The depth view alone first: of some 2600 utility draws a frame
		// only the casters, under two hundred, need the colour targets
		// saved, and every view handed out is a reference to give back.
		REX::W32::ID3D11DepthStencilView* dsv = nullptr;
		a_context.OMGetRenderTargets(0, nullptr, std::addressof(dsv));
		const auto shadowView = ShadowMapView(dsv);
		const bool shadowCaster = shadowView != kNoView;
		TallyUtilityDraw(dsv, shadowCaster);
		if (!shadowCaster || !_occlusion.Valid() || !BeginFrameCapture(a_context)) {
			if (dsv != nullptr) {
				dsv->Release();
			}
			return;
		}
		++_castersSeen;
		_savedDepth = dsv;
		a_context.VSGetConstantBuffers(kViewConstantSlot, 1, std::addressof(_savedView));
		if (_frameFirstView == nullptr) {
			_frameFirstView = _savedView;
		} else if (static_cast<const void*>(_savedView) != _frameFirstView && !_frameHadSecondView) {
			_frameHadSecondView = true;
			++_framesWithSecondView;
		}
		if (SkyOcclusion::CaptureBucket(DrawKey(a_context, a_call)) != _capturePhase) {
			ReleaseSaved();
			return;
		}
		a_context.OMGetRenderTargets(kSavedTargets, _savedTargets, nullptr);
		a_context.OMGetDepthStencilState(std::addressof(_savedDepthState), std::addressof(_savedStencilRef));
		a_context.RSGetState(std::addressof(_savedRasterizer));

		// Once: what the engine draws its casters with, for the record. The
		// depth function decides whether a map cleared to one could take
		// anything at all.
		if (!_reportedStates) {
			_reportedStates = true;
			LogShadowMapViews();
			REX::W32::D3D11_DEPTH_STENCIL_DESC depthDesc{};
			if (_savedDepthState != nullptr) {
				_savedDepthState->GetDesc(std::addressof(depthDesc));
			}
			REX::W32::D3D11_RASTERIZER_DESC rasterDesc{};
			if (_savedRasterizer != nullptr) {
				_savedRasterizer->GetDesc(std::addressof(rasterDesc));
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
		// as viewport and scissor, the engine's rasterizer without its bias.
		// Its culling stays: the ground arrives in the map, so the winding
		// survives our projection.
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
		if (auto* const unbiased = UnbiasedState(_savedRasterizer); unbiased != nullptr) {
			a_context.RSSetState(unbiased);
		}
		a_context.VSSetConstantBuffers(kViewConstantSlot, 1, std::addressof(_viewConstants));

		_repeating = true;
		a_call.Repeat(a_context);
		_repeating = false;
		++_castersRepeated;

		a_context.VSSetConstantBuffers(kViewConstantSlot, 1, std::addressof(_savedView));
		a_context.OMSetDepthStencilState(_savedDepthState, _savedStencilRef);
		a_context.RSSetState(_savedRasterizer);
		a_context.OMSetRenderTargets(kSavedTargets, _savedTargets, _savedDepth);
		a_context.RSSetViewports(_savedViewportCount, _savedViewports);
		a_context.RSSetScissorRects(_savedScissorCount, _savedScissors);
		ReleaseSaved();
	}

	void Skylighting::TallyUtilityDraw(REX::W32::ID3D11DepthStencilView* a_dsv, bool a_caster) noexcept
	{
		// Every frame, no readback: which depth target each utility draw
		// goes to, by slot and view, and how many casters a frame holds.
		// This is what found the sun's far cascades in views 1 and 2 of
		// DS_008; a change of the engine's cascades shows here first.
		const auto frame = Render::FrameCount();
		if (frame != _tallyFrame) {
			if (_tallyFrame != 0) {
				_castersPerFrameMin = std::min(_castersPerFrameMin, _frameCasters);
				_castersPerFrameMax = std::max(_castersPerFrameMax, _frameCasters);
			}
			_tallyFrame = frame;
			_frameCasters = 0;
			_frameFirstView = nullptr;
			_frameHadSecondView = false;
		}
		if (a_caster) {
			++_frameCasters;
		}
		// Keyed slot * views + view; one key past every slot for the rest.
		constexpr auto kViews = Render::Targets::kDepthStencilViews;
		std::size_t key = kNoDepthSlot * kViews;
		if (a_dsv != nullptr) {
			for (std::size_t i = 0; i < kDepthSlotsScanned * kViews && key == kNoDepthSlot * kViews; ++i) {
				if (Render::Targets::DepthStencilView(i / kViews, i % kViews) == a_dsv) {
					key = i;
				}
			}
		}
		++_utilityByDepth[key];
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
		if (_savedRasterizer != nullptr) {
			_savedRasterizer->Release();
			_savedRasterizer = nullptr;
		}
	}

	REX::W32::ID3D11RasterizerState* Skylighting::UnbiasedState(REX::W32::ID3D11RasterizerState* a_engine) noexcept
	{
		if (a_engine == nullptr) {
			return nullptr;
		}
		for (const auto& pair : _unbiased) {
			if (pair.engine == a_engine) {
				return pair.ours;
			}
		}
		if (_unbiased.size() >= kUnbiasedStates) {
			return nullptr;
		}
		auto* const device = Render::GetDevice();
		if (device == nullptr) {
			return nullptr;
		}
		REX::W32::D3D11_RASTERIZER_DESC desc{};
		a_engine->GetDesc(std::addressof(desc));
		REX::INFO(
			"Skylighting: caster rasterizer state {} (cull {}, bias {} slope {:.3f}) repeated without bias",
			_unbiased.size() + 1,
			static_cast<int>(desc.cullMode),
			desc.depthBias,
			desc.slopeScaledDepthBias);
		desc.depthBias = 0;
		desc.depthBiasClamp = 0.0f;
		desc.slopeScaledDepthBias = 0.0f;
		// The whole map is the viewport and the scissor rectangle both.
		desc.scissorEnable = false;
		REX::W32::ID3D11RasterizerState* ours = nullptr;
		if (device->CreateRasterizerState(std::addressof(desc), std::addressof(ours)) < 0) {
			REX::ERROR("Skylighting: an unbiased rasterizer state could not be created");
			return nullptr;
		}
		a_engine->AddRef();
		_unbiased.push_back({ a_engine, ours });
		return ours;
	}

	void Skylighting::ReleaseUnbiased() noexcept
	{
		for (auto& pair : _unbiased) {
			pair.engine->Release();
			pair.ours->Release();
		}
		_unbiased.clear();
	}

	// ------------------------------------------------------------- grid, pass

	bool Skylighting::FillConstants(float a_debugView, bool a_forProbes) noexcept
	{
		// The probe update works around the eye of the window; the pass
		// reconstructs its pixels around the eye of this frame, and the
		// window's cell origin moves by the difference.
		auto* const world = RE::Main::WorldRootCamera();
		if (world == nullptr) {
			return false;
		}
		float matrix[16]{};
		std::memcpy(matrix, world->worldToCam, sizeof(matrix));
		if (!Render::IsPlausibleViewProjection(matrix)) {
			return false;
		}
		const auto& translate = world->GetWorldTransform().translate;
		const auto camera = Render::FogCameraFromMatrix(world->worldToCam, translate.z);
		const double current[3]{ translate.x, translate.y, translate.z };
		const auto& eye = a_forProbes ? _windowEye : current;

		Constants data{};
		for (int i = 0; i < 16; ++i) {
			data.occlusionViewProj[i] = static_cast<float>(_matrix[i]);
		}
		for (int i = 0; i < 3; ++i) {
			data.occlusionDir[i] = static_cast<float>(_direction[i]);
			data.posOffset[i] = static_cast<float>(_window.cellOrigin[i] + _windowEye[i] - eye[i]);
			data.arrayOrigin[i] = static_cast<std::uint32_t>(_window.arrayOrigin[i]);
			data.validMargin[i] = _window.validMargin[i];
		}

		std::memcpy(data.cameraForward, camera.forward, sizeof(camera.forward));
		std::memcpy(data.cameraRight, camera.rightOverScaleX, sizeof(camera.rightOverScaleX));
		data.cameraRight[3] = camera.near;
		std::memcpy(data.cameraUp, camera.upOverScaleY, sizeof(camera.upOverScaleY));
		data.cameraUp[3] = a_debugView;
		data.params[0] = static_cast<float>(Settings::GetDouble("Skylighting/minVisibility"));

		// The pixels of the depth that hold the picture: the full resolution,
		// or the render rectangle while DLSS upscales (the pass runs at RT_028's
		// half of it and reads the depth at the matching pixel).
		const auto depth = Render::Resolution::DepthExtent(Render::Resolution::Current());
		if (depth.width == 0 || depth.height == 0) {
			return false;
		}
		data.params[2] = static_cast<float>(depth.width);
		data.params[3] = static_cast<float>(depth.height);
		return _constants.Update(std::addressof(data), sizeof(data));
	}

	void Skylighting::UpdateProbes()
	{
		auto* const context = Render::GetContext();
		// Only once the map is whole: in the cycle's last frame, after its
		// last share of the casters went in.
		if (context == nullptr || _update == nullptr || !_captureReady || _captureFrame != Render::FrameCount() ||
			_capturePhase != SkyOcclusion::kCaptureCycle - 1) {
			return;
		}

		// The window once a cycle, here, because this is what moves the ring
		// buffer; the passes until the next one read the same. It belongs to
		// the cycle's eye, the one the map was drawn around. After a reset
		// every cell is new: a margin wider than the grid leaves nothing
		// valid.
		_window = SkyOcclusion::WindowFor(_eye, _previousCell);
		std::copy(std::begin(_eye), std::end(_eye), std::begin(_windowEye));
		if (!_gridValid) {
			for (auto& margin : _window.validMargin) {
				margin = 1000;
			}
			_gridValid = true;
		}
		if (!FillConstants(0.0f, true)) {
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
		if (!FillConstants(a_debugView, false)) {
			return;
		}
		auto* const depth = Render::Targets::DepthSRV(Render::Targets::kSceneDepth);
		if (depth == nullptr) {
			return;
		}

		// The viewport from the target, not from the screen: RT_028 is half
		// resolution, the HDR target full - and of either only the part that
		// holds the picture, the render rectangle while DLSS upscales.
		REX::W32::ID3D11Resource* resource = nullptr;
		a_target->GetResource(std::addressof(resource));
		if (resource == nullptr) {
			return;
		}
		REX::W32::D3D11_TEXTURE2D_DESC desc{};
		reinterpret_cast<REX::W32::ID3D11Texture2D*>(resource)->GetDesc(std::addressof(desc));
		resource->Release();
		const auto picture = Render::Resolution::RenderPart(Render::Resolution::Current(), Render::Extent{ desc.width, desc.height });
		const REX::W32::D3D11_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(picture.width), static_cast<float>(picture.height), 0.0f, 1.0f };

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
		if (auto* const texture = Render::Targets::RenderTargetTexture(kAmbientOcclusionSlot); texture != nullptr) {
			REX::W32::D3D11_TEXTURE2D_DESC desc{};
			texture->GetDesc(std::addressof(desc));
			_ambientProbe.Capture(*context, reinterpret_cast<REX::W32::ID3D11Resource*>(texture), desc.width, desc.height, Render::FrameCount());
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
		if (auto* const texture = Render::Targets::RenderTargetTexture(Render::Targets::kSceneHDR); texture != nullptr) {
			REX::W32::D3D11_TEXTURE2D_DESC desc{};
			texture->GetDesc(std::addressof(desc));
			_sceneProbe.Capture(*context, reinterpret_cast<REX::W32::ID3D11Resource*>(texture), desc.width, desc.height, Render::FrameCount());
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
