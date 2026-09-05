#include "Render/Profiler.h"

#include "Render/Markers.h"
#include "Render/Renderer.h"

#include <REX/W32/KERNEL32.h>

#include <format>

namespace Render
{
	namespace
	{
		[[nodiscard]] std::int64_t Now() noexcept
		{
			std::int64_t counter = 0;
			static_cast<void>(REX::W32::QueryPerformanceCounter(std::addressof(counter)));
			return counter;
		}

		// A line every 600 frames rather than one per frame. A profiler that
		// floods the log is worse than one that says nothing.
		constexpr std::uint64_t kComplaintInterval = 600;
	}

	Profiler& Profiler::GetSingleton() noexcept
	{
		static Profiler profiler;
		return profiler;
	}

	bool Profiler::Initialize() noexcept
	{
		if (_ready) {
			return true;
		}

		// A refusal is final. Creating a query that failed once will fail
		// again, and retrying every frame would only fill the log.
		if (_refused) {
			return false;
		}

		if (GetDevice() == nullptr || GetContext() == nullptr) {
			_refused = true;
			REX::ERROR("profiler: no device or context, not measuring");
			return false;
		}

		std::int64_t frequency = 0;
		if (!REX::W32::QueryPerformanceFrequency(std::addressof(frequency)) || frequency == 0) {
			_refused = true;
			REX::ERROR("profiler: no performance counter, not measuring");
			return false;
		}

		_ticksToMs = 1000.0 / static_cast<double>(frequency);

		for (auto& slot : _slots) {
			if (!CreateSlot(slot)) {
				_refused = true;
				REX::ERROR("profiler: could not create a disjoint query, not measuring");
				Release();
				return false;
			}
		}

		_ready = true;
		REX::INFO("profiler ready, {} frames in flight", kFrameLatency);
		return true;
	}

	bool Profiler::CreateSlot(Slot& a_slot) noexcept
	{
		REX::W32::D3D11_QUERY_DESC desc{};
		desc.query = REX::W32::D3D11_QUERY_TIMESTAMP_DISJOINT;
		desc.miscFlags = 0;

		return GetDevice()->CreateQuery(std::addressof(desc), std::addressof(a_slot.disjoint)) >= 0 &&
		       a_slot.disjoint != nullptr;
	}

	REX::W32::ID3D11Query* Profiler::AcquireTimestamp() noexcept
	{
		if (!_spare.empty()) {
			auto* const reused = _spare.back();
			_spare.pop_back();
			return reused;
		}

		REX::W32::D3D11_QUERY_DESC desc{};
		desc.query = REX::W32::D3D11_QUERY_TIMESTAMP;
		desc.miscFlags = 0;

		REX::W32::ID3D11Query* query = nullptr;
		if (GetDevice()->CreateQuery(std::addressof(desc), std::addressof(query)) < 0) {
			return nullptr;
		}

		return query;
	}

	void Profiler::RecycleTimings(Slot& a_slot) noexcept
	{
		for (auto& timing : a_slot.timings) {
			_spare.push_back(timing.begin);
			_spare.push_back(timing.end);
		}

		a_slot.timings.clear();
		a_slot.inFlight = false;
	}

	void Profiler::ReleaseSlot(Slot& a_slot) noexcept
	{
		for (auto& timing : a_slot.timings) {
			if (timing.begin != nullptr) {
				timing.begin->Release();
			}
			if (timing.end != nullptr) {
				timing.end->Release();
			}
		}

		a_slot.timings.clear();
		a_slot.inFlight = false;

		if (a_slot.disjoint != nullptr) {
			a_slot.disjoint->Release();
			a_slot.disjoint = nullptr;
		}
	}

	void Profiler::Release() noexcept
	{
		for (auto& slot : _slots) {
			ReleaseSlot(slot);
		}

		for (auto* const query : _spare) {
			query->Release();
		}

		_spare.clear();
		_passes.Clear();
		_open.clear();
		_ready = false;
		_frameOpen = false;
	}

	void Profiler::SetMeasuring(bool a_measuring) noexcept
	{
		if (a_measuring == _measuring) {
			return;
		}

		_measuring = a_measuring;

		// Samples from before a pause next to samples from after it would
		// average into a number that never happened.
		_passes.Clear();
		_frameGpuMs = 0.0f;
		_frameCpuMs = 0.0f;
	}

	void Profiler::BeginFrame() noexcept
	{
		if (!_measuring || !Initialize()) {
			return;
		}

		++_frame;

		auto& slot = _slots[_writeSlot];
		if (slot.inFlight) {
			// Still not back after a whole turn around the ring. Waiting for it
			// would stall the render thread, which is the one thing this must
			// never do.
			++_discarded;
			if (_frame % kComplaintInterval == 0) {
				REX::WARN("profiler: dropping slots that never came back, {} so far", _discarded);
			}

			RecycleTimings(slot);
		}

		slot.frame = _frame;
		GetContext()->Begin(slot.disjoint);

		_frameOpen = true;
		BeginPass("Frame"sv);
	}

	void Profiler::EndFrame() noexcept
	{
		if (!_frameOpen) {
			return;
		}

		// Whatever a feature left open is closed here rather than carried into
		// the next frame, where its timestamps would belong to another slot.
		while (!_open.empty()) {
			EndPass();
		}

		auto& slot = _slots[_writeSlot];
		GetContext()->End(slot.disjoint);
		slot.inFlight = true;

		_writeSlot = (_writeSlot + 1) % kFrameLatency;
		_frameOpen = false;
	}

