#include "Render/StateGuard.h"

#include "Render/Renderer.h"

#include <cstddef>
#include <memory>

namespace Render
{
	namespace
	{
		template <class T>
		void ReleaseAll(T** a_objects, std::size_t a_count) noexcept
		{
			for (std::size_t i = 0; i < a_count; ++i) {
				if (a_objects[i] != nullptr) {
					a_objects[i]->Release();
					a_objects[i] = nullptr;
				}
			}
		}

		template <class T>
		void ReleaseOne(T*& a_object) noexcept
		{
			if (a_object != nullptr) {
				a_object->Release();
				a_object = nullptr;
			}
		}
	}

	StateGuard::StateGuard() noexcept
	{
		_context = GetContext();
		if (_context == nullptr) {
			return;
		}

		_context->OMGetRenderTargets(kRenderTargets, _renderTargets, std::addressof(_depthStencil));
		_context->OMGetBlendState(
			std::addressof(_blendState), _blendFactor, std::addressof(_sampleMask));
		_context->RSGetViewports(std::addressof(_viewportCount), _viewports);
		_context->RSGetState(std::addressof(_rasterizer));
		_context->OMGetDepthStencilState(
			std::addressof(_depthStencilState), std::addressof(_stencilReference));
		_context->IAGetPrimitiveTopology(std::addressof(_topology));

		_context->VSGetShader(std::addressof(_vertexShader), nullptr, nullptr);
		_context->PSGetShader(std::addressof(_pixelShader), nullptr, nullptr);
		_context->CSGetShader(std::addressof(_computeShader), nullptr, nullptr);

		_context->PSGetShaderResources(0, kPixelResources, _psResources);
		_context->CSGetShaderResources(0, kComputeSlots, _csResources);
		_context->CSGetUnorderedAccessViews(0, kComputeSlots, _csUAVs);
		_context->CSGetSamplers(0, kComputeSlots, _csSamplers);

		// Slot 1 rather than 0: that is where the Bend raymarch expects its
		// PerFrame buffer, and it is the only one we overwrite.
		_context->CSGetConstantBuffers(1, kComputeSlots, _csConstantBuffers);
	}

	StateGuard::~StateGuard() noexcept
	{
		if (_context == nullptr) {
			return;
		}

		_context->OMSetRenderTargets(kRenderTargets, _renderTargets, _depthStencil);
		_context->OMSetBlendState(_blendState, _blendFactor, _sampleMask);
		_context->RSSetViewports(_viewportCount, _viewports);
		_context->RSSetState(_rasterizer);
		_context->OMSetDepthStencilState(_depthStencilState, _stencilReference);
		_context->IASetPrimitiveTopology(_topology);

		_context->VSSetShader(_vertexShader, nullptr, 0);
		_context->PSSetShader(_pixelShader, nullptr, 0);
		_context->CSSetShader(_computeShader, nullptr, 0);

		_context->PSSetShaderResources(0, kPixelResources, _psResources);
		_context->CSSetShaderResources(0, kComputeSlots, _csResources);

		// The counts argument only means anything for append and counter
		// buffers, which none of these are; keeping the initial values is what
		// a null here asks for.
		_context->CSSetUnorderedAccessViews(0, kComputeSlots, _csUAVs, nullptr);
		_context->CSSetSamplers(0, kComputeSlots, _csSamplers);
		_context->CSSetConstantBuffers(1, kComputeSlots, _csConstantBuffers);

		ReleaseAll(_renderTargets, kRenderTargets);
		ReleaseOne(_depthStencil);
		ReleaseOne(_blendState);
		ReleaseOne(_rasterizer);
		ReleaseOne(_depthStencilState);
		ReleaseOne(_vertexShader);
		ReleaseOne(_pixelShader);
		ReleaseOne(_computeShader);

		ReleaseAll(_psResources, kPixelResources);
		ReleaseAll(_csResources, kComputeSlots);
		ReleaseAll(_csUAVs, kComputeSlots);
		ReleaseAll(_csSamplers, kComputeSlots);
		ReleaseAll(_csConstantBuffers, kComputeSlots);
	}
}
