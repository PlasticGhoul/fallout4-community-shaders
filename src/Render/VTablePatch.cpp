#include "Render/VTablePatch.h"

#include <Windows.h>

namespace Render
{
	namespace
	{
		// Lifts the page protection, performs the write, puts the protection
		// back. A failure to restore the protection is reported rather than
		// swallowed: the write has already happened at that point, and a caller
		// that believes the patch failed would be wrong about the world.
		template <class F>
		bool WithWritableSlot(void** a_slot, F&& a_write) noexcept
		{
			DWORD previous = 0;
			if (::VirtualProtect(a_slot, sizeof(void*), PAGE_READWRITE, &previous) == 0) {
				return false;
			}

			std::forward<F>(a_write)();

			DWORD ignored = 0;
			return ::VirtualProtect(a_slot, sizeof(void*), previous, &ignored) != 0;
		}
	}

	bool VTablePatch::Install(void* a_object, std::size_t a_index, void* a_replacement) noexcept
	{
		if (a_object == nullptr || a_replacement == nullptr || Installed()) {
			return false;
		}

		auto* const vtable = *reinterpret_cast<void***>(a_object);
		if (vtable == nullptr) {
			return false;
		}

		void** const slot = vtable + a_index;
		void* const original = *slot;

		if (!WithWritableSlot(slot, [&]() noexcept { *slot = a_replacement; })) {
			return false;
		}

		m_slot = slot;
		m_original = original;
		return true;
	}

	bool VTablePatch::Restore() noexcept
	{
		if (!Installed()) {
			return false;
		}

		void** const slot = m_slot;
		void* const original = m_original;

		if (!WithWritableSlot(slot, [&]() noexcept { *slot = original; })) {
			return false;
		}

		m_slot = nullptr;
		m_original = nullptr;
		return true;
	}
}
