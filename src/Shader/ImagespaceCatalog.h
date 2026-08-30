#pragma once

#include <RE/B/BSGraphics.h>

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
		///
		/// Deliberately a void*: commonlibf4's RE::BSShader describes an object
		/// Fallout 4 does not have, so its fields are reached through the
		/// measured offsets in BSShaderLayout instead.
		void* shader{ nullptr };

		/// The engine's own name for the shader package, e.g. "ISCopy".
		const char* fxpFilename{ nullptr };

		/// The single technique entry, when the pass has exactly one pixel
		/// shader. Null when it has none or more than one - we only replace
		/// what we can name unambiguously.
		RE::BSGraphics::PixelShader* slot{ nullptr };

		std::uint32_t techniqueID{ 0 };
	};

	/// Walks the effect list, proves each entry, and logs what it found. Writes
	/// nothing to the engine.
	///
	/// The catalog is run repeatedly until it yields something, because the
	/// engine fills its technique maps long after the effect list exists.
	/// a_verbose therefore governs the table: printed on the first run and on
	/// the run that succeeds, suppressed on the attempts in between.
	[[nodiscard]] std::vector<ImagespacePass> RunImagespaceCatalog(bool a_verbose);
}
