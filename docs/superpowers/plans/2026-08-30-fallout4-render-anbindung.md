# Fallout 4 Port — Teilprojekt B1 (Render-Anbindung) Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ein RenderDoc-Capture von Fallout 4, in dem ein von uns gesetzter Marker sichtbar ist.

**Architecture:** Drei Einheiten unter `src/Render/`. Ein generischer vtable-Patch trägt die riskante Zeigerarithmetik und ist als einziger Teil ohne D3D testbar. Darüber eine zustandslose Zugriffsschicht auf Device, Context und SwapChain, die vor der Installation einen Kreuzvergleich fährt. Darauf der Present-Hook, der einen Marker-Scope um den verketteten Aufruf legt.

**Tech Stack:** C++23, MSVC, commonlibf4 mit den `REX::W32`-Deklarationen für D3D11 und DXGI, `ID3DUserDefinedAnnotation` für die Marker. Kein Trampolin, keine neuen vcpkg-Abhängigkeiten.

**Spec:** `docs/superpowers/specs/2026-08-30-fallout4-render-anbindung-design.md`

## Global Constraints

Zusätzlich zu allem, was Teilprojekt A festgelegt hat (Plugin-Name `CommunityShadersFO4`, Version `0.1.0`, Ziel-Runtime exakt `1.11.240`, `/W4 /WX` auf unserem Target, Conventional Commits mit `Co-Authored-By`):

-   **Ausschließlich `REX::W32`.** `<d3d11.h>`, `<dxgi.h>` und `<d3d11_1.h>` werden **nicht** eingebunden. commonlibf4 liefert vollständige eigene Deklarationen; zwei Typsysteme nebeneinander wären eine Quelle für Fehler ohne Gegenwert.
-   **Kein Trampolin.** `InitInfo::trampoline` und `InitInfo::hook` bleiben auf `false`. Wer sie anfasst, hat den Plan verlassen.
-   **Keine neue vcpkg-Abhängigkeit.** Das Manifest bleibt bei `spdlog` mit Feature `wchar`.
-   **`IDXGISwapChain::Present` liegt auf vtable-Slot 8.** Abgezählt: `IUnknown` belegt 0–2, `IDXGIObject` 3–6, `IDXGIDeviceSubObject` 7.
-   **Marker-Name:** `CommunityShadersFO4 Frame <n>`.
-   **Frame-Logzeile alle 600 Frames**, auf Debug-Ebene.
-   **Installation bei `kGameDataReady`**, genau ein Versuch, kein Wiederholen und kein Pollen.
-   In keinem Fehlerfall wird das Laden des Plugins abgebrochen oder eine Ausnahme geworfen.

## Vorbedingung (muss ein Mensch erledigen)

Vor Task 5: **RenderDoc installieren** und beim Capture **Capture Child Processes** aktivieren. `f4se_loader.exe` startet das Spiel als Kindprozess; ohne die Option hängt RenderDoc am Loader und sieht keinen Frame. Die Tasks 1 bis 4 laufen ohne RenderDoc.

## Dateistruktur

