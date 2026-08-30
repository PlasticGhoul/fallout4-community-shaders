#include "Render/TargetInventory.h"

#include "Render/DebugName.h"
#include "Render/FormatNames.h"

#include <RE/B/BSGraphics.h>

#include <format>

namespace Render
{
	namespace
	{
		// Counts failures so they can be reported once at the end. With well
		// over a hundred objects, a message per failure would bury everything
		// else in the log.
		struct NamingTally
		{
			std::uint32_t attempted{ 0 };
			std::uint32_t failed{ 0 };

			void Apply(REX::W32::ID3D11DeviceChild* a_object, const std::string& a_name) noexcept
			{
				if (a_object == nullptr) {
					return;
				}
				++attempted;
				if (!SetDebugName(a_object, a_name)) {
					++failed;
				}
			}
		};

		// Logs one line describing a texture.
		void LogTexture(
			std::string_view a_label,
			REX::W32::ID3D11Texture2D* a_texture,
			std::uint32_t a_engineFormat)
		{
			REX::W32::D3D11_TEXTURE2D_DESC desc{};
			a_texture->GetDesc(std::addressof(desc));

			REX::INFO("{:<16} {:>5}x{:<5} {:<22} mips {:<2} samples {:<2} bind {:<40} engineFormat {}",
				a_label,
				desc.width,
				desc.height,
				FormatName(desc.format),
				desc.mipLevels,
				desc.sampleDesc.count,
				BindFlagsString(desc.bindFlags),
				a_engineFormat);
		}

		void InventoryRenderTargets(
			RE::BSGraphics::RendererData* a_data,
			RE::BSGraphics::RenderTargetManager* a_manager,
			NamingTally& a_tally)
		{
			REX::INFO("--- render targets ---");
			std::uint32_t occupied = 0;

			for (std::size_t i = 0; i < std::size(a_data->renderTargets); ++i) {
				auto& target = a_data->renderTargets[i];
				if (target.texture == nullptr) {
					continue;  // Most entries are empty; reporting them would be noise.
				}
				++occupied;

				const auto label = std::format("FO4_RT_{:03}", i);

				// Read as raw memory: BSGraphics::Format is only forward-declared
				// in commonlibf4, so it is an incomplete type and cannot be cast.
				// Side by side with the real DXGI format this yields the mapping
				// between the engine's enum and DXGI's.
				std::uint32_t engineFormat = 0;
				if (a_manager != nullptr && i < std::size(a_manager->renderTargetData)) {
					engineFormat = *reinterpret_cast<const std::uint32_t*>(
						std::addressof(a_manager->renderTargetData[i].format));
				}

				LogTexture(label, target.texture, engineFormat);

				a_tally.Apply(target.texture, label);
				a_tally.Apply(target.copyTexture, label + "_COPY");
				a_tally.Apply(target.rtView, label + "_RTV");
				a_tally.Apply(target.srView, label + "_SRV");
				a_tally.Apply(target.copySRView, label + "_COPY_SRV");
				a_tally.Apply(target.uaView, label + "_UAV");
			}

			REX::INFO("{} of {} render targets occupied", occupied, std::size(a_data->renderTargets));
		}

		void InventoryDepthStencils(RE::BSGraphics::RendererData* a_data, NamingTally& a_tally)
		{
			REX::INFO("--- depth stencil targets ---");
			std::uint32_t occupied = 0;

			for (std::size_t i = 0; i < std::size(a_data->depthStencilTargets); ++i) {
				auto& target = a_data->depthStencilTargets[i];
				if (target.texture == nullptr) {
					continue;
				}
				++occupied;

				const auto label = std::format("FO4_DS_{:03}", i);
				LogTexture(label, target.texture, 0);

				a_tally.Apply(target.texture, label);
				a_tally.Apply(target.srViewDepth, label + "_SRV_DEPTH");
				a_tally.Apply(target.srViewStencil, label + "_SRV_STENCIL");
				for (std::size_t v = 0; v < std::size(target.dsView); ++v) {
					a_tally.Apply(target.dsView[v], std::format("{}_DSV{}", label, v));
				}
				// The read-only DSV variants address the same texture with
				// different flags. The texture already carries a name, which is
				// what a capture tool shows, so they are left alone.
			}

			REX::INFO("{} of {} depth stencil targets occupied",
				occupied,
				std::size(a_data->depthStencilTargets));
		}

		void InventoryCubeMaps(RE::BSGraphics::RendererData* a_data, NamingTally& a_tally)
		{
			REX::INFO("--- cubemap render targets ---");
			std::uint32_t occupied = 0;

			for (std::size_t i = 0; i < std::size(a_data->cubeMapRenderTargets); ++i) {
				auto& target = a_data->cubeMapRenderTargets[i];
				if (target.texture == nullptr) {
					continue;
				}
				++occupied;

				const auto label = std::format("FO4_CUBE_{:03}", i);
				LogTexture(label, target.texture, 0);

				a_tally.Apply(target.texture, label);
				a_tally.Apply(target.srView, label + "_SRV");
				for (std::size_t v = 0; v < std::size(target.rtView); ++v) {
					a_tally.Apply(target.rtView[v], std::format("{}_RTV{}", label, v));
				}
			}

			REX::INFO("{} of {} cubemap targets occupied",
				occupied,
				std::size(a_data->cubeMapRenderTargets));
		}
	}

	void RunTargetInventory() noexcept
	{
		auto* const data = RE::BSGraphics::GetRendererData();
		if (data == nullptr) {
			REX::ERROR("renderer data unavailable, skipping the target inventory");
			return;
		}

		auto* const manager = RE::BSGraphics::RenderTargetManager::GetSingleton();
		if (manager == nullptr) {
			REX::WARN("render target manager unavailable; engine formats will read as 0");
		}

		REX::INFO("=== render target inventory ===");
		REX::INFO("array sizes: renderer {}/{}/{}, manager {}/{}/{}",
			std::size(data->renderTargets),
			std::size(data->depthStencilTargets),
			std::size(data->cubeMapRenderTargets),
			manager != nullptr ? std::size(manager->renderTargetData) : 0u,
			manager != nullptr ? std::size(manager->depthStencilTargetData) : 0u,
			manager != nullptr ? std::size(manager->cubeMapRenderTargetData) : 0u);

		NamingTally tally;
		InventoryRenderTargets(data, manager, tally);
		InventoryDepthStencils(data, tally);
		InventoryCubeMaps(data, tally);

		if (tally.failed != 0) {
			REX::WARN("named {} of {} objects; {} calls were rejected",
				tally.attempted - tally.failed,
				tally.attempted,
				tally.failed);
		} else {
			REX::INFO("named {} objects", tally.attempted);
		}

		REX::INFO("=== end of inventory ===");
	}
}
