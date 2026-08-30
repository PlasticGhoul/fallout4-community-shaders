#pragma once

namespace Runtime
{
	/// The one game version this build has been validated against.
	inline constexpr REL::Version kSupported = F4SE::RUNTIME_1_11_240;

	/// Exact match, deliberately not a range or a lower bound.
	///
	/// REX::FModule maps any unknown newer runtime onto the AE bucket, so a
	/// tolerant check would let a future game patch load this plugin against
	/// addresses that have moved. Free of game state so it can be tested on
	/// the host.
	[[nodiscard]] bool IsSupported(REL::Version a_version) noexcept;

	/// Name of the runtime bucket commonlibf4 resolved for the loaded process.
	/// Inspects the running module - never call this from a host test.
	[[nodiscard]] std::string_view BucketName() noexcept;
}
