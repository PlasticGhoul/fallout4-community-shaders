#include "Shader/BSShaderLayout.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
	int g_failures = 0;

	void Check(bool a_passed, const char* a_what)
	{
		std::printf("%s  %s\n", a_passed ? "ok  " : "FAIL", a_what);
		if (!a_passed) {
			++g_failures;
		}
	}

	// The four bytes the engine writes once per template instantiation, as the
	// little endian word they are: DE AD BE EF in memory.
	constexpr std::uint32_t kSentinel = 0xEFBEADDEu;

	// What a slot's value points at. Only the first sixteen bytes are read, and
	// only the id out of them, but the reader checks that all sixteen are
	// there.
	struct Technique
	{
		std::uint32_t id{ 0 };
		std::uint32_t padding{ 0 };
		const void* shader{ nullptr };
	};

	/// A BSShader-shaped buffer carrying one technique map, built the way the
	/// engine builds one.
	///
	/// The reader exists to be right about an object it cannot compile against,
	/// so what can be tested is the reading itself: that a well formed table is
	/// walked, that each malformation is refused for its own stated reason, and
	/// that a table larger than its header claims is diagnosed rather than
	/// guessed at. That last one is the case Fallout 4 actually presents.
	class FakeShader
	{
	public:
		FakeShader(std::uint32_t a_slots, std::uint32_t a_used) :
			_object(Shader::BSShaderOffset::kSize, std::uint8_t{ 0 }),
			_entries(
				static_cast<std::size_t>(a_slots) * Shader::ScatterTableOffset::kEntrySize,
				std::uint8_t{ 0 }),
			_techniques(a_used)
		{
			SetHeader(a_slots, a_slots - a_used);

			for (std::uint32_t i = 0; i < a_used; ++i) {
				_techniques[i].id = 100 + i;
				SetSlot(i, &_techniques[i], SentinelAddress());
			}
		}

		[[nodiscard]] const void* Object() const noexcept { return _object.data(); }

		[[nodiscard]] const void* SentinelAddress() const noexcept { return &_sentinel; }

		[[nodiscard]] void* SlotAddress(std::uint32_t a_index) noexcept
		{
			return _entries.data() +
			       static_cast<std::size_t>(a_index) * Shader::ScatterTableOffset::kEntrySize;
		}

		/// Writes the header the map reader reads. The capacity and free count
		/// are given rather than derived, which is what lets a test claim a
		/// size the array does not have.
		void SetHeader(std::uint32_t a_capacity, std::uint32_t a_free) noexcept
		{
			const void* const sentinel = SentinelAddress();
			const void* const entries = _entries.empty() ? nullptr : _entries.data();

			WriteMap(Shader::ScatterTableOffset::kCapacity, &a_capacity, sizeof(a_capacity));
			WriteMap(Shader::ScatterTableOffset::kFree, &a_free, sizeof(a_free));
			WriteMap(Shader::ScatterTableOffset::kGood, &a_capacity, sizeof(a_capacity));
			WriteMap(Shader::ScatterTableOffset::kSentinel, &sentinel, sizeof(sentinel));
			WriteMap(Shader::ScatterTableOffset::kEntries, &entries, sizeof(entries));
		}

		void SetSlot(std::uint32_t a_index, const void* a_value, const void* a_next) noexcept
		{
			auto* const slot = static_cast<std::uint8_t*>(SlotAddress(a_index));
			std::memcpy(slot, &a_value, sizeof(a_value));
			std::memcpy(slot + 8, &a_next, sizeof(a_next));
		}

		void BreakSentinel() noexcept { _sentinel = 0x11223344u; }

	private:
		void WriteMap(std::uintptr_t a_offset, const void* a_bytes, std::size_t a_size) noexcept
		{
			std::memcpy(
				_object.data() + Shader::BSShaderOffset::kVertexShaders + a_offset,
				a_bytes,
				a_size);
		}

		std::vector<std::uint8_t> _object;
		std::vector<std::uint8_t> _entries;
		std::vector<Technique> _techniques;
		std::uint32_t _sentinel{ kSentinel };
	};

	[[nodiscard]] const Shader::CapacityProbe* ProbeFor(
		const Shader::MapDump& a_dump,
		std::uint32_t a_capacity) noexcept
	{
		for (const auto& probe : a_dump.probes) {
			if (probe.capacity == a_capacity) {
				return &probe;
			}
		}

		return nullptr;
	}
}

