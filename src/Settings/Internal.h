#pragma once

#include "Settings/Settings.h"

#include <cstddef>
#include <map>
#include <vector>

namespace Settings::Impl
{
	/// One declared setting. Owns every string the public Entry views into,
	/// which is why the table below is node based: a vector would move these on
	/// its next growth and invalidate every view, and REX additionally keeps
	/// the path as a string_view of its own.
	struct Record
	{
		std::string path;
		std::string block;
		std::string key;
		Kind kind{ Kind::kBool };

		std::string labelKey;
		std::string labelText;
		std::string helpKey;
		std::string helpText;

		double min{ 0.0 };
		double max{ 0.0 };
		std::vector<std::string> choices;

		bool defaultBool{ false };
		double defaultNumber{ 0.0 };
		std::string defaultChoice;

		bool isFeatureSwitch{ false };

		/// Declaration order, so that both the panel and the written file are
		/// deterministic without depending on how the paths happen to sort.
		std::size_t ordinal{ 0 };
	};

	/// Keyed by path. Node based on purpose, see Record.
	std::map<std::string, Record, std::less<>>& Records();

	/// Null for a path that was never declared.
	Record* Find(std::string_view a_path) noexcept;

	/// The public view of a record.
	Entry ViewOf(const Record& a_record) noexcept;
}
