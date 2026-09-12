#include "Features/CloudShadows.h"

#include "Features/CloudShadows/CloudProjection.h"
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
#include <RE/N/NiLight.h>
#include <RE/S/Sky.h>

// RE/S/Sun.h is not self-contained; see ScreenSpaceShadows.cpp.
#include <RE/S/SkyObject.h>

namespace RE
{
	class BSShaderAccumulator;
	class BSTriShape;
	class NiBillboardNode;
	class NiDirectionalLight;
}

#include <RE/S/Sun.h>

#include <REX/W32/DXGI.h>

#include <cmath>
#include <cstring>
#include <string>
#include <string_view>

namespace Features
{
	namespace
	{
		constexpr auto kShaderFile = "CloudShadows/CloudShadows.hlsl";
		constexpr std::uint32_t kCloudsTechnique = 0x0005;
		constexpr std::uint64_t kLogInterval = 180;
		constexpr auto kCoverageFormat = REX::W32::DXGI_FORMAT_A8_UNORM;
		constexpr std::uint32_t kCubeSize = 512;

		/// The sky's per-view vertex constants, as the probe read them at the
		/// clouds draw on 2026-09-12: slot 12, 752 bytes, four 4x4 matrices
		/// in rows at the front - the camera rotation (right, up, forward,
		/// 0 0 0 1), the projection, their product without a translation, and
		/// the rotation transposed. Which of the four the vertex shader reads
		/// is not known, so all four are replaced together.
		constexpr std::uint32_t kSkyConstantSlot = 12;
		constexpr std::size_t kRotationOffset = 0;
		constexpr std::size_t kProjectionOffset = 64;
		constexpr std::size_t kViewProjectionOffset = 128;
		constexpr std::size_t kInverseRotationOffset = 192;
		constexpr std::size_t kMatricesEnd = 256;
		constexpr float kNearFallback = 15.0f;

		/// The layer's geometry constants, slot 2, 224 bytes as the probe read
		/// them: the world view projection in rows at the front - which is
		/// what the vertex shader positions with, the first repeat showed the
		/// camera's own picture in every face - and the layer's world matrix
		/// as three rows of four behind it, a turn about the vertical and a
		/// small translation.
		constexpr std::uint32_t kGeometryConstantSlot = 2;
		constexpr std::size_t kGeometryWvpOffset = 0;
		constexpr std::size_t kGeometryWorldOffset = 64;
		constexpr std::size_t kGeometryBytesNeeded = 112;

		/// D3D's cube faces, in the order TextureCube samples them: the
		/// direction the face looks along, its up, its right. Chosen so that
		/// sampling the cube with a world direction lands on what this face
		/// drew - checked against the face orientation table of the D3D spec.
		struct FaceAxes
		{
			float forward[3];
			float up[3];
			float right[3];
		};

