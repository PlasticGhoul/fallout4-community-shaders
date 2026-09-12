#include "Features/FrameTrace/FrameTraceLog.h"

#include <format>

namespace Features::Trace
{
	bool Recorder::Push(const RawRecord& a_record) noexcept
	{
		if (_count >= kMaxRecords) {
			++_overflow;
			return false;
		}

		_records[_count] = a_record;
		++_count;
		return true;
	}

	std::span<const RawRecord> Recorder::Records() const noexcept
	{
		return { _records.data(), _count };
	}

	void Recorder::Clear() noexcept
	{
		_count = 0;
		_overflow = 0;
	}

	namespace
	{
		std::string Bindings(std::string_view a_prefix, std::span<const Binding> a_bindings)
		{
			if (a_bindings.empty()) {
				return "-";
			}

			std::string text;
			for (const auto& binding : a_bindings) {
				if (!text.empty()) {
					text += ' ';
				}
				text += std::format("{}{} {}", a_prefix, binding.slot, binding.name);
			}
			return text;
		}
	}

	std::string FormatLine(std::size_t a_order, const Entry& a_entry)
	{
		return std::format(
			"  #{:<3} {:<24} 0x{:04X} {:<36} RTV {}  DSV {}  SRV {}",
			a_order,
			a_entry.className,
			a_entry.technique,
			a_entry.techniqueName,
			Bindings("RTV", a_entry.targets),
			a_entry.depth.empty() ? std::string{ "-" } : a_entry.depth,
			Bindings("SRV", a_entry.resources));
	}

	std::vector<std::size_t> CountByClass(
		std::span<const Entry> a_entries,
		std::size_t a_classCount)
	{
		std::vector<std::size_t> counts(a_classCount, 0);
		for (const auto& entry : a_entries) {
			if (entry.classIndex < a_classCount) {
				++counts[entry.classIndex];
			}
		}
		return counts;
	}
}
