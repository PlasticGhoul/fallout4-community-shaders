# Fallout 4 Port — Teilprojekt B2 (Render-Target-Inventar) Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ein RenderDoc-Capture, in dem die Render-Targets von Fallout 4 beschriftet erscheinen statt anonym, plus ein Befunddokument mit der erhobenen Tabelle.

**Architecture:** Drei Einheiten unter `src/Render/`. Eine reine Umsetzungstabelle von DXGI-Formaten und Bind-Flags in lesbaren Text trägt den einzigen host-testbaren Anteil. Darüber eine Funktion, die D3D-Objekten einen Debug-Namen anheftet. Darauf das Inventar, das einmalig über die drei Target-Arrays der Engine läuft, jede Textur nach ihrer D3D-Beschreibung fragt, alles benennt und eine Tabelle protokolliert.

**Tech Stack:** C++23, MSVC, `REX::W32` für D3D11 und DXGI, `SetPrivateData` mit `WKPDID_D3DDebugObjectName`. Kein Trampolin, kein neuer Hook, keine neuen vcpkg-Abhängigkeiten.

**Spec:** `docs/superpowers/specs/2026-08-30-fallout4-target-inventar-design.md`

## Global Constraints

Zusätzlich zu allem aus A und B1 (Plugin-Name `CommunityShadersFO4`, Version `0.1.0`, Ziel-Runtime exakt `1.11.240`, `/W4 /WX`, ausschließlich `REX::W32` ohne `<d3d11.h>`, kein Trampolin, Conventional Commits mit `Co-Authored-By`):

-   **Maßgebliche Datenquelle ist `ID3D11Texture2D::GetDesc`.** Die Eigenschaftstabelle der Engine wird nur roh zur Korrelation mitprotokolliert, weil `BSGraphics::Format` nur vorwärtsdeklariert und damit bedeutungslos ist.
-   **Kein Enum im commonlibf4-Fork.** B2 fasst den Fork nicht an.
-   **Kein Hook auf `OMSetRenderTargets`** und kein anderer neuer Eingriff.
-   **Namensschema** `FO4_RT_042`, dreistellig mit führenden Nullen, Ansichten mit Suffix.
-   **Fehler werden einmal protokolliert, nie pro Objekt.** Nichts bricht ab, nichts wirft.
-   **Ausführung genau einmal**, nach erfolgreichem `InstallSwapChainHook`.

## Vorbedingung

Keine. RenderDoc ist installiert, Fallout 4 AE 1.11.240 steht bereit, B1 liegt auf `dev`.

## Dateistruktur

| Datei                                   | Zuständigkeit                                                                                                 |
| --------------------------------------- | ------------------------------------------------------------------------------------------------------------- |
| `src/Render/FormatNames.h` / `.cpp`     | Reine Umsetzung von `DXGI_FORMAT` und Bind-Flags in lesbaren Text. Kennt nichts sonst — deshalb host-testbar. |
| `tests/FormatNamesTests.cpp`            | Host-Test dazu.                                                                                               |
| `src/Render/DebugName.h` / `.cpp`       | `SetDebugName` über `WKPDID_D3DDebugObjectName`. Ab Teilprojekt C auch für eigene Ressourcen.                 |
| `src/Render/TargetInventory.h` / `.cpp` | Läuft über die drei Target-Arrays, liest, benennt, protokolliert.                                             |
| `src/Render/SwapChainHook.cpp`          | Ruft das Inventar nach erfolgreicher Installation.                                                            |
| `CMakeLists.txt`                        | Test-Target für `FormatNamesTests`.                                                                           |
| `docs/fallout4-port/render-targets.md`  | Befunddokument, entsteht in Task 4.                                                                           |

---

### Task 1: Formatnamen mit Host-Test

Der einzige Teil von B2, der sich ohne Spiel prüfen lässt. Eine falsch zugeordnete Formatnummer würde die gesamte spätere Identifikationsarbeit in die Irre führen, deshalb testgetrieben und zuerst.

**Files:**

-   Create: `src/Render/FormatNames.h`, `src/Render/FormatNames.cpp`, `tests/FormatNamesTests.cpp`
-   Modify: `CMakeLists.txt`

**Interfaces:**

-   Consumes: nichts aus B2
-   Produces:

    -   `std::string_view Render::FormatName(REX::W32::DXGI_FORMAT) noexcept`
    -   `std::string Render::BindFlagsString(std::uint32_t)`

-   [ ] **Step 1: Den fehlschlagenden Test schreiben**

