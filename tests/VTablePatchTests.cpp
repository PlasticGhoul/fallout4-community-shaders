#include "Render/VTablePatch.h"

#include <cstdint>
#include <cstdio>
#include <memory>

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

	// A stand-in for a COM object: the first member is the vtable pointer, which
	// is the only thing VTablePatch looks at. The entries are sentinels, never
	// called, so they need not be real functions.
	struct FakeComObject
	{
		void** vtable;
	};

	constexpr std::size_t kSlotCount = 12;
	constexpr std::size_t kPresentSlot = 8;

	void* Sentinel(std::size_t a_index)
	{
		return reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xA000 + a_index));
	}
}

int main()
{
	// Heap rather than stack: VirtualProtect on a stack page is legal but
	// interacts with guard pages, and nothing here needs that argument.
	auto slots = std::make_unique<void*[]>(kSlotCount);
	for (std::size_t i = 0; i < kSlotCount; ++i) {
		slots[i] = Sentinel(i);
	}

	FakeComObject object{ slots.get() };
	void* const replacement = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xBEEF));

	Render::VTablePatch patch;
	Check(!patch.Installed(), "starts out not installed");

	Check(patch.Install(&object, kPresentSlot, replacement), "install reports success");
	Check(patch.Installed(), "reports itself installed afterwards");
	Check(patch.Original() == Sentinel(kPresentSlot), "remembers the entry it replaced");
	Check(slots[kPresentSlot] == replacement, "the target slot now holds the replacement");

	bool othersIntact = true;
	for (std::size_t i = 0; i < kSlotCount; ++i) {
		if (i != kPresentSlot && slots[i] != Sentinel(i)) {
			othersIntact = false;
		}
	}
	Check(othersIntact, "leaves every other slot untouched");

	Check(!patch.Install(&object, 3, replacement), "refuses a second install while active");
	Check(slots[3] == Sentinel(3), "the refused second install changed nothing");

	Check(patch.Restore(), "restore reports success");
	Check(slots[kPresentSlot] == Sentinel(kPresentSlot), "restore puts the original entry back");
	Check(!patch.Installed(), "reports itself not installed after restore");
	Check(!patch.Restore(), "a second restore reports failure");

	Render::VTablePatch fresh;
	Check(!fresh.Install(nullptr, kPresentSlot, replacement), "refuses a null object");
	Check(!fresh.Install(&object, kPresentSlot, nullptr), "refuses a null replacement");

	FakeComObject headless{ nullptr };
	Check(!fresh.Install(&headless, kPresentSlot, replacement), "refuses an object without a vtable");

	// The engine's case: the address library names the table, and the instance
	// we are after is exactly what we do not have yet.
	auto directSlots = std::make_unique<void*[]>(kSlotCount);
	for (std::size_t i = 0; i < kSlotCount; ++i) {
		directSlots[i] = Sentinel(i);
	}

	constexpr std::size_t kSetupTechniqueSlot = 2;

	Render::VTablePatch direct;
	Check(
		direct.InstallAtTable(directSlots.get(), kSetupTechniqueSlot, replacement),
		"installs into a table given by address");
	Check(
		directSlots[kSetupTechniqueSlot] == replacement,
		"the table slot now holds the replacement");
	Check(
		direct.Original() == Sentinel(kSetupTechniqueSlot),
		"remembers the table entry it replaced");
	Check(
		!direct.InstallAtTable(directSlots.get(), 5, replacement),
		"refuses a second table install while active");
	Check(direct.Restore(), "restores a table install");
	Check(
		directSlots[kSetupTechniqueSlot] == Sentinel(kSetupTechniqueSlot),
		"the table entry is back");

	Check(
		!direct.InstallAtTable(nullptr, kSetupTechniqueSlot, replacement),
		"refuses a null table");
	Check(
		!direct.InstallAtTable(directSlots.get(), kSetupTechniqueSlot, nullptr),
		"refuses a null replacement for a table install");

	if (g_failures != 0) {
		std::printf("\n%d check(s) failed\n", g_failures);
		return 1;
	}

	std::printf("\nall checks passed\n");
	return 0;
}
