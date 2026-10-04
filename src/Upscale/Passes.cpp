#include "Upscale/Passes.h"

#include "Render/DebugName.h"
#include "Render/Renderer.h"
#include "Shader/ShaderCompiler.h"
#include "Shader/ShaderSource.h"
#include "Util/GamePaths.h"

#include <cmath>
#include <filesystem>
#include <vector>

namespace Upscale
{
	namespace
	{
		/// Field for field the cbuffer Upscaling of the three compute shaders.
		struct Constants
		{
			std::uint32_t screenSize[2];
			std::uint32_t renderSize[2];
			float cameraData[4];
		};
		static_assert(sizeof(Constants) == 32);

		/// The engine's camera planes; doodlum reads the same two floats.
		constexpr std::uint64_t kCameraNear = 2712882;
		constexpr std::uint64_t kCameraFar = 2712883;

		std::filesystem::path ShaderRoot()
		{
			return Util::DataDirectory() / "Shaders" / "FO4";
		}

		template <class T>
		void ReleaseInto(T*& a_object) noexcept
		{
			if (a_object != nullptr) {
				a_object->Release();
				a_object = nullptr;
			}
		}

		std::uint32_t Groups(std::uint32_t a_pixels) noexcept
		{
			return (a_pixels + 7) / 8;
		}
	}

	Passes::~Passes()
	{
		Release();
	}