Datei `tests/FormatNamesTests.cpp`:

```cpp
#include "Render/FormatNames.h"

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

	void CheckName(REX::W32::DXGI_FORMAT a_format, std::string_view a_expected)
	{
		const auto actual = Render::FormatName(a_format);
		const bool passed = actual == a_expected;
		std::printf("%s  format %-4d -> %s\n",
			passed ? "ok  " : "FAIL",
			static_cast<int>(a_format),
			std::string(actual).c_str());
		if (!passed) {
			++g_failures;
		}
	}
}

int main()
{
	// Spot checks across the ranges that actually occur as render targets.
	CheckName(REX::W32::DXGI_FORMAT_R8G8B8A8_UNORM, "R8G8B8A8_UNORM");
	CheckName(REX::W32::DXGI_FORMAT_R16G16B16A16_FLOAT, "R16G16B16A16_FLOAT");
	CheckName(REX::W32::DXGI_FORMAT_R11G11B10_FLOAT, "R11G11B10_FLOAT");
	CheckName(REX::W32::DXGI_FORMAT_D24_UNORM_S8_UINT, "D24_UNORM_S8_UINT");
	CheckName(REX::W32::DXGI_FORMAT_R32_FLOAT, "R32_FLOAT");
	CheckName(REX::W32::DXGI_FORMAT_R8_UNORM, "R8_UNORM");
	CheckName(REX::W32::DXGI_FORMAT_UNKNOWN, "UNKNOWN");

	// An unmapped value must degrade to UNKNOWN rather than an empty string or
	// a crash. The caller logs the raw number alongside, so nothing is lost.
	CheckName(static_cast<REX::W32::DXGI_FORMAT>(9999), "UNKNOWN");

	Check(Render::BindFlagsString(0) == "-", "no flags renders as a dash");
	Check(
		Render::BindFlagsString(REX::W32::D3D11_BIND_RENDER_TARGET) == "RENDER_TARGET",
		"a single flag renders alone");
	Check(
		Render::BindFlagsString(
			REX::W32::D3D11_BIND_RENDER_TARGET |
			REX::W32::D3D11_BIND_SHADER_RESOURCE) == "RENDER_TARGET|SHADER_RESOURCE",
		"two flags join in declaration order, not set order");
	Check(
		Render::BindFlagsString(
			REX::W32::D3D11_BIND_SHADER_RESOURCE |
			REX::W32::D3D11_BIND_RENDER_TARGET) == "RENDER_TARGET|SHADER_RESOURCE",
		"the order of the operands does not change the result");
	Check(
		Render::BindFlagsString(
			REX::W32::D3D11_BIND_RENDER_TARGET |
			REX::W32::D3D11_BIND_SHADER_RESOURCE |
			REX::W32::D3D11_BIND_UNORDERED_ACCESS) ==
			"RENDER_TARGET|SHADER_RESOURCE|UNORDERED_ACCESS",
		"three flags join in declaration order");

	if (g_failures != 0) {
		std::printf("\n%d check(s) failed\n", g_failures);
		return 1;
	}

	std::printf("\nall checks passed\n");
	return 0;
}
```

-   [ ] **Step 2: Test-Target anhängen**

In `CMakeLists.txt`, im bestehenden `if(FO4CS_BUILD_TESTS)`-Block hinter `add_test(NAME VTablePatch COMMAND VTablePatchTests)`:

```cmake
    add_executable(
        FormatNamesTests
        "${CMAKE_SOURCE_DIR}/tests/FormatNamesTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Render/FormatNames.cpp"
    )

    target_include_directories(
        FormatNamesTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(FormatNamesTests PRIVATE cxx_std_23)
    target_precompile_headers(
        FormatNamesTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(FormatNamesTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            FormatNamesTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME FormatNames COMMAND FormatNamesTests)
```

-   [ ] **Step 3: Lauf zur Bestätigung, dass es fehlschlägt**

Run: `cmake -S . --preset FO4`
Erwartet: Konfigurationsfehler `Cannot find source file` für `src/Render/FormatNames.cpp`.

-   [ ] **Step 4: Die Schnittstelle schreiben**

Datei `src/Render/FormatNames.h`:

