#pragma once

#include <cstdint>
#include <optional>

namespace Bridge
{
	/// What a DLSS-NR feature is created for; another key means another feature.
	struct NeuralFeatureKey
	{
		std::uint32_t width{ 0 };
		std::uint32_t height{ 0 };
		std::uint32_t preset{ 0 };
		std::uint32_t format{ 0 };  // DXGI_FORMAT

		[[nodiscard]] bool operator==(const NeuralFeatureKey&) const noexcept = default;
	};

	/// Whether creating a feature may be attempted. Creation submits a list of
	/// its own and stalls the CPU until it ran, so a failure must not be
	/// retried every frame, and after a timeout the list's allocator must not
	/// be reset while the list may still be executing.
	class NeuralCreationGate
	{
	public:
		[[nodiscard]] bool MayCreate(const NeuralFeatureKey& a_key, std::uint64_t a_createCompleted) const noexcept
		{
			if (a_createCompleted < _submitted) {
				return false;
			}
			return !(_failed && *_failed == a_key);
		}
		/// The create list went to the queue with this fence value.
		void Submitted(std::uint64_t a_value) noexcept { _submitted = a_value; }
		void Failed(const NeuralFeatureKey& a_key) noexcept { _failed = a_key; }
		void Succeeded() noexcept { _failed.reset(); }
		/// After a resize: whatever failed may be tried again.
		void Forget() noexcept { _failed.reset(); }

	private:
		std::uint64_t _submitted{ 0 };
		std::optional<NeuralFeatureKey> _failed;
	};
}
