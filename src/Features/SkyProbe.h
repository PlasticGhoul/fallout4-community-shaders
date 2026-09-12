#pragma once

#include "Feature/Feature.h"
#include "Render/DrawHook.h"
#include "Render/DrawObserver.h"

#include <REX/W32/D3D11.h>

#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace Features
{
	/// diag: where does the engine draw its sky, with what blend state, and
	/// where in the vertex constants is the view projection? Answers the
	/// three questions the F4 spec asks before the capture is built. Goes
	/// away with the run.
	class SkyProbe final : public Feature, public Render::DrawObserver
	{
	public:
		[[nodiscard]] std::string_view Name() const override { return "SkyProbe"; }
		void Declare() override;
		[[nodiscard]] bool Setup() override;
		void Frame() override;
		void Shutdown() override;

		[[nodiscard]] bool Wants(const Render::CurrentTechnique& a_current) const noexcept override;
		void BeforeDraw(REX::W32::ID3D11DeviceContext& a_context, const Render::DrawCall& a_call) noexcept override;
		void AfterDraw(REX::W32::ID3D11DeviceContext& a_context, const Render::DrawCall& a_call) noexcept override;

	private:
		struct Row
		{
			std::uint64_t second{ 0 };
			std::uint64_t total{ 0 };
		};

		struct PendingBuffer
		{
			std::uint32_t slot{ 0 };
			REX::W32::ID3D11Buffer* staging{ nullptr };
			std::uint32_t bytes{ 0 };
		};

		void LogBlendState(REX::W32::ID3D11DeviceContext& a_context);
		void CopyVertexConstants(REX::W32::ID3D11DeviceContext& a_context);
		void ReadVertexConstants();
		void LogFormatSupport();

		Render::TechniqueFilter _sky{};

		/// The thunk may run on whichever thread the engine draws on; the
		/// once-a-second report runs from Present. One lock for everything
		/// below it - a probe, not a hot path.
		std::mutex _mutex;
		std::map<std::string, Row> _rows;
		std::set<std::uint32_t> _threads;
		std::uint64_t _frames{ 0 };
		std::uint64_t _lastFrameSeen{ 0 };
		Render::DrawHookCounts _lastCounts{};
		std::uint64_t _cloudFrames{ 0 };
		std::uint64_t _blendLoggedFrame{ 0 };
		std::uint64_t _dumpFrame{ 0 };
		bool _dumpRequested{ false };
		std::vector<PendingBuffer> _pending;
		float _worldToCamAtDump[16]{};
	};
}
