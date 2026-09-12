#include "Features/SkyProbe.h"

#include "Render/DebugName.h"
#include "Render/DrawHook.h"
#include "Render/Renderer.h"
#include "Render/SwapChainHook.h"
#include "Render/TechniqueTracker.h"
#include "Settings/Settings.h"

#include <RE/B/BSGraphics.h>
#include <RE/M/Main.h>
#include <RE/N/NiCamera.h>

#include <REX/W32/DXGI.h>
#include <REX/W32/KERNEL32.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

namespace Features
{
	namespace
	{
		constexpr std::uint32_t kCloudsTechnique = 0x0005;
		constexpr std::uint64_t kSecond = 180;
		constexpr std::uint32_t kConstantSlots = 14;

		const char* BlendName(REX::W32::D3D11_BLEND a_blend)
		{
			switch (a_blend) {
			case REX::W32::D3D11_BLEND_ZERO:
				return "ZERO";
			case REX::W32::D3D11_BLEND_ONE:
				return "ONE";
			case REX::W32::D3D11_BLEND_SRC_COLOR:
				return "SRC_COLOR";
			case REX::W32::D3D11_BLEND_INV_SRC_COLOR:
				return "INV_SRC_COLOR";
			case REX::W32::D3D11_BLEND_SRC_ALPHA:
				return "SRC_ALPHA";
			case REX::W32::D3D11_BLEND_INV_SRC_ALPHA:
				return "INV_SRC_ALPHA";
			case REX::W32::D3D11_BLEND_DEST_ALPHA:
				return "DEST_ALPHA";
			case REX::W32::D3D11_BLEND_INV_DEST_ALPHA:
				return "INV_DEST_ALPHA";
			case REX::W32::D3D11_BLEND_DEST_COLOR:
				return "DEST_COLOR";
			case REX::W32::D3D11_BLEND_INV_DEST_COLOR:
				return "INV_DEST_COLOR";
			default:
				return "other";
			}
		}

		const char* KindName(Render::DrawCall::Kind a_kind)
		{
			switch (a_kind) {
			case Render::DrawCall::Kind::kIndexed:
				return "DrawIndexed";
			case Render::DrawCall::Kind::kPlain:
				return "Draw";
			case Render::DrawCall::Kind::kIndexedInstanced:
				return "DrawIndexedInstanced";
			case Render::DrawCall::Kind::kInstanced:
				return "DrawInstanced";
			default:
				return "?";
			}
		}

		/// Does the sixteen float window at a_data read as a_matrix, by rows
		/// or by columns, within a thousandth of its largest entry?
		const char* MatchMatrix(const float* a_data, const float (&a_matrix)[16])
		{
			float largest = 0.0f;
			for (const auto value : a_matrix) {
				largest = std::max(largest, std::abs(value));
			}
			const auto tolerance = std::max(largest * 1.0e-3f, 1.0e-3f);

			bool rows = true;
			bool columns = true;
			for (int r = 0; r < 4; ++r) {
				for (int c = 0; c < 4; ++c) {
					const auto expected = a_matrix[r * 4 + c];
					if (std::abs(a_data[r * 4 + c] - expected) > tolerance) {
						rows = false;
					}
					if (std::abs(a_data[c * 4 + r] - expected) > tolerance) {
						columns = false;
					}
				}
			}
			return rows ? "rows" : columns ? "columns" :
			                                 nullptr;
		}
	}

	void SkyProbe::Declare()
	{
		Settings::DeclareFeature("SkyProbe", true)
			.Label("feature.sky_probe.name", "Sky Probe")
			.Help("feature.sky_probe.help", "Diagnostic for F4. Goes away after the run.");
	}

	bool SkyProbe::Setup()
	{
		const auto index = Render::ClassIndexOf("BSSkyShader");
		if (!index.has_value()) {
			REX::ERROR("SkyProbe: BSSkyShader is not among the shader classes");
			return false;
		}
		_sky = Render::TechniqueFilter{ *index, Render::kAnyTechnique };

		LogFormatSupport();

		{
			const std::scoped_lock lock{ _mutex };
			_rows.clear();
			_threads.clear();
		}
		_frames = 0;
		_lastCounts = Render::DrawHookCountsSoFar();
		Render::SetDrawObserver(this);
		REX::INFO("SkyProbe: watching every BSSkyShader draw (class index {})", *index);
		return true;
	}

	void SkyProbe::Shutdown()
	{
		Render::SetDrawObserver(nullptr);
		const std::scoped_lock lock{ _mutex };
		for (auto& pending : _pending) {
			if (pending.staging != nullptr) {
				pending.staging->Release();
			}
		}
		_pending.clear();
	}

