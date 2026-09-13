#include "Features/ShadowProbe.h"

#include "Render/DrawHook.h"
#include "Render/Renderer.h"
#include "Render/SwapChainHook.h"
#include "Render/Targets.h"
#include "Render/TechniqueTracker.h"
#include "Settings/Settings.h"

#include <RE/B/BSGraphics.h>
#include <RE/N/NiLight.h>
#include <RE/S/Sky.h>
// RE/S/Sun.h is not self-contained; see ScreenSpaceShadows.cpp.
#include <RE/S/SkyObject.h>
namespace RE
{
	class BSShaderAccumulator;
	class BSTriShape;
	class NiBillboardNode;
	class NiDirectionalLight;
}
#include <RE/S/Sun.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

namespace Features
{
	namespace
	{
		constexpr std::uint64_t kLogInterval = 180;

		/// Sixteen floats of a 64 byte block, three decimals each.
		std::string Row(const std::uint8_t* a_data, std::uint32_t a_bytes)
		{
			std::string row;
			for (std::uint32_t offset = 0; offset + sizeof(float) <= a_bytes; offset += sizeof(float)) {
				float value = 0.0f;
				std::memcpy(std::addressof(value), a_data + offset, sizeof(value));
				row += std::format("{:9.3f}", value);
			}
			return row;
		}

		/// Whether a 64 byte block ends in the row 0 0 0 1, which is what a
		/// 4x4 matrix in rows looks like when it is a view projection or a
		/// world transform with nothing in its last row.
		std::string OrthoHint(const std::uint8_t* a_data, std::uint32_t a_bytesLeft)
		{
			if (a_bytesLeft < 64) {
				return {};
			}
			float last[4]{};
			std::memcpy(last, a_data + 48, sizeof(last));
			const bool zeroes = std::abs(last[0]) < 1e-4f && std::abs(last[1]) < 1e-4f && std::abs(last[2]) < 1e-4f;
			const bool one = std::abs(last[3] - 1.0f) < 1e-4f;
			return zeroes && one ? "  <- 4x4, last row 0 0 0 1" : "";
		}
	}

	void ShadowProbe::Declare()
	{
		Settings::DeclareFeature("ShadowProbe", false)
			.Label("feature.shadow_probe.name", "Shadow Probe")
			.Help(
				"feature.shadow_probe.help",
				"Logs what the sun's shadow casters bind, once a second. A probe for F5; removed "
				"after its run.");
	}

	bool ShadowProbe::Setup()
	{
		_frame = 0;
		_frames = 0;
		_perFrameShadow = 0;
		_perFrameOther = 0;
		_lastFrameShadow = 0;
		_lastFrameOther = 0;
		_shadowDraws = 0;
		_otherDraws = 0;
		_otherBySlot.fill(0);
		_otherTechniques.fill(0);
		_otherTechniqueCount = 0;
		_dumpedThisSecond = false;

		const auto index = Render::ClassIndexOf("BSUtilityShader");
		if (!index.has_value()) {
			REX::ERROR("ShadowProbe: BSUtilityShader is not among the shader classes");
			return false;
		}
		_utility = Render::TechniqueFilter{ *index, Render::kAnyTechnique };
		if (!Render::AddDrawObserver(this)) {
			return false;
		}
		REX::INFO("ShadowProbe: watching BSUtilityShader, shadow map is slot {}", Render::Targets::kShadowMap);
		return true;
	}

	void ShadowProbe::Frame()
	{
		ReadSun();
		if (_frames == 0 || _frames % kLogInterval != 0 || _dumpedThisSecond) {
			return;
		}
		// Reached when no shadow caster dumped this second - indoors, or the
		// sun is down - so that the line is not simply missing.
		_dumpedThisSecond = true;
		std::string techniques;
		for (std::size_t i = 0; i < _otherTechniqueCount; ++i) {
			techniques += std::format("{}0x{:X}", i == 0 ? "" : " ", _otherTechniques[i]);
		}
		REX::INFO(
			"shadow probe: other utility targets DS_005 {} DS_007 {} DS_009 {} ({} other draw(s) total, {} caster(s)), techniques [{}]",
			_otherBySlot[5],
			_otherBySlot[7],
			_otherBySlot[9],
			_otherDraws,
			_shadowDraws,
			techniques);
	}

