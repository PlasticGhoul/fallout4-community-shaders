#include "Menu/Fonts.h"

#include "Util/GamePaths.h"

#include <imgui.h>

namespace Menu::Fonts
{
	namespace
	{
		ImFont* g_body = nullptr;
		ImFont* g_heading = nullptr;
		bool g_attempted = false;

		ImFont* LoadOne(const char* a_file)
		{
			const auto path = Util::PluginDataDirectory() / "Fonts" / a_file;

			std::error_code ec;
			if (!std::filesystem::exists(path, ec)) {
				REX::ERROR("font {} is missing, the built-in font stays", path.generic_string());
				return nullptr;
			}

			// Size zero on purpose. Before 1.92 a font was added at one fixed
			// size; now it is sized where it is pushed, so committing to a
			// number here would only be a number to keep in step with the
			// setting.
			auto* const font =
				ImGui::GetIO().Fonts->AddFontFromFileTTF(path.string().c_str(), 0.0f);

			if (font == nullptr) {
				REX::ERROR("font {} could not be read", path.generic_string());
			}
			return font;
		}
	}

	void Load() noexcept
	{
		if (g_attempted) {
			return;
		}
		g_attempted = true;

		g_body = LoadOne("IBMPlexSans-Regular.ttf");
		g_heading = LoadOne("IBMPlexSans-SemiBold.ttf");

		if (g_body != nullptr) {
			ImGui::GetIO().FontDefault = g_body;
			REX::INFO("overlay font loaded from {}", Util::PluginDataDirectory().generic_string());
		}
	}

	ImFont* Body() noexcept
	{
		return g_body;
	}

	ImFont* Heading() noexcept
	{
		// Falls back to the body face rather than to null: a heading in the
		// regular weight still reads as a heading through its size, whereas
		// null would silently keep whatever font happened to be pushed.
		return g_heading != nullptr ? g_heading : g_body;
	}
}
