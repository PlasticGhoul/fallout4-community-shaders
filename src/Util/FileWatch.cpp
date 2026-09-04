#include "Util/FileWatch.h"

namespace Util
{
	namespace
	{
		// A file that cannot be stat'ed right now yields the caller's previous
		// value, so a locked or missing file is neither a change nor a throw.
		std::filesystem::file_time_type TimestampOr(
			const std::filesystem::path& a_path,
			std::filesystem::file_time_type a_fallback)
		{
			std::error_code ec;
			const auto stamp = std::filesystem::last_write_time(a_path, ec);
			return ec ? a_fallback : stamp;
		}
	}

	void FileWatch::Reset(std::span<const std::filesystem::path> a_files)
	{
		_entries.clear();
		_entries.reserve(a_files.size());

		for (const auto& file : a_files) {
			_entries.emplace_back(file, TimestampOr(file, std::filesystem::file_time_type{}));
		}
	}

	void FileWatch::Rebase()
	{
		for (auto& [path, stamp] : _entries) {
			stamp = TimestampOr(path, stamp);
		}
	}

	bool FileWatch::Poll()
	{
		bool changed = false;

		// Every entry is visited even after the first hit: the timestamps all
		// have to be brought up to date, or the next Poll would report the same
		// change again.
		for (auto& [path, stamp] : _entries) {
			const auto current = TimestampOr(path, stamp);
			if (current != stamp) {
				stamp = current;
				changed = true;
			}
		}

		return changed;
	}
}
