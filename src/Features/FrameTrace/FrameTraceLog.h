#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Features::Trace
{
	/// What the thunk writes down: pointers and numbers, nothing that
	/// allocates. The views are held with a reference each and released by
	/// whoever resolves them.
	struct RawRecord
	{
		static constexpr std::size_t kTargets = 8;
		static constexpr std::size_t kResources = 16;

		std::size_t classIndex{ 0 };
		const void* self{ nullptr };
		std::uint32_t technique{ 0 };
		void* targets[kTargets]{};
		void* depth{ nullptr };
		void* resources[kResources]{};
	};

	/// A fixed field of records, written on the render thread inside the hook
	/// and read on the same thread from Present. No lock is needed because both
	/// sides are that one thread; what matters is that Push never allocates.
	class Recorder
	{
	public:
		static constexpr std::size_t kMaxRecords = 512;

		/// False once the field is full; the refusal is counted.
		bool Push(const RawRecord& a_record) noexcept;

		[[nodiscard]] std::span<const RawRecord> Records() const noexcept;
		[[nodiscard]] std::size_t Overflow() const noexcept { return _overflow; }

		/// Forgets the records. It does not release anything - the caller
		/// releases the views before calling this.
		void Clear() noexcept;

	private:
		std::array<RawRecord, kMaxRecords> _records{};
		std::size_t _count{ 0 };
		std::size_t _overflow{ 0 };
	};

	/// One bound object, by the slot it sat in.
	struct Binding
	{
		std::uint32_t slot{ 0 };
		std::string name;
	};

	/// A record with its names resolved.
	struct Entry
	{
		std::size_t classIndex{ 0 };
		std::string_view className;
		std::uint32_t technique{ 0 };
		std::string techniqueName;
		std::vector<Binding> targets;
		std::string depth;
		std::vector<Binding> resources;
	};

	/// The line the log carries for one call. Slots that were empty are left
	/// out; a stage with nothing bound reads as a dash.
	[[nodiscard]] std::string FormatLine(std::size_t a_order, const Entry& a_entry);

	/// How often each class ran, indexed by classIndex.
	[[nodiscard]] std::vector<std::size_t> CountByClass(
		std::span<const Entry> a_entries,
		std::size_t a_classCount);
}