		constexpr FaceAxes kFaces[6] = {
			{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, -1 } },
			{ { -1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
			{ { 0, 1, 0 }, { 0, 0, -1 }, { 1, 0, 0 } },
			{ { 0, -1, 0 }, { 0, 0, 1 }, { 1, 0, 0 } },
			{ { 0, 0, 1 }, { 0, 1, 0 }, { 1, 0, 0 } },
			{ { 0, 0, -1 }, { 0, 1, 0 }, { -1, 0, 0 } },
		};

		/// Field for field the cbuffer PerFrame of CloudShadows.hlsl.
		struct alignas(16) ShadowConstants
		{
			float cameraForward[4];
			float cameraRight[4];
			float cameraUp[4];
			float sunDirection[4];
			float params[4];
		};
		static_assert(sizeof(ShadowConstants) == 5 * 16);

		std::filesystem::path ShaderRoot()
		{
			return Util::DataDirectory() / "Shaders" / "FO4";
		}

		float DebugViewIndex(std::string_view a_choice) noexcept
		{
			if (a_choice == "coverage") {
				return 1.0f;
			}
			if (a_choice == "direction") {
				return 2.0f;
			}
			if (a_choice == "cube") {
				return 3.0f;
			}
			return 0.0f;
		}

		void WriteRow(float* a_row, const float (&a_axis)[3], float a_w) noexcept
		{
			a_row[0] = a_axis[0];
			a_row[1] = a_axis[1];
			a_row[2] = a_axis[2];
			a_row[3] = a_w;
		}

		void FaceViewProjection(const FaceAxes& a_face, float a_near, float (&a_out)[16]) noexcept;

		/// The four matrices of one face, 90 degrees wide, camera centred,
		/// in the arrangement the probe found: rows, no translation.
		void WriteFaceMatrices(std::uint8_t* a_image, const FaceAxes& a_face, float a_near) noexcept
		{
			float rotation[16]{};
			WriteRow(rotation + 0, a_face.right, 0.0f);
			WriteRow(rotation + 4, a_face.up, 0.0f);
			WriteRow(rotation + 8, a_face.forward, 0.0f);
			rotation[15] = 1.0f;

			// [1 0 0 0] [0 1 0 0] [0 0 1 -near] [0 0 1 0]: unit scales for 90
			// degrees, z = w - near as the engine has it.
			float projection[16]{};
			projection[0] = 1.0f;
			projection[5] = 1.0f;
			projection[10] = 1.0f;
			projection[11] = -a_near;
			projection[14] = 1.0f;

			float viewProjection[16]{};
			FaceViewProjection(a_face, a_near, viewProjection);

			float inverse[16]{};
			for (int i = 0; i < 3; ++i) {
				inverse[i * 4 + 0] = a_face.right[i];
				inverse[i * 4 + 1] = a_face.up[i];
				inverse[i * 4 + 2] = a_face.forward[i];
			}
			inverse[15] = 1.0f;

			std::memcpy(a_image + kRotationOffset, rotation, sizeof(rotation));
			std::memcpy(a_image + kProjectionOffset, projection, sizeof(projection));
			std::memcpy(a_image + kViewProjectionOffset, viewProjection, sizeof(viewProjection));
			std::memcpy(a_image + kInverseRotationOffset, inverse, sizeof(inverse));
		}

		/// The face's view projection, rows right / up / forward - near /
		/// forward, no translation: the sky is camera centred.
		void FaceViewProjection(const FaceAxes& a_face, float a_near, float (&a_out)[16]) noexcept
		{
			WriteRow(a_out + 0, a_face.right, 0.0f);
			WriteRow(a_out + 4, a_face.up, 0.0f);
			WriteRow(a_out + 8, a_face.forward, -a_near);
			WriteRow(a_out + 12, a_face.forward, 0.0f);
		}

		/// M = F * W for row matrices, W given as three rows of four with an
		/// implied (0 0 0 1) fourth: clip_i = sum_k F[i][k] * (W p)_k.
		void MultiplyWorld(const float (&a_face)[16], const float (&a_world)[12], float (&a_out)[16]) noexcept
		{
			for (int i = 0; i < 4; ++i) {
				for (int j = 0; j < 4; ++j) {
					float sum = a_face[i * 4 + 3] * (j == 3 ? 1.0f : 0.0f);
					for (int k = 0; k < 3; ++k) {
						sum += a_face[i * 4 + k] * a_world[k * 4 + j];
					}
					a_out[i * 4 + j] = sum;
				}
			}
		}

		/// The engine's near plane out of its projection block: z = w - near
		/// puts -near at row two, column three.
		float NearFrom(const std::vector<std::uint8_t>& a_image) noexcept
		{
			if (a_image.size() < kMatricesEnd) {
				return kNearFallback;
			}
			float value = 0.0f;
			std::memcpy(std::addressof(value), a_image.data() + kProjectionOffset + (2 * 4 + 3) * sizeof(float), sizeof(value));
			const auto near = -value;
			return (near > 0.1f && near < 1000.0f) ? near : kNearFallback;
		}
	}

	void CloudShadows::Declare()
	{
		Settings::DeclareFeature("CloudShadows", true)
			.Label("feature.cloud_shadows.name", "Cloud Shadows")
			.Help(
				"feature.cloud_shadows.help",
				"Shadows of the clouds overhead, moving across the landscape with them. Taken "
				"from the clouds the game draws, so they match the sky.");

		Settings::DeclareSlider("CloudShadows/opacity", 0.5, 0.0, 4.0)
			.Label("feature.cloud_shadows.opacity", "Opacity")
			.Help("feature.cloud_shadows.opacity_help", "How dark the cloud shadows are.");

		Settings::DeclareSlider("CloudShadows/cloudHeight", 2000.0, 500.0, 20000.0)
			.Label("feature.cloud_shadows.cloud_height", "Cloud Height")
			.Help(
				"feature.cloud_shadows.cloud_height_help",
				"Height of the cloud layer in metres. Sets how far the shadows are offset from "
				"the clouds when the sun is low.");

		Settings::DeclareChoice(
			"CloudShadows/debugView",
			"off",
			std::vector<std::string>{ "off", "coverage", "direction", "cube" })
			.Label("feature.cloud_shadows.debug_view", "Debug View")
			.Help(
				"feature.cloud_shadows.debug_view_help",
				"Shows the pass instead of the picture: the cloud coverage as the ground sees "
				"it, the direction it is sampled in, or the six faces of the coverage map.");
	}

