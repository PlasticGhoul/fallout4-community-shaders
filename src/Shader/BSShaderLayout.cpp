#include "Shader/BSShaderLayout.h"

#include "Util/SafeRead.h"

#include <array>
#include <bit>

namespace Shader
{
	namespace
	{
		template <class T>
		[[nodiscard]] T ReadAt(const void* a_base, std::uintptr_t a_offset) noexcept
		{
			return *reinterpret_cast<const T*>(
				reinterpret_cast<std::uintptr_t>(a_base) + a_offset);
		}

		// Generous rather than tight. The old bound of 256 was a guess made
		// when the only maps ever read held a single technique, and kDFComposite
		// alone holds 180 - one more doubling and a real table would have been
		// refused, silently and as an undercount. What keeps a wrong address
		// out is the sentinel and the chain pointers, not this.
		constexpr std::uint32_t kMaxCapacity = 65536;

		// RE::detail::BSTScatterTableSentinel, as the four bytes it is, in the
		// order they sit in memory.
		constexpr std::array<std::uint8_t, 4> kSentinelBytes{ 0xDEu, 0xADu, 0xBEu, 0xEFu };

		constexpr std::array<std::string_view, static_cast<std::size_t>(Stage::kTotal)> kStageNames{
			"vertex"sv,
			"hull"sv,
			"domain"sv,
			"pixel"sv,
			"compute"sv,
		};

		constexpr std::array<std::uintptr_t, static_cast<std::size_t>(Stage::kTotal)> kStageOffsets{
			BSShaderOffset::kVertexShaders,
			BSShaderOffset::kHullShaders,
			BSShaderOffset::kDomainShaders,
			BSShaderOffset::kPixelShaders,
			BSShaderOffset::kComputeShaders,
		};
	}

	std::string_view StageName(Stage a_stage) noexcept
	{
		const auto index = static_cast<std::size_t>(a_stage);
		return index < kStageNames.size() ? kStageNames[index] : "unknown"sv;
	}

	std::uintptr_t StageMapOffset(Stage a_stage) noexcept
	{
		const auto index = static_cast<std::size_t>(a_stage);
		return index < kStageOffsets.size() ? kStageOffsets[index] : 0;
	}

