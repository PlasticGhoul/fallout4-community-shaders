#pragma once

#include "Bridge/Bridge.h"
#include "Feature/Feature.h"

#include <cstdint>
#include <string>

namespace Features
{
	/// NVIDIA DLSS Frame Generation through the D3D12 bridge (src/Bridge).
	/// Whether the bridge exists was decided when the game created its device;
	/// this feature switches generation on and off within it and asks
	/// Bridge::Capture for what DLSS-G needs from the engine's frame - depth,
	/// motion vectors, the picture before the HUD, the camera. It owns nothing
	/// of the engine's and changes nothing in it: it only reads.
	class FrameGeneration final : public Feature
	{
	public:
		[[nodiscard]] std::string_view Name() const override { return "FrameGeneration"; }
		void Declare() override;
		[[nodiscard]] bool Setup() override;
		void Frame() override;
		void Shutdown() override;

	private:
		/// Why generation is off this frame; nullptr when nothing stands in the way.
		[[nodiscard]] const char* Blocker() const noexcept;

		bool _captureAcquired{ false };
		bool _generating{ false };
		std::string _blocker{ "<start>" };

		// For the once-a-second line.
		std::uint64_t _frames{ 0 };
	};
}