```cpp
#pragma once

// The project PCH stops at REL and REX; the DXGI and D3D11 declarations are
// separate headers. Naming those types in our own interface means including
// them here too.
#include <REX/W32/D3D11.h>
#include <REX/W32/DXGI.h>

namespace Render
{
	/// The DXGI format's name without its DXGI_FORMAT_ prefix, or "UNKNOWN".
	///
	/// Only the formats that plausibly appear as a render target are mapped;
	/// the full enum has well over a hundred entries and most are irrelevant
	/// here. Callers log the raw number alongside, so an unmapped format stays
	/// identifiable.
	[[nodiscard]] std::string_view FormatName(REX::W32::DXGI_FORMAT a_format) noexcept;

	/// The bind flags as text, joined with '|' in a fixed order regardless of
	/// how the caller combined them, so two identical flag sets always render
	/// identically. "-" when no flag is set.
	[[nodiscard]] std::string BindFlagsString(std::uint32_t a_bindFlags);
}
```

-   [ ] **Step 5: Die Implementierung schreiben**

Datei `src/Render/FormatNames.cpp`:

```cpp
#include "Render/FormatNames.h"

namespace Render
{
	std::string_view FormatName(REX::W32::DXGI_FORMAT a_format) noexcept
	{
		using namespace REX::W32;

		switch (a_format) {
		case DXGI_FORMAT_UNKNOWN:
			return "UNKNOWN"sv;

		case DXGI_FORMAT_R32G32B32A32_FLOAT:
			return "R32G32B32A32_FLOAT"sv;
		case DXGI_FORMAT_R32G32B32A32_UINT:
			return "R32G32B32A32_UINT"sv;
		case DXGI_FORMAT_R32G32B32_FLOAT:
			return "R32G32B32_FLOAT"sv;

		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			return "R16G16B16A16_FLOAT"sv;
		case DXGI_FORMAT_R16G16B16A16_UNORM:
			return "R16G16B16A16_UNORM"sv;
		case DXGI_FORMAT_R16G16B16A16_SNORM:
			return "R16G16B16A16_SNORM"sv;

		case DXGI_FORMAT_R32G32_FLOAT:
			return "R32G32_FLOAT"sv;
		case DXGI_FORMAT_R10G10B10A2_UNORM:
			return "R10G10B10A2_UNORM"sv;
		case DXGI_FORMAT_R11G11B10_FLOAT:
			return "R11G11B10_FLOAT"sv;

		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
			return "R8G8B8A8_TYPELESS"sv;
		case DXGI_FORMAT_R8G8B8A8_UNORM:
			return "R8G8B8A8_UNORM"sv;
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			return "R8G8B8A8_UNORM_SRGB"sv;
		case DXGI_FORMAT_R8G8B8A8_SNORM:
			return "R8G8B8A8_SNORM"sv;

		case DXGI_FORMAT_R16G16_FLOAT:
			return "R16G16_FLOAT"sv;
		case DXGI_FORMAT_R16G16_UNORM:
			return "R16G16_UNORM"sv;
		case DXGI_FORMAT_R16G16_SNORM:
			return "R16G16_SNORM"sv;

		case DXGI_FORMAT_R32_TYPELESS:
			return "R32_TYPELESS"sv;
		case DXGI_FORMAT_D32_FLOAT:
			return "D32_FLOAT"sv;
		case DXGI_FORMAT_R32_FLOAT:
			return "R32_FLOAT"sv;
		case DXGI_FORMAT_R32_UINT:
			return "R32_UINT"sv;

		case DXGI_FORMAT_R24G8_TYPELESS:
			return "R24G8_TYPELESS"sv;
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
			return "D24_UNORM_S8_UINT"sv;
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
			return "R24_UNORM_X8_TYPELESS"sv;

		case DXGI_FORMAT_R8G8_UNORM:
			return "R8G8_UNORM"sv;
		case DXGI_FORMAT_R8G8_SNORM:
			return "R8G8_SNORM"sv;

		case DXGI_FORMAT_R16_TYPELESS:
			return "R16_TYPELESS"sv;
		case DXGI_FORMAT_R16_FLOAT:
			return "R16_FLOAT"sv;
		case DXGI_FORMAT_D16_UNORM:
			return "D16_UNORM"sv;
		case DXGI_FORMAT_R16_UNORM:
			return "R16_UNORM"sv;
		case DXGI_FORMAT_R16_UINT:
			return "R16_UINT"sv;

		case DXGI_FORMAT_R8_UNORM:
			return "R8_UNORM"sv;
		case DXGI_FORMAT_R8_UINT:
			return "R8_UINT"sv;
		case DXGI_FORMAT_A8_UNORM:
			return "A8_UNORM"sv;

		case DXGI_FORMAT_R32G32B32A32_TYPELESS:
			return "R32G32B32A32_TYPELESS"sv;
		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
			return "R16G16B16A16_TYPELESS"sv;
		case DXGI_FORMAT_R32_SINT:
			return "R32_SINT"sv;
		case DXGI_FORMAT_B8G8R8A8_UNORM:
			return "B8G8R8A8_UNORM"sv;

		default:
			return "UNKNOWN"sv;
		}
	}

	std::string BindFlagsString(std::uint32_t a_bindFlags)
	{
		using namespace REX::W32;

		// Fixed order, so identical flag sets always render identically and the
		// log stays comparable between runs.
		static constexpr std::pair<std::uint32_t, std::string_view> kFlags[]{
			{ D3D11_BIND_RENDER_TARGET, "RENDER_TARGET"sv },
			{ D3D11_BIND_SHADER_RESOURCE, "SHADER_RESOURCE"sv },
			{ D3D11_BIND_UNORDERED_ACCESS, "UNORDERED_ACCESS"sv },
			{ D3D11_BIND_DEPTH_STENCIL, "DEPTH_STENCIL"sv },
			{ D3D11_BIND_VERTEX_BUFFER, "VERTEX_BUFFER"sv },
			{ D3D11_BIND_INDEX_BUFFER, "INDEX_BUFFER"sv },
			{ D3D11_BIND_CONSTANT_BUFFER, "CONSTANT_BUFFER"sv },
			{ D3D11_BIND_STREAM_OUTPUT, "STREAM_OUTPUT"sv },
			{ D3D11_BIND_DECODER, "DECODER"sv },
			{ D3D11_BIND_VIDEO_ENCODER, "VIDEO_ENCODER"sv },
		};

		std::string result;
		for (const auto& [bit, name] : kFlags) {
			if ((a_bindFlags & bit) != 0) {
				if (!result.empty()) {
					result += '|';
				}
				result.append(name);
			}
		}

		return result.empty() ? std::string{ "-" } : result;
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

Erwartet: `3/3 tests passed`, `FormatNamesTests` meldet alle Prüfungen mit `ok`.

Sollte `std::pair` unbekannt sein, ergänze `#include <utility>` in `FormatNames.cpp`.

