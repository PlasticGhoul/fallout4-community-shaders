#include "Menu/PerformancePanel.h"

#include "I18n/I18n.h"

#include <imgui.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Menu
{
	namespace
	{
		constexpr float kMargin = 10.0f;

		/// The numbers are refreshed four times a second, not once per frame.
		/// At 180 fps the digits change faster than anyone can read them, and a
		/// number nobody can read is not a measurement.
		constexpr double kRefreshSeconds = 0.25;

		/// One row as it is shown, without the history pointer: a retire can
		/// free that between two refreshes, and this outlives the frame it was
		/// copied in.
		struct ShownPass
		{
			std::string name;
			std::uint32_t depth{ 0 };
			float gpuMs{ 0.0f };
			float avgMs{ 0.0f };
			float p95Ms{ 0.0f };
			float p99Ms{ 0.0f };
		};

		/// Held between frames on purpose, and safe to hold: everything here
		/// runs on the render thread inside Present, one call at a time.
		struct Shown
		{
			double taken{ -1.0 };
			std::vector<ShownPass> passes;
			float fps{ 0.0f };
			float frameMs{ 0.0f };
			float cpuMs{ 0.0f };
		};

		Shown g_shown;

		/// The frame is the pass at depth zero. Looked up rather than assumed to
		/// be the first row, because a retire can reorder them.
		[[nodiscard]] const Render::PassResult* FindFrame(const PerformanceContext& a_context)
		{
			for (const auto& pass : a_context.passes) {
				if (pass.depth == 0) {
					return std::addressof(pass);
				}
			}

			return nullptr;
		}

		void RefreshIfDue(const PerformanceContext& a_context)
		{
			const double now = ImGui::GetTime();
			if (g_shown.taken >= 0.0 && now - g_shown.taken < kRefreshSeconds) {
				return;
			}

			g_shown.taken = now;
			g_shown.passes.clear();
			g_shown.passes.reserve(a_context.passes.size());

			for (const auto& pass : a_context.passes) {
				g_shown.passes.push_back(
					ShownPass{ pass.name, pass.depth, pass.gpuMs, pass.avgMs, pass.p95Ms, pass.p99Ms });
			}

			// Averages rather than the last sample: a value caught four times a
			// second would otherwise be whichever frame the clock landed on.
			const auto* const frame = FindFrame(a_context);
			g_shown.frameMs = frame != nullptr ? frame->avgMs : 0.0f;
			g_shown.cpuMs = frame != nullptr ? frame->cpuAvgMs : 0.0f;
			g_shown.fps = g_shown.frameMs > 0.0f ? 1000.0f / g_shown.frameMs : 0.0f;
		}

		/// Bright enough to read over a bright sky, and this is the one place
		/// the panel picks a colour rather than taking the theme's.
		constexpr ImVec4 kHudColour{ 1.0f, 1.0f, 1.0f, 1.0f };

		void PlaceInCorner(int a_corner)
		{
			const auto* const viewport = ImGui::GetMainViewport();
			const auto work = viewport->WorkPos;
			const auto size = viewport->WorkSize;

			const bool right = a_corner == 1 || a_corner == 3;
			const bool bottom = a_corner == 2 || a_corner == 3;

			const ImVec2 position{
				work.x + (right ? size.x - kMargin : kMargin),
				work.y + (bottom ? size.y - kMargin : kMargin)
			};

			const ImVec2 pivot{ right ? 1.0f : 0.0f, bottom ? 1.0f : 0.0f };

			ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
			ImGui::SetNextWindowBgAlpha(0.55f);
		}

		void DrawTable()
		{
			constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
			if (!ImGui::BeginTable("passes", 5, flags)) {
				return;
			}

			ImGui::TableSetupColumn(T("performance.pass", "Pass"), ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn(T("performance.ms", "ms"));
			ImGui::TableSetupColumn(T("performance.avg", "avg"));
			ImGui::TableSetupColumn(T("performance.p95", "p95"));
			ImGui::TableSetupColumn(T("performance.p99", "p99"));
			ImGui::TableHeadersRow();

			for (const auto& pass : g_shown.passes) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();

				// Indented rather than a tree: the shape is fixed per frame and
				// nothing here is worth collapsing.
				const std::string indented =
					std::string(static_cast<std::size_t>(pass.depth) * 4, ' ') + pass.name;
				ImGui::TextUnformatted(indented.c_str());

				for (const float value : { pass.gpuMs, pass.avgMs, pass.p95Ms, pass.p99Ms }) {
					ImGui::TableNextColumn();
					ImGui::Text("%.3f", static_cast<double>(value));
				}
			}

			ImGui::EndTable();
		}

		void DrawHistory(const PerformanceContext& a_context)
		{
			if (a_context.passes.empty() || a_context.passes.front().history == nullptr) {
				return;
			}

			const auto& history = *a_context.passes.front().history;
			const auto count = history.Count();
			if (count == 0) {
				return;
			}

			// Copied out oldest first, which is the order the plot wants and not
			// the order the ring keeps.
			std::vector<float> ordered;
			ordered.reserve(count);
			for (std::size_t i = 0; i < count; ++i) {
				ordered.push_back(history.Sample(i));
			}

			ImGui::PlotLines(
				"##frame",
				ordered.data(),
				static_cast<int>(ordered.size()),
				0,
				T("performance.frame_history", "Frame time"),
				0.0f,
				FLT_MAX,
				ImVec2{ 0.0f, 60.0f });
		}

		bool DrawCompact(const PerformanceContext& a_context)
		{
			PlaceInCorner(a_context.corner);

			constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
			                       ImGuiWindowFlags_AlwaysAutoResize |
			                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
			                       ImGuiWindowFlags_NoSavedSettings;

			if (!ImGui::Begin("##performance_hud", nullptr, flags)) {
				ImGui::End();
				return false;
			}

			ImGui::PushStyleColor(ImGuiCol_Text, kHudColour);

			if (!a_context.measuring) {
				// Not frozen numbers: a display that keeps showing the last
				// value it had is worse than one that says it stopped.
				ImGui::TextUnformatted(T("performance.paused", "Measurement is off"));
			} else {
				ImGui::Text("%.0f fps   %.2f ms",
					static_cast<double>(g_shown.fps),
					static_cast<double>(g_shown.frameMs));
				ImGui::Text("cpu %.2f   gpu %.2f",
					static_cast<double>(g_shown.cpuMs),
					static_cast<double>(g_shown.frameMs));
			}

			ImGui::PopStyleColor();
			ImGui::End();
			return true;
		}
	}

	bool DrawPerformancePanel(const PerformanceContext& a_context, Detail a_detail)
	{
		RefreshIfDue(a_context);

		if (a_detail == Detail::kCompact) {
			return DrawCompact(a_context);
		}

		if (!ImGui::Begin(T("performance.title", "Performance"))) {
			ImGui::End();
			return false;
		}

		if (!a_context.measuring) {
			ImGui::TextUnformatted(T("performance.paused", "Measurement is off"));
			ImGui::End();
			return true;
		}

		// Said here rather than in a help text nobody opens: the frame figure is
		// wall time between two presents and carries a vsync wait with it, while
		// a pass of our own is exact.
		ImGui::TextUnformatted(
			T("performance.frame_note", "Frame is wall time between presents; passes are exact."));
		ImGui::Separator();

		DrawTable();
		DrawHistory(a_context);

		ImGui::End();
		return true;
	}
}
