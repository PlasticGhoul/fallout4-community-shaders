#pragma once

#include "Feature/Feature.h"

#include <cstdint>
#include <string>

namespace Features
{
	/// NVIDIA DLSS Neural Rendering (DLSS 5) through the D3D12 bridge
	/// (src/Bridge). The bridge evaluates it over the picture taken before the
	/// HUD and composites the result under HUD and overlay; this feature
	/// declares the settings, decides every frame whether NR runs and hands
	/// the values over. Like frame generation it needs the bridge, and so the
	/// FrameGeneration archive's Streamline, but not frame generation itself.
	class NeuralRendering final : public Feature
	{
	public:
		[[nodiscard]] std::string_view Name() const override { return "NeuralRendering"; }
		void Declare() override;
		[[nodiscard]] bool Setup() override;
		void Frame() override;
		void Shutdown() override;

	private:
		bool _captureAcquired{ false };
		std::string _blocker{ "<start>" };
		std::uint64_t _frames{ 0 };
	};
}
