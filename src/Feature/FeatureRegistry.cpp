#include "Feature/FeatureRegistry.h"

#include <exception>

namespace Features
{
	namespace
	{
		void (*g_beginTiming)(std::string_view) = nullptr;
		void (*g_endTiming)() = nullptr;

		/// RAII, because Frame is called through Guarded and an exception on the
		/// way out would otherwise leave a pass open forever.
		class FrameTimingScope
		{
		public:
			explicit FrameTimingScope(std::string_view a_name) noexcept :
				_open(g_beginTiming != nullptr)
			{
				if (_open) {
					g_beginTiming(a_name);
				}
			}

			~FrameTimingScope() noexcept
			{
				if (_open && g_endTiming != nullptr) {
					g_endTiming();
				}
			}

			FrameTimingScope(const FrameTimingScope&) = delete;
			FrameTimingScope& operator=(const FrameTimingScope&) = delete;

		private:
			bool _open{ false };
		};

		// Every call into a feature goes through here. An exception escaping
		// into our Present hook would land in the engine, and what Fallout 4
		// does with that is not worth finding out. Costs nothing until it
		// throws.
		template <class F>
		bool Guarded(std::string_view a_name, const char* a_what, F&& a_call) noexcept
		{
			try {
				a_call();
				return true;
			} catch (const std::exception& e) {
				REX::ERROR("{}: {} threw: {}", a_name, a_what, e.what());
			} catch (...) {
				REX::ERROR("{}: {} threw an unknown exception", a_name, a_what);
			}
			return false;
		}
	}

	void Registry::Register(std::unique_ptr<Feature> a_feature)
	{
		if (a_feature == nullptr) {
			return;
		}

		// Declared before it goes into the table, and guarded like every other
		// call into a feature. A throw here says the feature's settings are
		// missing, not that the feature cannot run, so it is still registered:
		// its switch then reads as off, which is a state the player can see.
		static_cast<void>(Guarded(a_feature->Name(), "Declare", [&] { a_feature->Declare(); }));

		REX::INFO("registered feature {}", a_feature->Name());
		_entries.emplace_back(Entry{ std::move(a_feature), State::kOff });
	}

	void Registry::ForEach(
		const std::function<void(std::string_view, State)>& a_visit) const noexcept
	{
		for (const auto& entry : _entries) {
			a_visit(entry.feature->Name(), entry.state);
		}
	}

	void Registry::Tick(const EnabledQuery& a_query) noexcept
	{
		// Teardown first, and in reverse: a frame never has both the old and
		// the new owner of a resource alive at once.
		for (auto it = _entries.rbegin(); it != _entries.rend(); ++it) {
			if (it->state == State::kRunning && !a_query(it->feature->Name())) {
				ShutdownEntry(*it);
			}
		}

		for (auto& entry : _entries) {
			if (entry.state == State::kOff && a_query(entry.feature->Name())) {
				SetupEntry(entry);
			}
		}

		for (auto& entry : _entries) {
			if (entry.state == State::kRunning) {
				FrameEntry(entry);
			}
		}
	}

	void SetFrameTiming(void (*a_begin)(std::string_view), void (*a_end)()) noexcept
	{
		g_beginTiming = a_begin;
		g_endTiming = a_end;
	}

	void Registry::ClearRefusals() noexcept
	{
		for (auto& entry : _entries) {
			if (entry.state == State::kRefused) {
				entry.state = State::kOff;
			}
		}
	}

	State Registry::StateOf(std::string_view a_name) const noexcept
	{
		for (const auto& entry : _entries) {
			if (entry.feature->Name() == a_name) {
				return entry.state;
			}
		}
		return State::kOff;
	}

	std::size_t Registry::Count() const noexcept
	{
		return _entries.size();
	}

	void Registry::SetupEntry(Entry& a_entry) noexcept
	{
		const auto name = a_entry.feature->Name();

		bool accepted = false;
		const bool survived = Guarded(name, "Setup", [&] { accepted = a_entry.feature->Setup(); });

		if (survived && accepted) {
			a_entry.state = State::kRunning;
			REX::INFO("{}: running", name);
			return;
		}

		// Even a Setup that gave up halfway may hold something, so it still
		// gets its Shutdown before being written off.
		ShutdownEntry(a_entry);
		a_entry.state = State::kRefused;
		REX::ERROR("{}: refused", name);
	}

	void Registry::FrameEntry(Entry& a_entry) noexcept
	{
		const auto name = a_entry.feature->Name();

		// The one place every feature is measured from. A feature contributes
		// nothing for this, and gets nested passes of its own for free from F2
		// on, because the profiler keeps a stack.
		//
		// Only Frame is bracketed. Setup and Shutdown do not run per frame, and
		// a rolling history over single events says nothing.
		if (Guarded(name, "Frame", [&] {
				const FrameTimingScope timing{ name };
				a_entry.feature->Frame();
			})) {
			return;
		}

		ShutdownEntry(a_entry);
		a_entry.state = State::kRefused;
		REX::ERROR("{}: refused after throwing from Frame", name);
	}

	void Registry::ShutdownEntry(Entry& a_entry) noexcept
	{
		const auto name = a_entry.feature->Name();
		static_cast<void>(Guarded(name, "Shutdown", [&] { a_entry.feature->Shutdown(); }));

		// Set regardless: a Shutdown that threw leaves nothing better to do
		// than to stop calling into the feature.
		a_entry.state = State::kOff;
		REX::INFO("{}: off", name);
	}

	Registry& TheRegistry() noexcept
	{
		// Deliberately leaked. A function local static would be destroyed from
		// inside DLL detach, which would tear down features while the loader
		// lock is held and the engine is already gone: joining a thread there
		// deadlocks, and handing a pointer back to freed engine memory is
		// worse than not handing it back at all. The process is ending anyway.
		static Registry* const registry = new Registry;
		return *registry;
	}
}
