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

		// What the engine's own bookkeeping says about a render target, if
		// anything. The first inventory run showed that indexing the manager
		// with the renderer's index does not line the two up, so both the
		// direct row and a row found by reverse lookup are reported until it
		// is clear which - if either - is the right one.
		struct EngineRow
		{
			std::uint32_t idAtIndex{ 0 };       // renderTargetID[i]
			std::uint32_t formatAtIndex{ 0 };   // renderTargetData[i].format
			std::int32_t mappedRow{ -1 };       // j where renderTargetID[j] == i
			std::uint32_t formatAtMapped{ 0 };  // renderTargetData[j].format
		};

		// Logs one line describing a texture.
		void LogTexture(
			std::string_view a_label,
			REX::W32::ID3D11Texture2D* a_texture,
			const EngineRow& a_engine)
		{
			REX::W32::D3D11_TEXTURE2D_DESC desc{};
			a_texture->GetDesc(std::addressof(desc));

			REX::INFO(
				"{:<16} {:>5}x{:<5} {:<22} mips {:<2} samples {:<2} bind {:<40} "
				"id {:<4} fmt@i {:<3} row {:<4} fmt@row {}",
				a_label,
				desc.width,
				desc.height,
				FormatName(desc.format),
				desc.mipLevels,
				desc.sampleDesc.count,
				BindFlagsString(desc.bindFlags),
				a_engine.idAtIndex,
				a_engine.formatAtIndex,
				a_engine.mappedRow,
				a_engine.formatAtMapped);
		}

		// BSGraphics::Format is declared without enumerators, so the value is
		// read as a plain integer.
		std::uint32_t RawFormat(const RE::BSGraphics::RenderTargetProperties& a_props) noexcept
		{
			return *reinterpret_cast<const std::uint32_t*>(std::addressof(a_props.format));
		}

		EngineRow ReadEngineRow(RE::BSGraphics::RenderTargetManager* a_manager, std::size_t a_index)
		{
			EngineRow row;
			if (a_manager == nullptr) {
				return row;
			}

			if (a_index < std::size(a_manager->renderTargetData)) {
				row.idAtIndex = a_manager->renderTargetID[a_index];
				row.formatAtIndex = RawFormat(a_manager->renderTargetData[a_index]);
			}

			// The mapping may run the other way: an entry whose id names this
			// renderer index.
			for (std::size_t j = 0; j < std::size(a_manager->renderTargetID); ++j) {
				if (a_manager->renderTargetID[j] == a_index) {
					row.mappedRow = static_cast<std::int32_t>(j);
					row.formatAtMapped = RawFormat(a_manager->renderTargetData[j]);
					break;
				}
			}

			return row;
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

				LogTexture(label, target.texture, ReadEngineRow(a_manager, i));

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
				LogTexture(label, target.texture, EngineRow{});

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
				LogTexture(label, target.texture, EngineRow{});

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