	bool CloudShadows::Setup()
	{
		_reportedNoSun = false;
		_reportedNoTargets = false;
		_reportedLowSun = false;
		_draws = 0;
		_capturedDraws = 0;
		_repeatedDraws = 0;
		_layersReady = 0;
		_layerIndex = 0;
		_layerFrame = 0;
		_mainView = false;
		_repeating = false;
		_imageReady = false;
		for (auto& ready : _layerReady) {
			ready = false;
		}

		const auto skyIndex = Render::ClassIndexOf("BSSkyShader");
		if (!skyIndex.has_value()) {
			REX::ERROR("CloudShadows: BSSkyShader is not among the shader classes");
			return false;
		}
		_clouds = Render::TechniqueFilter{ *skyIndex, kCloudsTechnique };

		auto* const device = Render::GetDevice();
		std::uint32_t support = 0;
		if (device == nullptr ||
			device->CheckFormatSupport(kCoverageFormat, std::addressof(support)) < 0 ||
			(support & REX::W32::D3D11_FORMAT_SUPPORT_RENDER_TARGET) == 0 ||
			(support & REX::W32::D3D11_FORMAT_SUPPORT_BLENDABLE) == 0) {
			REX::ERROR("CloudShadows: A8_UNORM is not a blendable render target here (support 0x{:X})", support);
			return false;
		}

		if (!Render::InitFullscreenPass() || !Compile() || !EnsureStates()) {
			return false;
		}
		if (!_constants.Create(sizeof(ShadowConstants), "FO4CS_CB_CloudShadows")) {
			return false;
		}
		if (!_coverage.Create(kCubeSize, kCoverageFormat, "FO4CS_CUBE_CloudCoverage")) {
			return false;
		}

		_phase = Render::SubscribeFramePhase(
			Render::Phase::kBeforeComposite, "CloudShadows/Draw", [this] { Draw(); });
		if (_phase == Render::PhaseDispatcher::kNoToken) {
			REX::ERROR("CloudShadows: the frame phase has no room left");
			return false;
		}

		Render::SetDrawObserver(this);
		REX::INFO("CloudShadows: capturing BSSkyShader technique 0x{:04X} into a {} cube", kCloudsTechnique, kCubeSize);
		return true;
	}

	void CloudShadows::Frame()
	{
		if (_watch.Poll()) {
			REX::INFO("CloudShadows: shader changed, recompiling");
			static_cast<void>(Compile());
		}
		ReadStagedConstants();
	}

	void CloudShadows::Shutdown()
	{
		// First, before anything it could touch goes away.
		Render::SetDrawObserver(nullptr);

		Render::UnsubscribeFramePhase(Render::Phase::kBeforeComposite, _phase);
		_phase = Render::PhaseDispatcher::kNoToken;

		ReleaseSaved();

		if (_shader != nullptr) {
			_shader->Release();
			_shader = nullptr;
		}
		for (auto** state : { std::addressof(_multiply), std::addressof(_plain), std::addressof(_alphaComposite) }) {
			if (*state != nullptr) {
				(*state)->Release();
				*state = nullptr;
			}
		}
		if (_cullNone != nullptr) {
			_cullNone->Release();
			_cullNone = nullptr;
		}
		if (_linearClamp != nullptr) {
			_linearClamp->Release();
			_linearClamp = nullptr;
		}
		if (_staging != nullptr) {
			_staging->Release();
			_staging = nullptr;
		}
		for (auto*& buffer : _faceConstants) {
			if (buffer != nullptr) {
				buffer->Release();
				buffer = nullptr;
			}
		}
		for (auto*& buffer : _layerStaging) {
			if (buffer != nullptr) {
				buffer->Release();
				buffer = nullptr;
			}
		}
		for (auto*& buffer : _geometryConstants) {
			if (buffer != nullptr) {
				buffer->Release();
				buffer = nullptr;
			}
		}
		for (auto& image : _layerImage) {
			image.clear();
		}
		_image.clear();
		_imageReady = false;
		_constantBytes = 0;
		_geometryBytes = 0;
		_constants.Release();
		_coverage.Release();
	}