	void ShadowProbe::Shutdown()
	{
		Render::RemoveDrawObserver(this);
		for (auto*& buffer : _staging) {
			if (buffer != nullptr) {
				buffer->Release();
				buffer = nullptr;
			}
		}
		for (auto& last : _last) {
			last.clear();
		}
	}

	bool ShadowProbe::Wants(const Render::CurrentTechnique& a_current) const noexcept
	{
		return _utility.Matches(a_current);
	}

	void ShadowProbe::BeforeDraw(REX::W32::ID3D11DeviceContext& a_context, const Render::DrawCall&) noexcept
	{
		REX::W32::ID3D11DepthStencilView* dsv = nullptr;
		a_context.OMGetRenderTargets(0, nullptr, std::addressof(dsv));

		const auto frame = Render::FrameCount();
		if (frame != _frame) {
			_frame = frame;
			++_frames;
			_lastFrameShadow = _perFrameShadow;
			_lastFrameOther = _perFrameOther;
			_perFrameShadow = 0;
			_perFrameOther = 0;
			if (_frames % kLogInterval == 1) {
				_dumpedThisSecond = false;
			}
		}

		const bool shadow = dsv != nullptr && dsv == Render::Targets::DepthStencilView(Render::Targets::kShadowMap);
		if (shadow) {
			++_perFrameShadow;
			++_shadowDraws;
		} else {
			++_perFrameOther;
			++_otherDraws;
			for (std::size_t slot = 0; slot < kOtherSlots; ++slot) {
				if (dsv != nullptr && dsv == Render::Targets::DepthStencilView(slot)) {
					++_otherBySlot[slot];
				}
			}
			RememberTechnique(Render::CurrentTechniqueOf().technique);
		}
		if (dsv != nullptr) {
			dsv->Release();
		}

		if (shadow && _perFrameShadow == 1 && _frames % kLogInterval == 0 && !_dumpedThisSecond) {
			_dumpedThisSecond = true;
			DumpBuffers(a_context, frame);
		}
	}

	void ShadowProbe::AfterDraw(REX::W32::ID3D11DeviceContext&, const Render::DrawCall&) noexcept
	{}

