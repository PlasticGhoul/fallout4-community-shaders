#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Menu
{
	enum class PageKind
	{
		kGeneral,
		kPerformance,
		kFeature
	};

	struct Page
	{
		PageKind kind{ PageKind::kGeneral };
		/// "General", "Performance", or the feature's name. A key, not a
		/// caption: the panel translates the fixed two.
		std::string name;
	};

	/// Which pages the overlay has and which one is open. Knows neither ImGui
	/// nor the registry: it is handed names and answers with names, which is
	/// what lets a host test drive it.
	class PageList
	{
	public:
		static constexpr std::string_view kGeneralName = "General";
		static constexpr std::string_view kPerformanceName = "Performance";

		PageList();

		/// Replaces the feature pages, keeping the fixed two in front and the
		/// order given. The selection survives if its page still exists and
		/// falls back to General otherwise.
		void SetFeatures(std::span<const std::string_view> a_names);

		[[nodiscard]] std::span<const Page> Pages() const noexcept { return _pages; }
		[[nodiscard]] const Page& Selected() const noexcept { return _pages[_selected]; }

		/// Selecting a name that is no page leaves the selection where it is
		/// and returns false. The panel never gets to point at nothing.
		bool Select(std::string_view a_name) noexcept;

		/// The page a settings block belongs to: the Performance block to the
		/// performance page, a feature's block to its page, every other block
		/// to General. The one rule the panel used to hold, now here.
		[[nodiscard]] const Page& PageOf(std::string_view a_block) const noexcept;

	private:
		static constexpr std::size_t kFixedPages = 2;
		static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

		[[nodiscard]] std::size_t IndexOf(std::string_view a_name) const noexcept;

		std::vector<Page> _pages;
		std::size_t _selected{ 0 };
	};
}