	bool CloudShadows::EnsureGeometryCopies(REX::W32::ID3D11Buffer* a_engine) noexcept
	{
		REX::W32::D3D11_BUFFER_DESC desc{};
		a_engine->GetDesc(std::addressof(desc));
		if (desc.byteWidth < kGeometryBytesNeeded) {
			return false;
		}
		if (_geometryBytes == desc.byteWidth) {
			return true;
		}

		auto* const device = Render::GetDevice();
		if (device == nullptr) {
			return false;
		}

		for (auto*& buffer : _layerStaging) {
			if (buffer != nullptr) {
				buffer->Release();
				buffer = nullptr;
			}
		}
		for (auto*& buffer : _geometryConstants) {
			if (buffer != nullptr) {
				buffer->Release();
				buffer = nullptr;
			}
		}
		for (auto& ready : _layerReady) {
			ready = false;
		}

		REX::W32::D3D11_BUFFER_DESC stagingDesc{};
		stagingDesc.byteWidth = desc.byteWidth;
		stagingDesc.usage = REX::W32::D3D11_USAGE_STAGING;
		stagingDesc.cpuAccessFlags = REX::W32::D3D11_CPU_ACCESS_READ;
		for (auto*& buffer : _layerStaging) {
			if (device->CreateBuffer(std::addressof(stagingDesc), nullptr, std::addressof(buffer)) < 0) {
				REX::ERROR("CloudShadows: a staging copy of the layer constants could not be created");
				return false;
			}
		}

		REX::W32::D3D11_BUFFER_DESC faceDesc{};
		faceDesc.byteWidth = desc.byteWidth;
		faceDesc.usage = REX::W32::D3D11_USAGE_DEFAULT;
		faceDesc.bindFlags = REX::W32::D3D11_BIND_CONSTANT_BUFFER;
		for (auto*& buffer : _geometryConstants) {
			if (device->CreateBuffer(std::addressof(faceDesc), nullptr, std::addressof(buffer)) < 0) {
				REX::ERROR("CloudShadows: a face's geometry constants could not be created");
				return false;
			}
		}

		for (auto& image : _layerImage) {
			image.assign(desc.byteWidth, 0);
		}
		_geometryBytes = desc.byteWidth;
		REX::INFO("CloudShadows: layer constants are {} bytes in slot {}", desc.byteWidth, kGeometryConstantSlot);
		return true;
	}

	bool CloudShadows::ReadLayerGeometry(REX::W32::ID3D11DeviceContext& a_context, std::uint32_t a_layer) noexcept
	{
		// Last frame's copy of this layer's constants. The GPU finished it a
		// frame ago, so the map is not expected to wait; if it would, the
		// layer sits this frame out rather than stall the thread.
		REX::W32::D3D11_MAPPED_SUBRESOURCE mapped{};
		if (a_context.Map(_layerStaging[a_layer], 0, REX::W32::D3D11_MAP_READ, REX::W32::D3D11_MAP_FLAG_DO_NOT_WAIT, std::addressof(mapped)) < 0) {
			return false;
		}
		std::memcpy(_layerImage[a_layer].data(), mapped.data, _layerImage[a_layer].size());
		a_context.Unmap(_layerStaging[a_layer], 0);
		return true;
	}

	void CloudShadows::WriteFaceGeometry(REX::W32::ID3D11DeviceContext& a_context, std::uint32_t a_layer) noexcept
	{
		float world[12]{};
		std::memcpy(world, _layerImage[a_layer].data() + kGeometryWorldOffset, sizeof(world));
		const auto near = NearFrom(_image);

		std::vector<std::uint8_t> image = _layerImage[a_layer];
		for (std::uint32_t face = 0; face < kCapturedFaces; ++face) {
			float faceProjection[16]{};
			FaceViewProjection(kFaces[face], near, faceProjection);
			float wvp[16]{};
			MultiplyWorld(faceProjection, world, wvp);
			std::memcpy(image.data() + kGeometryWvpOffset, wvp, sizeof(wvp));
			a_context.UpdateSubresource(_geometryConstants[face], 0, nullptr, image.data(), 0, 0);
		}
	}

	bool CloudShadows::Wants(const Render::CurrentTechnique& a_current) const noexcept
	{
		// Our own repeats run through the same hook; they are not observed.
		return !_repeating && _clouds.Matches(a_current);
	}

	void CloudShadows::ReleaseSaved() noexcept
	{
		for (auto*& target : _savedTargets) {
			if (target != nullptr) {
				target->Release();
				target = nullptr;
			}
		}
		for (auto** object : { reinterpret_cast<REX::W32::IUnknown**>(std::addressof(_savedDepth)),
				 reinterpret_cast<REX::W32::IUnknown**>(std::addressof(_savedBlend)),
				 reinterpret_cast<REX::W32::IUnknown**>(std::addressof(_savedRasterizer)),
				 reinterpret_cast<REX::W32::IUnknown**>(std::addressof(_savedConstants)),
				 reinterpret_cast<REX::W32::IUnknown**>(std::addressof(_savedGeometry)) }) {
			if (*object != nullptr) {
				(*object)->Release();
				*object = nullptr;
			}
		}
	}