	void ShadowProbe::DumpBuffers(REX::W32::ID3D11DeviceContext& a_context, std::uint64_t a_frame) noexcept
	{
		REX::W32::ID3D11Buffer* buffers[kSlots]{};
		a_context.VSGetConstantBuffers(0, kSlots, buffers);

		std::string techniques;
		for (std::size_t i = 0; i < _otherTechniqueCount; ++i) {
			techniques += std::format("{}0x{:X}", i == 0 ? "" : " ", _otherTechniques[i]);
		}
		REX::INFO(
			"shadow probe: frame {}, {} caster(s) and {} other utility draw(s) last frame, sun [{:.3f} {:.3f} {:.3f}], technique 0x{:X}",
			a_frame,
			_lastFrameShadow,
			_lastFrameOther,
			_sun[0],
			_sun[1],
			_sun[2],
			Render::CurrentTechniqueOf().technique);
		REX::INFO(
			"shadow probe: other utility targets DS_005 {} DS_007 {} DS_009 {} ({} other draw(s) total), techniques [{}]",
			_otherBySlot[5],
			_otherBySlot[7],
			_otherBySlot[9],
			_otherDraws,
			techniques);

		for (std::uint32_t slot = 0; slot < kSlots; ++slot) {
			auto* const buffer = buffers[slot];
			if (buffer == nullptr) {
				continue;
			}
			REX::W32::D3D11_BUFFER_DESC desc{};
			buffer->GetDesc(std::addressof(desc));
			const auto bytes = std::min<std::uint32_t>(desc.byteWidth, kDumpBytes);
			if (!EnsureStaging(slot, desc.byteWidth)) {
				REX::INFO("  vs slot {}: {} bytes, no staging buffer", slot, desc.byteWidth);
				buffer->Release();
				continue;
			}
			a_context.CopyResource(_staging[slot], buffer);
			REX::W32::D3D11_MAPPED_SUBRESOURCE mapped{};
			if (a_context.Map(_staging[slot], 0, REX::W32::D3D11_MAP_READ, 0, std::addressof(mapped)) < 0) {
				REX::INFO("  vs slot {}: {} bytes, map failed", slot, desc.byteWidth);
				buffer->Release();
				continue;
			}
			const auto* const data = static_cast<const std::uint8_t*>(mapped.data);

			std::string changed = ", first dump";
			auto& last = _last[slot];
			if (last.size() == bytes) {
				std::uint32_t differing = 0;
				for (std::uint32_t i = 0; i < bytes; ++i) {
					differing += last[i] != data[i] ? 1u : 0u;
				}
				changed = std::format(", {} byte(s) changed since the last dump", differing);
			}
			REX::INFO("  vs slot {}: {} bytes{}", slot, desc.byteWidth, changed);
			for (std::uint32_t offset = 0; offset < bytes; offset += 64) {
				const auto left = bytes - offset;
				REX::INFO("    +{:03}: {}{}", offset, Row(data + offset, std::min(left, 64u)), OrthoHint(data + offset, left));
			}
			last.assign(data, data + bytes);
			a_context.Unmap(_staging[slot], 0);
			buffer->Release();
		}
	}

	bool ShadowProbe::EnsureStaging(std::uint32_t a_slot, std::uint32_t a_bytes) noexcept
	{
		auto*& buffer = _staging[a_slot];
		if (buffer != nullptr) {
			REX::W32::D3D11_BUFFER_DESC desc{};
			buffer->GetDesc(std::addressof(desc));
			if (desc.byteWidth == a_bytes) {
				return true;
			}
			buffer->Release();
			buffer = nullptr;
		}
		auto* const device = Render::GetDevice();
		if (device == nullptr) {
			return false;
		}
		REX::W32::D3D11_BUFFER_DESC desc{};
		desc.byteWidth = a_bytes;
		desc.usage = REX::W32::D3D11_USAGE_STAGING;
		desc.bindFlags = 0;
		desc.cpuAccessFlags = REX::W32::D3D11_CPU_ACCESS_READ;
		return device->CreateBuffer(std::addressof(desc), nullptr, std::addressof(buffer)) >= 0;
	}

	void ShadowProbe::RememberTechnique(std::uint32_t a_technique) noexcept
	{
		for (std::size_t i = 0; i < _otherTechniqueCount; ++i) {
			if (_otherTechniques[i] == a_technique) {
				return;
			}
		}
		if (_otherTechniqueCount < kTechniques) {
			_otherTechniques[_otherTechniqueCount++] = a_technique;
		}
	}

	void ShadowProbe::ReadSun() noexcept
	{
		// Row zero of the light's rotation, negated: towards the sun, as
		// F2 and F4 read it. The shadow map's view projection looks along
		// the light, so its third row should be this vector or its negation.
		const auto* const sky = RE::Sky::GetSingleton();
		if (sky == nullptr || sky->sun == nullptr || sky->sun->light.get() == nullptr) {
			return;
		}
		const auto* const light = reinterpret_cast<const RE::NiLight*>(sky->sun->light.get());
		const auto& rotate = light->GetWorldRotate();
		float towards[3]{ -rotate.entry[0][0], -rotate.entry[0][1], -rotate.entry[0][2] };
		const auto length = std::sqrt(towards[0] * towards[0] + towards[1] * towards[1] + towards[2] * towards[2]);
		if (length < 1e-4f) {
			return;
		}
		for (int i = 0; i < 3; ++i) {
			_sun[i] = towards[i] / length;
		}
	}
}
