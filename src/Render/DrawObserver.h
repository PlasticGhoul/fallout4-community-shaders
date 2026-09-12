#pragma once

#include "Render/TechniqueTracker.h"

#include <cstddef>
#include <cstdint>

namespace REX::W32
{
	struct ID3D11DeviceContext;
}

namespace Render
{
	/// Matches every technique of the class.
	inline constexpr std::uint32_t kAnyTechnique = 0xFFFFFFFFu;

	/// Which draws an observer wants: those issued while this class has this
	/// technique set up. Pure, so that it can be tested without a device.
	struct TechniqueFilter
	{
		std::size_t classIndex{ 0 };
		std::uint32_t technique{ kAnyTechnique };

		[[nodiscard]] bool Matches(const CurrentTechnique& a_current) const noexcept;
	};

	/// Somebody who wants to surround the engine's draw calls of one
	/// technique: swap targets and state before, put them back after, draw
	/// again in between. Called on the render thread from inside DrawIndexed.
	///
	/// A draw the observer does not want passes straight through, with one
	/// atomic load and one comparison spent on it.
	class DrawObserver
	{
	public:
		virtual ~DrawObserver() = default;

		[[nodiscard]] virtual bool Wants(const CurrentTechnique& a_current) const noexcept = 0;
		virtual void BeforeDraw(REX::W32::ID3D11DeviceContext& a_context) noexcept = 0;
		virtual void AfterDraw(
			REX::W32::ID3D11DeviceContext& a_context,
			std::uint32_t a_indexCount,
			std::uint32_t a_startIndex,
			std::int32_t a_baseVertex) noexcept = 0;
	};
}