	bool CloudShadows::EnsureConstantCopies(REX::W32::ID3D11DeviceContext& a_context, REX::W32::ID3D11Buffer* a_engine) noexcept
	{
		REX::W32::D3D11_BUFFER_DESC desc{};
		a_engine->GetDesc(std::addressof(desc));
		if (desc.byteWidth < kMatricesEnd) {
			return false;
		}

		if (_staging != nullptr && _constantBytes == desc.byteWidth) {
			return true;
		}

		auto* const device = Render::GetDevice();
		if (device == nullptr) {
			return false;
		}

		if (_staging != nullptr) {
			_staging->Release();
			_staging = nullptr;
		}
		for (auto*& buffer : _faceConstants) {
			if (buffer != nullptr) {
				buffer->Release();
				buffer = nullptr;
			}
		}
		_imageReady = false;

		REX::W32::D3D11_BUFFER_DESC stagingDesc{};
		stagingDesc.byteWidth = desc.byteWidth;
		stagingDesc.usage = REX::W32::D3D11_USAGE_STAGING;
		stagingDesc.cpuAccessFlags = REX::W32::D3D11_CPU_ACCESS_READ;
		if (device->CreateBuffer(std::addressof(stagingDesc), nullptr, std::addressof(_staging)) < 0) {
			REX::ERROR("CloudShadows: the staging copy of the sky constants could not be created");
			return false;
		}

		REX::W32::D3D11_BUFFER_DESC faceDesc{};
		faceDesc.byteWidth = desc.byteWidth;
		faceDesc.usage = REX::W32::D3D11_USAGE_DEFAULT;
		faceDesc.bindFlags = REX::W32::D3D11_BIND_CONSTANT_BUFFER;
		for (std::uint32_t face = 0; face < kCapturedFaces; ++face) {
			if (device->CreateBuffer(std::addressof(faceDesc), nullptr, std::addressof(_faceConstants[face])) < 0) {
				REX::ERROR("CloudShadows: face {} has no constant buffer", face);
				return false;
			}
		}

		_constantBytes = desc.byteWidth;
		_image.assign(desc.byteWidth, 0);
		REX::INFO("CloudShadows: sky constants are {} bytes in slot {}", desc.byteWidth, kSkyConstantSlot);
		static_cast<void>(a_context);
		return true;
	}

	void CloudShadows::ReadStagedConstants() noexcept
	{
		auto* const context = Render::GetContext();
		if (context == nullptr || _staging == nullptr || _stagingFrame == 0 || _copiedFrame == _stagingFrame) {
			return;
		}
		// A frame later than the copy, and never waiting: the render thread
		// must not stall on its own copy.
		if (Render::FrameCount() <= _stagingFrame) {
			return;
		}

		REX::W32::D3D11_MAPPED_SUBRESOURCE mapped{};
		if (context->Map(_staging, 0, REX::W32::D3D11_MAP_READ, REX::W32::D3D11_MAP_FLAG_DO_NOT_WAIT, std::addressof(mapped)) < 0) {
			return;
		}
		std::memcpy(_image.data(), mapped.data, _image.size());
		context->Unmap(_staging, 0);
		_copiedFrame = _stagingFrame;
		_imageReady = true;
	}

	void CloudShadows::WriteFaceConstants(REX::W32::ID3D11DeviceContext& a_context) noexcept
	{
		const auto near = NearFrom(_image);
		std::vector<std::uint8_t> image = _image;
		for (std::uint32_t face = 0; face < kCapturedFaces; ++face) {
			WriteFaceMatrices(image.data(), kFaces[face], near);
			a_context.UpdateSubresource(_faceConstants[face], 0, nullptr, image.data(), 0, 0);
		}
	}

