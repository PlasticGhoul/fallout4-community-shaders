#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Render
{
	/// Which shader class most recently set up which technique, on the render
	/// thread. The engine calls SetupTechnique once per technique and then
	/// issues the draws of every geometry using it, so between two calls this
	/// names the technique the draws belong to.
	struct CurrentTechnique
	{
		std::size_t classIndex{ 0 };
		std::uint32_t technique{ 0 };
		bool valid{ false };
	};

	/// The classes Shader::ShaderClasses lists, in its order.
	inline constexpr std::size_t kTrackedClasses = 13;

	/// Patches slot 02 of every class once, for the life of the process, with
	/// the same checks FramePhase makes and for the same reason it never takes
	/// a patch back. Returns true when at least one class is watched.
	///
	/// Install after FramePhase and before the Present hook: ShaderCensus
	/// patches the same slots while it counts and restores what it found
	/// underneath, which has to be ours.
	[[nodiscard]] bool InstallTechniqueTracker() noexcept;

	/// From the render thread. Invalid until the first SetupTechnique.
	[[nodiscard]] CurrentTechnique CurrentTechniqueOf() noexcept;

	/// The index Shader::ShaderClasses gives a class, by its RTTI name.
	[[nodiscard]] std::optional<std::size_t> ClassIndexOf(std::string_view a_className) noexcept;
}
