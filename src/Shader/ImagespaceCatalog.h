#pragma once

#include <RE/B/BSGraphics.h>
#include <RE/B/BSShader.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Shader
{
	/// One image space pass, as found in ImageSpaceManager::effectList and
	/// proven to be what it looks like.
	struct ImagespacePass
	{
		/// Read from the object's own RTTI, e.g. "BSImagespaceShaderCopy".
		std::string className;

		/// The BSShader subobject, already corrected for multiple inheritance.
		RE::BSShader* shader{ nullptr };

		/// The single technique entry, when the pass has exactly one pixel
		/// shader. Null when it has none or more than one - we only replace
		/// what we can name unambiguously.
		RE::BSGraphics::PixelShader* slot{ nullptr };

		std::uint32_t techniqueID{ 0 };
	};

	/// Walks the effect list, proves each entry, and logs a table of what it
	/// found. Writes nothing to the engine.
	[[nodiscard]] std::vector<ImagespacePass> RunImagespaceCatalog();
}
