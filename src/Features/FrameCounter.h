#pragma once

#include "Feature/Feature.h"

#include <cstdint>

namespace Features
{
	/// Counts frames while it runs, and says so when it starts and stops.
	///
	/// It exists to make independence visible: toggling it must not disturb
	/// whatever else is running, and its counter starting from zero again is
	/// the evidence that Shutdown actually ran.
	class FrameCounter : public Feature
	{
	public:
		[[nodiscard]] std::string_view Name() const override { return "FrameCounter"; }
		[[nodiscard]] bool Setup() override;
		void Frame() override;
		void Shutdown() override;

	private:
		static constexpr std::uint64_t kReportInterval = 600;

		std::uint64_t _frames{ 0 };
	};
}
