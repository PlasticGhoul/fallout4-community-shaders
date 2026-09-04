#include "Settings/Internal.h"

#include <algorithm>
#include <format>
#include <memory>
#include <utility>

namespace Settings
{
	namespace
	{
		// "Block/Key" and nothing else. One slash, neither half empty.
		bool SplitPath(std::string_view a_path, std::string_view& a_block, std::string_view& a_key)
		{
			const auto slash = a_path.find('/');
			if (slash == std::string_view::npos || slash == 0 || slash + 1 >= a_path.size()) {
				return false;
			}
			if (a_path.find('/', slash + 1) != std::string_view::npos) {
				return false;
			}

			a_block = a_path.substr(0, slash);
			a_key = a_path.substr(slash + 1);
			return true;
		}

		// Returns null when the path was refused or already taken, which is
		// what makes the returned handle a no-op rather than a crash.
		Impl::Record* Declare(std::string_view a_path, Kind a_kind)
		{
			std::string_view block;
			std::string_view key;
			if (!SplitPath(a_path, block, key)) {
				REX::ERROR("setting path {} is not <Block>/<Key>, ignored", a_path);
				return nullptr;
			}

			auto& records = Impl::Records();
			const auto [it, inserted] = records.try_emplace(std::string{ a_path });
			if (!inserted) {
				return nullptr;  // Declared twice; the first declaration wins.
			}

			auto& record = it->second;
			record.path = it->first;
			record.block = std::string{ block };
			record.key = std::string{ key };
			record.kind = a_kind;

			// Insertion counter, not the map's ordering: the table sorts by
			// path, which would put a block declared later ahead of one
			// declared earlier.
			record.ordinal = records.size() - 1;
			return std::addressof(record);
		}
	}

	Handle& Handle::Label(std::string_view a_key, std::string_view a_text) noexcept
	{
		if (auto* const record = static_cast<Impl::Record*>(_record)) {
			record->labelKey = std::string{ a_key };
			record->labelText = std::string{ a_text };
		}
		return *this;
	}

	Handle& Handle::Help(std::string_view a_key, std::string_view a_text) noexcept
	{
		if (auto* const record = static_cast<Impl::Record*>(_record)) {
			record->helpKey = std::string{ a_key };
			record->helpText = std::string{ a_text };
		}
		return *this;
	}

	Handle DeclareBool(std::string_view a_path, bool a_default)
	{
		auto* const record = Declare(a_path, Kind::kBool);
		if (record != nullptr) {
			record->defaultBool = a_default;
		}
		return Handle{ record };
	}

	Handle DeclareSlider(std::string_view a_path, double a_default, double a_min, double a_max)
	{
		auto* const record = Declare(a_path, Kind::kSlider);
		if (record != nullptr) {
			record->defaultNumber = a_default;
			record->min = a_min;
			record->max = a_max;
		}
		return Handle{ record };
	}

	Handle DeclareChoice(
		std::string_view a_path,
		std::string_view a_default,
		std::vector<std::string> a_choices)
	{
		auto* const record = Declare(a_path, Kind::kChoice);
		if (record != nullptr) {
			record->defaultChoice = std::string{ a_default };
			record->choices = std::move(a_choices);
		}
		return Handle{ record };
	}

	Handle DeclareKey(std::string_view a_path, std::uint32_t a_default)
	{
		auto* const record = Declare(a_path, Kind::kKey);
		if (record != nullptr) {
			record->defaultNumber = static_cast<double>(a_default);
		}
		return Handle{ record };
	}

	Handle DeclareFeature(std::string_view a_name, bool a_default)
	{
		const auto path = std::format("{}/enabled", a_name);
		auto handle = DeclareBool(path, a_default);

		// Marked here rather than through the handle: it is a property of the
		// declaration, not something a caller should be able to set.
		if (auto* const record = Impl::Find(path)) {
			record->isFeatureSwitch = true;
		}
		return handle;
	}

	void ForEachBlock(const std::function<void(std::string_view)>& a_visit)
	{
		// A block is as old as its oldest entry, so that adding a setting to an
		// existing block does not move the block.
		std::vector<std::pair<std::size_t, std::string_view>> blocks;

		for (const auto& entry : Impl::Records()) {
			const auto& record = entry.second;
			const auto known = std::find_if(
				blocks.begin(),
				blocks.end(),
				[&record](const auto& a_pair) { return a_pair.second == record.block; });

			if (known == blocks.end()) {
				blocks.emplace_back(record.ordinal, std::string_view{ record.block });
			} else if (record.ordinal < known->first) {
				known->first = record.ordinal;
			}
		}

		std::sort(blocks.begin(), blocks.end());

		for (const auto& block : blocks) {
			a_visit(block.second);
		}
	}

	void ForEachEntry(std::string_view a_block, const std::function<void(const Entry&)>& a_visit)
	{
		std::vector<const Impl::Record*> entries;

		for (const auto& entry : Impl::Records()) {
			if (entry.second.block == a_block) {
				entries.push_back(std::addressof(entry.second));
			}
		}

		std::sort(entries.begin(), entries.end(), [](const auto* a_lhs, const auto* a_rhs) {
			return a_lhs->ordinal < a_rhs->ordinal;
		});

		for (const auto* const record : entries) {
			a_visit(Impl::ViewOf(*record));
		}
	}

	namespace Impl
	{
		std::map<std::string, Record, std::less<>>& Records()
		{
			static std::map<std::string, Record, std::less<>> records;
			return records;
		}

		Record* Find(std::string_view a_path) noexcept
		{
			const auto it = Records().find(a_path);
			return it == Records().end() ? nullptr : std::addressof(it->second);
		}

		Entry ViewOf(const Record& a_record) noexcept
		{
			Entry entry;
			entry.path = a_record.path;
			entry.block = a_record.block;
			entry.key = a_record.key;
			entry.kind = a_record.kind;
			entry.labelKey = a_record.labelKey;
			entry.labelText = a_record.labelText;
			entry.helpKey = a_record.helpKey;
			entry.helpText = a_record.helpText;
			entry.min = a_record.min;
			entry.max = a_record.max;
			entry.choices = a_record.choices;
			entry.defaultBool = a_record.defaultBool;
			entry.defaultNumber = a_record.defaultNumber;
			entry.defaultChoice = a_record.defaultChoice;
			entry.isFeatureSwitch = a_record.isFeatureSwitch;
			return entry;
		}
	}
}
