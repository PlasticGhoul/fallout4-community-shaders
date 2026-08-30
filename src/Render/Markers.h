#pragma once

namespace Render
{
	/// Fetches ID3DUserDefinedAnnotation from the immediate context. Returns
	/// false when the interface is unavailable; markers then become no-ops and
	/// everything else carries on.
	bool InitMarkers() noexcept;

	/// Opens a named region on construction and closes it on destruction.
	///
	/// Safe to leave in shipping code: with no capture tool attached the
	/// annotation calls are close to free, so there is no reason to hide this
	/// behind a switch.
	class MarkerScope
	{
	public:
		explicit MarkerScope(const wchar_t* a_name) noexcept;
		~MarkerScope() noexcept;

		MarkerScope(const MarkerScope&) = delete;
		MarkerScope& operator=(const MarkerScope&) = delete;

	private:
		bool m_open{ false };
	};
}