	bool SkyProbe::Wants(const Render::CurrentTechnique& a_current) const noexcept
	{
		return _sky.Matches(a_current);
	}

	void SkyProbe::BeforeDraw(REX::W32::ID3D11DeviceContext& a_context, const Render::DrawCall& a_call) noexcept
	{
		const std::scoped_lock lock{ _mutex };
		_threads.insert(REX::W32::GetCurrentThreadId());

		const auto current = Render::CurrentTechniqueOf();
		const auto frame = Render::FrameCount();

		REX::W32::ID3D11RenderTargetView* target = nullptr;
		a_context.OMGetRenderTargets(1, std::addressof(target), nullptr);
		std::string name = target != nullptr ?
		                       Render::GetViewTargetName(reinterpret_cast<REX::W32::ID3D11View*>(target)) :
		                       std::string{ "<none>" };
		if (name.empty()) {
			name = std::format("{}", static_cast<void*>(target));
		}

		// Which face of the engine's cubemap, if it is one.
		if (const auto* const data = RE::BSGraphics::GetRendererData(); data != nullptr && target != nullptr) {
			for (std::uint32_t face = 0; face < 6; ++face) {
				if (data->cubeMapRenderTargets[0].rtView[face] == target) {
					name += std::format(" face {}", face);
				}
			}
		}
		if (target != nullptr) {
			target->Release();
		}

		auto& row = _rows[std::format("0x{:04X} {} -> {}", current.technique, KindName(a_call.kind), name)];
		++row.second;
		++row.total;

		if (current.technique == kCloudsTechnique && frame != _lastFrameSeen) {
			_lastFrameSeen = frame;
			++_cloudFrames;

			if (frame - _blendLoggedFrame >= kSecond) {
				_blendLoggedFrame = frame;
				LogBlendState(a_context);
			}

			// The main view only: that is the draw Weg B would repeat.
			if (_dumpRequested && name.starts_with("FO4_RT_004") && _pending.empty()) {
				_dumpRequested = false;
				CopyVertexConstants(a_context);
			}
		}
	}

	void SkyProbe::AfterDraw(REX::W32::ID3D11DeviceContext&, const Render::DrawCall&) noexcept
	{}

	void SkyProbe::LogBlendState(REX::W32::ID3D11DeviceContext& a_context)
	{
		REX::W32::ID3D11BlendState* state = nullptr;
		float factor[4]{};
		std::uint32_t mask = 0;
		a_context.OMGetBlendState(std::addressof(state), factor, std::addressof(mask));
		if (state == nullptr) {
			REX::INFO("SkyProbe: clouds draw with the default blend state (no blending)");
			return;
		}

		REX::W32::D3D11_BLEND_DESC desc{};
		state->GetDesc(std::addressof(desc));
		state->Release();

		const auto& target = desc.renderTarget[0];
		REX::INFO(
			"SkyProbe: clouds blend enable {} colour {} / {} op {} alpha {} / {} op {} write mask 0x{:X}",
			target.blendEnable ? "yes" : "no",
			BlendName(target.srcBlend), BlendName(target.destBlend),
			static_cast<int>(target.blendOp),
			BlendName(target.srcBlendAlpha), BlendName(target.destBlendAlpha),
			static_cast<int>(target.blendOpAlpha),
			target.renderTargetWriteMask);
	}

	void SkyProbe::CopyVertexConstants(REX::W32::ID3D11DeviceContext& a_context)
	{
		auto* const device = Render::GetDevice();
		auto* const world = RE::Main::WorldRootCamera();
		if (device == nullptr || world == nullptr) {
			return;
		}
		std::memcpy(_worldToCamAtDump, world->worldToCam, sizeof(_worldToCamAtDump));
		_dumpFrame = Render::FrameCount();

		REX::W32::ID3D11Buffer* buffers[kConstantSlots]{};
		a_context.VSGetConstantBuffers(0, kConstantSlots, buffers);

		for (std::uint32_t slot = 0; slot < kConstantSlots; ++slot) {
			auto* const source = buffers[slot];
			if (source == nullptr) {
				continue;
			}

			REX::W32::D3D11_BUFFER_DESC desc{};
			source->GetDesc(std::addressof(desc));

			REX::W32::D3D11_BUFFER_DESC stagingDesc{};
			stagingDesc.byteWidth = desc.byteWidth;
			stagingDesc.usage = REX::W32::D3D11_USAGE_STAGING;
			stagingDesc.cpuAccessFlags = REX::W32::D3D11_CPU_ACCESS_READ;

			REX::W32::ID3D11Buffer* staging = nullptr;
			if (device->CreateBuffer(std::addressof(stagingDesc), nullptr, std::addressof(staging)) >= 0) {
				a_context.CopyResource(staging, source);
				_pending.push_back({ slot, staging, desc.byteWidth });
			}
			source->Release();
		}

		REX::INFO(
			"SkyProbe: copied {} vertex constant buffer(s) at the clouds draw, frame {}",
			_pending.size(),
			_dumpFrame);
	}

