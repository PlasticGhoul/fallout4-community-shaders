#pragma once

#include <RE/B/BSGraphics.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace Shader
{
	/// Fallout 4's BSShader, as measured rather than as declared.
	///
	/// commonlibf4's RE/B/BSShader.h does not describe the runtime object. Its
	/// member order is right - vertex, hull, domain, pixel, compute, then the
	/// fxp filename - but every offset after shaderType is 0x78 too low, and
	/// the class is 0x190 rather than the asserted 0x118. Reading through it
	/// yields pointer halves where technique counts should be.
	///
	/// Measured in subproject C against Fallout 4 AE 1.11.240, on
	/// BSImagespaceShaderCopy and BSImagespaceShaderGammaCorrect:
	///
	///   0x018  shaderType, reads 0xC (kImageSpace) on every image space shader
	///   0x020  three unknown structures of 0x28 each, absent from commonlibf4
	///   0x098  vertexShaders, holding BSGraphics::VertexShader as declared
	///   0x0C8  hullShaders
	///   0x0F8  domainShaders
	///   0x128  pixelShaders
	///   0x158  computeShaders
	///   0x188  fxpFilename, reads "ISCopy" on BSImagespaceShaderCopy
	///   0x190  size, and also the offset of the ImageSpaceEffect subobject
	///
	/// The five map offsets are each confirmed by their own sentinel: the
	/// engine gives every instantiation a separate four byte DEADBEEF constant,
	/// and those sit at 0x0B0, 0x0E0, 0x110, 0x140 and 0x170 - exactly
	/// map + 0x18 for the offsets above. The technique entries confirm it a
	/// second time, each one's next pointer matching its own map's sentinel.
	namespace BSShaderOffset
	{
		inline constexpr std::uintptr_t kShaderType = 0x018;
		inline constexpr std::uintptr_t kVertexShaders = 0x098;
		inline constexpr std::uintptr_t kHullShaders = 0x0C8;
		inline constexpr std::uintptr_t kDomainShaders = 0x0F8;
		inline constexpr std::uintptr_t kPixelShaders = 0x128;
		inline constexpr std::uintptr_t kComputeShaders = 0x158;
		inline constexpr std::uintptr_t kFxpFilename = 0x188;
		inline constexpr std::size_t kSize = 0x190;
	}

	/// The scatter table interior, as commonlibf4 describes it. This part it
	/// gets right; only where the maps sit was wrong.
	namespace ScatterTableOffset
	{
		inline constexpr std::uintptr_t kCapacity = 0x0C;
		inline constexpr std::uintptr_t kFree = 0x10;
		inline constexpr std::uintptr_t kSentinel = 0x18;
		inline constexpr std::uintptr_t kEntries = 0x28;
		inline constexpr std::size_t kSize = 0x30;

		/// An entry is the value followed by the chain pointer. An unused slot
		/// has a null chain pointer; a used one points at the next entry or at
		/// the map's sentinel.
		inline constexpr std::size_t kEntrySize = 0x10;
	}

	/// The five technique maps, in the order they sit in the object.
	enum class Stage
	{
		kVertex,
		kHull,
		kDomain,
		kPixel,
		kCompute,

		kTotal
	};

	[[nodiscard]] std::string_view StageName(Stage a_stage) noexcept;
	[[nodiscard]] std::uintptr_t StageMapOffset(Stage a_stage) noexcept;

	/// One entry of a technique map, read the same way whatever the stage.
	///
	/// commonlibf4 declares a separate class per stage - VertexShader,
	/// HullShader and so on - but all five put the technique id at offset 0 and
	/// the D3D interface at offset 8, and only the tail beyond them differs.
	/// The census cares about neither tail, so it reads the common head.
	struct TechniqueEntry
	{
		std::uint32_t id{ 0 };
		const void* shader{ nullptr };
	};

	[[nodiscard]] std::int32_t ShaderType(const void* a_shader) noexcept;
	[[nodiscard]] const char* FxpFilename(const void* a_shader) noexcept;

	/// Reads one technique map. Returns an empty vector when the map is empty,
	/// which is the normal state for stages a shader does not use.
	///
	/// Refuses implausible maps rather than walking them: a capacity that is
	/// not a small power of two, or more used slots than the capacity allows,
	/// means the offset is wrong and following the entries pointer would be
	/// reading somebody else's memory.
	[[nodiscard]] std::vector<TechniqueEntry> Techniques(
		const void* a_shader,
		Stage a_stage) noexcept;

	/// All five stages added up. Cheap enough to poll with, which is what the
	/// census does: a count that has stopped moving is the only trustworthy
	/// sign that the engine has finished filling a shader's maps, and it fills
	/// them lazily.
	[[nodiscard]] std::size_t TotalTechniques(const void* a_shader) noexcept;

	/// The pixel stage as the type the engine declares for it. Kept apart from
	/// Techniques because a replacement needs the writable slot, not a reading
	/// of it.
	[[nodiscard]] std::vector<RE::BSGraphics::PixelShader*> PixelShaderTechniques(
		const void* a_shader) noexcept;
}
