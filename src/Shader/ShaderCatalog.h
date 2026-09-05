#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace Shader
{
	/// One of the thirteen BSShader classes Fallout 4 runs, paired with the
	/// BSShaderManager::ShaderEnum value it is expected to report.
	///
	/// The pairing is an assumption drawn from the names, and the census tests
	/// it: a class whose object reads a different shaderType is reported as a
	/// mismatch rather than silently believed.
	struct ShaderClass
	{
		/// As the object's own RTTI spells it.
		std::string_view className;

		/// The BSShaderManager::ShaderEnum enumerator we expect it to be.
		std::string_view enumerator;

		/// The value that enumerator has.
		std::int32_t shaderType;

		/// RE::VTABLE::<class>[0], resolved. Zero if the address library does
		/// not know the id, which on AE 1.11.240 happens for none of them -
		/// all fourteen were checked against version-1-11-240-0.bin before this
		/// table was written, because REL::ID::offset ends the process on an
		/// id it cannot resolve.
		std::uintptr_t vtable;
	};

	/// The table, with every vtable address resolved on first use.
	///
	/// Not resolved at static initialisation: the address library is not loaded
	/// until F4SE hands us kGameDataReady, and asking earlier would resolve
	/// against nothing.
	[[nodiscard]] std::span<const ShaderClass> ShaderClasses() noexcept;

	/// Logs what one shader object holds: its identity cross-check, the fxp
	/// name, and per stage the number of techniques with their ids.
	///
	/// a_withNames additionally calls the engine's own GetTechniqueName,
	/// vtable slot 09. That is a call into the game rather than a read of it,
	/// so callers pass true only for an object whose class was confirmed - the
	/// slot numbering is commonlibf4's, and a wrong index would call something
	/// else with these arguments.
	void ReportShaderTechniques(
		const ShaderClass& a_class,
		const void* a_shader,
		bool a_withNames) noexcept;
}