int main()
{
	// The offsets themselves, because they are the measurement everything else
	// rests on and a stray edit to them is silent.
	Check(Shader::BSShaderOffset::kVertexShaders == 0x098, "the vertex map sits at 0x098");
	Check(Shader::BSShaderOffset::kPixelShaders == 0x128, "the pixel map sits at 0x128");
	Check(Shader::BSShaderOffset::kSize == 0x190, "a BSShader is 0x190 bytes");
	Check(
		Shader::StageMapOffset(Shader::Stage::kDomain) == Shader::BSShaderOffset::kDomainShaders,
		"the domain stage names the domain map");
	Check(Shader::StageName(Shader::Stage::kHull) == "hull", "the hull stage is called hull");

	{
		FakeShader shader{ 8, 3 };
		const auto report = Shader::InspectMap(shader.Object(), Shader::Stage::kVertex);

		Check(report.refusedBecause == nullptr, "a well formed map is not refused");
		Check(report.capacity == 8 && report.free == 5, "the header is read as written");
		Check(report.sentinelReadable && report.sentinelBytes == kSentinel, "the sentinel is found");
		Check(report.techniques.size() == 3, "every used slot is walked");
		Check(
			report.techniques.size() == 3 && report.techniques[0].id == 100 &&
				report.techniques[2].id == 102,
			"the technique ids are read out of the slots");
	}

	{
		// An untouched map. Not a refusal: it is what a stage the shader does
		// not use looks like, and four of the five are that on most shaders.
		FakeShader shader{ 0, 0 };
		const auto report = Shader::InspectMap(shader.Object(), Shader::Stage::kVertex);

		Check(report.refusedBecause == nullptr, "an empty map is not refused");
		Check(report.techniques.empty(), "an empty map yields no techniques");
	}

	{
		FakeShader shader{ 8, 3 };
		shader.BreakSentinel();
		const auto report = Shader::InspectMap(shader.Object(), Shader::Stage::kVertex);

		Check(
			report.refusedBecause != nullptr &&
				std::strcmp(report.refusedBecause, "the sentinel is not DE AD BE EF") == 0,
			"a map without the sentinel is refused for that");
		Check(report.techniques.empty(), "a refused map yields no techniques");
	}

	{
		FakeShader shader{ 8, 3 };
		shader.SetSlot(1, nullptr, shader.SentinelAddress());
		const auto report = Shader::InspectMap(shader.Object(), Shader::Stage::kVertex);

		Check(
			report.refusedBecause != nullptr &&
				std::strcmp(report.refusedBecause, "a used slot holds nothing") == 0,
			"a used slot without a value is refused for that");
	}

	{
		FakeShader shader{ 8, 3 };
		const std::uintptr_t elsewhere = 0x1234;
		shader.SetSlot(1, shader.SlotAddress(0), reinterpret_cast<const void*>(elsewhere));
		const auto report = Shader::InspectMap(shader.Object(), Shader::Stage::kVertex);

		Check(
			report.refusedBecause != nullptr &&
				std::strcmp(report.refusedBecause, "a chain pointer leaves the table") == 0,
			"a chain out of the table is refused for that");
	}

	{
		// The header claims one more used slot than the array has. This is the
		// check that caught a wrong offset in the first place.
		FakeShader shader{ 8, 3 };
		shader.SetHeader(8, 4);
		const auto report = Shader::InspectMap(shader.Object(), Shader::Stage::kVertex);

		Check(
			report.refusedBecause != nullptr &&
				std::strcmp(
					report.refusedBecause,
					"used slots disagree with capacity minus free") == 0,
			"a used count that disagrees with the header is refused for that");
	}

	{
		FakeShader shader{ 8, 3 };
		shader.SetHeader(6, 3);
		const auto report = Shader::InspectMap(shader.Object(), Shader::Stage::kVertex);

		Check(
			report.refusedBecause != nullptr &&
				std::strcmp(report.refusedBecause, "capacity is not a power of two") == 0,
			"a capacity that is not a power of two is refused for that");
	}

	{
		// Fallout 4's case: an array of sixteen slots under a header that says
		// eight. The walk sees chains reaching past its idea of the end and
		// refuses. The dump is what has to name the real size.
		FakeShader shader{ 16, 12 };
		shader.SetHeader(8, 4);
		for (std::uint32_t i = 0; i < 12; ++i) {
			shader.SetSlot(i, shader.SlotAddress(i), shader.SlotAddress(15 - i));
		}

		const auto report = Shader::InspectMap(shader.Object(), Shader::Stage::kVertex);
		Check(report.refusedBecause != nullptr, "a map read at the wrong capacity is refused");

		const auto dump = Shader::DumpMap(shader.Object(), Shader::Stage::kVertex);
		Check(dump.headerReadable, "the dump carries the header out");
		Check(
			dump.headerReadable &&
				std::memcmp(
					dump.header.data() + Shader::ScatterTableOffset::kCapacity,
					"\x08\x00\x00\x00",
					4) == 0,
			"the dumped header holds the bytes the header holds");

		const auto* const claimed = ProbeFor(dump, 8);
		const auto* const real = ProbeFor(dump, 16);

		Check(claimed != nullptr && claimed->chainsAway > 0, "the claimed size shows chains leaving");
		Check(claimed != nullptr && !claimed->agreesWithFree, "the claimed size does not add up");
		Check(real != nullptr && real->chainsAway == 0, "the real size shows no chain leaving");
		Check(real != nullptr && real->used == 12, "the real size finds every used slot");
		Check(real != nullptr && real->agreesWithFree, "the real size adds up with the free count");

		Check(!dump.slots.empty(), "the dump carries slots out");
		Check(
			!dump.slots.empty() && dump.slots.front().index == 0 &&
				dump.slots.front().value == shader.SlotAddress(0),
			"the first dumped slot is the first slot");
	}

	{
		// Nothing to read is answered rather than followed.
		const auto report = Shader::InspectMap(nullptr, Shader::Stage::kVertex);
		Check(report.refusedBecause != nullptr, "a null shader is refused");

		const auto dump = Shader::DumpMap(nullptr, Shader::Stage::kVertex);
		Check(!dump.headerReadable && dump.probes.empty(), "a null shader dumps nothing");
	}

	std::printf("\n%s\n", g_failures == 0 ? "all checks passed" : "checks failed");
	return g_failures == 0 ? 0 : 1;
}
