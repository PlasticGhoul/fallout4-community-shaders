#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace Render
{
	/// A vtable of our own for a D3D11 device context.
	///
	/// The D3D11 runtime keeps each context's vtable inside the object, at
	/// offset eight, and rewrites its entries as the pipeline state changes:
	/// Draw, DrawIndexed and DrawIndexedInstanced flip between variants at
	/// every Flush, measured offline on 2026-09-12. An entry patched in that
	/// table survives until the next Flush, and Present flushes - which is why
	/// the first F4 probe counted nothing at all.
	///
	/// What the runtime never touches is the object's vtable pointer, nor a
	/// table it does not own. So this builds a table of its own and points
	/// the object at it. Every slot holds a nine byte forwarder that reads the
	/// runtime's embedded entry at call time and jumps there, so the variant
	/// switching keeps working underneath; the slots a caller overrides hold
	/// its own functions instead, and those chain through EmbeddedTableOf the
	/// same way. Pure tail calls: this and every argument arrive untouched.
	///
	/// Installed once, for the life of the process, like every other patch of
	/// the port. Testable on the host against a fake object that has an
	/// embedded table of its own.
	class ContextTable
	{
	public:
		/// ID3D11DeviceContext4 ends near 147; the runtime's table is read to
		/// here and no further, well inside the object.
		static constexpr std::size_t kSlots = 160;
		static constexpr std::size_t kEmbeddedOffset = 8;

		using Override = std::pair<std::size_t, void*>;

		ContextTable() = default;
		ContextTable(const ContextTable&) = delete;
		ContextTable& operator=(const ContextTable&) = delete;

		/// Whether a_object's first word points at offset eight - the layout
		/// this was measured against. Asked before Install; afterwards the
		/// first word points at our table, and this is false by design.
		[[nodiscard]] static bool HasEmbeddedTable(void* a_object) noexcept;

		/// The table the runtime keeps inside a_object, at offset eight,
		/// whether or not the object has been pointed elsewhere since. An
		/// override chains through this to reach the runtime's current entry.
		[[nodiscard]] static void** EmbeddedTableOf(void* a_object) noexcept;

		/// Builds the forwarders, applies a_overrides, points a_object at the
		/// new table. False, with nothing changed, when the object is not
		/// laid out as expected, when a slot is out of range, or when memory
		/// cannot be made executable.
		[[nodiscard]] bool Install(void* a_object, std::span<const Override> a_overrides) noexcept;

		[[nodiscard]] bool Installed() const noexcept { return _object != nullptr; }
		[[nodiscard]] void** Table() const noexcept { return _table; }

	private:
		void* _object{ nullptr };
		void** _table{ nullptr };
		std::uint8_t* _code{ nullptr };
	};
}
