#pragma once

#include <REX/W32/D3D11.h>

#include <cstdint>

namespace Render
{
	/// Saves the pipeline state our own passes touch, and puts it back.
	///
	/// What is saved is fixed rather than selectable. A guard that can be asked
	/// for less can be asked for too little, and that failure surfaces as a
	/// corrupted frame several passes further on, where nothing points back
	/// here.
	///
	/// Everything a D3D11 Get call hands back arrives with a reference taken.
	/// The destructor releases every one of them, which makes this the one
	/// place in the project that holds COM references: the rule from B1 not to
	/// reference or release engine objects covers pointers we fetch for
	/// ourselves, not ones the runtime hands over with a reference already
	/// added.
	class StateGuard
	{
	public:
		StateGuard() noexcept;
		~StateGuard() noexcept;

		StateGuard(const StateGuard&) = delete;
		StateGuard& operator=(const StateGuard&) = delete;

	private:
		/// D3D11 allows eight simultaneous render targets and sixteen
		/// viewports; both arrays are sized to the maximum so that nothing the
		/// engine had can fall off the end of them.
		static constexpr std::uint32_t kRenderTargets = 8;
		static constexpr std::uint32_t kViewports = 16;

		/// The slots our passes bind into, and no more: slot 0 of the pixel
		/// stage for the mask, and the compute stage's first slot of each kind
		/// for the raymarch.
		static constexpr std::uint32_t kPixelResources = 1;
		static constexpr std::uint32_t kComputeSlots = 1;

		REX::W32::ID3D11DeviceContext* _context{ nullptr };

		REX::W32::ID3D11RenderTargetView* _renderTargets[kRenderTargets]{};
		REX::W32::ID3D11DepthStencilView* _depthStencil{ nullptr };

		REX::W32::ID3D11BlendState* _blendState{ nullptr };
		float _blendFactor[4]{};
		std::uint32_t _sampleMask{ 0 };

		/// Rasteriser and depth-stencil state, because the full-screen draw
		/// sets its own. Left unsaved, whatever kDFLight had bound - a light
		/// volume's front-face culling, a stencil test - would decide whether
		/// our triangle arrives at all, and the composite would inherit ours.
		REX::W32::ID3D11RasterizerState* _rasterizer{ nullptr };
		REX::W32::ID3D11DepthStencilState* _depthStencilState{ nullptr };
		std::uint32_t _stencilReference{ 0 };

		REX::W32::D3D11_VIEWPORT _viewports[kViewports]{};
		std::uint32_t _viewportCount{ kViewports };

		REX::W32::D3D11_PRIMITIVE_TOPOLOGY _topology{};

		REX::W32::ID3D11VertexShader* _vertexShader{ nullptr };
		REX::W32::ID3D11PixelShader* _pixelShader{ nullptr };
		REX::W32::ID3D11ComputeShader* _computeShader{ nullptr };

		REX::W32::ID3D11ShaderResourceView* _psResources[kPixelResources]{};
		REX::W32::ID3D11ShaderResourceView* _csResources[kComputeSlots]{};
		REX::W32::ID3D11UnorderedAccessView* _csUAVs[kComputeSlots]{};
		REX::W32::ID3D11SamplerState* _csSamplers[kComputeSlots]{};
		REX::W32::ID3D11Buffer* _csConstantBuffers[kComputeSlots]{};
	};
}
