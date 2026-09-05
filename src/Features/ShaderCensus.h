#pragma once

#include "Feature/Feature.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Shader
{
	struct ShaderClass;
}

namespace Features
{
	/// Counts the techniques of Fallout 4's thirteen BSShader classes and
	/// reports what each had bound while it ran.
	///
	/// A measurement, not an effect. It exists to answer the one question that
	/// decides the order of the remaining subprojects: whether the technique
	/// map pointer swap that subproject C proved on image space passes can
	/// reach kDFLight, kDFComposite and kLighting as well. An image space pass
	/// carries exactly one technique with id 0, which is why the swap was
	/// unambiguous there.
	///
	/// commonlibf4 hands out no pointer to those thirteen objects, so the
	/// census patches slot 02 of each class's vtable - SetupTechnique - keeps
	/// the first this it is handed, and puts that class's entry back once it
	/// has reported on it. The address library knows every one of the fourteen
	/// vtable ids on AE 1.11.240; that was checked against
	/// version-1-11-240-0.bin before a line of this was written, because an
	/// unknown id ends the process rather than failing.
	///
	/// Each class reports on its own, when its maps have stopped growing.
	/// A single deadline for all thirteen does not work: the first run of this
	/// reported four seconds after startup, which on a machine holding 180 fps
	/// was still the main menu, and eleven classes had simply not run yet.
	/// Waiting for a fixed longer time would only move the guess.
	class ShaderCensus final : public Feature
	{
	public:
		[[nodiscard]] std::string_view Name() const override { return "ShaderCensus"sv; }

		void Declare() override;
		[[nodiscard]] bool Setup() override;
		void Frame() override;
		void Shutdown() override;

	private:
		/// Logs one class in full, with the identity cross-check that gates
		/// asking the engine for technique names.
		void ReportOne(std::size_t a_index, const Shader::ShaderClass& a_class) noexcept;

		/// How often the technique maps are counted. Walking five scatter
		/// tables for thirteen classes is not free, and nothing about this
		/// needs to happen every frame.
		static constexpr std::uint64_t kPollInterval = 60;

		/// Consecutive polls with an unchanged count before a class is
		/// considered settled. The engine fills its maps lazily, so a count
		/// taken at first sighting is nearly always short.
		static constexpr std::uint32_t kStablePolls = 3;

		/// Reported at the latest this many frames after first sighting, even
		/// if the count is still moving or has stayed at zero. Nothing is worth
		/// losing to a class that never settles.
		static constexpr std::uint64_t kPatienceFrames = 3600;

		/// How often to say what is still missing, and to report a class whose
		/// counts have grown since it was reported.
		static constexpr std::uint64_t kProgressInterval = 7200;

		struct ClassState
		{
			std::uint64_t firstSeenFrame{ 0 };
			std::size_t lastCount{ 0 };
			std::uint32_t stablePolls{ 0 };
			bool reported{ false };
		};

		std::uint64_t _frames{ 0 };
		bool _complete{ false };
		std::array<ClassState, 13> _state{};
	};
}
