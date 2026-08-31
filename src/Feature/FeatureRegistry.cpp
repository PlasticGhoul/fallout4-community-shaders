#include "Feature/FeatureRegistry.h"

#include <exception>

namespace Features
{
	namespace
	{
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

		REX::INFO("registered feature {}", a_feature->Name());
		_entries.emplace_back(Entry{ std::move(a_feature), State::kOff });
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

		if (Guarded(name, "Frame", [&] { a_entry.feature->Frame(); })) {
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