	void CloudShadows::BeforeDraw(REX::W32::ID3D11DeviceContext& a_context, const Render::DrawCall&) noexcept
	{
		_mainView = false;
		if (!_coverage.Valid()) {
			return;
		}

		// The main view only: the same draws go once more into the planar
		// water reflection, RT_002, which is not the picture the clouds are
		// seen in.
		REX::W32::ID3D11RenderTargetView* target = nullptr;
		a_context.OMGetRenderTargets(1, std::addressof(target), nullptr);
		const bool mainView = target != nullptr &&
		                      target == Render::Targets::RenderTargetView(Render::Targets::kSceneHDR);
		if (target != nullptr) {
			target->Release();
		}
		if (!mainView) {
			return;
		}

		a_context.VSGetConstantBuffers(kSkyConstantSlot, 1, std::addressof(_savedConstants));
		if (_savedConstants == nullptr || !EnsureConstantCopies(a_context, _savedConstants)) {
			ReleaseSaved();
			return;
		}

		// Once a frame: the sky's per-view constants are the same for all
		// nine layers. Copied now, read from Present a frame later.
		const auto frame = Render::FrameCount();
		if (_stagingFrame != frame) {
			a_context.CopyResource(_staging, _savedConstants);
			_stagingFrame = frame;
		}

		// Per draw: which layer this is, counted from the frame's first.
		if (_layerFrame != frame) {
			_layerFrame = frame;
			_layerIndex = 0;
		}
		_layer = _layerIndex++;
		if (_layer >= kMaxLayers) {
			ReleaseSaved();
			return;
		}

		a_context.VSGetConstantBuffers(kGeometryConstantSlot, 1, std::addressof(_savedGeometry));
		if (_savedGeometry == nullptr || !EnsureGeometryCopies(_savedGeometry)) {
			ReleaseSaved();
			return;
		}

		// Last frame's constants of this layer first, then this frame's copy
		// over them for the next.
		_layerReady[_layer] = ReadLayerGeometry(a_context, _layer);
		a_context.CopyResource(_layerStaging[_layer], _savedGeometry);
		if (_layerReady[_layer]) {
			++_layersReady;
		}

		++_capturedDraws;
		_mainView = true;
	}

	void CloudShadows::AfterDraw(REX::W32::ID3D11DeviceContext& a_context, const Render::DrawCall& a_call) noexcept
	{
		if (!_mainView) {
			return;
		}
		_mainView = false;

		if (!_imageReady || !_layerReady[_layer]) {
			ReleaseSaved();
			return;
		}

		const auto frame = Render::FrameCount();
		if (_facesWrittenFrame != frame) {
			_facesWrittenFrame = frame;
			WriteFaceConstants(a_context);
			for (std::uint32_t face = 0; face < kCapturedFaces; ++face) {
				_coverage.ClearFace(a_context, face, 0.0f);
				_faceClearedFrame[face] = frame;
			}
		}
		WriteFaceGeometry(a_context, _layer);

		a_context.OMGetRenderTargets(kSavedTargets, _savedTargets, std::addressof(_savedDepth));
		a_context.OMGetBlendState(std::addressof(_savedBlend), _savedFactor, std::addressof(_savedMask));
		a_context.RSGetState(std::addressof(_savedRasterizer));
		_savedViewportCount = 16;
		a_context.RSGetViewports(std::addressof(_savedViewportCount), _savedViewports);

		// No depth view: the cube has none, and without one D3D11 skips the
		// depth and stencil tests. Cull none: the face bases have the other
		// handedness from the engine's view, which flips the winding.
		const REX::W32::D3D11_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(_coverage.Size()), static_cast<float>(_coverage.Size()), 0.0f, 1.0f };
		const float factor[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
		a_context.RSSetViewports(1, std::addressof(viewport));
		a_context.RSSetState(_cullNone);
		a_context.OMSetBlendState(_alphaComposite, factor, 0xFFFFFFFFu);

		_repeating = true;
		for (std::uint32_t face = 0; face < kCapturedFaces; ++face) {
			auto* const rtv = _coverage.FaceRTV(face);
			a_context.OMSetRenderTargets(1, std::addressof(rtv), nullptr);
			a_context.VSSetConstantBuffers(kSkyConstantSlot, 1, std::addressof(_faceConstants[face]));
			a_context.VSSetConstantBuffers(kGeometryConstantSlot, 1, std::addressof(_geometryConstants[face]));
			a_call.Repeat(a_context);
			++_repeatedDraws;
		}
		_repeating = false;

		a_context.VSSetConstantBuffers(kSkyConstantSlot, 1, std::addressof(_savedConstants));
		a_context.VSSetConstantBuffers(kGeometryConstantSlot, 1, std::addressof(_savedGeometry));
		a_context.OMSetRenderTargets(kSavedTargets, _savedTargets, _savedDepth);
		a_context.OMSetBlendState(_savedBlend, _savedFactor, _savedMask);
		a_context.RSSetState(_savedRasterizer);
		a_context.RSSetViewports(_savedViewportCount, _savedViewports);
		ReleaseSaved();
	}

	void CloudShadows::Draw()
	{
		++_draws;

		const auto* const sky = RE::Sky::GetSingleton();
		if (sky == nullptr || sky->mode.get() != RE::Sky::Mode::kFull ||
			sky->sun == nullptr || sky->sun->light.get() == nullptr) {
			if (!_reportedNoSun) {
				REX::INFO("CloudShadows: no sun, standing down");
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
					"CloudShadows: standing down, depth {}, diffuse {}, specular {}",
					depth != nullptr ? "ok" : "missing",
					diffuse != nullptr ? "ok" : "missing",
					specular != nullptr ? "ok" : "missing");
				_reportedNoTargets = true;
			}
			return;
		}
		_reportedNoTargets = false;

