#pragma once

namespace Render
{
	/// Validates the renderer, then replaces IDXGISwapChain::Present with our
	/// thunk. Does nothing but log when validation fails. Safe to call once;
	/// further calls are ignored.
	void InstallSwapChainHook() noexcept;

	/// Frames observed since the hook was installed.
	[[nodiscard]] std::uint64_t FrameCount() noexcept;
}
