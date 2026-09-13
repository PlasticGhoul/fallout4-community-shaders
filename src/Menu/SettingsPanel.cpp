#include "Menu/SettingsPanel.h"

#include "Feature/FeatureRegistry.h"
#include "I18n/I18n.h"
#include "Menu/Fonts.h"
#include "Menu/KeyNames.h"
#include "Menu/PageList.h"
#include "Plugin.h"
#include "Settings/Settings.h"

#include <imgui.h>

#include <optional>
#include <string>
#include <string_view>
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

			// The format comes from the declared range. ImGui rounds the value
			// to it, so a fixed one turns a slider whose span is smaller than
			// its precision into a switch between the two ends - which is what
			// a hard-coded "%.1f" did to the first setting that had one.
			if (ImGui::SliderFloat(
					a_label,
					std::addressof(value),
					static_cast<float>(a_entry.min),
					static_cast<float>(a_entry.max),
					Settings::SliderFormat(a_entry.min, a_entry.max))) {
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
			const bool capturing = a_context.isCapturing && a_context.isCapturing(a_entry.path);

			std::string caption;
			if (capturing) {
				caption = T("menu.press_a_key", "Press a key...");
			} else {
				auto name = KeyName(key);
				caption = name.empty() ? std::format("0x{:02X}", key) : std::move(name);
			}

			if (ImGui::Button(caption.c_str(), ImVec2{ ImGui::GetFontSize() * 8.0f, 0.0f }) &&
				!capturing && a_context.armCapture) {
				a_context.armCapture(a_entry.path);
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

		struct FeatureRow
		{
			std::string name;
			Features::State state{ Features::State::kOff };
		};

		std::vector<FeatureRow> FeatureRows()
		{
			std::vector<FeatureRow> rows;
			Features::TheRegistry().ForEach(
				[&rows](std::string_view a_name, Features::State a_state) {
					rows.push_back(FeatureRow{ std::string{ a_name }, a_state });
				});
			return rows;
		}

		Features::State StateOf(const std::vector<FeatureRow>& a_rows, std::string_view a_name)
		{
			for (const auto& row : a_rows) {
				if (row.name == a_name) {
					return row.state;
				}
			}
			return Features::State::kOff;
		}

		/// A feature's block split the way the panel draws it: the switch goes
		/// into the list on the left, everything else onto the page.
		struct FeatureEntries
		{
			std::optional<Settings::Entry> switchEntry;
			std::vector<Settings::Entry> rest;
		};

		FeatureEntries CollectEntries(std::string_view a_block)
		{
			FeatureEntries entries;
			Settings::ForEachEntry(a_block, [&entries](const Settings::Entry& a_entry) {
				if (a_entry.isFeatureSwitch) {
					entries.switchEntry = a_entry;
				} else {
					entries.rest.push_back(a_entry);
				}
			});
			return entries;
		}

		const char* PageTitle(const Page& a_page)
		{
			switch (a_page.kind) {
			case PageKind::kGeneral:
				return T("menu.page.general", "General");
			case PageKind::kPerformance:
				return T("performance.title", "Performance");
			default:
				return a_page.name.c_str();
			}
		}

		void DrawHeading(const char* a_text)
		{
			ImGui::PushFont(Fonts::Heading(), 0.0f);
			ImGui::TextUnformatted(a_text);
			ImGui::PopFont();
		}

		void DrawFixedRow(PageList& a_pages, const Page& a_page)
		{
			const bool selected = a_pages.Selected().name == a_page.name;
			if (ImGui::Selectable(PageTitle(a_page), selected)) {
				a_pages.Select(a_page.name);
			}
		}

		void DrawFeatureRow(PageList& a_pages, const Page& a_page, Features::State a_state)
		{
			// One ID per feature, so that the label-less checkboxes of two rows
			// do not collapse into one widget.
			ImGui::PushID(a_page.name.c_str());

			const auto entries = CollectEntries(a_page.name);
			if (entries.switchEntry.has_value()) {
				// The switch itself, with no caption: the caption is the row.
				// Its help is not a tooltip here; it is the page's description.
				DrawBool(*entries.switchEntry, "##enabled");
			} else {
				// A feature that never declared its switch cannot be turned on
				// at all. An empty place keeps the names aligned.
				ImGui::Dummy(ImVec2{ ImGui::GetFrameHeight(), ImGui::GetFrameHeight() });
			}
			ImGui::SameLine();

			const char* const state = StateText(a_state);
			const float stateWidth = ImGui::CalcTextSize(state).x;
			const float rowWidth = ImGui::GetContentRegionAvail().x;
			const bool selected = a_pages.Selected().name == a_page.name;

			if (!entries.switchEntry.has_value()) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			}
			if (ImGui::Selectable(
					a_page.name.c_str(),
					selected,
					ImGuiSelectableFlags_None,
					ImVec2{ rowWidth - stateWidth - ImGui::GetStyle().ItemSpacing.x, 0.0f })) {
				a_pages.Select(a_page.name);
			}
			if (!entries.switchEntry.has_value()) {
				ImGui::PopStyleColor();
			}

			ImGui::SameLine();
			ImGui::TextDisabled("%s", state);

			ImGui::PopID();
		}

		void DrawPageList(PageList& a_pages, const std::vector<FeatureRow>& a_rows)
		{
			ImGui::SeparatorText(T("menu.section.general", "General"));
			for (const auto& page : a_pages.Pages()) {
				if (page.kind != PageKind::kFeature) {
					DrawFixedRow(a_pages, page);
				}
			}

			ImGui::SeparatorText(T("menu.section.features", "Features"));
			for (const auto& page : a_pages.Pages()) {
				if (page.kind == PageKind::kFeature) {
					DrawFeatureRow(a_pages, page, StateOf(a_rows, page.name));
				}
			}
		}

		/// Every block that is neither a feature nor Performance. Today that is
		/// Menu alone; the point is that no feature has to register itself as
		/// having a surface - the lists are matched by name.
		void DrawGeneralPage(const PanelContext& a_context, const PageList& a_pages)
		{
			DrawHeading(T("menu.page.general", "General"));
			ImGui::Separator();
			Settings::ForEachBlock([&](std::string_view a_block) {
				if (a_pages.PageOf(a_block).kind != PageKind::kGeneral) {
					return;
				}
				Settings::ForEachEntry(a_block, [&](const Settings::Entry& a_entry) {
					DrawEntry(a_entry, a_context);
				});
			});
		}

		void DrawPerformancePage(const PanelContext& a_context, const PerformanceContext& a_performance)
		{
			DrawHeading(T("performance.title", "Performance"));
			ImGui::Separator();
			DrawPerformanceTable(a_performance);
			ImGui::Separator();
			Settings::ForEachEntry(PageList::kPerformanceName, [&](const Settings::Entry& a_entry) {
				DrawEntry(a_entry, a_context);
			});
		}

		void DrawFeaturePage(const PanelContext& a_context, const Page& a_page, Features::State a_state)
		{
			DrawHeading(a_page.name.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("%s", StateText(a_state));

			// The description is the help of the switch, which every feature
			// declares already. Nothing new to declare for a page.
			const auto entries = CollectEntries(a_page.name);
			if (!entries.switchEntry.has_value()) {
				ImGui::TextWrapped("%s",
					T("menu.no_switch", "This feature declares no switch and cannot be turned on."));
			} else if (!entries.switchEntry->helpText.empty()) {
				ImGui::TextWrapped("%s",
					Translate(entries.switchEntry->helpKey, entries.switchEntry->helpText));
			}
			ImGui::Separator();

			for (const auto& entry : entries.rest) {
				DrawEntry(entry, a_context);
			}
		}

		void DrawPage(
			const PanelContext& a_context,
			const PerformanceContext& a_performance,
			const PageList& a_pages,
			const std::vector<FeatureRow>& a_rows)
		{
			const auto& page = a_pages.Selected();
			switch (page.kind) {
			case PageKind::kGeneral:
				DrawGeneralPage(a_context, a_pages);
				break;
			case PageKind::kPerformance:
				DrawPerformancePage(a_context, a_performance);
				break;
			case PageKind::kFeature:
				DrawFeaturePage(a_context, page, StateOf(a_rows, page.name));
				break;
			}
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

		/// State of the panel for the session, like the Skyrim menu's expansion
		/// states: which page is open. Not a setting, not written anywhere.
		PageList& ThePages()
		{
			static PageList pages;
			return pages;
		}
	}

	bool DrawSettingsPanel(const PanelContext& a_context, const PerformanceContext& a_performance)
	{
		bool closeWanted = false;

		// Sized to the screen on first use of a session, then left alone. ImGui
		// keeps no ini for us (io.IniFilename is null), so this is what "first
		// use" means here.
		const auto display = ImGui::GetIO().DisplaySize;
		ImGui::SetNextWindowSize(ImVec2{ display.x * 0.6f, display.y * 0.7f }, ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(
			ImVec2{ display.x * 0.5f, display.y * 0.5f }, ImGuiCond_FirstUseEver, ImVec2{ 0.5f, 0.5f });

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

			// The registry is constant, but the model is not to know that:
			// handed the names every frame, it keeps its selection by name.
			const auto rows = FeatureRows();
			std::vector<std::string_view> names;
			names.reserve(rows.size());
			for (const auto& row : rows) {
				names.emplace_back(row.name);
			}
			auto& pages = ThePages();
			pages.SetFeatures(names);

			// Everything above the footer is the two columns, and the footer
			// does not scroll: with forty features from F+ the buttons must not
			// walk off the bottom of the window.
			const auto footer =
				ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
			if (ImGui::BeginChild("body", ImVec2{ 0.0f, -footer })) {
				constexpr auto flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV |
				                       ImGuiTableFlags_SizingStretchProp;
				if (ImGui::BeginTable("columns", 2, flags)) {
					ImGui::TableSetupColumn("##pages", ImGuiTableColumnFlags_WidthStretch, 1.0f);
					ImGui::TableSetupColumn("##page", ImGuiTableColumnFlags_WidthStretch, 3.0f);
					ImGui::TableNextRow();

					// Each column is a child that scrolls for itself, the list
					// as long as the features and the page as long as its
					// settings.
					ImGui::TableNextColumn();
					if (ImGui::BeginChild("pages", ImVec2{ 0.0f, ImGui::GetContentRegionAvail().y })) {
						DrawPageList(pages, rows);
					}
					ImGui::EndChild();

					ImGui::TableNextColumn();
					if (ImGui::BeginChild("page", ImVec2{ 0.0f, ImGui::GetContentRegionAvail().y })) {
						DrawPage(a_context, a_performance, pages, rows);
					}
					ImGui::EndChild();

					ImGui::EndTable();
				}
			}
			ImGui::EndChild();

			ImGui::Separator();
			DrawFooter(closeWanted);
		}
		ImGui::End();

		return closeWanted;
	}
}