		auto* const world = RE::Main::WorldRootCamera();
		auto* const context = Render::GetContext();
		if (world == nullptr || context == nullptr || _shader == nullptr || !_coverage.Valid()) {
			return;
		}

		float matrix[16]{};
		std::memcpy(matrix, world->worldToCam, sizeof(matrix));
		if (!Render::IsPlausibleViewProjection(matrix)) {
			return;
		}
		const auto camera = Render::FogCameraFromMatrix(world->worldToCam, world->GetWorldTransform().translate.z);

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

		// Below the horizon there is no ray up to the clouds.
		if (towardsSun[2] <= 0.0f) {
			if (!_reportedLowSun) {
				REX::INFO("CloudShadows: sun below the horizon, standing down");
				_reportedLowSun = true;
			}
			return;
		}
		_reportedLowSun = false;

		const auto debugView = DebugViewIndex(Settings::GetString("CloudShadows/debugView"));
		const auto cloudHeight =
			static_cast<float>(Settings::GetDouble("CloudShadows/cloudHeight")) * Clouds::kUnitsPerMetre;

		ShadowConstants data{};
		data.cameraForward[0] = camera.forward[0];
		data.cameraForward[1] = camera.forward[1];
		data.cameraForward[2] = camera.forward[2];
		data.cameraRight[0] = camera.rightOverScaleX[0];
		data.cameraRight[1] = camera.rightOverScaleX[1];
		data.cameraRight[2] = camera.rightOverScaleX[2];
		data.cameraRight[3] = camera.near;
		data.cameraUp[0] = camera.upOverScaleY[0];
		data.cameraUp[1] = camera.upOverScaleY[1];
		data.cameraUp[2] = camera.upOverScaleY[2];
		data.cameraUp[3] = debugView;
		data.sunDirection[0] = towardsSun[0];
		data.sunDirection[1] = towardsSun[1];
		data.sunDirection[2] = towardsSun[2];
		data.sunDirection[3] = static_cast<float>(Settings::GetDouble("CloudShadows/opacity"));
		data.params[0] = cloudHeight;
		data.params[1] = Clouds::kEarthRadiusMetres * Clouds::kUnitsPerMetre;

		if (_draws % kLogInterval == 0) {
			REX::INFO(
				"clouds: {} cloud draw(s) seen, {} with layer constants, {} repeated into {} faces in {} frames, "
				"view constants {} ({} bytes, near {:.2f}), layer constants {} bytes, "
				"sun [{:.3f} {:.3f} {:.3f}], cloud height {:.0f}",
				_capturedDraws, _layersReady, _repeatedDraws, kCapturedFaces, kLogInterval,
				_imageReady ? "ready" : "pending", _constantBytes, NearFrom(_image), _geometryBytes,
				towardsSun[0], towardsSun[1], towardsSun[2], cloudHeight);
			_capturedDraws = 0;
			_layersReady = 0;
			_repeatedDraws = 0;
		}

		const Render::StateGuard guard;
		const Render::PassScope scope{ "CloudShadows/Shadow" };

		context->OMSetRenderTargets(0, nullptr, nullptr);
		static_cast<void>(_constants.Update(std::addressof(data), sizeof(data)));
		auto* const buffer = _constants.Buffer();

		const float blendFactor[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
		if (debugView > 0.0f) {
			context->OMSetRenderTargets(1, std::addressof(diffuse), nullptr);
			context->OMSetBlendState(_plain, blendFactor, 0xFFFFFFFFu);
		} else {
			REX::W32::ID3D11RenderTargetView* targets[2]{ diffuse, specular };
			context->OMSetRenderTargets(2, targets, nullptr);
			context->OMSetBlendState(_multiply, blendFactor, 0xFFFFFFFFu);
		}

		REX::W32::ID3D11ShaderResourceView* resources[2]{ _coverage.SRV(), depth };
		context->PSSetShaderResources(0, 2, resources);
		context->PSSetSamplers(0, 1, std::addressof(_linearClamp));
		context->PSSetConstantBuffers(1, 1, std::addressof(buffer));
		context->PSSetShader(_shader, nullptr, 0);

		Render::DrawFullscreen();
	}

