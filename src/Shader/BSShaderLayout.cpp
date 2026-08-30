#include "Shader/BSShaderLayout.h"

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
	}

	std::int32_t ShaderType(const void* a_shader) noexcept
	{
		return ReadAt<std::int32_t>(a_shader, BSShaderOffset::kShaderType);
	}

	const char* FxpFilename(const void* a_shader) noexcept
	{
		return ReadAt<const char*>(a_shader, BSShaderOffset::kFxpFilename);
	}

	std::vector<RE::BSGraphics::PixelShader*> PixelShaderTechniques(const void* a_shader) noexcept
	{
		std::vector<RE::BSGraphics::PixelShader*> techniques;

		const auto map =
			reinterpret_cast<std::uintptr_t>(a_shader) + BSShaderOffset::kPixelShaders;

		const auto capacity =
			ReadAt<std::uint32_t>(reinterpret_cast<const void*>(map), ScatterTableOffset::kCapacity);
		const auto free =
			ReadAt<std::uint32_t>(reinterpret_cast<const void*>(map), ScatterTableOffset::kFree);
		const auto entries =
			ReadAt<std::uintptr_t>(reinterpret_cast<const void*>(map), ScatterTableOffset::kEntries);

		if (capacity == 0 || entries == 0) {
			return techniques;  // A stage this shader does not use.
		}

		// A scatter table capacity is always a power of two, and free slots can
		// never outnumber the slots themselves. Either failing means we are not
		// looking at a scatter table.
		if (capacity > kMaxCapacity || !std::has_single_bit(capacity) || free > capacity) {
			return techniques;
		}

		techniques.reserve(capacity - free);

		for (std::uint32_t i = 0; i < capacity; ++i) {
			const auto slot = entries + i * ScatterTableOffset::kEntrySize;

			// An unused slot has no chain pointer; that is what marks it unused.
			if (ReadAt<std::uintptr_t>(reinterpret_cast<const void*>(slot), 8) == 0) {
				continue;
			}

			auto* const entry =
				ReadAt<RE::BSGraphics::PixelShader*>(reinterpret_cast<const void*>(slot), 0);
			if (entry != nullptr) {
				techniques.push_back(entry);
			}
		}

		return techniques;
	}
}
