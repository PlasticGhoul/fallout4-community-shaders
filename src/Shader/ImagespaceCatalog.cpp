#include "Shader/ImagespaceCatalog.h"

#include "Shader/BSShaderLayout.h"

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

		// Only these carry a BSShader. Everything else in the effect list is a
		// plain ImageSpaceEffect subclass and is counted, not reported.
		constexpr auto kShaderPrefix = "BSImagespaceShader"sv;

		// kImageSpace. Every image space shader measured in subproject C reads
		// exactly this.
		constexpr std::int32_t kImageSpaceShaderType = 0xC;

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
		//
		// Returns the address of the BSShader half as a void*, deliberately:
		// commonlibf4's RE::BSShader describes the wrong object, so the fields
		// are reached through the measured offsets in BSShaderLayout rather
		// than through its members.
		void* ShaderBaseOf(const RE::ImageSpaceEffect* a_effect, const ObjectInfo& a_info) noexcept
		{
			const auto address =
				reinterpret_cast<std::uintptr_t>(a_effect) - a_info.subobjectOffset;
			auto* const candidate = reinterpret_cast<void*>(address);

			const auto whole = Describe(candidate);
			if (!whole.has_value()) {
				return nullptr;
			}

			if (whole->className != a_info.className || whole->subobjectOffset != 0) {
				return nullptr;
			}

			return candidate;
		}

		// Stage three: the fields have to look like a BSShader before we believe
		// any of this. Returns the reason to log, or nullptr when it holds.
		const char* ImplausibleBecause(const void* a_shader) noexcept
		{
			if (ShaderType(a_shader) != kImageSpaceShaderType) {
				return "not an image space shader type";
			}

			if (FxpFilename(a_shader) == nullptr) {
				return "no fxp filename";
			}

			return nullptr;
		}
	}

	std::vector<ImagespacePass> RunImagespaceCatalog(bool a_verbose)
	{
		std::vector<ImagespacePass> found;

		auto* const manager = RE::ImageSpaceManager::GetSingleton();
		if (manager == nullptr) {
			REX::ERROR("no image space manager, skipping the catalog");
			return found;
		}

		if (manager->effectList.empty()) {
			if (a_verbose) {
				REX::WARN("the image space effect list is still empty");
			}
			return found;
		}

		if (a_verbose) {
			REX::INFO("=== image space catalog ===");
		}

		std::uint32_t visited = 0;
		std::uint32_t unnamed = 0;
		std::uint32_t plainEffects = 0;
		std::uint32_t noBase = 0;
		std::uint32_t implausible = 0;
		std::uint32_t withoutTechniques = 0;

		for (auto* const effect : manager->effectList) {
			++visited;

			const auto info = Describe(effect);
			if (!info.has_value()) {
				++unnamed;
				continue;
			}

			// Plain image space effects have no BSShader half at all. They are
			// counted so the totals add up, not reported one by one.
			if (!std::string_view{ info->className }.starts_with(kShaderPrefix)) {
				++plainEffects;
				continue;
			}

			auto* const shader = ShaderBaseOf(effect, *info);
			if (shader == nullptr) {
				++noBase;
				if (a_verbose) {
					REX::WARN(
						"{}: no matching class at -0x{:X}, the offset does not hold",
						info->className,
						info->subobjectOffset);
				}
				continue;
			}

			if (const auto* const reason = ImplausibleBecause(shader); reason != nullptr) {
				++implausible;
				if (a_verbose) {
					REX::WARN("{}: {}", info->className, reason);
				}
				continue;
			}

			const auto techniques = PixelShaderTechniques(shader);
			if (techniques.empty()) {
				// Normal, and not a fault: plenty of passes are compute or
				// vertex only, and the engine fills these maps lazily.
				++withoutTechniques;
				continue;
			}

			ImagespacePass pass;
			pass.className = info->className;
			pass.shader = shader;
			pass.fxpFilename = FxpFilename(shader);

			// Only an unambiguous single technique is worth recording as a
			// target; anything else we report but do not offer for replacement.
			if (techniques.size() == 1) {
				pass.slot = techniques.front();
				pass.techniqueID = techniques.front()->id;
			}

			if (a_verbose) {
				std::string listed;
				for (const auto* const entry : techniques) {
					listed += std::format(
						"{}{}@{}",
						listed.empty() ? "" : ",",
						entry->id,
						static_cast<const void*>(entry->shader));
				}

				REX::INFO(
					"{:<48} fxp {:<12} ps [{}]",
					pass.className,
					pass.fxpFilename,
					listed);
			}

			found.push_back(std::move(pass));
		}

		if (a_verbose || !found.empty()) {
			REX::INFO(
				"{} visited: {} with pixel techniques, {} without, {} plain effects, "
				"{} unnamed, {} without a shader base, {} implausible",
				visited,
				found.size(),
				withoutTechniques,
				plainEffects,
				unnamed,
				noBase,
				implausible);
			REX::INFO("=== end of catalog ===");
		}

		return found;
	}
}
