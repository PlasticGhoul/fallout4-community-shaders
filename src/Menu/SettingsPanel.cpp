#include "Menu/SettingsPanel.h"

#include "Feature/FeatureRegistry.h"
#include "I18n/I18n.h"
#include "Menu/Fonts.h"
#include "Menu/KeyNames.h"
#include "Plugin.h"
#include "Settings/Settings.h"

#include <imgui.h>

#include <string>
#include <vector>

namespace Menu
{
	namespace
	{
		// Entry views hold string_views into the schema's own std::strings,
		// which are null terminated. T() and ImGui both want a const char*, and
		// this is the one place that relies on it.
		const char* Translate(std::string_view a_key, std::string_view a_english)
		{
			return T(a_key, a_english.empty() ? nullptr : a_english.data());
		}

		// The one rule that makes a slider write once instead of sixty times a
		// second: change the value while dragging, write when the drag ends. A
		// checkbox goes inactive in the same frame it changes, so one branch
		// serves both.
		void CommitOnRelease()
		{
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				Settings::Save();
			}
		}

		void DrawHelp(const Settings::Entry& a_entry)
		{
			if (a_entry.helpText.empty() || !ImGui::IsItemHovered()) {
				return;
			}
			ImGui::SetTooltip("%s", Translate(a_entry.helpKey, a_entry.helpText));
		}

		void DrawBool(const Settings::Entry& a_entry, const char* a_label)
		{
			bool value = Settings::GetBool(a_entry.path);
			if (ImGui::Checkbox(a_label, std::addressof(value))) {
				Settings::SetBool(a_entry.path, value);
			}
			CommitOnRelease();
		}

		void DrawSlider(const Settings::Entry& a_entry, const char* a_label)
		{
			auto value = static_cast<float>(Settings::GetDouble(a_entry.path));
			if (ImGui::SliderFloat(
					a_label,
					std::addressof(value),
					static_cast<float>(a_entry.min),
					static_cast<float>(a_entry.max),
					"%.1f")) {
				Settings::SetDouble(a_entry.path, value);
			}
			CommitOnRelease();
		}