	bool CloudShadows::Compile()
	{
		const auto source = Shader::LoadSource(ShaderRoot(), kShaderFile);
		if (!source) {
			const std::filesystem::path only[] = { ShaderRoot() / kShaderFile };
			_watch.Reset(only);
			REX::ERROR("CloudShadows: {}", source.error());
			return false;
		}
		_watch.Reset(source->files);

		const auto compiled = Shader::CompilePixelShader(source->text, "CloudShadows.hlsl", "main");
		if (!compiled.Succeeded()) {
			REX::ERROR("CloudShadows: {}", compiled.diagnostics);
			return false;
		}

		auto* const device = Render::GetDevice();
		REX::W32::ID3D11PixelShader* shader = nullptr;
		if (device == nullptr ||
			device->CreatePixelShader(compiled.bytecode.data(), compiled.bytecode.size(), nullptr, std::addressof(shader)) < 0) {
			REX::ERROR("CloudShadows: the shadow shader could not be created");
			return false;
		}

		if (_shader != nullptr) {
			_shader->Release();
		}
		_shader = shader;
		static_cast<void>(Render::SetDebugName(reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_shader), "FO4CS_PS_CloudShadows"));
		REX::INFO("CloudShadows: shader compiled");
		return true;
	}

	bool CloudShadows::EnsureStates()
	{
		auto* const device = Render::GetDevice();
		if (device == nullptr) {
			return false;
		}

		if (_multiply == nullptr) {
			// dest * src, the same state F2's Modulate uses.
			REX::W32::D3D11_BLEND_DESC desc{};
			auto& target = desc.renderTarget[0];
			target.blendEnable = true;
			target.srcBlend = REX::W32::D3D11_BLEND_ZERO;
			target.destBlend = REX::W32::D3D11_BLEND_SRC_COLOR;
			target.blendOp = REX::W32::D3D11_BLEND_OP_ADD;
			target.srcBlendAlpha = REX::W32::D3D11_BLEND_ZERO;
			target.destBlendAlpha = REX::W32::D3D11_BLEND_SRC_ALPHA;
			target.blendOpAlpha = REX::W32::D3D11_BLEND_OP_ADD;
			target.renderTargetWriteMask = static_cast<std::uint8_t>(REX::W32::D3D11_COLOR_WRITE_ENABLE_ALL);
			if (device->CreateBlendState(std::addressof(desc), std::addressof(_multiply)) < 0) {
				REX::ERROR("CloudShadows: the multiply blend state could not be created");
				return false;
			}
		}

		if (_plain == nullptr) {
			// Overwrite, for the debug views.
			REX::W32::D3D11_BLEND_DESC desc{};
			auto& target = desc.renderTarget[0];
			target.blendEnable = false;
			target.renderTargetWriteMask = static_cast<std::uint8_t>(REX::W32::D3D11_COLOR_WRITE_ENABLE_ALL);
			if (device->CreateBlendState(std::addressof(desc), std::addressof(_plain)) < 0) {
				REX::ERROR("CloudShadows: the plain blend state could not be created");
				return false;
			}
		}

		if (_alphaComposite == nullptr) {
			// Alpha only: a = src.a + dest.a * (1 - src.a). The colour never
			// reaches the target, which is what lets the engine's own cloud
			// shader write our coverage.
			REX::W32::D3D11_BLEND_DESC desc{};
			auto& target = desc.renderTarget[0];
			target.blendEnable = true;
			target.srcBlend = REX::W32::D3D11_BLEND_ZERO;
			target.destBlend = REX::W32::D3D11_BLEND_ONE;
			target.blendOp = REX::W32::D3D11_BLEND_OP_ADD;
			target.srcBlendAlpha = REX::W32::D3D11_BLEND_ONE;
			target.destBlendAlpha = REX::W32::D3D11_BLEND_INV_SRC_ALPHA;
			target.blendOpAlpha = REX::W32::D3D11_BLEND_OP_ADD;
			target.renderTargetWriteMask = static_cast<std::uint8_t>(REX::W32::D3D11_COLOR_WRITE_ENABLE_ALPHA);
			if (device->CreateBlendState(std::addressof(desc), std::addressof(_alphaComposite)) < 0) {
				REX::ERROR("CloudShadows: the alpha composite blend state could not be created");
				return false;
			}
		}

		if (_cullNone == nullptr) {
			REX::W32::D3D11_RASTERIZER_DESC desc{};
			desc.fillMode = REX::W32::D3D11_FILL_SOLID;
			desc.cullMode = REX::W32::D3D11_CULL_NONE;
			desc.depthClipEnable = true;
			if (device->CreateRasterizerState(std::addressof(desc), std::addressof(_cullNone)) < 0) {
				REX::ERROR("CloudShadows: the rasterizer state could not be created");
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
				REX::ERROR("CloudShadows: the sampler could not be created");
				return false;
			}
		}

		return true;
	}
}