| Datei                                 | Zuständigkeit                                                                                                                                     |
| ------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/Render/VTablePatch.h` / `.cpp`   | Ersetzt einen Eintrag in einer COM-vtable, merkt sich das Original, stellt es wieder her. Kennt weder D3D noch die Engine — deshalb host-testbar. |
| `tests/VTablePatchTests.cpp`          | Host-Test gegen eine synthetische vtable.                                                                                                         |
| `src/Render/Renderer.h` / `.cpp`      | Zustandslose Zugriffsschicht auf Device, Context, SwapChain; Kreuzvergleich und Protokollierung.                                                  |
| `src/Render/Markers.h` / `.cpp`       | `ID3DUserDefinedAnnotation` besorgen, RAII-Scope anbieten.                                                                                        |
| `src/Render/SwapChainHook.h` / `.cpp` | Besitzt den Present-Patch, den Thunk und den Frame-Zähler.                                                                                        |
| `src/XSEPlugin.cpp`                   | Ruft die Installation bei `kGameDataReady`.                                                                                                       |
| `CMakeLists.txt`                      | Test-Target für `VTablePatchTests`.                                                                                                               |

---

### Task 1: vtable-Patch mit Host-Test

Die einzige Einheit in B1, die sich ohne laufendes Spiel prüfen lässt — und zugleich die riskanteste. Deshalb testgetrieben und zuerst.

**Files:**

-   Create: `src/Render/VTablePatch.h`, `src/Render/VTablePatch.cpp`, `tests/VTablePatchTests.cpp`
-   Modify: `CMakeLists.txt`

**Interfaces:**

-   Consumes: nichts aus B1
-   Produces: `Render::VTablePatch` mit `bool Install(void*, std::size_t, void*) noexcept`, `bool Restore() noexcept`, `bool Installed() const noexcept`, `void* Original() const noexcept`

-   [ ] **Step 1: Den fehlschlagenden Test schreiben**

Datei `tests/VTablePatchTests.cpp`:

```cpp
#include "Render/VTablePatch.h"