	void Profiler::BeginPass(std::string_view a_name) noexcept
	{
		if (!_measuring || !_ready || !_frameOpen) {
			return;
		}

		auto& slot = _slots[_writeSlot];

		Timing timing;
		timing.name = std::string{ a_name };
		timing.depth = static_cast<std::uint32_t>(_open.size());
		timing.begin = AcquireTimestamp();
		timing.end = AcquireTimestamp();
		timing.cpuBegin = Now();

		if (timing.begin == nullptr || timing.end == nullptr) {
			if (timing.begin != nullptr) {
				_spare.push_back(timing.begin);
			}
			return;
		}

		// A timestamp query is issued with End alone; Begin belongs to the range
		// queries and would be rejected here.
		GetContext()->End(timing.begin);

		// Widened on the stack: BeginEvent copies the string, so it does not
		// have to outlive this call. Every pass name comes from our own source
		// and is ASCII; a name out of a translation file never reaches here.
		const std::wstring wide{ a_name.begin(), a_name.end() };
		timing.marked = PushMarker(wide.c_str());

		slot.timings.push_back(std::move(timing));
		_open.push_back(slot.timings.size() - 1);
	}

	void Profiler::EndPass() noexcept
	{
		if (_open.empty()) {
			return;
		}

		auto& slot = _slots[_writeSlot];
		auto& timing = slot.timings[_open.back()];
		_open.pop_back();

		GetContext()->End(timing.end);
		timing.cpuMs = static_cast<float>(static_cast<double>(Now() - timing.cpuBegin) * _ticksToMs);

		if (timing.marked) {
			PopMarker();
		}
	}

	void Profiler::Collect() noexcept
	{
		if (!_ready || !_measuring) {
			return;
		}

		for (auto& slot : _slots) {
			if (slot.inFlight) {
				CollectSlot(slot);
			}
		}

		_passes.Retire(_frame);
	}

	void Profiler::CollectSlot(Slot& a_slot) noexcept
	{
		auto* const context = GetContext();

		REX::W32::D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
		if (context->GetData(
				a_slot.disjoint,
				std::addressof(disjoint),
				sizeof(disjoint),
				REX::W32::D3D11_ASYNC_GETDATA_DONOTFLUSH) != 0) {
			return;  // S_FALSE: not ready. Asked again next frame.
		}

		const bool usable = disjoint.disjoint == 0 && disjoint.frequency != 0;
		if (!usable) {
			// The GPU clock moved during this frame. Every number in it would be
			// a lie, so none of them is kept.
			++_discarded;
		}

		for (auto& timing : a_slot.timings) {
			std::uint64_t begin = 0;
			std::uint64_t end = 0;

			const bool read =
				context->GetData(
					timing.begin, std::addressof(begin), sizeof(begin),
					REX::W32::D3D11_ASYNC_GETDATA_DONOTFLUSH) == 0 &&
				context->GetData(
					timing.end, std::addressof(end), sizeof(end),
					REX::W32::D3D11_ASYNC_GETDATA_DONOTFLUSH) == 0;

			if (usable && read && end >= begin) {
				const auto gpuMs = static_cast<float>(
					static_cast<double>(end - begin) * 1000.0 /
					static_cast<double>(disjoint.frequency));

				// Found by name, not by a remembered index: a retire between
				// issuing and collecting shifts every index behind the hole.
				if (const auto index = _passes.Find(timing.name); index != PassTable::kNoPass) {
					_passes.Sample(index, timing.depth, gpuMs, timing.cpuMs, a_slot.frame);
				} else if (_frame % kComplaintInterval == 0) {
					REX::WARN("profiler: no room left for the pass named {}", timing.name);
				}

				if (timing.depth == 0) {
					_frameGpuMs = gpuMs;
					_frameCpuMs = timing.cpuMs;
				}
			}
		}

		RecycleTimings(a_slot);
	}

	void Profiler::LogSnapshot() const noexcept
	{
		const auto results = _passes.Results();
		if (results.empty()) {
			REX::INFO("=== performance snapshot: nothing measured ===");
			return;
		}

		const auto samples =
			results.front().history != nullptr ? results.front().history->Count() : std::size_t{ 0 };

		REX::INFO("=== performance snapshot over {} frames ===", samples);
		REX::INFO("  {:<22} {:>7} {:>8} {:>8} {:>8}", "pass", "ms", "avg", "p95", "p99");

		for (const auto& result : results) {
			const std::string indented =
				std::string(static_cast<std::size_t>(result.depth) * 2, ' ') + result.name;

			REX::INFO("  {:<22} {:>7.2f} {:>8.2f} {:>8.2f} {:>8.2f}",
				indented,
				result.gpuMs,
				result.avgMs,
				result.p95Ms,
				result.p99Ms);
		}

		const float fps = _frameGpuMs > 0.0f ? 1000.0f / _frameGpuMs : 0.0f;
		REX::INFO("  {:.1f} fps, cpu {:.2f} ms, gpu {:.2f} ms, {} frame(s) discarded",
			fps,
			_frameCpuMs,
			_frameGpuMs,
			_discarded);
	}

	PassScope::PassScope(std::string_view a_name) noexcept
	{
		Profiler::GetSingleton().BeginPass(a_name);
	}

	PassScope::~PassScope() noexcept
	{
		Profiler::GetSingleton().EndPass();
	}
}
