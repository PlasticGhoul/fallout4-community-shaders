#pragma once

#include <cstddef>
#include <vector>

namespace Util
{
	/// Every eight byte aligned place in the game module's writable data where
	/// a_value is stored, up to a_limit finds.
	///
	/// This exists because commonlibf4 hands out no pointer to several engine
	/// singletons, and waiting for one to walk past through a hook only works
	/// for the ones that run. Given one object we already hold, its slot in the
	/// engine's own table can be found, and the table's neighbours are then the
	/// objects we could not catch.
	///
	/// Reads module memory only, never dereferences what it finds, and is
	/// therefore safe to run at any time. It answers about this moment: nothing
	/// stops the engine writing a different pointer afterwards.
	[[nodiscard]] std::vector<void* const*> FindPointerInModuleData(
		const void* a_value,
		std::size_t a_limit = 8);
}