	bool Passes::Compile() noexcept
	{
		struct Source
		{
			const char* file;
			bool pixel;
		};
		constexpr Source kSources[]{
			{ "DLSS/DilateMotionVectors.hlsl", false },
			{ "DLSS/FullDepth.hlsl", false },
			{ "DLSS/RCAS.hlsl", false },
			{ "DLSS/SSLRRaytracing.hlsl", true },
		};

		auto* const device = Render::GetDevice();
		if (device == nullptr) {
			return false;
		}

		std::vector<std::filesystem::path> watched;
		REX::W32::ID3D11ComputeShader* compute[3]{};
		REX::W32::ID3D11PixelShader* pixel = nullptr;
		bool ok = true;
		std::size_t computeIndex = 0;
		for (const auto& source : kSources) {
			const auto text = Shader::LoadSource(ShaderRoot(), source.file);
			if (!text) {
				watched.push_back(ShaderRoot() / source.file);
				REX::ERROR("DLSS: {}", text.error());
				ok = false;
				continue;
			}
			watched.insert(watched.end(), text->files.begin(), text->files.end());
			const auto result = source.pixel ?
			                        Shader::CompilePixelShader(text->text, source.file, "main") :
			                        Shader::CompileComputeShader(text->text, source.file, "main");
			if (!result.Succeeded()) {
				REX::ERROR("DLSS: {}", result.diagnostics);
				ok = false;
				continue;
			}
			if (source.pixel) {
				ok = ok && device->CreatePixelShader(result.bytecode.data(), result.bytecode.size(), nullptr, std::addressof(pixel)) >= 0;
			} else {
				ok = ok && device->CreateComputeShader(result.bytecode.data(), result.bytecode.size(), nullptr, std::addressof(compute[computeIndex])) >= 0;
				++computeIndex;
			}
		}
		_watch.Reset(watched);

		if (!ok) {
			for (auto*& shader : compute) {
				ReleaseInto(shader);
			}
			ReleaseInto(pixel);
			return false;
		}

		ReleaseInto(_dilate);
		ReleaseInto(_fullDepth);
		ReleaseInto(_sharpen);
		ReleaseInto(_reflections);
		_dilate = compute[0];
		_fullDepth = compute[1];
		_sharpen = compute[2];
		_reflections = pixel;
		static_cast<void>(Render::SetDebugName(reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_dilate), "FO4CS_CS_DLSSDilate"));
		static_cast<void>(Render::SetDebugName(reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_fullDepth), "FO4CS_CS_DLSSFullDepth"));
		static_cast<void>(Render::SetDebugName(reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_sharpen), "FO4CS_CS_DLSSSharpen"));
		static_cast<void>(Render::SetDebugName(reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_reflections), "FO4CS_PS_DLSSReflections"));

		if (!_constants.Create(sizeof(Constants), "FO4CS_CB_DLSS")) {
			return false;
		}
		REX::INFO("DLSS: shaders compiled");
		return true;
	}

	bool Passes::PollReload() noexcept
	{
		if (!_watch.Poll()) {
			return false;
		}
		REX::INFO("DLSS: shaders changed, recompiling");
		return Compile();
	}

	bool Passes::EnsureTextures(Render::Extent a_render, Render::Extent a_display) noexcept
	{
		const auto matches = [](const Render::Texture& a_texture, Render::Extent a_size) {
			return a_texture.Valid() && a_texture.Width() == a_size.width && a_texture.Height() == a_size.height;
		};
		if (!matches(_color, a_render) &&
			!_color.Create(a_render.width, a_render.height, REX::W32::DXGI_FORMAT_R11G11B10_FLOAT, "FO4CS_TEX_DLSSColor")) {
			return false;
		}
		if (!matches(_motion, a_render) &&
			!_motion.Create(a_render.width, a_render.height, REX::W32::DXGI_FORMAT_R16G16_FLOAT, "FO4CS_TEX_DLSSMotion")) {
			return false;
		}
		if (!matches(_depth, a_display) &&
			!_depth.Create(a_display.width, a_display.height, REX::W32::DXGI_FORMAT_R32_FLOAT, "FO4CS_TEX_DLSSDepth")) {
			return false;
		}
		if (!matches(_sharpenSource, a_display) &&
			!_sharpenSource.Create(a_display.width, a_display.height, REX::W32::DXGI_FORMAT_R11G11B10_FLOAT, "FO4CS_TEX_DLSSSharpen")) {
			return false;
		}
		return true;
	}

	void Passes::Bind(REX::W32::ID3D11DeviceContext& a_context, Render::Extent a_render, Render::Extent a_display, float a_extra) noexcept
	{
		const auto nearPlane = *reinterpret_cast<const float*>(REL::ID(kCameraNear).address());
		const auto farPlane = *reinterpret_cast<const float*>(REL::ID(kCameraFar).address());
		Constants data{};
		data.screenSize[0] = a_display.width;
		data.screenSize[1] = a_display.height;
		data.renderSize[0] = a_render.width;
		data.renderSize[1] = a_render.height;
		data.cameraData[0] = farPlane;
		data.cameraData[1] = nearPlane;
		data.cameraData[2] = farPlane - nearPlane;
		data.cameraData[3] = farPlane * nearPlane;
		if (a_extra >= 0.0f) {
			// RCAS reads its sharpness where the others read the far plane.
			data.cameraData[0] = a_extra;
		}
		static_cast<void>(_constants.Update(std::addressof(data), sizeof(data)));
		auto* const buffer = _constants.Buffer();
		a_context.CSSetConstantBuffers(0, 1, std::addressof(buffer));
	}

	void Passes::Unbind(REX::W32::ID3D11DeviceContext& a_context) noexcept
	{
		REX::W32::ID3D11ShaderResourceView* noViews[2]{ nullptr, nullptr };
		REX::W32::ID3D11UnorderedAccessView* noUAVs[2]{ nullptr, nullptr };
		REX::W32::ID3D11Buffer* noBuffer = nullptr;
		a_context.CSSetShaderResources(0, 2, noViews);
		a_context.CSSetUnorderedAccessViews(0, 2, noUAVs, nullptr);
		a_context.CSSetConstantBuffers(0, 1, std::addressof(noBuffer));
		a_context.CSSetShader(nullptr, nullptr, 0);
	}

	void Passes::DilateMotionVectors(
		REX::W32::ID3D11DeviceContext& a_context,
		REX::W32::ID3D11ShaderResourceView* a_motion,
		REX::W32::ID3D11ShaderResourceView* a_depth,
		Render::Extent a_render,
		Render::Extent a_display) noexcept
	{
		if (_dilate == nullptr || !_motion.Valid()) {
			return;
		}
		Bind(a_context, a_render, a_display, -1.0f);
		REX::W32::ID3D11ShaderResourceView* views[2]{ a_motion, a_depth };
		auto* const uav = _motion.UAV();
		a_context.CSSetShaderResources(0, 2, views);
		a_context.CSSetUnorderedAccessViews(0, 1, std::addressof(uav), nullptr);
		a_context.CSSetShader(_dilate, nullptr, 0);
		a_context.Dispatch(Groups(a_render.width), Groups(a_render.height), 1);
		Unbind(a_context);
	}

	void Passes::CopyColor(REX::W32::ID3D11DeviceContext& a_context, REX::W32::ID3D11Resource* a_scene, Render::Extent a_render) noexcept
	{
		if (!_color.Valid() || a_scene == nullptr) {
			return;
		}
		// A colour target: a boxed copy is fine here, unlike from a depth
		// stencil resource (F5).
		const REX::W32::D3D11_BOX box{ 0, 0, 0, a_render.width, a_render.height, 1 };
		a_context.CopySubresourceRegion(
			reinterpret_cast<REX::W32::ID3D11Resource*>(_color.Resource()), 0, 0, 0, 0, a_scene, 0, std::addressof(box));
	}

	void Passes::Sharpen(
		REX::W32::ID3D11DeviceContext& a_context,
		REX::W32::ID3D11Resource* a_scene,
		REX::W32::ID3D11UnorderedAccessView* a_sceneUAV,
		float a_sharpness,
		Render::Extent a_display) noexcept
	{
		if (_sharpen == nullptr || !_sharpenSource.Valid() || a_scene == nullptr || a_sceneUAV == nullptr) {
			return;
		}
		a_context.CopyResource(reinterpret_cast<REX::W32::ID3D11Resource*>(_sharpenSource.Resource()), a_scene);
		Bind(a_context, a_display, a_display, a_sharpness);
		auto* const view = _sharpenSource.SRV();
		a_context.CSSetShaderResources(0, 1, std::addressof(view));
		a_context.CSSetUnorderedAccessViews(0, 1, std::addressof(a_sceneUAV), nullptr);
		a_context.CSSetShader(_sharpen, nullptr, 0);
		a_context.Dispatch(Groups(a_display.width), Groups(a_display.height), 1);
		Unbind(a_context);
	}

	void Passes::FullDepth(
		REX::W32::ID3D11DeviceContext& a_context,
		REX::W32::ID3D11ShaderResourceView* a_depth,
		REX::W32::ID3D11UnorderedAccessView* a_linearMip0,
		Render::Extent a_render,
		Render::Extent a_display) noexcept
	{
		if (_fullDepth == nullptr || !_depth.Valid() || a_depth == nullptr) {
			return;
		}
		Bind(a_context, a_render, a_display, -1.0f);
		REX::W32::ID3D11UnorderedAccessView* uavs[2]{ _depth.UAV(), a_linearMip0 };
		a_context.CSSetShaderResources(0, 1, std::addressof(a_depth));
		a_context.CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
		a_context.CSSetShader(_fullDepth, nullptr, 0);
		a_context.Dispatch(Groups(a_display.width), Groups(a_display.height), 1);
		Unbind(a_context);
	}

	REX::W32::ID3D11Resource* Passes::ColorInput() const noexcept
	{
		return reinterpret_cast<REX::W32::ID3D11Resource*>(_color.Resource());
	}

	REX::W32::ID3D11Resource* Passes::MotionVectors() const noexcept
	{
		return reinterpret_cast<REX::W32::ID3D11Resource*>(_motion.Resource());
	}

	void Passes::Release() noexcept
	{
		ReleaseInto(_dilate);
		ReleaseInto(_fullDepth);
		ReleaseInto(_sharpen);
		ReleaseInto(_reflections);
		_color.Release();
		_motion.Release();
		_depth.Release();
		_sharpenSource.Release();
		_constants.Release();
	}
}