		void DrawChoice(const Settings::Entry& a_entry, const char* a_label)
		{
			const auto current = Settings::GetString(a_entry.path);

			if (!ImGui::BeginCombo(a_label, current.c_str())) {
				return;
			}

			for (const auto& choice : a_entry.choices) {
				const bool selected = choice == current;
				if (ImGui::Selectable(choice.c_str(), selected)) {
					Settings::SetString(a_entry.path, choice);

					// Written here rather than through CommitOnRelease: a combo
					// closes as it is picked, and the deactivation that follows
					// belongs to the combo, not to the item.
					Settings::Save();
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}

			ImGui::EndCombo();
		}

		void DrawKey(
			const Settings::Entry& a_entry,
			const char* a_label,
			const PanelContext& a_context)
		{
			const auto key = Settings::GetUInt32(a_entry.path);
			const bool capturing = a_context.isCapturing && a_context.isCapturing();

			std::string caption;
			if (capturing) {
				caption = T("menu.press_a_key", "Press a key...");
			} else {
				auto name = KeyName(key);
				caption = name.empty() ? std::format("0x{:02X}", key) : std::move(name);
			}

			if (ImGui::Button(caption.c_str(), ImVec2{ ImGui::GetFontSize() * 8.0f, 0.0f }) &&
				!capturing && a_context.armCapture) {
				a_context.armCapture();
			}

			ImGui::SameLine();
			ImGui::TextUnformatted(a_label);
		}

		void DrawEntry(const Settings::Entry& a_entry, const PanelContext& a_context)
		{
			// Keyed by path, not by label: two settings may translate to the
			// same words, and ImGui would then treat them as one widget.
			ImGui::PushID(a_entry.path.data(), a_entry.path.data() + a_entry.path.size());

			const char* const label = Translate(a_entry.labelKey, a_entry.labelText);

			switch (a_entry.kind) {
			case Settings::Kind::kBool:
				DrawBool(a_entry, label);
				break;
			case Settings::Kind::kSlider:
				DrawSlider(a_entry, label);
				break;
			case Settings::Kind::kChoice:
				DrawChoice(a_entry, label);
				break;
			case Settings::Kind::kKey:
				DrawKey(a_entry, label, a_context);
				break;
			}

			DrawHelp(a_entry);
			ImGui::PopID();
		}

		const char* StateText(Features::State a_state)
		{
			switch (a_state) {
			case Features::State::kRunning:
				return T("menu.state.running", "running");
			case Features::State::kRefused:
				// The only place a player learns that their tick did nothing.
				// Without it the refusal exists only in the log.
				return T("menu.state.refused", "refused");
			default:
				return T("menu.state.off", "off");
			}
		}

		std::vector<std::string> FeatureNames()
		{
			std::vector<std::string> names;
			Features::TheRegistry().ForEach(
				[&names](std::string_view a_name, Features::State) { names.emplace_back(a_name); });
			return names;
		}

		/// Blocks with no feature of the same name. Today that is Menu alone;
		/// the point is that no feature has to register itself as having a
		/// surface - the two lists are matched by name.
		void DrawGeneral(const PanelContext& a_context, const std::vector<std::string>& a_features)
		{
			if (!ImGui::CollapsingHeader(
					T("menu.general", "General"),
					ImGuiTreeNodeFlags_DefaultOpen)) {
				return;
			}

			ImGui::Indent();
			Settings::ForEachBlock([&](std::string_view a_block) {
				const bool isFeature = std::find(a_features.begin(), a_features.end(), a_block) !=
				                       a_features.end();
				if (isFeature) {
					return;
				}

				Settings::ForEachEntry(a_block, [&](const Settings::Entry& a_entry) {
					DrawEntry(a_entry, a_context);
				});
			});
			ImGui::Unindent();
		}

		void DrawFeature(
			std::string_view a_name,
			Features::State a_state,
			const PanelContext& a_context)
		{
			const Settings::Entry* switchEntry = nullptr;
			Settings::Entry stored{};

			std::vector<Settings::Entry> rest;
			Settings::ForEachEntry(a_name, [&](const Settings::Entry& a_entry) {
				if (a_entry.isFeatureSwitch) {
					stored = a_entry;
					switchEntry = std::addressof(stored);
				} else {
					rest.push_back(a_entry);
				}
			});

			ImGui::PushID(a_name.data(), a_name.data() + a_name.size());

			if (switchEntry != nullptr) {
				// The switch is the heading's checkbox, not a line among the
				// feature's settings, which is why it was pulled out above.
				DrawEntry(*switchEntry, a_context);
			} else {
				// A feature that never declared its switch cannot be turned on
				// at all. Saying so beats drawing an empty row.
				ImGui::TextDisabled("%s", std::string{ a_name }.c_str());
			}

			ImGui::SameLine();
			ImGui::TextDisabled("%s", StateText(a_state));

			if (!rest.empty()) {
				ImGui::Indent();
				for (const auto& entry : rest) {
					DrawEntry(entry, a_context);
				}
				ImGui::Unindent();
			}

			ImGui::PopID();
		}

		void DrawFeatures(const PanelContext& a_context)
		{
			if (!ImGui::CollapsingHeader(
					T("menu.features", "Features"),
					ImGuiTreeNodeFlags_DefaultOpen)) {
				return;
			}

			ImGui::Indent();
			Features::TheRegistry().ForEach(
				[&a_context](std::string_view a_name, Features::State a_state) {
					DrawFeature(a_name, a_state, a_context);
				});
			ImGui::Unindent();
		}

		void DrawFooter(bool& a_closeWanted)
		{
			if (ImGui::Button(T("menu.restore_defaults", "Restore defaults"))) {
				ImGui::OpenPopup("confirm-restore");
			}

			if (ImGui::BeginPopupModal(
					"confirm-restore",
					nullptr,
					ImGuiWindowFlags_AlwaysAutoResize)) {
				// Asked, because a change is written the moment it is made:
				// there is no "just do not save" to fall back on.
				ImGui::TextUnformatted(
					T("menu.restore_confirm", "Put every setting back to its default?"));
				ImGui::Separator();

				if (ImGui::Button(T("menu.yes", "Yes"))) {
					Settings::RestoreDefaults();
					Settings::Save();
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button(T("menu.no", "No"))) {
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			ImGui::SameLine();
			if (ImGui::Button(T("menu.close", "Close"))) {
				a_closeWanted = true;
			}
		}
	}

	bool DrawSettingsPanel(const PanelContext& a_context)
	{
		bool closeWanted = false;

		ImGui::SetNextWindowSize(ImVec2{ 560.0f, 520.0f }, ImGuiCond_FirstUseEver);
		if (ImGui::Begin(T("menu.title", "Community Shaders"))) {
			ImGui::PushFont(Fonts::Heading(), 0.0f);
			ImGui::TextUnformatted(Plugin::NAME.data());
			ImGui::PopFont();

			ImGui::SameLine();
			// BUILD_DESCRIBE rather than the version triple: it is what answers
			// "which build is this" in a bug report, and cmake/Plugin.h.in
			// declares NAME, VERSION and this, and nothing else.
			ImGui::TextDisabled("%s", Plugin::BUILD_DESCRIBE.data());

			ImGui::Text(
				"%s %llu",
				T("menu.frame", "Frame"),
				static_cast<unsigned long long>(a_context.frame));

			ImGui::Separator();

			// Everything above the footer scrolls, and the footer does not:
			// with forty features from F+ the buttons must not walk off the
			// bottom of the window.
			const auto footer =
				ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

			if (ImGui::BeginChild("body", ImVec2{ 0.0f, -footer })) {
				const auto features = FeatureNames();
				DrawGeneral(a_context, features);
				DrawFeatures(a_context);
			}
			ImGui::EndChild();

			ImGui::Separator();
			DrawFooter(closeWanted);
		}
		ImGui::End();

		return closeWanted;
	}
}
