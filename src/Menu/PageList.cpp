#include "Menu/PageList.h"

namespace Menu
{
	PageList::PageList() :
		_pages{
			Page{ PageKind::kGeneral, std::string{ kGeneralName } },
			Page{ PageKind::kPerformance, std::string{ kPerformanceName } }
		}
	{}

	void PageList::SetFeatures(std::span<const std::string_view> a_names)
	{
		// Copied out first: the resize below may free the string the
		// selection points at.
		const std::string selectedName = _pages[_selected].name;

		_pages.resize(kFixedPages);
		for (const auto name : a_names) {
			_pages.push_back(Page{ PageKind::kFeature, std::string{ name } });
		}

		const auto index = IndexOf(selectedName);
		_selected = index == kNone ? 0 : index;
	}

	bool PageList::Select(std::string_view a_name) noexcept
	{
		const auto index = IndexOf(a_name);
		if (index == kNone) {
			return false;
		}
		_selected = index;
		return true;
	}

	const Page& PageList::PageOf(std::string_view a_block) const noexcept
	{
		// From the second page on: General is where everything else lands,
		// so a block that happened to be called General needs no rule.
		for (std::size_t i = 1; i < _pages.size(); ++i) {
			if (_pages[i].name == a_block) {
				return _pages[i];
			}
		}
		return _pages[0];
	}

	std::size_t PageList::IndexOf(std::string_view a_name) const noexcept
	{
		for (std::size_t i = 0; i < _pages.size(); ++i) {
			if (_pages[i].name == a_name) {
				return i;
			}
		}
		return kNone;
	}
}
