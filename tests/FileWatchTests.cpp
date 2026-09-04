#include "Util/FileWatch.h"

#include <chrono>
#include <cstdio>
#include <fstream>

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

	void WriteFile(const std::filesystem::path& a_path, std::string_view a_content)
	{
		std::ofstream stream{ a_path, std::ios::binary };
		stream.write(a_content.data(), static_cast<std::streamsize>(a_content.size()));
	}

	// The timestamp is set explicitly rather than by sleeping: the test stays
	// deterministic and does not depend on filesystem timestamp granularity.
	void AgeForward(const std::filesystem::path& a_path)
	{
		const auto now = std::filesystem::last_write_time(a_path);
		std::filesystem::last_write_time(a_path, now + std::chrono::seconds{ 5 });
	}
}

int main()
{
	const auto root = std::filesystem::temp_directory_path() / "fo4cs-filewatch-tests";
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);

	const auto first = root / "first.hlsl";
	const auto second = root / "second.hlsli";
	WriteFile(first, "one\n");
	WriteFile(second, "two\n");

	const std::vector<std::filesystem::path> files{ first, second };

	Util::FileWatch watch;
	watch.Reset(files);

	Check(!watch.Poll(), "nothing changed right after Reset");

	AgeForward(second);
	Check(watch.Poll(), "a changed file is reported");
	Check(!watch.Poll(), "the same change is not reported twice");

	AgeForward(first);
	AgeForward(second);
	Check(watch.Poll(), "two files changing at once report once");
	Check(!watch.Poll(), "and then go quiet again");

	std::filesystem::remove(second);
	bool threw = false;
	try {
		static_cast<void>(watch.Poll());
	} catch (...) {
		threw = true;
	}
	Check(!threw, "a deleted file does not throw");

	// An empty watch set is the normal state before the first shader loads.
	Util::FileWatch empty;
	Check(!empty.Poll(), "an empty watch reports nothing");

	// Rebase hides a change that already happened, but nothing after it. This
	// is what keeps our own settings write from looking like somebody else's.
	{
		const auto owned = root / "owned.json";
		WriteFile(owned, "{}\n");

		Util::FileWatch watchOwned;
		const std::vector<std::filesystem::path> ours{ owned };
		watchOwned.Reset(ours);

		AgeForward(owned);
		watchOwned.Rebase();
		Check(!watchOwned.Poll(), "a change before Rebase is not reported");

		AgeForward(owned);
		Check(watchOwned.Poll(), "but a change after Rebase still is");
	}

	std::filesystem::remove_all(root);
	std::printf("%d failure(s)\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
