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
		inline constexpr std::uintptr_t kGood = 0x14;
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

		/// The BSGraphics::<stage>Shader the slot holds. This is the address a
		/// replacement writes through, so it is kept rather than only read
		/// from.
		void* entry{ nullptr };

		/// The D3D11 interface that entry currently carries.
		const void* shader{ nullptr };
	};

	/// Everything one technique map says about itself, plus the verdict on
	/// whether it was a technique map at all.
	///
	/// The raw fields are carried out rather than only logged from inside,
	/// because being wrong about an offset twice is what this exists to end.
	/// A reader can compare the four header numbers against BSTScatterTable in
	/// commonlibf4 without another run of the game.
	struct MapReport
	{
		std::uintptr_t offset{ 0 };
		const void* address{ nullptr };

		std::uint32_t capacity{ 0 };
		std::uint32_t free{ 0 };
		std::uint32_t good{ 0 };
		const void* sentinel{ nullptr };
		const void* entries{ nullptr };

		/// The four bytes the sentinel points at, when they could be read at
		/// all. The engine writes DE AD BE EF there, once per template
		/// instantiation, and that is the proof subproject C used to establish
		/// these offsets in the first place.
		bool sentinelReadable{ false };
		std::uint32_t sentinelBytes{ 0 };

		/// Null when the table was walked. Otherwise why it was not, and
		/// techniques is then empty by construction.
		const char* refusedBecause{ nullptr };

		std::vector<TechniqueEntry> techniques;
	};

	/// Reads a technique map's header, proves it is one, and walks it.
	///
	/// Refuses rather than walks whenever a proof fails. A capacity that is not
	/// a power of two, a sentinel that does not point at DE AD BE EF, a chain
	/// pointer that leaves the table's own array, a used count that disagrees
	/// with capacity minus free: any of these means the address is not a
	/// scatter table, and following it reads somebody else's memory. That
	/// happened - BSDFPrePassShader produced a list with repeated ids and then
	/// took the process with it.
	[[nodiscard]] MapReport InspectMap(const void* a_shader, Stage a_stage) noexcept;

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

	/// All five stages added up.
	struct MapTotals
	{
		std::size_t techniques{ 0 };

		/// Maps that could not prove themselves. A map is refused while the
		/// engine is growing it - the header still describes the old array
		/// while the new one is half filled and its chains still point into
		/// the old - so a refusal is usually a moment rather than a fault, and
		/// worth waiting out.
		std::size_t refused{ 0 };
	};

	/// Cheap enough to poll with, which is what the census does: a count that
	/// has stopped moving, with nothing refused, is the only trustworthy sign
	/// that the engine has finished filling a shader's maps. It fills them
	/// lazily, and grows them while the game runs.
	[[nodiscard]] MapTotals SummariseMaps(const void* a_shader) noexcept;

	/// The pixel stage as the type the engine declares for it. Kept apart from
	/// Techniques because a replacement needs the writable slot, not a reading
	/// of it.
	[[nodiscard]] std::vector<RE::BSGraphics::PixelShader*> PixelShaderTechniques(
		const void* a_shader) noexcept;
}
