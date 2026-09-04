#include "Menu/Theme.h"

#include <imgui.h>

namespace Menu
{
	void ApplyTheme(float a_fontSize) noexcept
	{
		auto& style = ImGui::GetStyle();

		// Reset first. ScaleAllSizes multiplies what is already there, so
		// applying the theme twice without this would scale a style that had
		// been scaled once already, and the overlay would grow with every
		// change to the font size.
		style = ImGuiStyle{};
		ImGui::StyleColorsDark();

		style.WindowRounding = 4.0f;
		style.ChildRounding = 4.0f;
		style.FrameRounding = 3.0f;
		style.PopupRounding = 4.0f;
		style.ScrollbarRounding = 3.0f;
		style.GrabRounding = 3.0f;

		style.WindowPadding = ImVec2{ 12.0f, 12.0f };
		style.FramePadding = ImVec2{ 8.0f, 4.0f };
		style.ItemSpacing = ImVec2{ 8.0f, 6.0f };
		style.WindowTitleAlign = ImVec2{ 0.5f, 0.5f };

		auto* const colors = style.Colors;

		// Opaque, not translucent, and this is not a matter of taste: the
		// overlay is read over whatever the game is drawing, and a see-through
		// panel over Boston at night cannot be read at all.
		colors[ImGuiCol_WindowBg] = ImVec4{ 0.09f, 0.09f, 0.10f, 0.98f };
		colors[ImGuiCol_ChildBg] = ImVec4{ 0.09f, 0.09f, 0.10f, 0.00f };
		colors[ImGuiCol_PopupBg] = ImVec4{ 0.11f, 0.11f, 0.12f, 0.99f };
		colors[ImGuiCol_TitleBgActive] = ImVec4{ 0.15f, 0.15f, 0.17f, 1.00f };
		colors[ImGuiCol_TitleBg] = ImVec4{ 0.11f, 0.11f, 0.12f, 1.00f };

		colors[ImGuiCol_FrameBg] = ImVec4{ 0.17f, 0.17f, 0.19f, 1.00f };
		colors[ImGuiCol_FrameBgHovered] = ImVec4{ 0.23f, 0.23f, 0.26f, 1.00f };
		colors[ImGuiCol_FrameBgActive] = ImVec4{ 0.28f, 0.28f, 0.31f, 1.00f };

		colors[ImGuiCol_Header] = ImVec4{ 0.20f, 0.20f, 0.23f, 1.00f };
		colors[ImGuiCol_HeaderHovered] = ImVec4{ 0.26f, 0.26f, 0.30f, 1.00f };
		colors[ImGuiCol_HeaderActive] = ImVec4{ 0.31f, 0.31f, 0.36f, 1.00f };

		colors[ImGuiCol_Button] = ImVec4{ 0.22f, 0.22f, 0.25f, 1.00f };
		colors[ImGuiCol_ButtonHovered] = ImVec4{ 0.29f, 0.29f, 0.33f, 1.00f };
		colors[ImGuiCol_ButtonActive] = ImVec4{ 0.35f, 0.35f, 0.40f, 1.00f };

		// The one accent, used for a checked box and a dragged slider. Amber
		// rather than a blue: Fallout 4's own interface is green on black, and
		// a blue would read as belonging to it.
		colors[ImGuiCol_CheckMark] = ImVec4{ 0.98f, 0.73f, 0.29f, 1.00f };
		colors[ImGuiCol_SliderGrab] = ImVec4{ 0.85f, 0.62f, 0.24f, 1.00f };
		colors[ImGuiCol_SliderGrabActive] = ImVec4{ 0.98f, 0.73f, 0.29f, 1.00f };

		colors[ImGuiCol_Separator] = ImVec4{ 0.28f, 0.28f, 0.31f, 1.00f };
		colors[ImGuiCol_Text] = ImVec4{ 0.92f, 0.92f, 0.93f, 1.00f };
		colors[ImGuiCol_TextDisabled] = ImVec4{ 0.52f, 0.52f, 0.55f, 1.00f };

		// Spacing has to grow with the text, or a larger font sits in padding
		// meant for a smaller one.
		style.ScaleAllSizes(a_fontSize / kReferenceFontSize);
	}
}
