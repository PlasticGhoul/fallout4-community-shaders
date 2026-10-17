#pragma once

#include <array>
#include <cstdint>

namespace Bridge
{
	/// Which of two shared slots a frame writes, and which fence value frees
	/// it. D3D11 writes slot k, D3D12 reads it; D3D11 may write k again only
	/// once D3D12 has signalled the value it was given when it took k. Every
	/// one of those waits is on the GPU. The one exception is the CPU's: the
	/// D3D12 command allocator of k may be reset only once that value has
	/// completed, which with two slots means D3D12 is two frames behind.
	class SlotRing
	{
	public:
		static constexpr std::uint32_t kSlots = 2;

		[[nodiscard]] std::uint32_t Current() const noexcept { return _current; }

		/// What D3D11 waits for before writing Current(); 0 when the slot was
		/// never handed over.
		[[nodiscard]] std::uint64_t ReleaseValue() const noexcept { return _release[_current]; }

		[[nodiscard]] bool Reusable(std::uint64_t a_completed) const noexcept { return a_completed >= _release[_current]; }

		/// D3D12 will signal a_value once it is done with Current(); the next
		/// frame writes the other slot.
		void Submit(std::uint64_t a_value) noexcept
		{
			_release[_current] = a_value;
			_current = (_current + 1) % kSlots;
		}

		void Reset() noexcept
		{
			_current = 0;
			_release = {};
		}

	private:
		std::uint32_t _current{ 0 };
		std::array<std::uint64_t, kSlots> _release{};
	};
}
