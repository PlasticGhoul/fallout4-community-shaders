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

		/// Frames per second Streamline actually presented, generated ones
		/// included; shown only while frame generation runs.
		float shownFps{ 0.0f };
		bool generating{ false };

		bool measuring{ false };
		bool hud{ false };

		/// 0 top left, 1 top right, 2 bottom left, 3 bottom right.
		int corner{ 1 };
	};

	/// The table and the frame history, drawn into whatever window is
	/// current. The performance page of the overlay embeds it; the compact
	/// display in the corner does not.
	void DrawPerformanceTable(const PerformanceContext& a_context);

	/// The small display in the corner while the overlay is closed: four
	/// numbers, no decoration, no input. Returns whether anything was drawn,
	/// which is what tells the overlay whether it has draw data worth handing
	/// to the backend.
	bool DrawPerformanceHud(const PerformanceContext& a_context);
}
