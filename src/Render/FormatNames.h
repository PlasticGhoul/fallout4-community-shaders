#pragma once

// The project PCH stops at REL and REX; the DXGI and D3D11 declarations are
// separate headers. Naming those types in our own interface means including
// them here too.
#include <REX/W32/D3D11.h>
#include <REX/W32/DXGI.h>

namespace Render
{
	/// The DXGI format's name without its DXGI_FORMAT_ prefix, or "UNKNOWN".
	///
	/// Only the formats that plausibly appear as a render target are mapped;
	/// the full enum has well over a hundred entries and most are irrelevant
	/// here. Callers log the raw number alongside, so an unmapped format stays
	/// identifiable.
	[[nodiscard]] std::string_view FormatName(REX::W32::DXGI_FORMAT a_format) noexcept;

	/// The bind flags as text, joined with '|' in a fixed order regardless of
	/// how the caller combined them, so two identical flag sets always render
	/// identically. "-" when no flag is set.
	[[nodiscard]] std::string BindFlagsString(std::uint32_t a_bindFlags);
}
