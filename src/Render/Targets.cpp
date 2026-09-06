#include "Render/Targets.h"

#include "Render/DebugName.h"

#include <RE/B/BSGraphics.h>

#include <string>

namespace Render::Targets
{
	namespace
	{
		const RE::BSGraphics::RenderTarget* ColourSlot(std::size_t a_slot) noexcept
		{
			const auto* const data = RE::BSGraphics::GetRendererData();
			if (data == nullptr || a_slot >= std::size(data->renderTargets)) {
				return nullptr;
			}

			const auto& target = data->renderTargets[a_slot];
			return target.texture != nullptr ? std::addressof(target) : nullptr;
		}

		std::string NameOf(REX::W32::ID3D11Texture2D* a_texture) noexcept
		{
			if (a_texture == nullptr) {
				return "<empty>";
			}

			auto name = GetDebugName(reinterpret_cast<REX::W32::ID3D11DeviceChild*>(a_texture));
			return name.empty() ? std::string{ "<unnamed>" } : name;
		}
	}

	REX::W32::ID3D11RenderTargetView* RenderTargetView(std::size_t a_slot) noexcept
	{
		const auto* const target = ColourSlot(a_slot);
		return target != nullptr ? target->rtView : nullptr;
	}

	REX::W32::ID3D11ShaderResourceView* RenderTargetSRV(std::size_t a_slot) noexcept
	{
		const auto* const target = ColourSlot(a_slot);
		return target != nullptr ? target->srView : nullptr;
	}

	REX::W32::ID3D11Texture2D* RenderTargetTexture(std::size_t a_slot) noexcept
	{
		const auto* const target = ColourSlot(a_slot);
		return target != nullptr ? target->texture : nullptr;
	}

	REX::W32::ID3D11ShaderResourceView* DepthSRV(std::size_t a_slot) noexcept
	{
		const auto* const data = RE::BSGraphics::GetRendererData();
		if (data == nullptr || a_slot >= std::size(data->depthStencilTargets)) {
			return nullptr;
		}

		return data->depthStencilTargets[a_slot].srViewDepth;
	}

	void LogSlots() noexcept
	{
		const auto* const data = RE::BSGraphics::GetRendererData();
		if (data == nullptr) {
			REX::ERROR("targets: no renderer data, nothing to report");
			return;
		}

		REX::INFO(
			"target slots: diffuse {} is {}, specular {} is {}, normals {} is {}",
			kLightDiffuse,
			NameOf(RenderTargetTexture(kLightDiffuse)),
			kLightSpecular,
			NameOf(RenderTargetTexture(kLightSpecular)),
			kGBufferNormal,
			NameOf(RenderTargetTexture(kGBufferNormal)));

		// The depth has no texture pointer of its own here, so it is named
		// through the view instead - GetViewTargetName follows a view to the
		// resource the inventory labelled.
		auto* const depth = DepthSRV(kSceneDepth);
		REX::INFO(
			"scene depth slot {} is {}",
			kSceneDepth,
			depth != nullptr ?
				GetViewTargetName(reinterpret_cast<REX::W32::ID3D11View*>(depth)) :
				std::string{ "<empty>" });
	}
}
