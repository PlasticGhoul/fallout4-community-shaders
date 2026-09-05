#pragma once

#include "Feature/Feature.h"

#include <cstdint>

namespace Features
{
	/// Counts the techniques of Fallout 4's thirteen BSShader classes and
	/// reports what the three deferred passes have bound while they run.
	///
	/// A measurement, not an effect. It exists to answer the one question that
	/// decides the order of the remaining subprojects: whether the technique
	/// map pointer swap that subproject C proved on image space passes can
	/// reach kDFLight, kDFComposite and kLighting as well. An image space pass
	/// carries exactly one technique with id 0, which is why the swap was
	/// unambiguous there; nobody has counted the others.
	///
	/// commonlibf4 hands out no pointer to those thirteen objects, so the
	/// census patches slot 02 of each class's vtable - SetupTechnique - keeps
	/// the first this it is handed, and puts the entry back once it has
	/// reported. The address library knows every one of the fourteen vtable
	/// ids on AE 1.11.240; that was checked against version-1-11-240-0.bin
	/// before a line of this was written, because an unknown id ends the
	/// process rather than failing.
	class ShaderCensus final : public Feature
	{
	public:
		[[nodiscard]] std::string_view Name() const override { return "ShaderCensus"sv; }

		void Declare() override;
		[[nodiscard]] bool Setup() override;
		void Frame() override;
		void Shutdown() override;

	private:
		/// Reports and then unhooks, whether or not every class has been seen.
		/// Fifteen seconds at sixty frames: a class such as
		/// BSFaceCustomizationShader may not run at all in a walking session,
		/// and waiting for it would mean never reporting.
		static constexpr std::uint64_t kSettleFrames = 900;

		std::uint64_t _frames{ 0 };
		bool _reported{ false };
	};
}
