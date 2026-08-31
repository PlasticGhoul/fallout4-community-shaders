#pragma once

#include <filesystem>
#include <span>
#include <utility>
#include <vector>

namespace Util
{
	/// Polls the modification times of a fixed set of files.
	///
	/// Deliberately polling rather than ReadDirectoryChangesW: REX::W32 declares
	/// neither that function nor FindFirstChangeNotification, so the event-based
	/// route would mean including <Windows.h> and running two type systems side
	/// by side. For a handful of files, asking is cheaper than the apparatus.
	class FileWatch
	{
	public:
		/// Replaces the watched set and takes the current timestamps as the
		/// baseline, so the next Poll only reports changes made from now on.
		void Reset(std::span<const std::filesystem::path> a_files);

		/// True when at least one watched file changed since the previous call.
		///
		/// A file that cannot be read right now - an editor may still hold it
		/// open - keeps its old timestamp and is tried again next time. A file
		/// that vanished is treated the same way: never an exception.
		[[nodiscard]] bool Poll();

	private:
		std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> _entries;
	};
}
