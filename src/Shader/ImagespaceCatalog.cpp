#include "Shader/ImagespaceCatalog.h"

#include <RE/I/ImageSpaceEffect.h>
#include <RE/I/ImageSpaceManager.h>
#include <REX/W32/RTTI.h>

#include <format>
#include <optional>

namespace Shader
{
	namespace
	{
		// MSVC's type descriptor: a vftable pointer, a spare, then the
		// decorated name as a plain null terminated string.
		struct TypeDescriptor
		{
			const void* vftable;
			void* spare;
			char name[1];
		};

		struct ObjectInfo
		{
			std::string className;
			std::uint32_t subobjectOffset{ 0 };
		};

		// MSVC stores a pointer to the complete object locator immediately
		// before the first entry of every polymorphic vtable.
		const REX::W32::RTTICompleteObjectLocator* LocatorOf(const void* a_object) noexcept
		{
			const auto* const vtable = *static_cast<const void* const*>(a_object);
			if (vtable == nullptr) {
				return nullptr;
			}

			return *(static_cast<const REX::W32::RTTICompleteObjectLocator* const*>(vtable) - 1);
		}

		// Reads class name and subobject offset out of the object itself.
		//
		// Deliberately not a comparison against RE::VTABLE ids: REL::ID::offset
		// calls REX::FAIL for an id the address library does not know, which
		// ends the process. With 162 imagespace classes that is a real risk and
		// an unnecessary one - the compiler already wrote both answers into the
		// binary.
		std::optional<ObjectInfo> Describe(const void* a_object) noexcept
		{
			if (a_object == nullptr) {
				return std::nullopt;
			}

			const auto* const locator = LocatorOf(a_object);
			if (locator == nullptr || locator->signature != 1) {
				return std::nullopt;
			}

			// The locator records its own RVA. Recomputing the module base from
			// it and comparing against the game module proves the vtable really
			// belongs to Fallout4.exe rather than to whatever the pointer
			// happened to land in.
			const auto base = reinterpret_cast<std::uintptr_t>(locator) - locator->self;
			if (base != REX::FModule::GetExecutingModule().GetBaseAddress()) {
				return std::nullopt;
			}

			const auto* const descriptor =
				reinterpret_cast<const TypeDescriptor*>(base + locator->typeDescriptor);

			std::string_view decorated{ descriptor->name };
			if (!decorated.starts_with(".?AV")) {
				return std::nullopt;
			}

			decorated.remove_prefix(4);
			if (decorated.ends_with("@@")) {
				decorated.remove_suffix(2);
			}

			return ObjectInfo{ std::string{ decorated }, locator->offset };
		}

		// Stage two of the safety net: the address computed from the subobject
		// offset must itself carry a locator of the same class whose offset is
		// zero. That proves the arithmetic from both ends.
		RE::BSShader* ShaderBaseOf(
			const RE::ImageSpaceEffect* a_effect,
			const ObjectInfo& a_info) noexcept
		{
			const auto address =
				reinterpret_cast<std::uintptr_t>(a_effect) - a_info.subobjectOffset;
			auto* const candidate = reinterpret_cast<RE::BSShader*>(address);

			const auto whole = Describe(candidate);
			if (!whole.has_value()) {
				return nullptr;
			}

			if (whole->className != a_info.className || whole->subobjectOffset != 0) {
				return nullptr;
			}

			return candidate;
		}

		// Stage three: the fields have to look like a BSShader before we
		// believe any of this.
		bool LooksPlausible(const RE::BSShader* a_shader) noexcept
		{
			if (a_shader->shaderType < 0 || a_shader->shaderType > 0x40) {
				return false;
			}

			if (a_shader->fxpFilename == nullptr) {
				return false;
			}

			const auto techniques = a_shader->pixelShaders.size();
			return techniques > 0 && techniques < 64;
		}
	}

	std::vector<ImagespacePass> RunImagespaceCatalog()
	{
		std::vector<ImagespacePass> found;

		auto* const manager = RE::ImageSpaceManager::GetSingleton();
		if (manager == nullptr) {
			REX::ERROR("no image space manager, skipping the catalog");
			return found;
		}

		if (manager->effectList.empty()) {
			REX::WARN("the image space effect list is still empty");
			return found;
		}

		REX::INFO("=== image space catalog ===");
		REX::INFO("{} entries in the effect list", manager->effectList.size());

		std::uint32_t rejected = 0;

		for (auto* const effect : manager->effectList) {
			const auto info = Describe(effect);
			if (!info.has_value()) {
				++rejected;
				continue;
			}

			auto* const shader = ShaderBaseOf(effect, *info);
			if (shader == nullptr || !LooksPlausible(shader)) {
				REX::WARN("{}: rejected by the safety net", info->className);
				++rejected;
				continue;
			}

			ImagespacePass pass;
			pass.className = info->className;
			pass.shader = shader;

			// Only an unambiguous single technique is worth recording as a
			// target; anything else we log but do not offer for replacement.
			if (shader->pixelShaders.size() == 1) {
				auto* const entry = *shader->pixelShaders.begin();
				pass.slot = entry;
				pass.techniqueID = entry->id;
			}

			std::string techniques;
			for (auto* const entry : shader->pixelShaders) {
				techniques += std::format(
					"{}{}@{}",
					techniques.empty() ? "" : ",",
					entry->id,
					static_cast<const void*>(entry->shader));
			}

			REX::INFO(
				"{:<44} +{:<4} type {:<3} fxp {:<28} ps [{}]",
				pass.className,
				info->subobjectOffset,
				shader->shaderType,
				shader->fxpFilename,
				techniques);

			found.push_back(std::move(pass));
		}

		REX::INFO("{} passes described, {} rejected", found.size(), rejected);
		REX::INFO("=== end of catalog ===");

		return found;
	}
}
