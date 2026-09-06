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
		bool g_refused = false;
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

		REX::INFO("fullscreen pass ready");
		return true;
	}

	void ReleaseFullscreenPass() noexcept
	{
		if (g_vertexShader != nullptr) {
			g_vertexShader->Release();
			g_vertexShader = nullptr;
		}

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
		context->VSSetShader(g_vertexShader, nullptr, 0);
		context->Draw(3, 0);
	}
}
