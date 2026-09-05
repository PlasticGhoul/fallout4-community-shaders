#pragma once

#include "Render/PassStatistics.h"

#include <span>

namespace Menu
{
	/// What the panel needs from around it, handed in rather than reached for.
	/// The panel draws; it reads no setting and knows no D3D.
	struct PerformanceContext
	{
		std::span<const Render::PassResult> passes;

		float frameGpuMs{ 0.0f };
		float frameCpuMs{ 0.0f };

		bool measuring{ false };
		bool hud{ false };

		/// 0 top left, 1 top right, 2 bottom left, 3 bottom right.
		int corner{ 1 };
	};

	enum class Detail
	{
		/// While the overlay is closed: four numbers, no decoration, no input.
		kCompact,

		/// While the overlay is open: the whole table with its history.
		kFull
	};

	/// Returns whether anything was drawn, which is what tells the overlay
	/// whether it has draw data worth handing to the backend.
	bool DrawPerformancePanel(const PerformanceContext& a_context, Detail a_detail);
}