-   [ ] **Step 7: Den Test kurz brechen, um zu belegen, dass er greift**

Vertausche in `FormatName` vorübergehend die Rückgaben von `DXGI_FORMAT_R8G8B8A8_UNORM` und `DXGI_FORMAT_R11G11B10_FLOAT`, baue und führe `ctest` aus.

Erwartet: genau diese beiden Prüfungen schlagen fehl, Rückgabewert 1. Danach zurückändern und erneut prüfen.

-   [ ] **Step 8: Commit**

```bash
git add -A
git commit -F - <<'MSG'
feat: add readable names for dxgi formats and bind flags

A lookup table and a few bit tests - exactly the kind of code where a
mistake sits quietly and misleads everything downstream. A wrong format
name would send the render target identification in B2 off course, so it
gets a host test.

Bind flags render in a fixed order rather than the order the caller
combined them, so identical flag sets always produce identical text and
the log stays comparable between runs.

Only formats that plausibly appear as render targets are mapped. The
inventory logs the raw number alongside, so anything unmapped stays
identifiable.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
```

---

### Task 2: Debug-Namen für D3D-Objekte

**Files:**

-   Create: `src/Render/DebugName.h`, `src/Render/DebugName.cpp`

**Interfaces:**

-   Consumes: nichts aus Task 1
-   Produces: `bool Render::SetDebugName(REX::W32::ID3D11DeviceChild*, std::string_view) noexcept`

-   [ ] **Step 1: Die Schnittstelle schreiben**

Datei `src/Render/DebugName.h`:

```cpp
#pragma once

#include <REX/W32/D3D11.h>

namespace Render
{
	/// Attaches a name that D3D debugging tools display, RenderDoc included.
	///
	/// Returns false for a null object or when D3D rejects the call. Callers
	/// are expected to report a failure once rather than per object: with
	/// hundreds of resources, a per-object message would bury the log.
	bool SetDebugName(REX::W32::ID3D11DeviceChild* a_object, std::string_view a_name) noexcept;
}
```

-   [ ] **Step 2: Die Implementierung schreiben**

Datei `src/Render/DebugName.cpp`:

