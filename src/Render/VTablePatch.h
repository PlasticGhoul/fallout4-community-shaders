#pragma once

namespace Render
{
	/// Replaces a single entry in a COM object's virtual function table.
	///
	/// Deliberately knows nothing about D3D or the engine: the pointer
	/// arithmetic and the page-protection dance are the risky part of the
	/// Present hook, and keeping them here is what makes them testable on the
	/// host against a synthetic vtable.
	class VTablePatch
	{
	public:
		VTablePatch() = default;

		VTablePatch(const VTablePatch&) = delete;
		VTablePatch& operator=(const VTablePatch&) = delete;

		/// Replaces slot a_index of a_object's vtable and remembers what was
		/// there. Returns false and changes nothing when the object or the
		/// replacement is null, when a patch is already active, or when the page
		/// protection cannot be lifted.
		[[nodiscard]] bool Install(void* a_object, std::size_t a_index, void* a_replacement) noexcept;

		/// The same, for a table known by its own address rather than through
		/// an instance. That is the engine's case: the address library names
		/// the vtable of a class, and the first instance is what we are trying
		/// to find - so there is no object to dereference yet.
		///
		/// Patching the table reaches every instance of the class at once,
		/// including ones created afterwards, which is what makes it a way to
		/// catch a singleton the game never hands out.
		[[nodiscard]] bool InstallAtTable(
			void** a_vtable,
			std::size_t a_index,
			void* a_replacement) noexcept;

		/// Puts the remembered entry back. Returns false when nothing is
		/// installed or the page protection cannot be lifted.
		bool Restore() noexcept;

		[[nodiscard]] bool Installed() const noexcept { return m_slot != nullptr; }
		[[nodiscard]] void* Original() const noexcept { return m_original; }

	private:
		void** m_slot{ nullptr };
		void* m_original{ nullptr };
	};
}
