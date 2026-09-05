#include "Shader/ShaderCatalog.h"

#include "Shader/BSShaderLayout.h"

#include <RE/IDs_VTABLE.h>

#include <algorithm>
#include <array>
#include <format>
#include <span>
#include <string>
#include <vector>

namespace Shader
{
	namespace
	{
		// BSShader::GetTechniqueName, vtable slot 09 by commonlibf4's numbering
		// in RE/B/BSShader.h. On x64 a member function takes its object in RCX,
		// so a free function pointer with the object as the first parameter is
		// the right shape.
		constexpr std::size_t kGetTechniqueNameSlot = 9;
		using GetTechniqueNameFn = void (*)(const void*, std::uint32_t, char*, std::uint32_t);

		// Long enough for anything the engine is likely to spell out, short
		// enough to sit on the stack of a render thread call.
		constexpr std::size_t kNameBufferSize = 256;

		// A map larger than this is reported by its count alone. Nothing is
		// gained by writing a thousand ids into a log, and the point of the
		// census is the count.
		constexpr std::size_t kMaxListed = 512;

		// Roughly a terminal width, so a long list wraps rather than producing
		// one unreadable line.
		constexpr std::size_t kLineWidth = 140;

		std::string TechniqueName(const void* a_shader, std::uint32_t a_id) noexcept
		{
			auto** const vtable = *reinterpret_cast<void** const*>(a_shader);
			if (vtable == nullptr) {
				return {};
			}

			const auto call = reinterpret_cast<GetTechniqueNameFn>(vtable[kGetTechniqueNameSlot]);

			std::array<char, kNameBufferSize> buffer{};
			call(a_shader, a_id, buffer.data(), static_cast<std::uint32_t>(buffer.size() - 1));

			// The engine may leave the buffer untouched for an id it does not
			// recognise, and a name full of control characters would mean the
			// slot is not the one we think it is. Either way, say nothing
			// rather than write rubbish into the log.
			std::string name{ buffer.data() };
			for (const auto character : name) {
				if (static_cast<unsigned char>(character) < 0x20) {
					return {};
				}
			}

			return name;
		}

		// The header of every map, whatever the verdict on it.
		//
		// This is a raw dump on purpose. A layout assumption that misses twice
		// is not worth a third guess: the four numbers plus the sentinel are
		// what BSTScatterTable actually declares, so a reader can check them
		// against commonlibf4 without another run of the game.
		void LogMapHeader(const MapReport& a_report, Stage a_stage) noexcept
		{
			REX::INFO(
				"    map {:<8} @+0x{:03X}  capacity {:<6} free {:<6} good {:<6} "
				"sentinel {} [{}]  entries {}  -> {}",
				StageName(a_stage),
				a_report.offset,
				a_report.capacity,
				a_report.free,
				a_report.good,
				a_report.sentinel,
				a_report.sentinelReadable ? std::format("{:08X}", a_report.sentinelBytes) : "unreadable",
				a_report.entries,
				a_report.refusedBecause != nullptr ?
					std::string{ "REFUSED, " } + a_report.refusedBecause :
					std::format("{} used", a_report.techniques.size()));
		}

		// Sixteen bytes to a row, so an offset can be found by counting rows.
		constexpr std::size_t kHexRow = 16;

		std::string Hex(std::span<const std::uint8_t> a_bytes) noexcept
		{
			std::string text;
			for (const auto byte : a_bytes) {
				text += std::format("{}{:02X}", text.empty() ? "" : " ", byte);
			}

			return text;
		}

		// A refused map as the bytes it is made of.
		//
		// Written out only when a map refuses, and then together with a map of
		// the same shader that walked. One of the two is a scatter table for
		// certain, so the pair says what a good header looks like beside the
		// bad one - which a dump of the bad one alone does not.
		void LogMapDump(const MapDump& a_dump, Stage a_stage) noexcept
		{
			REX::INFO("    dump {:<8} @+0x{:03X}  map at {}  entries {}",
				StageName(a_stage),
				a_dump.offset,
				a_dump.address,
				a_dump.entries);

			if (!a_dump.headerReadable) {
				REX::INFO("             the header is not readable, so there is nothing to dump");
				return;
			}

			const std::span header{ a_dump.header };
			for (std::size_t row = 0; row < header.size(); row += kHexRow) {
				REX::INFO("             header   +0x{:02X}  {}",
					row,
					Hex(header.subspan(row, std::min(kHexRow, header.size() - row))));
			}

			if (a_dump.prologueReadable) {
				const std::span prologue{ a_dump.prologue };
				for (std::size_t row = 0; row < prologue.size(); row += kHexRow) {
					REX::INFO("             prologue -0x{:02X}  {}",
						prologue.size() - row,
						Hex(prologue.subspan(row, std::min(kHexRow, prologue.size() - row))));
				}
			} else if (a_dump.entries != nullptr) {
				REX::INFO("             prologue unreadable, so the array opens its own page");
			}

			for (const auto& slot : a_dump.slots) {
				REX::INFO("             slot {:>5}  value {}  next {}",
					slot.index,
					slot.value,
					slot.next);
			}

			for (const auto& probe : a_dump.probes) {
				if (!probe.readable) {
					REX::INFO(
						"             as if capacity {:<5} not readable that far, and nor is anything larger",
						probe.capacity);
					continue;
				}

				REX::INFO(
					"             as if capacity {:<5} {:>4} used, {:>4} chained within, "
					"{:>4} to the sentinel, {:>4} away, {:>4} valueless{}",
					probe.capacity,
					probe.used,
					probe.chainsWithin,
					probe.chainsToSentinel,
					probe.chainsAway,
					probe.valuelessSlots,
					probe.agreesWithFree ? "  <- agrees with the header's free count" : "");
			}
		}