```cpp
#include "Render/DebugName.h"

namespace Render
{
	namespace
	{
		// WKPDID_D3DDebugObjectName. REX::W32 does not declare it, so it is
		// defined here once - and only here. The inherited Skyrim project made
		// exactly this a rule: never duplicate the GUID across call sites.
		inline constexpr REX::W32::GUID kDebugObjectName{
			0x429B8C22,
			0x9188,
			0x4B0C,
			{ 0x87, 0x42, 0xAC, 0xB0, 0xBF, 0x85, 0xC2, 0x00 }
		};
	}

	bool SetDebugName(REX::W32::ID3D11DeviceChild* a_object, std::string_view a_name) noexcept
	{
		if (a_object == nullptr || a_name.empty()) {
			return false;
		}

		// The name is stored as a plain byte blob without a terminator; the
		// length is what identifies its extent.
		const auto result = a_object->SetPrivateData(
			kDebugObjectName,
			static_cast<std::uint32_t>(a_name.size()),
			a_name.data());

		return result >= 0;
	}
}
```

-   [ ] **Step 3: Bauen**

Run: `cmake -S . --preset FO4 && cmake --build --preset FO4`
Erwartet: Build grün.

-   [ ] **Step 4: Commit**

```bash
git add -A
git commit -F - <<'MSG'
feat: attach debug names to d3d objects

Wrap SetPrivateData with WKPDID_D3DDebugObjectName so RenderDoc shows a
name instead of an anonymous resource id. The GUID is not in REX::W32 and
is defined here once, deliberately in a unit of its own: subproject C
will name our own resources with the same function.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
```

---

### Task 3: Das Inventar

**Files:**

-   Create: `src/Render/TargetInventory.h`, `src/Render/TargetInventory.cpp`
-   Modify: `src/Render/SwapChainHook.cpp`

**Interfaces:**

-   Consumes: `Render::FormatName`, `Render::BindFlagsString` (Task 1), `Render::SetDebugName` (Task 2)
-   Produces: `void Render::RunTargetInventory() noexcept`

-   [ ] **Step 1: Die Schnittstelle schreiben**

Datei `src/Render/TargetInventory.h`:

```cpp
#pragma once

namespace Render
{
	/// Walks the engine's three render target arrays once: reads each texture's
	/// D3D description, gives every non-null object a debug name so capture
	/// tools show it, and logs a table.
	///
	/// Reads and labels; never changes how the engine renders.
	void RunTargetInventory() noexcept;
}
```

-   [ ] **Step 2: Die Implementierung schreiben**

Datei `src/Render/TargetInventory.cpp`:

