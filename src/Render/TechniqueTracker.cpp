#include "Render/TechniqueTracker.h"

#include "Render/VTablePatch.h"
#include "Shader/ShaderCatalog.h"
#include "Util/ObjectRTTI.h"

#include <array>
#include <atomic>
#include <string>
#include <utility>

namespace Render
{
	namespace
	{
		using SetupTechniqueFn = bool (*)(void*, std::uint32_t);

		constexpr std::size_t kSetupTechniqueSlot = 2;

		// One word: bit 63 valid, bits 32..47 the class, bits 0..31 the
		// technique. A reader on the same thread never sees half an update,
		// and a reader on another thread sees a whole one or the previous.
		constexpr std::uint64_t kValidBit = std::uint64_t{ 1 } << 63;

		std::array<VTablePatch, kTrackedClasses> g_patches{};
		std::array<void*, kTrackedClasses> g_original{};
		std::atomic<std::uint64_t> g_current{ 0 };
		bool g_installed = false;

		template <std::size_t N>
		bool ThunkSetupTechnique(void* a_self, std::uint32_t a_pass) noexcept
		{
			g_current.store(
				kValidBit | (static_cast<std::uint64_t>(N) << 32) | a_pass,
				std::memory_order_relaxed);
			return reinterpret_cast<SetupTechniqueFn>(g_original[N])(a_self, a_pass);
		}

		template <std::size_t... I>
		constexpr std::array<SetupTechniqueFn, sizeof...(I)> MakeThunks(std::index_sequence<I...>)
		{
			return { &ThunkSetupTechnique<I>... };
		}

		constexpr auto kThunks = MakeThunks(std::make_index_sequence<kTrackedClasses>{});
	}

	bool InstallTechniqueTracker() noexcept
	{
		if (g_installed) {
			return true;
		}

		const auto classes = Shader::ShaderClasses();
		std::size_t installed = 0;

		for (std::size_t i = 0; i < classes.size() && i < kTrackedClasses; ++i) {
			auto** const table = reinterpret_cast<void**>(classes[i].vtable);
			if (table == nullptr) {
				REX::WARN("technique tracker: no vtable address for {}", classes[i].className);
				continue;
			}

			// The table has to be the one the id promised, and a primary one,
			// before an entry is touched: the first call through a wrongly
			// patched entry ends the process.
			const auto identity = Util::DescribeVTable(table);
			if (!identity.has_value() ||
				identity->className != classes[i].className ||
				identity->subobjectOffset != 0) {
				REX::WARN(
					"technique tracker: the vtable id for {} names {} at +0x{:X}, leaving it alone",
					classes[i].className,
					identity.has_value() ? identity->className : std::string{ "nothing" },
					identity.has_value() ? identity->subobjectOffset : 0);
				continue;
			}

			if (!g_patches[i].InstallAtTable(
					table, kSetupTechniqueSlot, reinterpret_cast<void*>(kThunks[i]))) {
				REX::WARN("technique tracker: could not patch {}", classes[i].className);
				continue;
			}

			g_original[i] = g_patches[i].Original();
			++installed;
		}

		if (installed == 0) {
			REX::ERROR("technique tracker: no class could be patched");
			return false;
		}

		g_installed = true;
		REX::INFO(
			"technique tracker: watching {} of {} shader classes, patches stay for the process",
			installed,
			classes.size());
		return true;
	}

	CurrentTechnique CurrentTechniqueOf() noexcept
	{
		const auto word = g_current.load(std::memory_order_relaxed);
		CurrentTechnique current;
		current.valid = (word & kValidBit) != 0;
		current.classIndex = static_cast<std::size_t>((word >> 32) & 0xFFFF);
		current.technique = static_cast<std::uint32_t>(word & 0xFFFFFFFFu);
		return current;
	}

	std::optional<std::size_t> ClassIndexOf(std::string_view a_className) noexcept
	{
		const auto classes = Shader::ShaderClasses();
		for (std::size_t i = 0; i < classes.size(); ++i) {
			if (classes[i].className == a_className) {
				return i;
			}
		}
		return std::nullopt;
	}
}
