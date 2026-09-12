#include "Render/ContextTable.h"

#include <REX/W32/KERNEL32.h>

#include <cstring>

namespace Render
{
	namespace
	{
		/// mov rax, qword ptr [rcx + disp32] ; jmp rax
		///
		/// rcx is this on x64. The displacement is the embedded table's offset
		/// plus the slot's, so the forwarder reads the runtime's current entry
		/// for its own slot and jumps there with every register as it came.
		constexpr std::size_t kForwarderBytes = 9;
		constexpr std::size_t kForwarderStride = 16;

		void WriteForwarder(std::uint8_t* a_at, std::size_t a_slot) noexcept
		{
			const auto displacement = static_cast<std::uint32_t>(ContextTable::kEmbeddedOffset + a_slot * sizeof(void*));
			a_at[0] = 0x48;
			a_at[1] = 0x8B;
			a_at[2] = 0x81;
			std::memcpy(a_at + 3, std::addressof(displacement), sizeof(displacement));
			a_at[7] = 0xFF;
			a_at[8] = 0xE0;
		}
	}

	void** ContextTable::EmbeddedTableOf(void* a_object) noexcept
	{
		return reinterpret_cast<void**>(static_cast<std::uint8_t*>(a_object) + kEmbeddedOffset);
	}

	bool ContextTable::HasEmbeddedTable(void* a_object) noexcept
	{
		return a_object != nullptr && *reinterpret_cast<void***>(a_object) == EmbeddedTableOf(a_object);
	}

	bool ContextTable::Install(void* a_object, std::span<const Override> a_overrides) noexcept
	{
		if (Installed() || !HasEmbeddedTable(a_object)) {
			return false;
		}
		for (const auto& [slot, function] : a_overrides) {
			if (slot >= kSlots || function == nullptr) {
				return false;
			}
		}

		// Never freed: the object points here for the rest of the process,
		// and a thread may be inside a forwarder at any moment.
		constexpr auto codeBytes = kSlots * kForwarderStride;
		auto* const code = static_cast<std::uint8_t*>(REX::W32::VirtualAlloc(
			nullptr, codeBytes, REX::W32::MEM_COMMIT | REX::W32::MEM_RESERVE, REX::W32::PAGE_READWRITE));
		auto* const table = static_cast<void**>(REX::W32::VirtualAlloc(
			nullptr, kSlots * sizeof(void*), REX::W32::MEM_COMMIT | REX::W32::MEM_RESERVE, REX::W32::PAGE_READWRITE));
		if (code == nullptr || table == nullptr) {
			return false;
		}

		std::memset(code, 0xCC, codeBytes);
		for (std::size_t slot = 0; slot < kSlots; ++slot) {
			WriteForwarder(code + slot * kForwarderStride, slot);
			table[slot] = code + slot * kForwarderStride;
		}
		for (const auto& [slot, function] : a_overrides) {
			table[slot] = function;
		}

		std::uint32_t previous = 0;
		if (!REX::W32::VirtualProtect(code, codeBytes, REX::W32::PAGE_EXECUTE_READ, std::addressof(previous))) {
			return false;
		}
		static_cast<void>(REX::W32::FlushInstructionCache(REX::W32::GetCurrentProcess(), code, codeBytes));

		// One aligned pointer write. A thread mid-call keeps the table it
		// read; both tables are valid at every moment.
		*reinterpret_cast<void***>(a_object) = table;

		_object = a_object;
		_table = table;
		_code = code;
		return true;
	}
}