```cpp
#include "Render/TargetInventory.h"

#include "Render/DebugName.h"
#include "Render/FormatNames.h"

#include <RE/B/BSGraphics.h>

#include <format>

namespace Render
{
	namespace
	{
		// Counts failures so they can be reported once at the end. With well
		// over a hundred objects, a message per failure would bury everything
		// else in the log.
		struct NamingTally
		{
			std::uint32_t attempted{ 0 };
			std::uint32_t failed{ 0 };

			void Apply(REX::W32::ID3D11DeviceChild* a_object, const std::string& a_name) noexcept
			{
				if (a_object == nullptr) {
					return;
				}
				++attempted;
				if (!SetDebugName(a_object, a_name)) {
					++failed;
				}
			}
		};

		// Logs one line describing a texture, or reports why it could not.
		void LogTexture(
			std::string_view              a_label,
			REX::W32::ID3D11Texture2D*    a_texture,
			std::uint32_t                 a_engineFormat)
		{
			REX::W32::D3D11_TEXTURE2D_DESC desc{};
			a_texture->GetDesc(std::addressof(desc));

			REX::INFO("{:<16} {:>5}x{:<5} {:<22} mips {:<2} samples {:<2} bind {:<40} engineFormat {}",
				a_label,
				desc.width,
				desc.height,
				FormatName(desc.format),
				desc.mipLevels,
				desc.sampleDesc.count,
				BindFlagsString(desc.bindFlags),
				a_engineFormat);
		}

		void InventoryRenderTargets(RE::BSGraphics::RendererData* a_data,
			RE::BSGraphics::RenderTargetManager*                  a_manager,
			NamingTally&                                          a_tally)
		{
			REX::INFO("--- render targets ---");
			std::uint32_t occupied = 0;

			for (std::size_t i = 0; i < std::size(a_data->renderTargets); ++i) {
				auto& target = a_data->renderTargets[i];
				if (target.texture == nullptr) {
					continue;  // Most entries are empty; reporting them would be noise.
				}
				++occupied;

				const auto label = std::format("FO4_RT_{:03}", i);

				// The engine's own format enum is only forward-declared in
				// commonlibf4, so it is logged raw. Side by side with the real
				// DXGI format it yields the mapping between the two.
				// Read as raw memory: BSGraphics::Format is only forward-declared
				// in commonlibf4, so it is an incomplete type and cannot be cast.
				std::uint32_t engineFormat = 0;
				if (a_manager != nullptr && i < std::size(a_manager->renderTargetData)) {
					engineFormat = *reinterpret_cast<const std::uint32_t*>(
						std::addressof(a_manager->renderTargetData[i].format));
				}

				LogTexture(label, target.texture, engineFormat);

				a_tally.Apply(target.texture, label);
				a_tally.Apply(target.copyTexture, label + "_COPY");
				a_tally.Apply(target.rtView, label + "_RTV");
				a_tally.Apply(target.srView, label + "_SRV");
				a_tally.Apply(target.copySRView, label + "_COPY_SRV");
				a_tally.Apply(target.uaView, label + "_UAV");
			}

			REX::INFO("{} of {} render targets occupied", occupied, std::size(a_data->renderTargets));
		}

		void InventoryDepthStencils(RE::BSGraphics::RendererData* a_data, NamingTally& a_tally)
		{
			REX::INFO("--- depth stencil targets ---");
			std::uint32_t occupied = 0;

			for (std::size_t i = 0; i < std::size(a_data->depthStencilTargets); ++i) {
				auto& target = a_data->depthStencilTargets[i];
				if (target.texture == nullptr) {
					continue;
				}
				++occupied;

				const auto label = std::format("FO4_DS_{:03}", i);
				LogTexture(label, target.texture, 0);

				a_tally.Apply(target.texture, label);
				a_tally.Apply(target.srViewDepth, label + "_SRV_DEPTH");
				a_tally.Apply(target.srViewStencil, label + "_SRV_STENCIL");
				for (std::size_t v = 0; v < std::size(target.dsView); ++v) {
					a_tally.Apply(target.dsView[v], std::format("{}_DSV{}", label, v));
				}
				// The read-only DSV variants address the same texture with
				// different flags. The texture already carries a name, which is
				// what a capture tool shows, so they are left alone.
			}

			REX::INFO("{} of {} depth stencil targets occupied",
				occupied, std::size(a_data->depthStencilTargets));
		}

		void InventoryCubeMaps(RE::BSGraphics::RendererData* a_data, NamingTally& a_tally)
		{
			REX::INFO("--- cubemap render targets ---");
			std::uint32_t occupied = 0;

			for (std::size_t i = 0; i < std::size(a_data->cubeMapRenderTargets); ++i) {
				auto& target = a_data->cubeMapRenderTargets[i];
				if (target.texture == nullptr) {
					continue;
				}
				++occupied;

				const auto label = std::format("FO4_CUBE_{:03}", i);
				LogTexture(label, target.texture, 0);

				a_tally.Apply(target.texture, label);
				a_tally.Apply(target.srView, label + "_SRV");
				for (std::size_t v = 0; v < std::size(target.rtView); ++v) {
					a_tally.Apply(target.rtView[v], std::format("{}_RTV{}", label, v));
				}
			}

			REX::INFO("{} of {} cubemap targets occupied",
				occupied, std::size(a_data->cubeMapRenderTargets));
		}
	}

	void RunTargetInventory() noexcept
	{
		auto* const data = RE::BSGraphics::GetRendererData();
		if (data == nullptr) {
			REX::ERROR("renderer data unavailable, skipping the target inventory");
			return;
		}

		auto* const manager = RE::BSGraphics::RenderTargetManager::GetSingleton();
		if (manager == nullptr) {
			REX::WARN("render target manager unavailable; engine formats will read as 0");
		}

		REX::INFO("=== render target inventory ===");
		REX::INFO("array sizes: renderer {}/{}/{}, manager {}/{}/{}",
			std::size(data->renderTargets),
			std::size(data->depthStencilTargets),
			std::size(data->cubeMapRenderTargets),
			manager != nullptr ? std::size(manager->renderTargetData) : 0u,
			manager != nullptr ? std::size(manager->depthStencilTargetData) : 0u,
			manager != nullptr ? std::size(manager->cubeMapRenderTargetData) : 0u);

		NamingTally tally;
		InventoryRenderTargets(data, manager, tally);
		InventoryDepthStencils(data, tally);
		InventoryCubeMaps(data, tally);

		if (tally.failed != 0) {
			REX::WARN("named {} of {} objects; {} calls were rejected",
				tally.attempted - tally.failed, tally.attempted, tally.failed);
		} else {
			REX::INFO("named {} objects", tally.attempted);
		}

		REX::INFO("=== end of inventory ===");
	}
}
```

