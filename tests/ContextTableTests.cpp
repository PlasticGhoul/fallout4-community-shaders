#include "Render/ContextTable.h"

#include <array>
#include <cstdio>

namespace
{
	int g_failures = 0;

	void Check(bool a_passed, const char* a_what)
	{
		std::printf("%s  %s\n", a_passed ? "ok  " : "FAIL", a_what);
		if (!a_passed) {
			++g_failures;
		}
	}

	/// Laid out like the runtime's context: the first word points at the
	/// table that follows it.
	struct FakeContext
	{
		void** vtable;
		void* embedded[Render::ContextTable::kSlots];
	};

	void* g_lastSelf = nullptr;

	int Slot3(void* a_self, int a_value)
	{
		g_lastSelf = a_self;
		return a_value + 3;
	}

	int Slot3Variant(void* a_self, int a_value)
	{
		g_lastSelf = a_self;
		return a_value + 300;
	}

	int Slot5(void* a_self, int a_value)
	{
		g_lastSelf = a_self;
		return a_value + 5;
	}

	int Slot5Override(void* a_self, int a_value)
	{
		g_lastSelf = a_self;
		// Chains the way a real override does: through the embedded table.
		const auto original = reinterpret_cast<int (*)(void*, int)>(Render::ContextTable::EmbeddedTableOf(a_self)[5]);
		return original(a_self, a_value) + 5000;
	}

	using Fn = int (*)(void*, int);

	int Call(void* a_object, std::size_t a_slot, int a_value)
	{
		// The way compiled code calls a virtual: through the object's first word.
		auto** const table = *reinterpret_cast<void***>(a_object);
		return reinterpret_cast<Fn>(table[a_slot])(a_object, a_value);
	}
}

int main()
{
	// Unbuffered: a crash inside generated code must not swallow the lines
	// that came before it.
	std::setvbuf(stdout, nullptr, _IONBF, 0);

	{
		FakeContext fake{};
		fake.vtable = fake.embedded;
		fake.embedded[3] = reinterpret_cast<void*>(&Slot3);
		fake.embedded[5] = reinterpret_cast<void*>(&Slot5);

		Check(Render::ContextTable::HasEmbeddedTable(&fake), "the object is laid out with its table at offset eight");
		Check(Render::ContextTable::EmbeddedTableOf(&fake) == fake.embedded, "and that table is found there");

		Render::ContextTable table;
		const Render::ContextTable::Override overrides[] = { { 5, reinterpret_cast<void*>(&Slot5Override) } };
		Check(table.Install(&fake, overrides), "the table installs on a well laid out object");
		Check(fake.vtable == table.Table(), "and the object now points at it");
		Check(table.Table() != fake.embedded, "which is not the embedded one");

		g_lastSelf = nullptr;
		Check(Call(&fake, 3, 10) == 13, "a plain slot forwards to the embedded entry");
		Check(g_lastSelf == &fake, "with this intact");

		// The runtime rewrites its embedded entry; the forwarder follows.
		fake.embedded[3] = reinterpret_cast<void*>(&Slot3Variant);
		Check(Call(&fake, 3, 10) == 310, "a forwarder is bound at call time, not at install");

		Check(Call(&fake, 5, 1) == 5006, "an overridden slot reaches the override, which chains to the embedded entry");
		Check(g_lastSelf == &fake, "with this intact there too");
		Check(!Render::ContextTable::HasEmbeddedTable(&fake), "after install the object no longer points at its own table");
		Check(Render::ContextTable::EmbeddedTableOf(&fake) == fake.embedded, "but the embedded table is still where it was");
	}

	{
		// An object whose first word does not point at offset eight is not
		// the object this was measured against.
		std::array<void*, 4> elsewhere{};
		FakeContext fake{};
		fake.vtable = elsewhere.data();
		Check(!Render::ContextTable::HasEmbeddedTable(&fake), "another layout has no embedded table");

		Render::ContextTable table;
		Check(!table.Install(&fake, {}), "and is refused");
		Check(fake.vtable == elsewhere.data(), "with the object left alone");
	}

	{
		FakeContext fake{};
		fake.vtable = fake.embedded;
		Render::ContextTable table;
		const Render::ContextTable::Override overrides[] = { { Render::ContextTable::kSlots, nullptr } };
		Check(!table.Install(&fake, overrides), "a slot past the end is refused");
		Check(fake.vtable == fake.embedded, "and changes nothing");
	}

	std::printf("\n%s\n", g_failures == 0 ? "all checks passed" : "checks failed");
	return g_failures == 0 ? 0 : 1;
}
