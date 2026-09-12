#include "Features/FrameTrace/FrameTraceLog.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	int g_failures = 0;

	void Check(bool a_passed, const char* a_what)
	{
		std::printf("%s  %s\n", a_passed ? "ok  " : "FAIL", a_what);
		if (!a_passed) {
			++g_failures;
		}
	}

	bool Contains(const std::string& a_text, const char* a_part)
	{
		return a_text.find(a_part) != std::string::npos;
	}
}

int main()
{
	using namespace Features::Trace;

	{
		// A line carries everything that identifies the call: order, class,
		// technique id in hex, its name, and what was bound, by slot.
		Entry entry;
		entry.className = "BSDFCompositeShader";
		entry.technique = 0x209;
		entry.techniqueName = "DFComposite_Base_ApplyAO";
		entry.targets = { { 0, "FO4_RT_058" }, { 1, "FO4_RT_059" } };
		entry.depth = "FO4_DS_002";
		entry.resources = { { 0, "FO4_RT_022" }, { 3, "FO4_RT_020" } };

		const auto line = FormatLine(17, entry);
		Check(Contains(line, "#17"), "the order is in the line");
		Check(Contains(line, "BSDFCompositeShader"), "and the class");
		Check(Contains(line, "0x0209"), "and the technique id, four hex digits");
		Check(Contains(line, "DFComposite_Base_ApplyAO"), "and its name");
		Check(Contains(line, "RTV0 FO4_RT_058"), "and the first target with its slot");
		Check(Contains(line, "RTV1 FO4_RT_059"), "and the second");
		Check(Contains(line, "DSV FO4_DS_002"), "and the depth view");
		Check(Contains(line, "SRV3 FO4_RT_020"), "and a resource with the slot it sat in");
	}

	{
		// Nothing bound reads as nothing, not as an empty list of slots.
		Entry entry;
		entry.className = "BSUtilityShader";
		entry.technique = 1;
		const auto line = FormatLine(1, entry);
		Check(Contains(line, "RTV -"), "no targets reads as a dash");
		Check(Contains(line, "DSV -"), "so does no depth view");
		Check(Contains(line, "SRV -"), "and no resources");
		Check(!Contains(line, "RTV0"), "and no slot is invented");
	}

	{
		// An unnamed technique still gets a line; the id stands alone.
		Entry entry;
		entry.className = "BSWaterShader";
		entry.technique = 0x1F;
		const auto line = FormatLine(2, entry);
		Check(Contains(line, "0x001F"), "an unnamed technique shows its id");
	}

	{
		std::vector<Entry> entries(5);
		entries[0].classIndex = 2;
		entries[1].classIndex = 0;
		entries[2].classIndex = 2;
		entries[3].classIndex = 2;
		entries[4].classIndex = 1;

		const auto counts = CountByClass(entries, 3);
		Check(counts.size() == 3, "one count per class");
		Check(counts[0] == 1 && counts[1] == 1 && counts[2] == 3, "each class counted where it ran");
	}

	{
		Recorder recorder;
		Check(recorder.Records().empty(), "a fresh recorder holds nothing");

		RawRecord record{};
		record.classIndex = 4;
		record.technique = 7;
		Check(recorder.Push(record), "a record is taken");
		Check(recorder.Records().size() == 1, "and counted");
		Check(recorder.Records()[0].technique == 7, "and kept as given");

		for (std::size_t i = 1; i < Recorder::kMaxRecords; ++i) {
			static_cast<void>(recorder.Push(record));
		}
		Check(recorder.Records().size() == Recorder::kMaxRecords, "the recorder fills to its cap");
		Check(!recorder.Push(record), "the next is refused");
		Check(!recorder.Push(record), "and the one after");
		Check(recorder.Overflow() == 2, "and both are counted as overflow");

		recorder.Clear();
		Check(recorder.Records().empty() && recorder.Overflow() == 0, "Clear empties both");
	}

	std::printf("\n%s\n", g_failures == 0 ? "all checks passed" : "checks failed");
	return g_failures == 0 ? 0 : 1;
}