-   [ ] **Step 3: Nach dem Present-Hook aufrufen**

In `src/Render/SwapChainHook.cpp` den Include ergänzen:

```cpp
#include "Render/TargetInventory.h"
```

und am Ende von `InstallSwapChainHook`, hinter `static_cast<void>(InitMarkers());`:

```cpp
		RunTargetInventory();
```

Der Platz ist bewusst gewählt: das Inventar läuft nur, wenn der Kreuzvergleich aus B1 vorher durchging. Ohne gesicherten Renderer-Zugriff wäre jede gelesene Zahl wertlos.

-   [ ] **Step 4: Bauen und prüfen**

Run:

```pwsh
cmake -S . --preset FO4
cmake --build --preset FO4
ctest --test-dir build/FO4 -C Release --output-on-failure
pwsh tools/verify-plugin.ps1
```

Erwartet: Build grün, `3/3 tests passed`, Artefaktprüfung `all checks passed`.

Zwei Stellen, an denen der Bau Anpassung erzwingen kann, beide ohne Designfolgen:

-   Heißt das Formatfeld in `RenderTargetProperties` anders als `format`, richte dich nach `extern/CommonLibF4/include/RE/B/BSGraphics.h`.
-   Sollte `std::addressof` auf dem Feld scheitern, weil der unvollständige Typ auch das verhindert, lies über den Offset der Struktur: `*reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::byte*>(std::addressof(a_manager->renderTargetData[i])) + 8)` — `format` liegt laut `RenderTargetProperties` auf Offset `0x08`.

-   [ ] **Step 5: Commit**

```bash
git add -A
git commit -F - <<'MSG'
feat: inventory and label the engine's render targets

Walk the three target arrays once after the Present hook is up, ask each
texture for its own D3D description, and give every object a name that
capture tools display. An anonymous capture becomes a labelled one, which
is worth more than a list: it makes every future identification cheap
rather than just the first.

The engine's own format enum is logged raw beside the real DXGI format.
It is only forward-declared in commonlibf4, so the number means nothing
by itself - but side by side the two yield the mapping.

Empty entries are skipped silently; most of the hundred are empty.
Naming failures are counted and reported once at the end rather than per
object.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
```

---

### Task 4: Abnahme im Spiel und Befunddokument

Braucht einen Menschen an einer Fallout-4-Installation mit RenderDoc.

**Files:**

-   Create: `docs/fallout4-port/render-targets.md`
-   Modify: `docs/fallout4-port/ROADMAP.md`

**Interfaces:**

-   Consumes: alles Vorherige
-   Produces: das Befunddokument, auf das spätere Teilprojekte sich stützen

-   [ ] **Step 1: Kaltbau aus frischem Klon**

```pwsh
git clone --recursive -b dev https://github.com/PlasticGhoul/fallout4-community-shaders.git C:\_coldtest
cd C:\_coldtest
cmake -S . --preset FO4
cmake --build --preset FO4
ctest --test-dir build/FO4 -C Release --output-on-failure
```

Erwartet: läuft ohne Handgriffe durch, drei Tests grün. Anschließend denselben Build-Befehl ein zweites Mal ausführen; erwartet: keine Übersetzungseinheit wird neu gebaut. Den Klon danach entfernen.

Der Pfad muss kurz sein: der Visual-Studio-Generator bricht bei zu langen Verzeichnissen mit `MSB6003` ab und zeigt dabei fälschlich auf `link.exe`.

-   [ ] **Step 2: Deployen**

```pwsh
cmake -S . --preset FO4 -D "FO4CS_DEPLOY_DIR=F:/SteamLibrary/steamapps/common/Fallout 4/Data"
cmake --build --preset FO4
```

Erwartet: `Deploying to ...`, die DLL im Zielordner ist byteidentisch mit `build/FO4/Release/CommunityShadersFO4.dll`.

-   [ ] **Step 3: Starten und Tabelle prüfen**

Fallout 4 AE über `f4se_loader.exe` starten, bis ins Hauptmenü.

Datei `C:\Users\minni\Documents\My Games\Fallout4\F4SE\CommunityShadersFO4.log`

Erwartet, hinter den Zeilen aus A und B1:

