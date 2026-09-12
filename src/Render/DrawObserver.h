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

	/// One draw call of the engine, as the hook saw it, in a shape that lets
	/// an observer issue the very same call again.
	struct DrawCall
	{
		enum class Kind : std::uint8_t
		{
			kIndexed,
			kPlain,
			kIndexedInstanced,
			kInstanced,
		};

		Kind kind{ Kind::kIndexed };
		std::uint32_t count{ 0 };          // indices or vertices, per instance
		std::uint32_t instanceCount{ 1 };  // one for the non-instanced kinds
		std::uint32_t start{ 0 };          // start index or start vertex
		std::int32_t baseVertex{ 0 };      // indexed kinds only
		std::uint32_t startInstance{ 0 };  // instanced kinds only

		/// Issues this call on a_context, through the engine's own entry, so
		/// that the hook does not see it a second time.
		void Repeat(REX::W32::ID3D11DeviceContext& a_context) const noexcept;
	};

	/// Somebody who wants to surround the engine's draw calls of one
	/// technique: swap targets and state before, put them back after, draw
	/// again in between. Called on the thread the engine draws on, from
	/// inside the draw call.
	///
	/// A draw the observer does not want passes straight through, with one
	/// atomic load and one comparison spent on it.
	class DrawObserver
	{
	public:
		virtual ~DrawObserver() = default;

		[[nodiscard]] virtual bool Wants(const CurrentTechnique& a_current) const noexcept = 0;
		virtual void BeforeDraw(REX::W32::ID3D11DeviceContext& a_context, const DrawCall& a_call) noexcept = 0;
		virtual void AfterDraw(REX::W32::ID3D11DeviceContext& a_context, const DrawCall& a_call) noexcept = 0;
	};
}