#include <cstdio>
#include <cstdint>
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
	void* const   replacement = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xBEEF));

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

	if (g_failures != 0) {
		std::printf("\n%d check(s) failed\n", g_failures);
		return 1;
	}

	std::printf("\nall checks passed\n");
	return 0;
}
```

-   [ ] **Step 2: Test-Target anhängen**

In `CMakeLists.txt`, innerhalb des bestehenden `if(FO4CS_BUILD_TESTS)`-Blocks, hinter `add_test(NAME Runtime COMMAND RuntimeTests)`:

```cmake
    add_executable(
        VTablePatchTests
        "${CMAKE_SOURCE_DIR}/tests/VTablePatchTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Render/VTablePatch.cpp"
    )

    target_include_directories(
        VTablePatchTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(VTablePatchTests PRIVATE cxx_std_23)
    target_precompile_headers(
        VTablePatchTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(VTablePatchTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            VTablePatchTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME VTablePatch COMMAND VTablePatchTests)
```

-   [ ] **Step 3: Lauf zur Bestätigung, dass es fehlschlägt**

Run:

```pwsh
cmake -S . --preset FO4
```

Erwartet: Konfigurationsfehler `Cannot find source file` für `src/Render/VTablePatch.cpp`.

-   [ ] **Step 4: Die Schnittstelle schreiben**

Datei `src/Render/VTablePatch.h`:

```cpp
#pragma once

namespace Render
{
	/// Replaces a single entry in a COM object's virtual function table.
	///
	/// Deliberately knows nothing about D3D or the engine: the pointer
	/// arithmetic and the page-protection dance are the risky part of the
	/// Present hook, and keeping them here is what makes them testable on the
	/// host against a synthetic vtable.
	class VTablePatch
	{
	public:
		VTablePatch() = default;

		VTablePatch(const VTablePatch&) = delete;
		VTablePatch& operator=(const VTablePatch&) = delete;

		/// Replaces slot a_index of a_object's vtable and remembers what was
		/// there. Returns false and changes nothing when the object or the
		/// replacement is null, when a patch is already active, or when the page
		/// protection cannot be lifted.
		[[nodiscard]] bool Install(void* a_object, std::size_t a_index, void* a_replacement) noexcept;

		/// Puts the remembered entry back. Returns false when nothing is
		/// installed or the page protection cannot be lifted.
		bool Restore() noexcept;

		[[nodiscard]] bool  Installed() const noexcept { return m_slot != nullptr; }
		[[nodiscard]] void* Original() const noexcept { return m_original; }

	private:
		void** m_slot{ nullptr };
		void*  m_original{ nullptr };
	};
}
```

-   [ ] **Step 5: Die Implementierung schreiben**

Datei `src/Render/VTablePatch.cpp`:

```cpp
#include "Render/VTablePatch.h"

#include <Windows.h>

namespace Render
{
	namespace
	{
		// Lifts the page protection, performs the write, puts the protection
		// back. A failure to restore the protection is reported rather than
		// swallowed: the write has already happened at that point, and a caller
		// that believes the patch failed would be wrong about the world.
		template <class F>
		bool WithWritableSlot(void** a_slot, F&& a_write) noexcept
		{
			DWORD previous = 0;
			if (::VirtualProtect(a_slot, sizeof(void*), PAGE_READWRITE, &previous) == 0) {
				return false;
			}

			std::forward<F>(a_write)();

			DWORD ignored = 0;
			return ::VirtualProtect(a_slot, sizeof(void*), previous, &ignored) != 0;
		}
	}

	bool VTablePatch::Install(void* a_object, std::size_t a_index, void* a_replacement) noexcept
	{
		if (a_object == nullptr || a_replacement == nullptr || Installed()) {
			return false;
		}

		auto* const vtable = *reinterpret_cast<void***>(a_object);
		if (vtable == nullptr) {
			return false;
		}

		void** const slot = vtable + a_index;
		void* const  original = *slot;

		if (!WithWritableSlot(slot, [&]() noexcept { *slot = a_replacement; })) {
			return false;
		}

		m_slot = slot;
		m_original = original;
		return true;
	}

	bool VTablePatch::Restore() noexcept
	{
		if (!Installed()) {
			return false;
		}

		void** const slot = m_slot;
		void* const  original = m_original;

		if (!WithWritableSlot(slot, [&]() noexcept { *slot = original; })) {
			return false;
		}

		m_slot = nullptr;
		m_original = nullptr;
		return true;
	}
}
```

-   [ ] **Step 6: Lauf zur Bestätigung, dass es besteht**

Run:

```pwsh
cmake -S . --preset FO4
cmake --build --preset FO4
ctest --test-dir build/FO4 -C Release --output-on-failure
```

Erwartet: `2/2 tests passed`, `VTablePatchTests` meldet alle Prüfungen mit `ok`.

-   [ ] **Step 7: Den Test kurz brechen, um zu belegen, dass er greift**

Ändere in `VTablePatch.cpp` vorübergehend `void** const slot = vtable + a_index;` zu `void** const slot = vtable + a_index + 1;`, baue und führe `ctest` aus.

Erwartet: mindestens die Prüfungen `remembers the entry it replaced`, `the target slot now holds the replacement` und `leaves every other slot untouched` schlagen fehl, Rückgabewert 1. Danach zurückändern und erneut prüfen, dass alles besteht.

-   [ ] **Step 8: Commit**

```bash
git add -A
git commit -F - <<'MSG'
feat: add a testable com vtable patch

Pull the pointer arithmetic and the page-protection handling of the
upcoming Present hook into their own unit. It knows nothing about D3D,
which is what lets a host test exercise it against a synthetic vtable.

The test covers the three failures that would actually happen: patching
the wrong slot, losing the original entry, and leaving the page
writable. Shifting the slot index by one fails three of its checks.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
```

---

### Task 2: Zugriffsschicht und Kreuzvergleich

**Files:**

-   Create: `src/Render/Renderer.h`, `src/Render/Renderer.cpp`

**Interfaces:**

-   Consumes: nichts aus Task 1
-   Produces:

    -   `Render::Device` = `REX::W32::ID3D11Device`, `Render::Context` = `REX::W32::ID3D11DeviceContext`, `Render::SwapChain` = `REX::W32::IDXGISwapChain`
    -   `Render::Device* Render::GetDevice() noexcept`
    -   `Render::Context* Render::GetContext() noexcept`
    -   `Render::SwapChain* Render::GetSwapChain() noexcept`
    -   `bool Render::ValidateAndLog() noexcept`

-   [ ] **Step 1: Die Schnittstelle schreiben**

Datei `src/Render/Renderer.h`:

```cpp
#pragma once

namespace Render
{
	// commonlibf4 declares the D3D11 and DXGI interfaces itself, under REX::W32.
	// We stay inside that namespace and never include <d3d11.h>: the
	// declarations are complete, and one type system is simpler than two.
	using Device = REX::W32::ID3D11Device;
	using Context = REX::W32::ID3D11DeviceContext;
	using SwapChain = REX::W32::IDXGISwapChain;

	/// Each of these reads through to the engine on every call rather than
	/// caching. The read is cheap, and caching would risk handing out a stale
	/// pointer after a device loss or a window change.
	///
	/// The objects belong to the engine. We neither AddRef nor Release them.
	[[nodiscard]] Device*    GetDevice() noexcept;
	[[nodiscard]] Context*   GetContext() noexcept;
	[[nodiscard]] SwapChain* GetSwapChain() noexcept;

	/// Confirms that the three pointers describe one coherent D3D11 object
	/// family, and logs what was found.
	///
	/// This is the safety net for the whole subproject. Every address here is
	/// resolved through REL::VariantID, which pads a missing third entry with
	/// the second - so our AE runtime reads the NG id. If that assumption were
	/// wrong we would be handed a pointer into unrelated memory, and a wrong
	/// pointer that happens to be non-null is far worse than a null one.
	[[nodiscard]] bool ValidateAndLog() noexcept;
}
```

-   [ ] **Step 2: Die Implementierung schreiben**

Datei `src/Render/Renderer.cpp`:

```cpp
#include "Render/Renderer.h"

namespace Render
{
	Device* GetDevice() noexcept
	{
		const auto* const data = RE::BSGraphics::GetRendererData();
		return data != nullptr ? data->device : nullptr;
	}

	Context* GetContext() noexcept
	{
		const auto* const data = RE::BSGraphics::GetRendererData();
		return data != nullptr ? data->context : nullptr;
	}

	SwapChain* GetSwapChain() noexcept
	{
		const auto* const window = RE::BSGraphics::GetCurrentRendererWindow();
		return window != nullptr ? window->swapChain : nullptr;
	}

	bool ValidateAndLog() noexcept
	{
		auto* const device = GetDevice();
		auto* const context = GetContext();
		auto* const swapChain = GetSwapChain();

		REX::INFO("renderer: device {}, context {}, swapchain {}",
			static_cast<const void*>(device),
			static_cast<const void*>(context),
			static_cast<const void*>(swapChain));

		if (device == nullptr || context == nullptr || swapChain == nullptr) {
			REX::ERROR("renderer is not ready, refusing to install");
			return false;
		}

		// Both GetDevice calls hand back a reference we own and must drop.
		Device* fromContext = nullptr;
		context->GetDevice(&fromContext);

		Device* fromSwapChain = nullptr;
		swapChain->GetDevice(REX::W32::IID_ID3D11Device, reinterpret_cast<void**>(&fromSwapChain));

		const bool coherent = fromContext == device && fromSwapChain == device;

		if (!coherent) {
			REX::ERROR(
				"device mismatch: engine {}, via context {}, via swapchain {}. "
				"Address resolution is wrong for this runtime; refusing to install.",
				static_cast<const void*>(device),
				static_cast<const void*>(fromContext),
				static_cast<const void*>(fromSwapChain));
		}

		if (fromContext != nullptr) {
			fromContext->Release();
		}
		if (fromSwapChain != nullptr) {
			fromSwapChain->Release();
		}

		if (!coherent) {
			return false;
		}

		REX::INFO("device cross-check passed, feature level {:#x}",
			static_cast<std::uint32_t>(device->GetFeatureLevel()));

		REX::W32::DXGI_SWAP_CHAIN_DESC desc{};
		if (swapChain->GetDesc(std::addressof(desc)) >= 0) {
			REX::INFO("swapchain {}x{}, format {}, buffers {}",
				desc.bufferDesc.width,
				desc.bufferDesc.height,
				static_cast<std::uint32_t>(desc.bufferDesc.format),
				desc.bufferCount);
		} else {
			REX::WARN("could not read the swapchain description");
		}

		return true;
	}
}
```

-   [ ] **Step 3: Bauen**

Run: `cmake -S . --preset FO4 && cmake --build --preset FO4`

Erwartet: Build grün.

Zwei Stellen, an denen der erste Bau Anpassung erzwingen kann, beide ohne Designfolgen:

-   Heißen die Felder von `REX::W32::DXGI_SWAP_CHAIN_DESC` anders als `bufferDesc.width`, `bufferDesc.height`, `bufferDesc.format` und `bufferCount`, richte dich nach `extern/CommonLibF4/lib/commonlib-shared/include/REX/W32/DXGI.h`.
-   Existiert `REX::WARN` nicht, verwende `REX::INFO`.

-   [ ] **Step 4: Commit**

```bash
git add -A
git commit -F - <<'MSG'
feat: add renderer access with a device cross-check

Wrap the engine's device, context and swap chain in one place, reading
through on every call rather than caching, so a device loss cannot leave
us on a stale pointer.

ValidateAndLog is the safety net for the subproject. Every address comes
from REL::VariantID, which pads a missing third entry with the second,
so our AE runtime reads the NG id. If that were wrong we would hold a
pointer into unrelated memory. The device the engine reports, the one
the context reports and the one the swap chain reports must be the same
object, or nothing gets installed.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
```

---

### Task 3: Marker

**Files:**

-   Create: `src/Render/Markers.h`, `src/Render/Markers.cpp`

**Interfaces:**

-   Consumes: `Render::GetContext()` aus Task 2
-   Produces:

    -   `bool Render::InitMarkers() noexcept`
    -   `class Render::MarkerScope` mit `explicit MarkerScope(const wchar_t*) noexcept` und Destruktor

-   [ ] **Step 1: Die Schnittstelle schreiben**

Datei `src/Render/Markers.h`:

```cpp
#pragma once

namespace Render
{
	/// Fetches ID3DUserDefinedAnnotation from the immediate context. Returns
	/// false when the interface is unavailable; markers then become no-ops and
	/// everything else carries on.
	bool InitMarkers() noexcept;

	/// Opens a named region on construction and closes it on destruction.
	///
	/// Safe to leave in shipping code: with no capture tool attached the
	/// annotation calls are close to free, so there is no reason to hide this
	/// behind a switch.
	class MarkerScope
	{
	public:
		explicit MarkerScope(const wchar_t* a_name) noexcept;
		~MarkerScope() noexcept;

		MarkerScope(const MarkerScope&) = delete;
		MarkerScope& operator=(const MarkerScope&) = delete;

	private:
		bool m_open{ false };
	};
}
```

-   [ ] **Step 2: Die Implementierung schreiben**

Datei `src/Render/Markers.cpp`:

```cpp
#include "Render/Markers.h"

#include "Render/Renderer.h"

namespace Render
{
	namespace
	{
		REX::W32::ID3DUserDefinedAnnotation* g_annotation = nullptr;
	}

	bool InitMarkers() noexcept
	{
		if (g_annotation != nullptr) {
			return true;
		}

		auto* const context = GetContext();
		if (context == nullptr) {
			return false;
		}

		REX::W32::ID3DUserDefinedAnnotation* annotation = nullptr;
		const auto result = context->QueryInterface(
			REX::W32::IID_ID3DUserDefinedAnnotation,
			reinterpret_cast<void**>(std::addressof(annotation)));

		if (result < 0 || annotation == nullptr) {
			REX::INFO("ID3DUserDefinedAnnotation unavailable, markers disabled");
			return false;
		}

		// Held for the life of the process, like the context it came from.
		g_annotation = annotation;
		REX::INFO("debug markers available");
		return true;
	}

	MarkerScope::MarkerScope(const wchar_t* a_name) noexcept
	{
		if (g_annotation != nullptr && a_name != nullptr) {
			g_annotation->BeginEvent(a_name);
			m_open = true;
		}
	}

	MarkerScope::~MarkerScope() noexcept
	{
		if (m_open) {
			g_annotation->EndEvent();
		}
	}
}
```

-   [ ] **Step 3: Bauen**

Run: `cmake --build --preset FO4`
Erwartet: Build grün.

-   [ ] **Step 4: Commit**

```bash
git add -A
git commit -F - <<'MSG'
feat: add debug marker scopes

Fetch ID3DUserDefinedAnnotation from the immediate context and offer an
RAII scope over it. When the interface is missing the scopes turn into
no-ops rather than an error: markers are a diagnostic, not a feature.

The calls stay in shipping code. With no capture tool attached they cost
almost nothing, so hiding them behind a switch would buy nothing and
guarantee they are off whenever someone actually needs them.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
```

---

### Task 4: Present-Hook und Verdrahtung

**Files:**

-   Create: `src/Render/SwapChainHook.h`, `src/Render/SwapChainHook.cpp`
-   Modify: `src/XSEPlugin.cpp`

**Interfaces:**

-   Consumes: `Render::VTablePatch` (Task 1), `Render::GetSwapChain`, `Render::ValidateAndLog` (Task 2), `Render::InitMarkers`, `Render::MarkerScope` (Task 3)
-   Produces: `void Render::InstallSwapChainHook() noexcept`, `std::uint64_t Render::FrameCount() noexcept`

-   [ ] **Step 1: Die Schnittstelle schreiben**

Datei `src/Render/SwapChainHook.h`:

```cpp
#pragma once

namespace Render
{
	/// Validates the renderer, then replaces IDXGISwapChain::Present with our
	/// thunk. Does nothing but log when validation fails. Safe to call once;
	/// further calls are ignored.
	void InstallSwapChainHook() noexcept;

	/// Frames observed since the hook was installed.
	[[nodiscard]] std::uint64_t FrameCount() noexcept;
}
```

-   [ ] **Step 2: Die Implementierung schreiben**

Datei `src/Render/SwapChainHook.cpp`:

```cpp
#include "Render/SwapChainHook.h"

#include "Render/Markers.h"
#include "Render/Renderer.h"
#include "Render/VTablePatch.h"

#include <atomic>
#include <format>

namespace Render
{
	namespace
	{
		// IUnknown occupies 0-2, IDXGIObject 3-6, IDXGIDeviceSubObject 7, so
		// IDXGISwapChain::Present is the eighth entry.
		constexpr std::size_t kPresentSlot = 8;

		// Every 600 frames is roughly every ten seconds at 60 fps: enough to
		// show the counter is alive without filling the log.
		constexpr std::uint64_t kLogInterval = 600;

		using Present_t = REX::W32::HRESULT (*)(SwapChain*, std::uint32_t, std::uint32_t);

		VTablePatch            g_patch;
		Present_t              g_originalPresent = nullptr;
		std::atomic<std::uint64_t> g_frames{ 0 };
		bool                   g_installed = false;

		REX::W32::HRESULT Present(SwapChain* a_swapChain, std::uint32_t a_syncInterval, std::uint32_t a_flags)
		{
			const auto frame = g_frames.fetch_add(1, std::memory_order_relaxed) + 1;

			if (frame % kLogInterval == 0) {
				REX::DEBUG("frame {}", frame);
			}

			const auto name = std::format(L"CommunityShadersFO4 Frame {}", frame);

			// The scope wraps the chained call, so a capture shows a named block
			// per frame rather than a bare event.
			const MarkerScope scope{ name.c_str() };

			// Deliberately the remembered pointer, not IDXGISwapChain::Present.
			// If ENB, an overlay or an upscaler sits below us, calling through
			// DXGI directly would skip it.
			return g_originalPresent(a_swapChain, a_syncInterval, a_flags);
		}
	}

	void InstallSwapChainHook() noexcept
	{
		if (g_installed) {
			return;
		}

		if (!ValidateAndLog()) {
			return;
		}

		auto* const swapChain = GetSwapChain();

		if (!g_patch.Install(swapChain, kPresentSlot, reinterpret_cast<void*>(&Present))) {
			REX::ERROR("could not patch the swapchain vtable, leaving Present alone");
			return;
		}

		// Read back from the patch rather than walking the vtable a second time:
		// that arithmetic lives in VTablePatch precisely so it exists once.
		g_originalPresent = reinterpret_cast<Present_t>(g_patch.Original());
		g_installed = true;

		REX::INFO("Present hooked, chaining to {}", g_patch.Original());

		static_cast<void>(InitMarkers());
	}

	std::uint64_t FrameCount() noexcept
	{
		return g_frames.load(std::memory_order_relaxed);
	}
}
```

-   [ ] **Step 3: An `kGameDataReady` verdrahten**

In `src/XSEPlugin.cpp` den Include ergänzen:

```cpp
#include "Render/SwapChainHook.h"
```

und den Zweig im Nachrichten-Handler erweitern:

```cpp
		case F4SE::MessagingInterface::kGameDataReady:
			REX::INFO("kGameDataReady received");
			Render::InstallSwapChainHook();
			break;
```

-   [ ] **Step 4: Bauen und prüfen**

Run:

```pwsh
cmake -S . --preset FO4
cmake --build --preset FO4
ctest --test-dir build/FO4 -C Release --output-on-failure
pwsh tools/verify-plugin.ps1
```

Erwartet: Build grün, `2/2 tests passed`, Artefaktprüfung `all checks passed`.

Existiert `REX::DEBUG` nicht, verwende `REX::INFO` und halte das Intervall bei 600.

-   [ ] **Step 5: Commit**

```bash
git add -A
git commit -F - <<'MSG'
feat: hook present and count frames

Replace IDXGISwapChain::Present with a thunk that counts frames and
wraps the call in a named marker scope, installed once the game reports
its data ready. Earlier is too soon; the renderer is not necessarily up
at kPostPostLoad.

The thunk calls the pointer it replaced rather than DXGI's Present. If
ENB, an overlay or an upscaler already sits on the chain, calling
through directly would silently skip it.

Nothing is installed unless the device cross-check passes first.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
```

---

### Task 5: Abnahme im Spiel

Braucht einen Menschen an einer Fallout-4-Installation und ein installiertes RenderDoc.

**Files:**

-   Modify: `docs/fallout4-port/ROADMAP.md`

**Interfaces:**

-   Consumes: alles Vorherige
-   Produces: den bestätigten Beleg, dass die Adressauflösung für AE trägt — die Voraussetzung, auf der B2 und C aufbauen

-   [ ] **Step 1: Kaltbau aus frischem Klon**

```pwsh
git clone --recursive -b dev https://github.com/PlasticGhoul/fallout4-community-shaders.git C:\_coldtest
cd C:\_coldtest
cmake -S . --preset FO4
cmake --build --preset FO4
ctest --test-dir build/FO4 -C Release --output-on-failure
```

Erwartet: läuft ohne Handgriffe durch, beide Tests grün.

Anschließend `cmake --build --preset FO4` ein zweites Mal ausführen. Erwartet: keine
Übersetzungseinheit wird neu gebaut. Den Klon danach wieder entfernen.

Der Pfad muss kurz sein: der Visual-Studio-Generator bricht bei zu langen Verzeichnissen mit `MSB6003` ab und zeigt dabei fälschlich auf `link.exe`.

-   [ ] **Step 2: Deployen**

```pwsh
cmake -S . --preset FO4 -D "FO4CS_DEPLOY_DIR=F:/SteamLibrary/steamapps/common/Fallout 4/Data"
cmake --build --preset FO4
```

Erwartet: `Deploying to ...` erscheint, die DLL im Zielordner ist byteidentisch mit `build/FO4/Release/CommunityShadersFO4.dll`.

-   [ ] **Step 3: Starten und Log prüfen**

Fallout 4 AE über `f4se_loader.exe` starten, bis ins Hauptmenü.

Datei `C:\Users\minni\Documents\My Games\Fallout4\F4SE\CommunityShadersFO4.log`

Erwartet, zusätzlich zu den Zeilen aus Teilprojekt A:

-   `renderer: device 0x…, context 0x…, swapchain 0x…` mit drei Zeigern ungleich null
-   `device cross-check passed, feature level 0x…` — der Wert muss `0xb000` oder `0xb100` sein, also D3D-Feature-Level 11.0 oder 11.1
-   `swapchain <breite>x<höhe>, format …, buffers …` mit einer Auflösung, die zum Spielfenster passt
-   `debug markers available`
-   `Present hooked, chaining to 0x…`

**Schlägt der Kreuzvergleich fehl**, steht stattdessen `device mismatch` mit allen drei Zeigern im Log. Das wäre die wichtigste Erkenntnis des gesamten Teilprojekts: die Adressauflösung träfe für AE nicht zu, und B1 endet hier mit einem Befund statt einem Feature. In dem Fall Zeiger und Runtime notieren, in der Roadmap festhalten und das weitere Vorgehen besprechen — nicht weiterbauen.

-   [ ] **Step 4: Frame-Zähler prüfen**

Log-Ebene auf Debug stellen, ins Spiel laden und rund eine Minute laufen lassen.

Erwartet: `frame 600`, `frame 1200`, `frame 1800` … in gleichmäßigen Abständen. Bleibt der Zähler stehen, läuft Present nicht über unseren Thunk.

-   [ ] **Step 5: RenderDoc-Capture**

In RenderDoc unter _Launch Application_:

-   Executable Path: `F:\SteamLibrary\steamapps\common\Fallout 4\f4se_loader.exe`
-   Working Directory: `F:\SteamLibrary\steamapps\common\Fallout 4`
-   **Capture Child Processes** aktivieren

Starten, im Hauptmenü mit F12 ein Frame aufnehmen, das Capture öffnen.

Erwartet: im Event-Browser erscheint ein Block `CommunityShadersFO4 Frame <n>`. **Das ist das Abnahmekriterium des Teilprojekts.**

-   [ ] **Step 6: Stabilität**

Mehrere Minuten spielen, Hauptmenü und Spielwelt, mindestens einen Zonenwechsel.

Erwartet: kein Absturz, kein neues Crashlog in `Documents\My Games\Fallout4\F4SE\`.

-   [ ] **Step 7: Roadmap fortschreiben**

Status von B1 auf abgeschlossen setzen und den Abschnitt „Für Teilprojekt B bestätigt" ergänzen um: das Ergebnis des Kreuzvergleichs, das Feature-Level, die Auflösung des SwapChain, den Wert des Present-Originalzeigers samt der Frage, ob dort bereits ein fremder Hook saß, und die Bestätigung, dass Slot 8 stimmt.

-   [ ] **Step 8: Commit**

```bash
git add -A
git commit -F - <<'MSG'
docs: record subproject b1 acceptance

Mark the render hookup complete and write down what the in-game run
confirmed: that the addresses REL::VariantID resolves for the AE runtime
really do point at the engine's D3D11 objects, the feature level, the
swap chain description, and whether anything already sat on Present.

B2 and C build on that address resolution, so it is worth stating as a
measured fact rather than leaving it implied by things not crashing.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
```

---

## Offene Risiken

-   **Der Kreuzvergleich schlägt fehl.** Dann trifft die Annahme über `REL::VariantID` für AE nicht zu, und B1 endet mit einem Befund statt einem Feature. Der Plan behandelt das ausdrücklich als gültiges Ergebnis, nicht als Fehler: Task 5 Schritt 3 sagt, was dann zu tun ist.
-   **Ein fremder Hook sitzt bereits auf Present.** Mit ENB derzeit nicht der Fall, aber Upscaler und Overlays tun es. Die Verkettung über den gemerkten Zeiger deckt das ab; der protokollierte Originalzeiger zeigt, ob wir allein waren.
-   **Der SwapChain wird neu erzeugt.** Bei Auflösungs- oder Modus-Wechsel kann die Engine einen neuen SwapChain anlegen; unser Patch säße dann auf dem alten Objekt und der Frame-Zähler bliebe stehen. B1 behandelt das nicht — Schritt 4 der Abnahme würde es sichtbar machen, und die Behandlung gehört dann in ein eigenes Stück Arbeit.
-   **Feldnamen in `REX::W32::DXGI_SWAP_CHAIN_DESC`.** Task 2 Schritt 3 nennt die Datei, in der im Zweifel nachzusehen ist. Ohne Designfolgen.
