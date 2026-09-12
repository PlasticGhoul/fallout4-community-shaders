#pragma once

#include "Feature/Feature.h"

namespace Features
{
	/// One frame of the engine as a list, on a key press.
	///
	/// A measurement, not an effect, and the sibling of ShaderCensus: the
	/// census answered which techniques exist, this answers in what order the
	/// twelve shader classes run inside a frame and what each had bound. That
	/// is the question every screen space feature asks before it picks its
	/// anchor, so it is a permanent feature rather than a probe written four
	/// times over.
	///
	/// Slot 02 of each class's vtable is patched once and never restored - see
	/// FramePhase for why a vtable entry is not taken back in a running game.
	/// Disarmed, the thunk costs one atomic load. Armed by the key, it records
	/// the next whole frame between two Presents into a fixed field of raw
	/// pointers, and the frame after that resolves the names and writes the
	/// block.
	class FrameTrace final : public Feature
	{
	public:
		[[nodiscard]] std::string_view Name() const override { return "FrameTrace"; }
		void Declare() override;
		[[nodiscard]] bool Setup() override;
		void Frame() override;
		void Shutdown() override;
	};
}