-   `=== render target inventory ===`
-   eine Zeile `array sizes: renderer 101/13/2, manager 100/12/1`
-   je Abschnitt eine Tabelle mit Index, Auflösung, DXGI-Format im Klartext, Mips, Samples, Bind-Flags und roher Engine-Formatzahl
-   je Abschnitt eine Zusammenfassung `N of M ... occupied`
-   `named N objects`

Plausibilitätsprüfung: die Auflösungen müssen 2560×1440 oder dessen Teiler sein, und die Formate müssen im Klartext erscheinen. Steht dort massenhaft `UNKNOWN`, fehlen Einträge in der Tabelle aus Task 1 — dann die betroffenen Zahlen nachtragen.

-   [ ] **Step 4: RenderDoc-Capture**

In RenderDoc unter _Launch Application_: Executable `F:\SteamLibrary\steamapps\common\Fallout 4\f4se_loader.exe`, Working Directory der Spielordner, **Capture Child Processes** aktiviert. Starten, im Hauptmenü F12 drücken.

Prüfung ohne die Oberfläche, wie in B1 bewährt: das Capture liegt unter `%TEMP%\RenderDoc\` als `.rdc`. Suche darin nach der Zeichenkette `FO4_RT_`:

```bash
python - <<'EOF'
import io, re, glob, os
files = glob.glob(os.path.expandvars(r"%TEMP%\RenderDoc\*.rdc"))
newest = max(files, key=os.path.getmtime)
data = io.open(newest, "rb").read()
print(os.path.basename(newest), "->", len(re.findall(rb"FO4_RT_", data)), "Treffer")
EOF
```

Erwartet: deutlich mehr als null Treffer. **Das ist das Abnahmekriterium von B2.**

-   [ ] **Step 5: Stabilität**

Mehrere Minuten spielen, Hauptmenü und Spielwelt.
Erwartet: kein neues Crashlog in `Documents\My Games\Fallout4\F4SE\`.

-   [ ] **Step 6: Befunddokument schreiben**

Datei `docs/fallout4-port/render-targets.md` mit:

-   Datum, Spielversion, Auflösung, unter der erhoben wurde
-   die vollständige Tabelle aus dem Log, als Markdown-Tabelle
-   die Zahl belegter Einträge je Array
-   die Auflösung der Unstimmigkeit 101/13/2 gegen 100/12/1: welcher Index ist der überzählige, und was steht darin
-   die abgeleitete Zuordnung `BSGraphics::Format` zu `DXGI_FORMAT`, soweit die Daten sie hergeben
-   erste Vermutungen zur Bedeutung einzelner Targets, **ausdrücklich als Vermutung gekennzeichnet**, mit der Begründung aus Format und Auflösung

-   [ ] **Step 7: Roadmap fortschreiben**

Status von B2 auf abgeschlossen setzen und einen Abschnitt „Aus Teilprojekt B2 bestätigt" ergänzen: Zahl der belegten Targets, ob die Beschriftung in RenderDoc ankam, und der Verweis auf das Befunddokument.

-   [ ] **Step 8: Commit**

```bash
git add -A
git commit -F - <<'MSG'
docs: record the render target inventory

Write down what the run actually produced: how many of the hundred
entries are occupied, their dimensions and formats, and which index
accounts for the mismatch between the renderer's arrays and the
manager's.

Guesses about what individual targets are for are marked as guesses.
The point of B2 was the instrument, not the identification - later
subprojects name the targets they need, and by then they will know
which ones those are.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
```

---

## Offene Risiken

-   **Die Beschriftung kommt in RenderDoc nicht an.** Wir setzen die Namen erst _nach_ Erzeugung der Ressourcen. Das sollte wirken, ist aber unbewiesen. Schlägt es fehl, verliert B2 seinen Hauptzweck; die protokollierte Tabelle bliebe als Ertrag. Task 4 Schritt 4 stellt es fest.
-   **Massenhafte `UNKNOWN`-Formate.** Die Tabelle deckt rund vierzig Formate ab, nicht alle 122. Task 4 Schritt 3 nennt das Vorgehen: fehlende Zahlen nachtragen.
-   **`BSGraphics::Format` lässt sich nicht casten**, weil es nur vorwärtsdeklariert ist. Task 3 Schritt 4 nennt den Ausweg über eine rohe Speicherleseoperation.
-   **Zu viele Targets sind zum Zeitpunkt des Inventars noch leer.** Dann sagt das Ergebnis wenig, und die Frage, wann die übrigen entstehen, wird selbst zum Befund — festzuhalten im Dokument, nicht durch Pollen zu umgehen.