	void SkyProbe::ReadVertexConstants()
	{
		const std::scoped_lock lock{ _mutex };
		auto* const context = Render::GetContext();
		if (context == nullptr || _pending.empty()) {
			return;
		}

		// Read a frame later: the copy was queued behind the engine's own
		// work, and a wait here would stall the render thread.
		if (Render::FrameCount() <= _dumpFrame + 1) {
			return;
		}

		for (auto& pending : _pending) {
			REX::W32::D3D11_MAPPED_SUBRESOURCE mapped{};
			if (context->Map(
					pending.staging, 0, REX::W32::D3D11_MAP_READ, REX::W32::D3D11_MAP_FLAG_DO_NOT_WAIT,
					std::addressof(mapped)) < 0) {
				REX::INFO("SkyProbe: vs cb slot {} not readable yet", pending.slot);
				continue;
			}

			const auto* const floats = static_cast<const float*>(mapped.data);
			const auto count = pending.bytes / sizeof(float);
			const char* found = nullptr;
			std::size_t foundAt = 0;
			for (std::size_t offset = 0; offset + 16 <= count; offset += 4) {
				if (const auto* const how = MatchMatrix(floats + offset, _worldToCamAtDump); how != nullptr) {
					found = how;
					foundAt = offset * sizeof(float);
					break;
				}
			}

			if (found != nullptr) {
				REX::INFO(
					"SkyProbe: worldToCam FOUND in vs cb slot {} at byte {} stored by {}",
					pending.slot, foundAt, found);
			} else {
				std::string head;
				for (std::size_t i = 0; i < std::min<std::size_t>(count, 64); ++i) {
					head += std::format("{}{:.4g}", i == 0 ? "" : " ", floats[i]);
				}
				REX::INFO("SkyProbe: vs cb slot {} ({} bytes) no match, head: {}", pending.slot, pending.bytes, head);
			}

			context->Unmap(pending.staging, 0);
			pending.staging->Release();
			pending.staging = nullptr;
		}

		std::erase_if(_pending, [](const PendingBuffer& a_pending) { return a_pending.staging == nullptr; });
	}

	void SkyProbe::LogFormatSupport()
	{
		auto* const device = Render::GetDevice();
		if (device == nullptr) {
			return;
		}

		std::uint32_t support = 0;
		const auto hr = device->CheckFormatSupport(REX::W32::DXGI_FORMAT_A8_UNORM, std::addressof(support));
		REX::INFO(
			"SkyProbe: A8_UNORM support 0x{:X} (hr {}) render target {} blendable {}",
			support, hr,
			(support & REX::W32::D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0 ? "yes" : "no",
			(support & REX::W32::D3D11_FORMAT_SUPPORT_BLENDABLE) != 0 ? "yes" : "no");
	}

	void SkyProbe::Frame()
	{
		++_frames;
		ReadVertexConstants();

		if (_frames % kSecond != 0) {
			return;
		}

		const auto counts = Render::DrawHookCountsSoFar();
		REX::INFO(
			"SkyProbe: in {} frames: DrawIndexed {}, Draw {}, DrawIndexedInstanced {}, DrawInstanced {}, "
			"ExecuteCommandList {}",
			kSecond,
			counts.indexed - _lastCounts.indexed,
			counts.plain - _lastCounts.plain,
			counts.indexedInstanced - _lastCounts.indexedInstanced,
			counts.instanced - _lastCounts.instanced,
			counts.executeCommandList - _lastCounts.executeCommandList);
		_lastCounts = counts;

		const std::scoped_lock lock{ _mutex };
		std::string threads;
		for (const auto id : _threads) {
			threads += std::format("{}{}", threads.empty() ? "" : " ", id);
		}
		REX::INFO(
			"SkyProbe: clouds drawn in {} frame(s), sky draws on {} thread(s) [{}]",
			_cloudFrames, _threads.size(), threads);
		_cloudFrames = 0;

		for (auto& [key, row] : _rows) {
			REX::INFO("  sky {}: {} this second, {} since start", key, row.second, row.total);
			row.second = 0;
		}

		_dumpRequested = true;
	}
}
