#pragma once

#include "Render/PhaseDispatcher.h"

#include <cstdint>
#include <functional>
#include <string_view>

namespace Render
{
	/// The points inside a frame where a screen space feature does its work.
	///
	/// Each is reached by replacing slot 02 - SetupTechnique - in the main
	/// vtable of one BSShader class. The first call of that class in each
	/// frame is the moment; every later call in the same frame passes straight
	/// through.
	enum class Phase : std::uint8_t
	{
		/// BSDFCompositeShader. The G-buffer is complete and kDFLight has
		/// written the direct light to RT_058/059, but the composite has not
		/// drawn. Where a feature modulates the lighting - F2.
		kBeforeComposite,

		/// The first class after composite and sky. The opaque scene is
		/// finished in the HDR target; transparents have not drawn. Where a
		/// feature draws onto the picture - F3. Which class that is was
		/// measured by FrameTrace; see the table in FramePhase.cpp.
		kAfterOpaque,

		kCount
	};

	/// Installs every phase's patch. Once, for the life of the process, like
	/// the Present hook from B1: the overlay can switch a feature off in the
	/// middle of a running game, and taking a vtable entry back while another
	/// thread stands in it is a race nothing can win. Features subscribe and
	/// unsubscribe instead.
	///
	/// **Install it before any feature runs.** ShaderCensus patches the same
	/// slots while it counts and restores them as soon as a class has
	/// reported. Underneath it, our thunk is what its Restore writes back;
	/// on top of it, our thunk is what its Restore overwrites. This runs from
	/// kGameDataReady, a feature's Setup runs from Present, which is later.
	///
	/// Returns true when at least kBeforeComposite is in place; a phase that
	/// could not be patched says so in the log and its subscribers never run.
	[[nodiscard]] bool InstallFramePhase() noexcept;

	/// The callback runs inside a Render::PassScope named after a_name, so it
	/// is measured by F1 and named in a capture without the caller doing
	/// anything about it. Returns PhaseDispatcher::kNoToken when the cap is
	/// reached or the phase is out of range.
	///
	/// **The name must not be the feature's own.** The profiler keys its rows
	/// by name alone, and the registry already measures every feature's Frame
	/// under that name from Present. A phase subscribed under the same one
	/// lands in the same row, where the two samples a frame average against
	/// each other: F2's first snapshot showed the draw at half its cost, with
	/// a p95 of exactly twice the mean. "<Feature>/Draw" is the convention.
	PhaseDispatcher::Token SubscribeFramePhase(
		Phase a_phase,
		std::string_view a_name,
		std::function<void()> a_callback);

	void UnsubscribeFramePhase(Phase a_phase, PhaseDispatcher::Token a_token) noexcept;

	/// How often the phase has fired. A number that stays put while the game
	/// renders and something is subscribed means the entry has been
	/// overwritten - see the ordering note above. It is the only symptom.
	[[nodiscard]] std::uint64_t FramePhaseHits(Phase a_phase) noexcept;

	/// The technique id of the call that fired the phase most recently. The
	/// anchor is "the first call of this class in the frame", and which
	/// technique that is was measured in two frames; a feature that logs this
	/// once a second finds out whether it holds in every frame.
	[[nodiscard]] std::uint32_t FramePhaseLastTechnique(Phase a_phase) noexcept;
}