	MapReport InspectMap(const void* a_shader, Stage a_stage) noexcept
	{
		MapReport report;
		report.offset = StageMapOffset(a_stage);

		if (a_shader == nullptr || report.offset == 0) {
			report.refusedBecause = "no shader";
			return report;
		}

		const auto* const map = reinterpret_cast<const void*>(
			reinterpret_cast<std::uintptr_t>(a_shader) + report.offset);
		report.address = map;

		report.capacity = ReadAt<std::uint32_t>(map, ScatterTableOffset::kCapacity);
		report.free = ReadAt<std::uint32_t>(map, ScatterTableOffset::kFree);
		report.good = ReadAt<std::uint32_t>(map, ScatterTableOffset::kGood);
		report.sentinel = ReadAt<const void*>(map, ScatterTableOffset::kSentinel);
		report.entries = ReadAt<const void*>(map, ScatterTableOffset::kEntries);

		// The sentinel is read whatever the verdict: it is the single most
		// telling number in the header, and a report that omits it cannot be
		// used to decide whether the offset was wrong.
		if (Util::IsReadableRange(report.sentinel, kSentinelBytes.size())) {
			report.sentinelReadable = true;
			report.sentinelBytes = ReadAt<std::uint32_t>(report.sentinel, 0);
		}

		// A stage this shader does not use. Not a refusal - it is what an
		// untouched BSTScatterTable looks like.
		if (report.capacity == 0 && report.entries == nullptr) {
			return report;
		}

		if (report.entries == nullptr) {
			report.refusedBecause = "a capacity but no entries";
			return report;
		}

		if (!std::has_single_bit(report.capacity)) {
			report.refusedBecause = "capacity is not a power of two";
			return report;
		}

		if (report.capacity > kMaxCapacity) {
			report.refusedBecause = "capacity beyond anything plausible";
			return report;
		}

		if (report.free > report.capacity) {
			report.refusedBecause = "more free slots than slots";
			return report;
		}

		const std::size_t span =
			static_cast<std::size_t>(report.capacity) * ScatterTableOffset::kEntrySize;
		if (!Util::IsReadableRange(report.entries, span)) {
			report.refusedBecause = "the entries array is not readable";
			return report;
		}

		if (!report.sentinelReadable) {
			report.refusedBecause = "the sentinel is not readable";
			return report;
		}

		if (std::bit_cast<std::array<std::uint8_t, 4>>(report.sentinelBytes) != kSentinelBytes) {
			report.refusedBecause = "the sentinel is not DE AD BE EF";
			return report;
		}

		const auto first = reinterpret_cast<std::uintptr_t>(report.entries);
		const auto last = first + span;
		const auto sentinel = reinterpret_cast<std::uintptr_t>(report.sentinel);

		std::vector<TechniqueEntry> walked;
		walked.reserve(report.capacity - report.free);

		for (std::uint32_t i = 0; i < report.capacity; ++i) {
			const auto slot = first + i * ScatterTableOffset::kEntrySize;

			// An unused slot has no chain pointer; that is what marks it
			// unused.
			const auto next = ReadAt<std::uintptr_t>(reinterpret_cast<const void*>(slot), 8);
			if (next == 0) {
				continue;
			}

			// A used slot's chain either ends at this table's own sentinel or
			// points at another slot of this same table, aligned to a slot
			// boundary. Anything else means this is not a table, and one such
			// slot condemns the whole thing rather than only itself.
			const bool chained =
				next == sentinel ||
				(next >= first && next < last &&
					(next - first) % ScatterTableOffset::kEntrySize == 0);

			if (!chained) {
				report.refusedBecause = "a chain pointer leaves the table";
				return report;
			}

			auto* const entry = ReadAt<void*>(reinterpret_cast<const void*>(slot), 0);
			if (entry == nullptr) {
				report.refusedBecause = "a used slot holds nothing";
				return report;
			}

			// Every stage class puts the technique id at 0 and the D3D
			// interface at 8; only what follows differs between them.
			if (!Util::IsReadableRange(entry, 16)) {
				report.refusedBecause = "a used slot points at unreadable memory";
				return report;
			}

			walked.push_back(TechniqueEntry{
				ReadAt<std::uint32_t>(entry, 0),
				entry,
				ReadAt<const void*>(entry, 8) });
		}

		// The header says how many slots are taken. A walk that finds a
		// different number was not walking this table.
		if (walked.size() != static_cast<std::size_t>(report.capacity - report.free)) {
			report.refusedBecause = "used slots disagree with capacity minus free";
			return report;
		}

		report.techniques = std::move(walked);
		return report;
	}

	std::int32_t ShaderType(const void* a_shader) noexcept
	{
		return ReadAt<std::int32_t>(a_shader, BSShaderOffset::kShaderType);
	}

	const char* FxpFilename(const void* a_shader) noexcept
	{
		return ReadAt<const char*>(a_shader, BSShaderOffset::kFxpFilename);
	}

	std::vector<TechniqueEntry> Techniques(const void* a_shader, Stage a_stage) noexcept
	{
		return InspectMap(a_shader, a_stage).techniques;
	}

	std::size_t TotalTechniques(const void* a_shader) noexcept
	{
		if (a_shader == nullptr) {
			return 0;
		}

		std::size_t total = 0;
		for (auto stage = 0; stage < static_cast<int>(Stage::kTotal); ++stage) {
			total += InspectMap(a_shader, static_cast<Stage>(stage)).techniques.size();
		}

		return total;
	}

	std::vector<RE::BSGraphics::PixelShader*> PixelShaderTechniques(const void* a_shader) noexcept
	{
		std::vector<RE::BSGraphics::PixelShader*> techniques;

		for (const auto& entry : InspectMap(a_shader, Stage::kPixel).techniques) {
			// entry.entry, not entry.shader: a replacement writes into the
			// engine's BSGraphics::PixelShader, and entry.shader is only the
			// D3D interface currently sitting in it.
			techniques.push_back(static_cast<RE::BSGraphics::PixelShader*>(entry.entry));
		}

		return techniques;
	}
}
