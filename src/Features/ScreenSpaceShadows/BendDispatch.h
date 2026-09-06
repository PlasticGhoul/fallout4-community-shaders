#pragma once

namespace Features::Bend
{
	/// One compute dispatch of the Bend sweep.
	struct Dispatch
	{
		int waveCount[3]{ 0, 0, 0 };
		int waveOffset[2]{ 0, 0 };
	};

	/// What one frame's screen space shadow costs in dispatches.
	///
	/// Eight is the capacity of Bend's own list. Typical is one or two with the
	/// sun off screen and four to six with it on screen.
	struct DispatchPlan
	{
		float lightCoordinate[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
		Dispatch dispatches[8]{};
		int count{ 0 };
	};

	/// Wraps Bend::BuildDispatchList.
	///
	/// The wrapper exists because bend_sss_cpu.h defines its function in the
	/// header without inline: included from two translation units - the test
	/// and the feature - it would be a duplicate symbol. It is included from
	/// BendDispatch.cpp and nowhere else.
	///
	/// a_lightProjection is the light direction transformed by the view
	/// projection, without the w divide: float4(-direction, 0) for the sun.
	/// The render bounds are always the whole viewport, so they are not a
	/// parameter of this.
	[[nodiscard]] DispatchPlan BuildPlan(
		const float a_lightProjection[4],
		const int a_viewportSize[2]);
}
