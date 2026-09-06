#pragma once

#include "Render/PhaseDispatcher.h"

#include <cstdint>
#include <functional>
#include <string_view>

namespace Render
{
	/// The one point inside a frame where the G-buffer is complete and the
	/// lighting has been written, but the composite has not drawn yet.
	///
	/// It is reached by replacing slot 02 - SetupTechnique - in the main vtable
	/// of BSDFCompositeShader. The first call of each frame is the moment we
	/// want; every later call in the same frame passes straight through.
	///
	/// The patch is installed once and stays for the life of the process, like
	/// the Present hook from B1. It is deliberately not owned by a feature: the
	/// overlay can switch a feature off in the middle of a running game, and
	/// taking a vtable entry back while another thread stands in it is a race
	/// nothing can win. Features subscribe and unsubscribe instead.
	///
	/// **Install it before any feature runs.** ShaderCensus patches the same
	/// slot of the same class while it counts, and restores it as soon as that
	/// class has reported. Underneath it, our thunk is what its Restore writes
	/// back, and the chain survives; on top of it, our thunk is what its
	/// Restore overwrites, and the phase goes silent with nothing to see. The
	/// ordering is what keeps that from happening: this runs from
	/// kGameDataReady, a feature's Setup runs from Present, which is later.
	[[nodiscard]] bool InstallFramePhase() noexcept;

	/// The callback runs inside a Render::PassScope named after a_name, so it
	/// is measured by F1 and named in a capture without the caller doing
	/// anything about it. Returns PhaseDispatcher::kNoToken when the cap is
	/// reached.
	PhaseDispatcher::Token SubscribeFramePhase(
		std::string_view a_name,
		std::function<void()> a_callback);

	void UnsubscribeFramePhase(PhaseDispatcher::Token a_token) noexcept;

	/// How often the phase has fired. A number that stays put while the game
	/// renders and something is subscribed means the entry has been overwritten
	/// - see the ordering note above. It is the only symptom there is.
	[[nodiscard]] std::uint64_t FramePhaseHits() noexcept;
}