		// The ids themselves, and their engine-given names. The count and the
		// verdict are already on the header line above.
		void LogStage(
			const void* a_shader,
			const std::vector<TechniqueEntry>& a_techniques,
			bool a_withNames) noexcept
		{
			if (a_techniques.empty()) {
				return;  // A stage this shader does not use. The normal case.
			}

			if (a_techniques.size() > kMaxListed) {
				REX::INFO("             too many to list");
				return;
			}

			std::string line;
			for (const auto& technique : a_techniques) {
				auto item = std::format("{}", technique.id);
				if (a_withNames) {
					if (const auto name = TechniqueName(a_shader, technique.id); !name.empty()) {
						item += std::format("={}", name);
					}
				}

				if (!line.empty() && line.size() + item.size() + 2 > kLineWidth) {
					REX::INFO("             {}", line);
					line.clear();
				}

				line += line.empty() ? item : ", " + item;
			}

			if (!line.empty()) {
				REX::INFO("             {}", line);
			}
		}
	}

	std::span<const ShaderClass> ShaderClasses() noexcept
	{
		// Resolved once, on the first call, which is after kGameDataReady.
		static const std::array<ShaderClass, 13> classes{
			ShaderClass{ "BSEffectShader"sv, "kEffect"sv, 0x0, RE::VTABLE::BSEffectShader[0].address() },
			ShaderClass{ "BSUtilityShader"sv, "kUtility"sv, 0x1, RE::VTABLE::BSUtilityShader[0].address() },
			ShaderClass{ "BSDistantTreeShader"sv, "kDistantTree"sv, 0x2, RE::VTABLE::BSDistantTreeShader[0].address() },
			ShaderClass{ "BSParticleShader"sv, "kParticle"sv, 0x3, RE::VTABLE::BSParticleShader[0].address() },
			ShaderClass{ "BSDFPrePassShader"sv, "kDFPrepass"sv, 0x4, RE::VTABLE::BSDFPrePassShader[0].address() },
			ShaderClass{ "BSDFLightShader"sv, "kDFLight"sv, 0x5, RE::VTABLE::BSDFLightShader[0].address() },
			ShaderClass{ "BSDFCompositeShader"sv, "kDFComposite"sv, 0x6, RE::VTABLE::BSDFCompositeShader[0].address() },
			ShaderClass{ "BSSkyShader"sv, "kSky"sv, 0x7, RE::VTABLE::BSSkyShader[0].address() },
			ShaderClass{ "BSLightingShader"sv, "kLighting"sv, 0x8, RE::VTABLE::BSLightingShader[0].address() },
			ShaderClass{ "BSBloodSplatterShader"sv, "kBloodSpatter"sv, 0x9, RE::VTABLE::BSBloodSplatterShader[0].address() },
			ShaderClass{ "BSWaterShader"sv, "kWater"sv, 0xA, RE::VTABLE::BSWaterShader[0].address() },
			ShaderClass{ "BSFaceCustomizationShader"sv, "kFaceCustomization"sv, 0xB, RE::VTABLE::BSFaceCustomizationShader[0].address() },
			ShaderClass{ "BSImagespaceShader"sv, "kImageSpace"sv, 0xC, RE::VTABLE::BSImagespaceShader[0].address() },
		};

		return classes;
	}

	void ReportShaderTechniques(
		const ShaderClass& a_class,
		const void* a_shader,
		bool a_withNames) noexcept
	{
		if (a_shader == nullptr) {
			REX::INFO("{:<26} {:<18} never seen", a_class.className, a_class.enumerator);
			return;
		}

		const auto type = ShaderType(a_shader);
		const auto* const fxp = FxpFilename(a_shader);

		REX::INFO(
			"{:<26} {:<18} shaderType {} ({}), fxp {}",
			a_class.className,
			a_class.enumerator,
			type,
			type == a_class.shaderType ? "as expected" : "MISMATCH",
			fxp != nullptr ? fxp : "(none)");

		auto refused = static_cast<int>(Stage::kTotal);
		auto walked = static_cast<int>(Stage::kTotal);

		for (auto stage = 0; stage < static_cast<int>(Stage::kTotal); ++stage) {
			const auto which = static_cast<Stage>(stage);
			const auto report = InspectMap(a_shader, which);

			LogMapHeader(report, which);
			LogStage(a_shader, report.techniques, a_withNames);

			if (report.refusedBecause != nullptr) {
				if (refused == static_cast<int>(Stage::kTotal)) {
					refused = stage;
				}
			} else if (!report.techniques.empty() && walked == static_cast<int>(Stage::kTotal)) {
				walked = stage;
			}
		}

		if (refused == static_cast<int>(Stage::kTotal)) {
			return;
		}

		// Every refused map, and one that walked beside them. The comparison is
		// the point: both were read through the same offsets out of the same
		// object, so whatever differs between them is what the refusal is made
		// of.
		for (auto stage = 0; stage < static_cast<int>(Stage::kTotal); ++stage) {
			const auto which = static_cast<Stage>(stage);
			if (InspectMap(a_shader, which).refusedBecause != nullptr) {
				LogMapDump(DumpMap(a_shader, which), which);
			}
		}

		if (walked != static_cast<int>(Stage::kTotal)) {
			REX::INFO("    the same shader's {} map, which walked, for comparison:",
				StageName(static_cast<Stage>(walked)));
			LogMapDump(DumpMap(a_shader, static_cast<Stage>(walked)), static_cast<Stage>(walked));
		}
	}
}
