#pragma once

#include <array>
#include <optional>

namespace Render
{
	/// Whether a matrix could be a view projection at all: every entry finite,
	/// and not the whole thing zero.
	///
	/// Deliberately no cleverer than that. A heuristic that guessed at the
	/// shape of a projection would sooner or later refuse a valid one, and
	/// refusing a valid matrix costs a feature for the session while accepting
	/// an odd one costs a frame.
	[[nodiscard]] bool IsPlausibleViewProjection(const float (&a_matrix)[16]) noexcept;

	/// The current camera's view projection, row major, or nothing.
	///
	/// Read from BSGraphics::State::GetSingleton()->cameraState, which is a
	/// **value** member of the singleton: no pointer supplied by the engine is
	/// dereferenced on the way.
	///
	/// That is the whole point of taking this route.
	/// BSGraphics::RendererData::shadowState is declared as a pointer at offset
	/// zero and is not one in Fallout 4 AE 1.11.240 - it read as 0x1B70, which
	/// is the offset of RendererShadowState inside BSGraphics::Context, and
	/// following it crashed the game at 0x1B70 + 0x7B0. commonlibf4 describes
	/// that field the way it describes BSShader's: plausibly, and wrongly.
	///
	/// The first successful read cross-checks the singleton against the swap
	/// chain and logs what it found, the same safety net B1 put in front of the
	/// device: a wrong pointer that happens to be readable is far worse than a
	/// null one.
	[[nodiscard]] std::optional<std::array<float, 16>> ViewProjection() noexcept;
}
