#pragma once

#include "Render/Extent.h"
#include "Render/Resources.h"
#include "Util/FileWatch.h"

#include <REX/W32/D3D11.h>

namespace Upscale
{
	/// The compute passes around DLSS and the reflections shader it swaps in:
	/// dilating the motion vectors, copying the render rectangle of the scene
	/// out (NGX takes input and output as separate resources), sharpening,
	/// and the depth at display size for the passes after DLSS. Every method
	/// unbinds what it bound.
	class Passes
	{
	public:
		Passes() = default;
		~Passes();

		Passes(const Passes&) = delete;
		Passes& operator=(const Passes&) = delete;

		/// All four shaders; swaps them in only when every one compiled.
		[[nodiscard]] bool Compile() noexcept;
		/// True when a watched file changed and the shaders were recompiled.
		[[nodiscard]] bool PollReload() noexcept;

		[[nodiscard]] bool EnsureTextures(Render::Extent a_render, Render::Extent a_display) noexcept;

		void DilateMotionVectors(
			REX::W32::ID3D11DeviceContext& a_context,
			REX::W32::ID3D11ShaderResourceView* a_motion,
			REX::W32::ID3D11ShaderResourceView* a_depth,
			Render::Extent a_render,
			Render::Extent a_display) noexcept;
		void CopyColor(REX::W32::ID3D11DeviceContext& a_context, REX::W32::ID3D11Resource* a_scene, Render::Extent a_render) noexcept;
		void Sharpen(
			REX::W32::ID3D11DeviceContext& a_context,
			REX::W32::ID3D11Resource* a_scene,
			REX::W32::ID3D11UnorderedAccessView* a_sceneUAV,
			float a_sharpness,
			Render::Extent a_display) noexcept;
		void FullDepth(
			REX::W32::ID3D11DeviceContext& a_context,
			REX::W32::ID3D11ShaderResourceView* a_depth,
			REX::W32::ID3D11UnorderedAccessView* a_linearMip0,
			Render::Extent a_render,
			Render::Extent a_display) noexcept;

		[[nodiscard]] REX::W32::ID3D11PixelShader* Reflections() const noexcept { return _reflections; }
		[[nodiscard]] REX::W32::ID3D11Resource* ColorInput() const noexcept;
		[[nodiscard]] REX::W32::ID3D11Resource* MotionVectors() const noexcept;
		[[nodiscard]] REX::W32::ID3D11ShaderResourceView* FullDepthSRV() const noexcept { return _depth.SRV(); }

		/// Idempotent.
		void Release() noexcept;

	private:
		void Bind(REX::W32::ID3D11DeviceContext& a_context, Render::Extent a_render, Render::Extent a_display, float a_extra) noexcept;
		static void Unbind(REX::W32::ID3D11DeviceContext& a_context) noexcept;

		REX::W32::ID3D11ComputeShader* _dilate{ nullptr };
		REX::W32::ID3D11ComputeShader* _fullDepth{ nullptr };
		REX::W32::ID3D11ComputeShader* _sharpen{ nullptr };
		REX::W32::ID3D11PixelShader* _reflections{ nullptr };
		Render::Texture _color;
		Render::Texture _motion;
		Render::Texture _depth;
		Render::Texture _sharpenSource;
		Render::ConstantBuffer _constants;
		Util::FileWatch _watch;
	};
}
