#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace Util
{
	/// What MSVC's run time type information says about an object.
	struct ObjectInfo
	{
		/// The undecorated class name, e.g. "BSLightingShader".
		std::string className;

		/// Where the pointer we asked about sits inside the complete object.
		/// Zero for a single inheritance class, non-zero for a subobject such
		/// as the ImageSpaceEffect half of an image space shader.
		std::uint32_t subobjectOffset{ 0 };
	};

	/// Reads class name and subobject offset out of the object itself.
	///
	/// Deliberately not a comparison against RE::VTABLE ids: REL::ID::offset
	/// calls REX::FAIL for an id the address library does not know, which ends
	/// the process. Blindly resolving a table of ids is therefore a crash risk,
	/// and an unnecessary one - the compiler already wrote both answers into
	/// the binary, in front of every polymorphic vtable.
	///
	/// Returns nothing for a null pointer, for a locator that fails its own
	/// signature check, and for one whose recomputed module base is not the
	/// game's - which is what keeps a wild pointer from being read as a class.
	[[nodiscard]] std::optional<ObjectInfo> DescribeObject(const void* a_object) noexcept;

	/// The same, for a table rather than an object.
	///
	/// This is what makes a vtable id from the address library checkable before
	/// it is used: patching an entry of a table that turns out to belong to
	/// another class would be found out at the first call, which is a crash.
	/// The locator sits in front of the table either way, so the answer is
	/// available beforehand.
	[[nodiscard]] std::optional<ObjectInfo> DescribeVTable(const void* const* a_vtable) noexcept;
}
