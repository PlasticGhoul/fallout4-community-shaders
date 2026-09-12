#include "Render/FullscreenPass.h"

#include "Render/DebugName.h"
#include "Render/Renderer.h"
#include "Shader/ShaderCompiler.h"
#include "Shader/ShaderSource.h"
#include "Util/GamePaths.h"

#include <memory>

namespace Render
{
	namespace
	{
		constexpr auto kShaderFile = "Fullscreen.hlsl";

		REX::W32::ID3D11VertexShader* g_vertexShader = nullptr;
		REX::W32::ID3D11RasterizerState* g_rasterizer = nullptr;
		REX::W32::ID3D11DepthStencilState* g_depthStencil = nullptr;
		bool g_refused = false;

		void ReleaseStates() noexcept
		{
			if (g_rasterizer != nullptr) {
				g_rasterizer->Release();
				g_rasterizer = nullptr;
			}
			if (g_depthStencil != nullptr) {
				g_depthStencil->Release();
				g_depthStencil = nullptr;
			}
		}

		/// Solid fill, no culling, no scissor. Culling is the one that matters:
		/// the triangle is wound one way, and a state that culls the other is
		/// what kDFLight binds for the back halves of its light volumes.
		bool CreateRasterizer(REX::W32::ID3D11Device& a_device) noexcept
		{
			REX::W32::D3D11_RASTERIZER_DESC desc{};
			desc.fillMode = REX::W32::D3D11_FILL_SOLID;
			desc.cullMode = REX::W32::D3D11_CULL_NONE;
			desc.frontCounterClockwise = false;
			desc.depthBias = 0;
			desc.depthBiasClamp = 0.0f;
			desc.slopeScaledDepthBias = 0.0f;
			desc.depthClipEnable = true;
			desc.scissorEnable = false;
			desc.multisampleEnable = false;
			desc.antialiasedLineEnable = false;

			return a_device.CreateRasterizerState(
					   std::addressof(desc), std::addressof(g_rasterizer)) >= 0;
		}

		/// No depth test, no depth write, no stencil. A full-screen pass has
		/// nothing to test against - it binds no depth view - but D3D11 keeps
		/// a stencil test running against a bound view even so, and the
		/// engine's may well be bound when the caller has not unbound it.
		bool CreateDepthStencil(REX::W32::ID3D11Device& a_device) noexcept
		{
			REX::W32::D3D11_DEPTH_STENCIL_DESC desc{};
			desc.depthEnable = false;
			desc.depthWriteMask = REX::W32::D3D11_DEPTH_WRITE_MASK_ZERO;
			desc.depthFunc = REX::W32::D3D11_COMPARISON_ALWAYS;
			desc.stencilEnable = false;
			desc.stencilReadMask = 0;
			desc.stencilWriteMask = 0;

			for (auto* face : { std::addressof(desc.frontFace), std::addressof(desc.backFace) }) {
				face->stencilFailOp = REX::W32::D3D11_STENCIL_OP_KEEP;
				face->stencilDepthFailOp = REX::W32::D3D11_STENCIL_OP_KEEP;
				face->stencilPassOp = REX::W32::D3D11_STENCIL_OP_KEEP;
				face->stencilFunc = REX::W32::D3D11_COMPARISON_ALWAYS;
			}

			return a_device.CreateDepthStencilState(
					   std::addressof(desc), std::addressof(g_depthStencil)) >= 0;
		}
	}

	bool InitFullscreenPass() noexcept
	{
		if (g_vertexShader != nullptr) {
			return true;
		}
		if (g_refused) {
			return false;
		}

		const auto root = Util::DataDirectory() / "Shaders" / "FO4";
		const auto source = Shader::LoadSource(root, kShaderFile);
		if (!source) {
			REX::ERROR("fullscreen pass: {}", source.error());
			g_refused = true;
			return false;
		}

		const auto compiled = Shader::CompileVertexShader(source->text, kShaderFile, "main");
		if (!compiled.Succeeded()) {
			REX::ERROR("fullscreen pass: {}", compiled.diagnostics);
			g_refused = true;
			return false;
		}

		auto* const device = GetDevice();
		if (device == nullptr) {
			REX::ERROR("fullscreen pass: no device");
			g_refused = true;
			return false;
		}

		if (device->CreateVertexShader(
				compiled.bytecode.data(),
				compiled.bytecode.size(),
				nullptr,
				std::addressof(g_vertexShader)) < 0) {
			REX::ERROR("fullscreen pass: the vertex shader could not be created");
			g_refused = true;
			return false;
		}

		static_cast<void>(SetDebugName(
			reinterpret_cast<REX::W32::ID3D11DeviceChild*>(g_vertexShader),
			"FO4CS_VS_Fullscreen"));

		if (!CreateRasterizer(*device) || !CreateDepthStencil(*device)) {
			REX::ERROR("fullscreen pass: the rasteriser or depth-stencil state could not be created");
			ReleaseStates();
			g_vertexShader->Release();
			g_vertexShader = nullptr;
			g_refused = true;
			return false;
		}

		static_cast<void>(SetDebugName(
			reinterpret_cast<REX::W32::ID3D11DeviceChild*>(g_rasterizer),
			"FO4CS_RS_Fullscreen"));
		static_cast<void>(SetDebugName(
			reinterpret_cast<REX::W32::ID3D11DeviceChild*>(g_depthStencil),
			"FO4CS_DSS_Fullscreen"));

		REX::INFO("fullscreen pass ready");
		return true;
	}

	void ReleaseFullscreenPass() noexcept
	{
		if (g_vertexShader != nullptr) {
			g_vertexShader->Release();
			g_vertexShader = nullptr;
		}

		ReleaseStates();
		g_refused = false;
	}

	void DrawFullscreen() noexcept
	{
		auto* const context = GetContext();
		if (context == nullptr || g_vertexShader == nullptr) {
			return;
		}

		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(REX::W32::D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->RSSetState(g_rasterizer);
		context->OMSetDepthStencilState(g_depthStencil, 0);
		context->VSSetShader(g_vertexShader, nullptr, 0);
		context->Draw(3, 0);
	}
}
