#include "Shader/BSShaderLayout.h"

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

		// The engine never grows one of these past a handful of techniques, so
		// anything larger is a sign that the offset is wrong rather than that
		// the shader is unusual.
		constexpr std::uint32_t kMaxCapacity = 256;

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

		// Walks one scatter table and hands back the address held in every used
		// slot. What that address points at is the caller's business - the five
		// stage classes differ only past their first sixteen bytes.
		std::vector<const void*> MapEntries(const void* a_shader, std::uintptr_t a_mapOffset) noexcept
		{
			std::vector<const void*> entries;

			const auto* const map = reinterpret_cast<const void*>(
				reinterpret_cast<std::uintptr_t>(a_shader) + a_mapOffset);

			const auto capacity = ReadAt<std::uint32_t>(map, ScatterTableOffset::kCapacity);
			const auto free = ReadAt<std::uint32_t>(map, ScatterTableOffset::kFree);
			const auto slots = ReadAt<std::uintptr_t>(map, ScatterTableOffset::kEntries);

			if (capacity == 0 || slots == 0) {
				return entries;  // A stage this shader does not use.
			}

			// A scatter table capacity is always a power of two, and free slots
			// can never outnumber the slots themselves. Either failing means we
			// are not looking at a scatter table.
			if (capacity > kMaxCapacity || !std::has_single_bit(capacity) || free > capacity) {
				return entries;
			}

			entries.reserve(capacity - free);

			for (std::uint32_t i = 0; i < capacity; ++i) {
				const auto* const slot =
					reinterpret_cast<const void*>(slots + i * ScatterTableOffset::kEntrySize);

				// An unused slot has no chain pointer; that is what marks it
				// unused.
				if (ReadAt<std::uintptr_t>(slot, 8) == 0) {
					continue;
				}

				const auto* const entry = ReadAt<const void*>(slot, 0);
				if (entry != nullptr) {
					entries.push_back(entry);
				}
			}

			return entries;
		}
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
		std::vector<TechniqueEntry> techniques;

		const auto offset = StageMapOffset(a_stage);
		if (a_shader == nullptr || offset == 0) {
			return techniques;
		}

		for (const auto* const entry : MapEntries(a_shader, offset)) {
			// Every stage class puts the technique id at 0 and the D3D
			// interface at 8; only what follows differs between them.
			techniques.push_back(
				TechniqueEntry{ ReadAt<std::uint32_t>(entry, 0), ReadAt<const void*>(entry, 8) });
		}

		return techniques;
	}

	std::vector<RE::BSGraphics::PixelShader*> PixelShaderTechniques(const void* a_shader) noexcept
	{
		std::vector<RE::BSGraphics::PixelShader*> techniques;

		if (a_shader == nullptr) {
			return techniques;
		}

		for (const auto* const entry : MapEntries(a_shader, BSShaderOffset::kPixelShaders)) {
			techniques.push_back(
				const_cast<RE::BSGraphics::PixelShader*>(
					static_cast<const RE::BSGraphics::PixelShader*>(entry)));
		}

		return techniques;
	}
}
