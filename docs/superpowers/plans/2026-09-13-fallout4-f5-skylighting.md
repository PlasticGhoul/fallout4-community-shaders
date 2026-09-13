# F5 — Skylighting: Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Himmelssichtbarkeit aus den Schattenwerfern der Engine in ein Sondengitter tragen und
damit das Umgebungslicht vor dem Composite dämpfen, sichtbar im Spiel und meßbar in der Tafel.

**Architecture:** Drei Stufen je Frame. Erfassung: der Draw-Hook wiederholt jeden Schattenwerfer-Zug
des `BSUtilityShader` in ein eigenes Tiefenziel mit einer Orthoprojektion aus einer wechselnden
Himmelsrichtung. Gitter: ein Compute-Shader trägt in `kBeforeComposite` die Sichtbarkeit als SH2 in
ein 3D-Gitter um die Kamera ein, über Frames akkumuliert. Pass: in der neuen Phase `kComposite`
multipliziert ein Vollbildpass den Faktor in `RT_028`, das der Composite auf das Umgebungslicht
anwendet. Eine Sonde vor dem Bau der Erfassung findet den Konstantenpuffer und bestätigt `RT_028`.

**Tech Stack:** C++23, MSVC `/W4 /WX`, D3D11 über REX, HLSL `vs/ps/cs_5_0`, ImGui nur über
`Declare`; Host-Tests als `main` mit `Check`.

**Spec:** `docs/superpowers/specs/2026-09-13-fallout4-f5-skylighting-design.md`

## Global Constraints

-   `/W4 /WX` auf unserem Ziel; nie `/WX` lockern.
-   Jede sichtbare Zeichenkette über `T("key", "English")`; nach Änderungen an Schlüsseln
    `python tools/extract-i18n.py --write`, sonst fällt `python tools/extract-i18n.py`.
-   Kein ImGui in `src/Features/`; ein Feature bekommt seine Seite allein durch `Declare`. Die
    `Help` des Schalters ist die Beschreibung oben auf der Seite: ein Satz über den Effekt.
-   Bauen: `<cmake>\cmake.exe --build --preset FO4` (deployt ins Spiel); Tests:
    `<cmake>\ctest.exe --test-dir build/FO4 -C Release --output-on-failure`. In jeder PowerShell
    vorher `$env:VCPKG_ROOT = "C:\vcpkg"` und `$cmake = "C:\Program Files\Microsoft Visual
Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"`; `<cmake>` steht für
    diesen Pfad.
-   Jeder Host-Test wird nach dem Grünwerden gebrochen; vorher benennen, welche Prüfung fällt;
    **erst prüfen, daß der Bau geglückt ist**. Mutationen per Edit setzen und zurücknehmen, nie
    `git checkout <datei>`.
-   Conventional Commits, Titel ≤ 50, Body bei 72, Abschluß
    `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`; nach „files were modified by this
    hook" erneut `git add` und committen.
-   Quelltext und HLSL mit Write/Edit schreiben, nie über die Shell.
-   Engine-Zug umgeben heißt: **alle acht** RTVs, DSV, Viewports, Blend, Rasterizer, ersetzte
    Puffer sichern und zurückgeben (F4).
-   Diagnoseausgaben je Sekunde (`kLogInterval = 180` Frames), nie einmalig; jede Ablehnung nennt
    den Grund.
-   Branch `port/f5-skylighting`, Basis `dev`. Ein Diagnose-Feature kommt in einem eigenen Commit
    und geht mit `git revert` wieder.

---

## Dateien

| Datei                                                                                                                 | Aufgabe                                    |
| --------------------------------------------------------------------------------------------------------------------- | ------------------------------------------ |
| `src/Render/DrawHook.h/.cpp`, `src/Features/CloudShadows.cpp`                                                         | Beobachterliste statt eines Beobachters    |
| `src/Render/Targets.h/.cpp`                                                                                           | `kShadowMap`, `DepthStencilView`           |
| `src/Render/FramePhase.h/.cpp`                                                                                        | Phase `kComposite`                         |
| `src/Render/DepthTarget.h/.cpp` (neu)                                                                                 | eigenes Tiefenziel                         |
| `src/Render/Resources.h/.cpp`                                                                                         | `Texture3D`                                |
| `src/Features/TargetView.h/.cpp` (neu), `package/Features/TargetView/…`                                               | Diagnose: ein Engine-Ziel über das Bild    |
| `src/Features/ShadowProbe.h/.cpp` (neu, temporär)                                                                     | Sonde für Lauf 1                           |
| `src/Features/Skylighting/OcclusionProjection.h/.cpp` (neu), `tests/SkylightingProjectionTests.cpp`                   | reine Rechnung mit Host-Test               |
| `src/Features/Skylighting.h/.cpp` (neu)                                                                               | das Feature                                |
| `package/Shaders/FO4/Common/SphericalHarmonics.hlsli`, `…/Common/Reconstruct.hlsli`                                   | gemeinsame Bausteine (Basis)               |
| `package/Features/Skylighting/Shaders/FO4/Skylighting/{Skylighting.hlsl, UpdateProbes.hlsl, SkylightingCommon.hlsli}` | die Shader                                 |
| `src/Feature/FeatureSystem.cpp`, `CMakeLists.txt`, `tools/verify-package.ps1`, `en.json`                              | Registrierung, Test, Paketprüfung, Katalog |
| `docs/fallout4-port/ROADMAP.md`, `frame-order.md`, `render-targets.md`                                                | Befund                                     |

Plugin-Quellen werden per Glob eingesammelt; nur der Test braucht einen CMake-Eintrag.

---

## Task 1: Der Draw-Hook nimmt mehrere Beobachter

**Files:**

-   Modify: `src/Render/DrawHook.h`, `src/Render/DrawHook.cpp`, `src/Features/CloudShadows.cpp`
    (`Setup`: `Render::SetDrawObserver(this)`; `Shutdown`: `Render::SetDrawObserver(nullptr)`)

**Interfaces:**

-   Produces: `bool Render::AddDrawObserver(DrawObserver*) noexcept` (false, wenn voll),
    `void Render::RemoveDrawObserver(DrawObserver*) noexcept`; `kMaxDrawObservers = 4`.
    `SetDrawObserver` entfällt.

-   [ ] **Step 1: Header**

In `DrawHook.h` die Deklaration von `SetDrawObserver` samt Kommentar ersetzen durch:

```cpp
	/// Up to four observers, each asked in turn whether it wants a draw; the
	/// first that does surrounds it. A feature adds itself from Setup and
	/// removes itself from Shutdown before it releases anything. The thunk
	/// may run on whichever thread the engine draws on; an observer keeps
	/// that in mind. F5 turned the single slot into this list: cloud shadows
	/// and skylighting both watch, different classes.
	inline constexpr std::size_t kMaxDrawObservers = 4;
	[[nodiscard]] bool AddDrawObserver(DrawObserver* a_observer) noexcept;
	void RemoveDrawObserver(DrawObserver* a_observer) noexcept;
```

und `#include <cstddef>` zu den Includes.

-   [ ] **Step 2: Implementierung**

In `DrawHook.cpp` `std::atomic<DrawObserver*> g_observer{ nullptr };` ersetzen durch

```cpp
		std::array<std::atomic<DrawObserver*>, kMaxDrawObservers> g_observers{};
```

(`#include <array>`), und die Stelle, die den Beobachter fragt (heute Zeilen 44–51), durch

```cpp
			DrawObserver* observer = nullptr;
			const auto current = CurrentTechniqueOf();
			for (auto& slot : g_observers) {
				auto* const candidate = slot.load(std::memory_order_acquire);
				if (candidate != nullptr && candidate->Wants(current)) {
					observer = candidate;
					break;
				}
			}
			if (observer == nullptr) {
				return Chain(a_self, a_call);  // wie heute der Durchlaß ohne Beobachter heißt
			}
			observer->BeforeDraw(*a_self, a_call);
			Chain(a_self, a_call);
			observer->AfterDraw(*a_self, a_call);
```

— `Chain` steht hier für den bestehenden Weiterleitungsaufruf; er wird nicht umbenannt, nur der
Beobachter-Teil ersetzt. `SetDrawObserver` ersetzen durch:

```cpp
	bool AddDrawObserver(DrawObserver* a_observer) noexcept
	{
		for (auto& slot : g_observers) {
			DrawObserver* expected = nullptr;
			if (slot.compare_exchange_strong(expected, a_observer, std::memory_order_acq_rel)) {
				return true;
			}
		}
		REX::ERROR("draw hook: no room for another observer ({} slots)", kMaxDrawObservers);
		return false;
	}

	void RemoveDrawObserver(DrawObserver* a_observer) noexcept
	{
		for (auto& slot : g_observers) {
			DrawObserver* expected = a_observer;
			slot.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
		}
	}
```

-   [ ] **Step 3: CloudShadows umstellen**

`Setup`: `Render::SetDrawObserver(this);` → `if (!Render::AddDrawObserver(this)) { return false; }`
(vor der `REX::INFO`-Zeile). `Shutdown`: `Render::SetDrawObserver(nullptr);` →
`Render::RemoveDrawObserver(this);`.

-   [ ] **Step 4: Bauen, Tests**

Voller Bau, `ctest`: 24 grün. `DrawObserverTests` prüft den Filter, nicht die Liste; die Liste hat
keinen Host-Test — sie ist vier Zeilen Atomics und wird von Lauf 1 (zwei Beobachter zugleich:
Cloud Shadows und die Sonde) im Spiel belegt.

-   [ ] **Step 5: Commit**

```bash
git add src/Render/DrawHook.h src/Render/DrawHook.cpp src/Features/CloudShadows.cpp
git commit -m "feat: the draw hook takes up to four observers"
```

---

## Task 2: `Targets::DepthStencilView` und `kShadowMap`

**Files:**

-   Modify: `src/Render/Targets.h`, `src/Render/Targets.cpp`

-   [ ] **Step 1: Header**

Nach `kSceneDepth`:

```cpp
	/// FO4_DS_008, R16_TYPELESS, 4096 square. The sun's shadow map: what
	/// BSUtilityShader draws into, around forty calls a frame, before
	/// kDFLight. Measured by FrameTrace on 2026-09-12.
	inline constexpr std::size_t kShadowMap = 8;
```

Nach `DepthSRV`:

```cpp
	/// The depth as a target: DepthStencilTarget::dsView[0]. What a draw into
	/// this slot has bound, which is how an observer recognises the shadow
	/// casters.
	[[nodiscard]] REX::W32::ID3D11DepthStencilView* DepthStencilView(std::size_t a_slot) noexcept;
```

-   [ ] **Step 2: Implementierung**

Nach `DepthSRV` in `Targets.cpp`, mit derselben Bereichsprüfung:

```cpp
	REX::W32::ID3D11DepthStencilView* DepthStencilView(std::size_t a_slot) noexcept
	{
		const auto* const data = RE::BSGraphics::GetRendererData();
		if (data == nullptr || a_slot >= std::size(data->depthStencilTargets)) {
			return nullptr;
		}
		return data->depthStencilTargets[a_slot].dsView[0];
	}
```

In `LogSlots` eine Zeile nach dem Muster der Tiefe: `"shadow map slot {} is {}"` mit
`DebugNameOf(DepthStencilView(kShadowMap))` bzw. `"missing"`, so wie die Zeile für `kSceneDepth`
gebaut ist.

-   [ ] **Step 3: Bauen, Commit**

```bash
git add src/Render/Targets.h src/Render/Targets.cpp
git commit -m "feat: name the shadow map and hand out depth views"
```

---

## Task 3: `Features::TargetView`

**Files:**

-   Create: `src/Features/TargetView.h`, `src/Features/TargetView.cpp`,
    `package/Features/TargetView/CORE`, `package/Features/TargetView/Shaders/FO4/TargetView/TargetView.hlsl`
-   Modify: `src/Feature/FeatureSystem.cpp` (nach `FrameTrace`), `tools/verify-package.ps1`

**Interfaces:**

-   Consumes: `Render::Targets::RenderTargetSRV(slot)`, `RenderTargetView(kSceneHDR)`,
    `Render::SubscribeFramePhase(kAfterOpaque, …)`, `Render::DrawFullscreen`,
    `Shader::LoadSource`, `Shader::CompilePixelShader`, `Render::ConstantBuffer`.

-   [ ] **Step 1: Shader**

```hlsl
// One engine render target laid over the finished picture, for looking at
// what a pass leaves behind. Drawn in kAfterOpaque, after the sky, so every
// pixel shows it - F4's cube view drew before the sky and was blind there.
Texture2D<float4> Source : register(t0);
SamplerState PointClamp : register(s0);

cbuffer PerFrame : register(b1)
{
	float4 Params;  // x channel (0 rgb, 1 r, 2 g, 3 b, 4 a), yzw unused
};

struct PixelInput
{
	float4 position: SV_POSITION;
	float2 uv: TEXCOORD0;
};

float4 main(PixelInput input) : SV_TARGET0
{
	const float4 value = Source.Sample(PointClamp, input.uv);
	const int channel = (int)(Params.x + 0.5);
	if (channel == 1)
		return float4(value.rrr, 1.0);
	if (channel == 2)
		return float4(value.ggg, 1.0);
	if (channel == 3)
		return float4(value.bbb, 1.0);
	if (channel == 4)
		return float4(value.aaa, 1.0);
	return float4(value.rgb, 1.0);
}
```

-   [ ] **Step 2: Feature**

`TargetView.h`: Klasse `TargetView final : public Feature` mit `Name()` `"TargetView"`, den fünf
Methoden, privat `Draw()`, `Compile()`, `Render::ConstantBuffer _constants`,
`REX::W32::ID3D11PixelShader* _shader`, `REX::W32::ID3D11SamplerState* _pointClamp`,
`REX::W32::ID3D11BlendState* _plain`, `Render::PhaseDispatcher::Token _phase`, `Util::FileWatch
_watch`, `std::uint64_t _draws`.

`TargetView.cpp`, `Declare`:

```cpp
	void TargetView::Declare()
	{
		Settings::DeclareFeature("TargetView", false)
			.Label("feature.target_view.name", "Target View")
			.Help(
				"feature.target_view.help",
				"Lays one of the engine's render targets over the picture. A tool for finding "
				"out what a pass leaves behind; off unless you are looking.");
		Settings::DeclareChoice(
			"TargetView/target",
			"RT_028",
			std::vector<std::string>{ "RT_028", "RT_009", "RT_003", "RT_020", "RT_022", "RT_023", "RT_024", "RT_057", "RT_039" })
			.Label("feature.target_view.target", "Target")
			.Help("feature.target_view.target_help", "Which render target to show, by its slot.");
		Settings::DeclareChoice(
			"TargetView/channel",
			"rgb",
			std::vector<std::string>{ "rgb", "r", "g", "b", "a" })
			.Label("feature.target_view.channel", "Channel")
			.Help("feature.target_view.channel_help", "All three colour channels, or one of the four as grey.");
	}
```

`Setup`: `InitFullscreenPass`, `Compile`, `_constants.Create(16, "FO4CS_CB_TargetView")`, Sampler
(Point, Clamp) und Blend (aus, Maske ALL) wie `EnsureStates` in `CloudShadows`,
`SubscribeFramePhase(kAfterOpaque, "TargetView/Draw", [this] { Draw(); })`. `Frame`: Wächter,
`recompiling`. `Shutdown`: Phase austragen, alles freigeben.

`Draw`:

```cpp
	void TargetView::Draw()
	{
		++_draws;
		const auto choice = Settings::GetString("TargetView/target");
		// "RT_028" -> 28. The slot is the number in the name, and the name is
		// what a capture shows, so nothing has to be translated.
		std::size_t slot = 0;
		if (choice.size() == 6 && choice.starts_with("RT_")) {
			slot = static_cast<std::size_t>(std::stoul(choice.substr(3)));
		}
		auto* const source = Render::Targets::RenderTargetSRV(slot);
		auto* const scene = Render::Targets::RenderTargetView(Render::Targets::kSceneHDR);
		auto* const context = Render::GetContext();
		if (source == nullptr || scene == nullptr || context == nullptr || _shader == nullptr) {
			if (_draws % kLogInterval == 0) {
				REX::INFO("TargetView: nothing drawn, slot {} source {}, scene {}", slot,
					source != nullptr ? "ok" : "missing", scene != nullptr ? "ok" : "missing");
			}
			return;
		}
		const auto channelName = Settings::GetString("TargetView/channel");
		const float channel = channelName == "r" ? 1.0f : channelName == "g" ? 2.0f : channelName == "b" ? 3.0f : channelName == "a" ? 4.0f : 0.0f;
		const float params[4]{ channel, 0.0f, 0.0f, 0.0f };

		const Render::StateGuard guard;
		const Render::PassScope scope{ "TargetView/Show" };
		context->OMSetRenderTargets(0, nullptr, nullptr);
		static_cast<void>(_constants.Update(params, sizeof(params)));
		auto* const buffer = _constants.Buffer();
		const float blendFactor[4]{};
		context->OMSetRenderTargets(1, std::addressof(scene), nullptr);
		context->OMSetBlendState(_plain, blendFactor, 0xFFFFFFFFu);
		context->PSSetShaderResources(0, 1, std::addressof(source));
		context->PSSetSamplers(0, 1, std::addressof(_pointClamp));
		context->PSSetConstantBuffers(1, 1, std::addressof(buffer));
		context->PSSetShader(_shader, nullptr, 0);
		Render::DrawFullscreen();
	}
```

`Compile` nach dem Muster von `CloudShadows::Compile` mit `kShaderFile = "TargetView/TargetView.hlsl"`.
Der `StateGuard` sichert seit F4 PS-Sampler 0 und zwei PS-SRVs; das reicht hier.

-   [ ] **Step 3: Registrieren, Paket**

`FeatureSystem.cpp`: `#include "Features/TargetView.h"`; nach `FrameTrace`:

```cpp
			// A tool like the trace: draws one engine target over the picture
			// from kAfterOpaque and owns nothing of the engine's.
			TheRegistry().Register(std::make_unique<TargetView>());
```

`package/Features/TargetView/CORE` (leere Datei). `verify-package.ps1`: in der Prüfung des
Basisarchivs eine Zeile nach dem Muster von `Fullscreen.hlsl`:

```powershell
    Check (
        $baseEntries -contains "Shaders/FO4/TargetView/TargetView.hlsl"
    ) "and the target view's shader, a core tool"
```

-   [ ] **Step 4: i18n, Bauen, Tests, Paket**

```pwsh
python tools/extract-i18n.py --write
& "<cmake>\cmake.exe" --build --preset FO4
& "<cmake>\ctest.exe" --test-dir build/FO4 -C Release
& "<cmake>\cmake.exe" --build --preset FO4 --target package
pwsh tools/verify-package.ps1
```

-   [ ] **Step 5: Commit**

```bash
git add src/Features/TargetView.h src/Features/TargetView.cpp package/Features/TargetView src/Feature/FeatureSystem.cpp tools/verify-package.ps1 package/F4SE/Plugins/CommunityShadersFO4/Translations/en.json
git commit -m "feat: a debug view that lays an engine target over the picture"
```

---

## Task 4: Die Sonde `ShadowProbe`

**Files:**

-   Create: `src/Features/ShadowProbe.h`, `src/Features/ShadowProbe.cpp`
-   Modify: `src/Feature/FeatureSystem.cpp` (nach `TargetView`)

**Interfaces:**

-   Consumes: `Render::AddDrawObserver` (Task 1), `Render::Targets::DepthStencilView` (Task 2),
    `Render::ClassIndexOf("BSUtilityShader")`, `Render::TechniqueFilter`, `Render::FrameCount`.

-   [ ] **Step 1: Feature**

`DeclareFeature("ShadowProbe", false)` mit Help `"Logs what the sun's shadow casters bind, once a
second. A probe for F5; removed after its run."`. `Setup`: Klassenindex, Filter mit
`kAnyTechnique`, Sonnenrichtung wie `CloudShadows::Draw` (Zeile 0 der Lichtrotation, negiert),
`AddDrawObserver(this)`. `Shutdown`: `RemoveDrawObserver(this)`, Staging-Puffer freigeben.

`BeforeDraw` — der Kern; blockierendes `Map` ist hier erlaubt, weil es einmal je Sekunde in einem
Diagnose-Feature geschieht:

```cpp
	void ShadowProbe::BeforeDraw(REX::W32::ID3D11DeviceContext& a_context, const Render::DrawCall&) noexcept
	{
		REX::W32::ID3D11DepthStencilView* dsv = nullptr;
		a_context.OMGetRenderTargets(0, nullptr, std::addressof(dsv));
		const auto frame = Render::FrameCount();
		if (frame != _frame) {
			_frame = frame;
			++_frames;
			_perFrameShadow = 0;
			_perFrameOther = 0;
		}
		const bool shadow = dsv != nullptr && dsv == Render::Targets::DepthStencilView(Render::Targets::kShadowMap);
		if (shadow) {
			++_perFrameShadow;
			++_shadowDraws;
		} else {
			++_perFrameOther;
			++_otherDraws;
			for (const auto slot : { std::size_t{ 5 }, std::size_t{ 7 }, std::size_t{ 9 } }) {
				if (dsv != nullptr && dsv == Render::Targets::DepthStencilView(slot)) {
					++_otherBySlot[slot];
				}
			}
			RememberTechnique(Render::CurrentTechniqueOf().technique);  // bis 16 verschiedene, für die Regen-Frage
		}
		if (dsv != nullptr) {
			dsv->Release();
		}
		if (!shadow || _perFrameShadow != 1 || _frames % kLogInterval != 0) {
			return;
		}

		// The first shadow caster of every 180th frame: what its vertex
		// stage has bound, and the bytes of each buffer.
		REX::W32::ID3D11Buffer* buffers[kSlots]{};
		a_context.VSGetConstantBuffers(0, kSlots, buffers);
		REX::INFO("shadow probe: frame {}, {} caster(s) last frame, {} other utility draw(s), sun [{:.3f} {:.3f} {:.3f}]",
			frame, _lastFrameShadow, _lastFrameOther, _sun[0], _sun[1], _sun[2]);
		for (std::uint32_t slot = 0; slot < kSlots; ++slot) {
			auto* const buffer = buffers[slot];
			if (buffer == nullptr) {
				continue;
			}
			REX::W32::D3D11_BUFFER_DESC desc{};
			buffer->GetDesc(std::addressof(desc));
			const auto bytes = std::min<std::uint32_t>(desc.byteWidth, kDumpBytes);
			if (!EnsureStaging(slot, desc.byteWidth)) {
				buffer->Release();
				continue;
			}
			a_context.CopyResource(_staging[slot], buffer);
			REX::W32::D3D11_MAPPED_SUBRESOURCE mapped{};
			if (a_context.Map(_staging[slot], 0, REX::W32::D3D11_MAP_READ, 0, std::addressof(mapped)) >= 0) {
				const auto* const data = static_cast<const std::uint8_t*>(mapped.data);
				REX::INFO("  vs slot {}: {} bytes{}", slot, desc.byteWidth, ChangedSince(slot, data, bytes));
				for (std::uint32_t offset = 0; offset < bytes; offset += 64) {
					REX::INFO("    +{:03}: {}{}", offset, Row(data + offset, std::min(bytes - offset, 64u)), OrthoHint(data + offset, bytes - offset));
				}
				std::memcpy(_last[slot].data(), data, bytes);
				_lastBytes[slot] = bytes;
				a_context.Unmap(_staging[slot], 0);
			}
			buffer->Release();
		}
	}
```

Hilfen im anonymen Namensraum: `Row` formatiert 16 Floats mit `{:.3f}`; `OrthoHint` gibt
`"  <- 4x4, last row 0 0 0 1"` zurück, wenn 64 Byte vorliegen und die Floats 12..15 gleich
`0 0 0 1` sind (Toleranz 1e-4), sonst leer; `ChangedSince` vergleicht mit `_last[slot]` und gibt
`", {} byte(s) changed since last dump"` zurück (oder `", first dump"`). `EnsureStaging` legt je
Slot einen Staging-Puffer der Größe an (`D3D11_USAGE_STAGING`, `CPU_ACCESS_READ`). `kSlots = 14`,
`kDumpBytes = 256`, `kLogInterval = 180`. `AfterDraw` leer. In `Frame`: `_lastFrameShadow` und
`_lastFrameOther` aus den Zählern des vorigen Frames übernehmen; je Sekunde zusätzlich eine
Zeile `"shadow probe: other utility targets DS_005 {} DS_007 {} DS_009 {} techniques {}"`.

-   [ ] **Step 2: Registrieren, bauen, committen**

```cpp
			// The probe for F5. Watches the utility shader's draws; removed
			// after run 1 with a revert of this commit.
			TheRegistry().Register(std::make_unique<ShadowProbe>());
```

```pwsh
python tools/extract-i18n.py --write
& "<cmake>\cmake.exe" --build --preset FO4
& "<cmake>\ctest.exe" --test-dir build/FO4 -C Release
```

```bash
git add src/Features/ShadowProbe.h src/Features/ShadowProbe.cpp src/Feature/FeatureSystem.cpp package/F4SE/Plugins/CommunityShadersFO4/Translations/en.json
git commit -m "diag: probe what the sun's shadow casters bind"
```

---

## Task 5: Lauf 1 — Sonde

Kein Code vor dem Lauf. Prüfliste an den Nutzer, nummeriert, mit „was schiefgehen kann":

1. Overlay: `Shadow Probe` und `Target View` einschalten. Fehlen sie: `Declare` lief nicht.
2. Sanctuary, draußen, Tageszeit 12 (`set gamehour to 12`), eine Minute stehen, dann eine Minute
   gehen. Log füllt sich je Sekunde.
3. `Target View` auf `RT_028`, `rgb`: was ist zu sehen? Dann `r`, `g`, `b`, `a` einzeln. Bitte
   beschreiben: grau mit dunklen Ecken und Kanten (SSAO), eine Farbe, Schwarz, Rauschen.
4. `Target View` auf `RT_009`: Reflexionen? Auf `RT_020`: Normalen (bunt)?
5. `fw 1CA7E4` (Regen), eine halbe Minute; dann `fw 2B52A`.
6. Beide Diagnose-Features aus. F11.

Log auswerten: die Slot-Liste; in welchem Slot eine 4×4 mit `0 0 0 1` steht, ob ihre dritte Zeile
(Floats 8..10) der negierten Sonnenrichtung entspricht (Blick entlang des Lichts), wie groß die
Translation (Floats 3, 7, 11) ist — klein: kamerarelative Geometrie, Größenordnung 80000:
absolute Welt —, und ob sich der Puffer je Sekunde ändert (Sicht) oder nicht. Die Regen-Zeile.
Beide Beobachter (Cloud Shadows und Sonde) liefen zugleich: die `clouds:`-Zeile muß weiter
1620 Züge zeigen.

**Ergebnis festhalten** in einer Notiz `docs/fallout4-port/f5-probe.md` (kommt in die Roadmap in
Task 14, dann gelöscht — oder gleich in die Roadmap, wenn die Zeit reicht): Slot, Offset,
Translationskonvention, `RT_028`-Kanal und -Inhalt, Regenbefund. Die vier Konstanten für Task 10:
`kViewConstantSlot`, `kViewProjectionOffset`, `kGeometryIsCameraRelative`, `kAmbientWriteMask`.

Danach:

```bash
git revert --no-edit <hash von "diag: probe what the sun's shadow casters bind">
python tools/extract-i18n.py --write
git add package/F4SE/Plugins/CommunityShadersFO4/Translations/en.json
git commit --amend --no-edit
```

(Der Revert nimmt Sonde und Registrierung; der Katalog verliert die zwei Schlüssel.)

---

## Task 6: Phase `kComposite`

**Files:**

-   Modify: `src/Render/FramePhase.h`, `src/Render/FramePhase.cpp`

**Interfaces:**

-   Produces: `Render::Phase::kComposite`.

-   [ ] **Step 1: Enum**

In `FramePhase.h` vor `kCount`:

```cpp
		/// BSDFCompositeShader setting up technique 0x0040, BSDFComposite -
		/// the draw that reads the whole g-buffer, both light targets, RT_028
		/// and RT_009 and writes the finished HDR scene. Fires right before
		/// that call goes to the engine, once a frame, whatever came before
		/// it in the same class. Where a feature changes an input of the
		/// composite - F5 multiplies into RT_028 here. kBeforeComposite is
		/// the first call of the class, BSDFCompositeBase, and whether the
		/// engine's SSAO passes run between the two is not measured; this
		/// phase does not have to know.
		kComposite,
```

-   [ ] **Step 2: Anker und Thunk**

In `FramePhase.cpp` bekommt `Anchor` ein Feld

```cpp
			/// kAnyTechnique fires on the first call of the frame; a technique
			/// id fires on the first call of the frame with that technique.
			std::uint32_t technique{ kAnyTechnique };
```

mit `constexpr std::uint32_t kAnyTechnique = 0xFFFFFFFFu;` davor, und die Tabelle wird

```cpp
		std::array<Anchor, kAnchorCount> g_anchors{ {
			{ "BSDFCompositeShader" },
			{ "BSEffectShader", kSkyMarker },
			{ "BSDFCompositeShader", kNoGate, 0x0040u },
			{ "BSSkyShader" },
		} };
```

Die Reihenfolge folgt dem Enum: `kBeforeComposite`, `kAfterOpaque`, `kComposite`, dann der
Marker. **Zwei Anker auf derselben Klasse:** `InstallOne` patcht denselben Slot zweimal; der zweite
Patch findet unseren ersten Thunk als „Original" und kettet an ihn. Das ist richtig und gewollt —
der Aufruf läuft durch beide Thunks —, aber `DescribeVTable` prüft die Tabelle, nicht den Eintrag,
also bleibt die Prüfung gültig. Im Thunk wird vor dem Dispatch geprüft:

```cpp
			if (anchor.technique != kAnyTechnique && a_pass != anchor.technique) {
				return reinterpret_cast<SetupTechniqueFn>(anchor.original)(a_self, a_pass);
			}
```

`Anchor` braucht dafür einen Konstruktor oder Aggregat-Initialisierung mit drei Feldern in dieser
Reihenfolge: `className`, `after`, `technique` — `technique` also **vor** `patch` einordnen.

-   [ ] **Step 3: Bauen, Tests, Commit**

Voller Bau; im Log des nächsten Laufs erscheint `frame phase 2 installed on BSDFCompositeShader,
chaining to <Adresse unseres Thunks 0>`. `PhaseDispatcherTests` unverändert grün.

```bash
git add src/Render/FramePhase.h src/Render/FramePhase.cpp
git commit -m "feat: a frame phase right before the composite draws"
```

---

## Task 7: `Render::DepthTarget` und `Render::Texture3D`

**Files:**

-   Create: `src/Render/DepthTarget.h`, `src/Render/DepthTarget.cpp`
-   Modify: `src/Render/Resources.h`, `src/Render/Resources.cpp`

**Interfaces:**

-   Produces: `Render::DepthTarget` mit `Create(size, debugName)`, `Release()`, `Clear(context,
depth)`, `DSV()`, `SRV()`, `Size()`, `Valid()`; `Render::Texture3D` mit `Create(w, h, d,
format, debugName)`, `Release()`, `ClearUint(context, value)`, `SRV()`, `UAV()`, `Valid()`.

-   [ ] **Step 1: DepthTarget.h**

```cpp
#pragma once

#include <REX/W32/D3D11.h>

#include <cstdint>
#include <string_view>

namespace Render
{
	/// A depth buffer we own: one view to draw into, one to sample. Square,
	/// R32_TYPELESS, no stencil, one mip. What F5 repeats the engine's shadow
	/// casters into; the shape F6's volumes will read from.
	class DepthTarget
	{
	public:
		DepthTarget() = default;
		~DepthTarget() { Release(); }
		DepthTarget(const DepthTarget&) = delete;
		DepthTarget& operator=(const DepthTarget&) = delete;

		/// Replaces whatever was there.
		[[nodiscard]] bool Create(std::uint32_t a_size, std::string_view a_debugName) noexcept;
		/// Idempotent.
		void Release() noexcept;
		void Clear(REX::W32::ID3D11DeviceContext& a_context, float a_depth) noexcept;

		[[nodiscard]] REX::W32::ID3D11DepthStencilView* DSV() const noexcept { return _dsv; }
		[[nodiscard]] REX::W32::ID3D11ShaderResourceView* SRV() const noexcept { return _srv; }
		[[nodiscard]] std::uint32_t Size() const noexcept { return _size; }
		[[nodiscard]] bool Valid() const noexcept { return _texture != nullptr; }

	private:
		REX::W32::ID3D11Texture2D* _texture{ nullptr };
		REX::W32::ID3D11DepthStencilView* _dsv{ nullptr };
		REX::W32::ID3D11ShaderResourceView* _srv{ nullptr };
		std::uint32_t _size{ 0 };
	};
}
```

-   [ ] **Step 2: DepthTarget.cpp**

Nach dem Muster von `CubeTarget.cpp`: `D3D11_TEXTURE2D_DESC` mit `format = DXGI_FORMAT_R32_TYPELESS`,
`bindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE`, `arraySize 1`; DSV mit
`format = DXGI_FORMAT_D32_FLOAT`, `viewDimension = D3D11_DSV_DIMENSION_TEXTURE2D`,
`texture2D.mipSlice = 0`; SRV mit `format = DXGI_FORMAT_R32_FLOAT`,
`viewDimension = D3D_SRV_DIMENSION_TEXTURE2D`, ein Mip. Debug-Namen `<name>`, `<name>_dsv`. `Clear`:
`a_context.ClearDepthStencilView(_dsv, D3D11_CLEAR_DEPTH, a_depth, 0)` wenn gültig. Fehler mit
`REX::ERROR("depth target {}: …")` wie beim Würfel.

-   [ ] **Step 3: Texture3D**

In `Resources.h` nach `Texture`:

```cpp
	/// A volume with a shader view and an unordered view over all of it. What
	/// F5 keeps its probe grid in; one mip, default usage.
	class Texture3D
	{
	public:
		Texture3D() = default;
		~Texture3D();
		Texture3D(const Texture3D&) = delete;
		Texture3D& operator=(const Texture3D&) = delete;

		[[nodiscard]] bool Create(
			std::uint32_t a_width,
			std::uint32_t a_height,
			std::uint32_t a_depth,
			REX::W32::DXGI_FORMAT a_format,
			std::string_view a_debugName) noexcept;
		void Release() noexcept;
		/// Every cell to the same four integers, through the UAV.
		void ClearUint(REX::W32::ID3D11DeviceContext& a_context, std::uint32_t a_value) noexcept;

		[[nodiscard]] REX::W32::ID3D11ShaderResourceView* SRV() const noexcept { return _srv; }
		[[nodiscard]] REX::W32::ID3D11UnorderedAccessView* UAV() const noexcept { return _uav; }
		[[nodiscard]] bool Valid() const noexcept { return _texture != nullptr; }

	private:
		REX::W32::ID3D11Texture3D* _texture{ nullptr };
		REX::W32::ID3D11ShaderResourceView* _srv{ nullptr };
		REX::W32::ID3D11UnorderedAccessView* _uav{ nullptr };
	};
```

`Resources.cpp`: `Create` wie `Texture::Create` mit `D3D11_TEXTURE3D_DESC` (`width`, `height`,
`depth`, `mipLevels 1`, `format`, `usage DEFAULT`, `bindFlags SHADER_RESOURCE | UNORDERED_ACCESS`),
`CreateTexture3D`; SRV `D3D_SRV_DIMENSION_TEXTURE3D`, `texture3D.mostDetailedMip 0, mipLevels 1`;
UAV `D3D11_UAV_DIMENSION_TEXTURE3D`, `texture3D.mipSlice 0, firstWSlice 0, wSize = a_depth`.
`ClearUint`: `const std::uint32_t values[4]{ v, v, v, v }; a_context.ClearUnorderedAccessViewUint(_uav, values);`.
Debug-Name über `SetDebugName`. `Release` idempotent, Destruktor ruft es.

-   [ ] **Step 4: Bauen, Commit**

```bash
git add src/Render/DepthTarget.h src/Render/DepthTarget.cpp src/Render/Resources.h src/Render/Resources.cpp
git commit -m "feat: a depth target and a volume texture of our own"
```

---

## Task 8: `Skylighting::OcclusionProjection` mit Host-Test

**Files:**

-   Create: `src/Features/Skylighting/OcclusionProjection.h`,
    `src/Features/Skylighting/OcclusionProjection.cpp`, `tests/SkylightingProjectionTests.cpp`
-   Modify: `CMakeLists.txt` (nach dem Block `CloudProjectionTests`)

**Interfaces:**

-   Produces: Namensraum `Features::Skylighting` (Datei `Skylighting/OcclusionProjection.h`) mit
    den Konstanten und drei Funktionen unten.

-   [ ] **Step 1: Header**

```cpp
#pragma once

#include <cstdint>

namespace Features::Skylighting
{
	/// The cube the sky is seen from, in game units: the template's
	/// occlusionDistance. Centred on the camera.
	inline constexpr double kOcclusionDistance = 10000.0;
	inline constexpr std::uint32_t kOcclusionMapSize = 2048;
	/// The probe grid: cells over 10000 x 10000 x 5000 units, the template's
	/// ARRAY_DIM and ARRAY_SIZE. Both shaders carry the same numbers.
	inline constexpr std::uint32_t kGridSize[3] = { 256, 256, 128 };
	inline constexpr double kGridExtent[3] = { 10000.0, 10000.0, 5000.0 };

	/// The sky direction of a frame: the template's R2 sequence on the unit
	/// disc, the radius scaled so that the zenith angle stays within
	/// a_maxZenith, lifted onto the sphere. Points down (z < 0) - the way
	/// the occlusion camera looks.
	void SkyDirection(std::uint32_t a_index, double a_seed, double a_maxZenith, double (&a_out)[3]) noexcept;

	/// Orthographic projection looking along a_direction at a cube of edge
	/// kOcclusionDistance around a_centre, rows as the engine's vertex
	/// shaders read them (clip_i = sum_j M[i][j] p_j): x and y span the cube
	/// face in [-1, 1], z runs 0 at the near face to 1 at the far face,
	/// w is 1. Sixteen doubles, row major.
	void OcclusionMatrix(const double (&a_centre)[3], const double (&a_direction)[3], double (&a_out)[16]) noexcept;

	struct GridWindow
	{
		/// Origin of the camera's cell, relative to the eye.
		double cellOrigin[3];
		/// Where the ring buffer starts, in [0, kGridSize).
		std::int32_t arrayOrigin[3];
		/// previous cell - current cell, per axis: which edge of the grid is
		/// new this frame. Zero while the camera stays in its cell.
		std::int32_t validMargin[3];
	};

	/// The template's GetCommonBufferData. a_previousCell is the cell of
	/// the last frame and is updated to this frame's.
	GridWindow WindowFor(const double (&a_eye)[3], std::int32_t (&a_previousCell)[3]) noexcept;
}
```

-   [ ] **Step 2: Test**

```cpp
#include "Features/Skylighting/OcclusionProjection.h"

#include <cmath>
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

	bool Near(double a_value, double a_expected, double a_tolerance)
	{
		return std::abs(a_value - a_expected) <= a_tolerance;
	}

	void Apply(const double (&a_m)[16], const double (&a_p)[3], double a_w, double (&a_out)[4])
	{
		for (int i = 0; i < 4; ++i) {
			a_out[i] = a_m[i * 4 + 0] * a_p[0] + a_m[i * 4 + 1] * a_p[1] + a_m[i * 4 + 2] * a_p[2] + a_m[i * 4 + 3] * a_w;
		}
	}

	constexpr double kPi = 3.14159265358979323846;
}

int main()
{
	using namespace Features::Skylighting;

	// Directions: down, unit, within the zenith angle, not all the same.
	{
		bool allDown = true, allUnit = true, within = true;
		double first[3]{}, second[3]{};
		SkyDirection(0, 0.37, kPi / 2.0, first);
		SkyDirection(1, 0.37, kPi / 2.0, second);
		for (std::uint32_t i = 0; i < 64; ++i) {
			double d[3]{};
			SkyDirection(i, 0.37, kPi / 2.0, d);
			allDown = allDown && d[2] < 0.0;
			allUnit = allUnit && Near(std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), 1.0, 1e-9);
		}
		for (std::uint32_t i = 0; i < 64; ++i) {
			double d[3]{};
			SkyDirection(i, 0.37, 30.0 * kPi / 180.0, d);
			within = within && std::acos(-d[2]) <= 30.0 * kPi / 180.0 + 1e-9;
		}
		Check(allDown, "every direction points down");
		Check(allUnit, "every direction is a unit vector");
		Check(within, "at 30 degrees no direction leaves the cone");
		Check(!Near(first[0], second[0], 1e-6) || !Near(first[1], second[1], 1e-6), "consecutive directions differ");
	}

	// The matrix: centre to the middle, near face to 0, far face to 1, the
	// cube's edge to the clip edge, a vector's w stays 0.
	{
		const double centre[3]{ 100.0, -200.0, 50.0 };
		const double direction[3]{ 0.0, 0.0, -1.0 };
		double m[16]{};
		OcclusionMatrix(centre, direction, m);
		double out[4]{};
		Apply(m, centre, 1.0, out);
		Check(Near(out[0], 0.0, 1e-9) && Near(out[1], 0.0, 1e-9) && Near(out[2], 0.5, 1e-9) && Near(out[3], 1.0, 1e-9),
			"the centre lands in the middle at half depth");
		const double nearPoint[3]{ 100.0, -200.0, 50.0 + 5000.0 };  // against the direction: towards the sky
		Apply(m, nearPoint, 1.0, out);
		Check(Near(out[2], 0.0, 1e-9), "the near face is depth 0");
		const double farPoint[3]{ 100.0, -200.0, 50.0 - 5000.0 };
		Apply(m, farPoint, 1.0, out);
		Check(Near(out[2], 1.0, 1e-9), "the far face is depth 1");
		const double edge[3]{ 100.0 + 5000.0, -200.0, 50.0 };
		Apply(m, edge, 1.0, out);
		Check(Near(std::abs(out[0]), 1.0, 1e-9) || Near(std::abs(out[1]), 1.0, 1e-9), "the cube's edge is the clip edge");
		Check(Near(m[12], 0.0, 0.0) && Near(m[13], 0.0, 0.0) && Near(m[14], 0.0, 0.0) && Near(m[15], 1.0, 0.0), "the last row is 0 0 0 1");
		Apply(m, direction, 0.0, out);
		Check(Near(out[0], 0.0, 1e-9) && Near(out[1], 0.0, 1e-9) && out[2] > 0.0 && Near(out[3], 0.0, 1e-9),
			"the direction itself maps to positive depth and no offset");
	}

	// A tilted direction still gives an orthonormal frame: the direction's
	// perpendiculars map to unit clip extents.
	{
		const double centre[3]{};
		const double s = std::sqrt(0.5);
		const double direction[3]{ s, 0.0, -s };
		double m[16]{};
		OcclusionMatrix(centre, direction, m);
		const double along[3]{ s * 5000.0, 0.0, -s * 5000.0 };
		double out[4]{};
		Apply(m, along, 1.0, out);
		Check(Near(out[0], 0.0, 1e-9) && Near(out[1], 0.0, 1e-9) && Near(out[2], 1.0, 1e-9), "along the tilted direction: depth 1, no offset");
		const double across[3]{ 0.0, 5000.0, 0.0 };
		Apply(m, across, 1.0, out);
		Check(Near(std::abs(out[0]), 1.0, 1e-9) || Near(std::abs(out[1]), 1.0, 1e-9), "across it: the clip edge");
	}

	// The window: standing still, one cell along x, a leap.
	{
		std::int32_t cell[3]{ 0, 0, 0 };
		const double eye[3]{ 12.0, -7.0, 3.0 };
		auto window = WindowFor(eye, cell);
		const double cellSize = kGridExtent[0] / kGridSize[0];
		Check(window.validMargin[0] == 0 && window.validMargin[1] == 0 && window.validMargin[2] == 0, "still: nothing is new");
		Check(std::abs(window.cellOrigin[0]) <= cellSize * 0.5 + 1e-9 && std::abs(window.cellOrigin[1]) <= cellSize * 0.5 + 1e-9,
			"still: the cell origin is within half a cell of the eye");
		Check(window.arrayOrigin[0] >= 0 && window.arrayOrigin[0] < 256, "the array origin is inside the grid");
		const auto originBefore = window.arrayOrigin[0];
		const double moved[3]{ 12.0 + cellSize, -7.0, 3.0 };
		window = WindowFor(moved, cell);
		Check(window.validMargin[0] == -1 && window.validMargin[1] == 0, "one cell along x: the far x edge is new");
		Check(window.arrayOrigin[0] == (originBefore + 1) % 256, "and the ring buffer moved by one");
		const double leapt[3]{ 12.0 + cellSize * 301.0, -7.0, 3.0 };
		window = WindowFor(leapt, cell);
		Check(window.validMargin[0] <= -256, "a leap makes the whole grid new");
	}

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
```

-   [ ] **Step 3: CMake**

Nach `add_test(NAME CloudProjection COMMAND CloudProjectionTests)` den Block aus
`CloudProjectionTests` kopieren mit `SkylightingProjectionTests`,
`tests/SkylightingProjectionTests.cpp`, `src/Features/Skylighting/OcclusionProjection.cpp`,
`add_test(NAME SkylightingProjection COMMAND SkylightingProjectionTests)`.

-   [ ] **Step 4: Rot sehen**

Bauen des Test-Ziels: „Cannot find source file …/OcclusionProjection.cpp".

-   [ ] **Step 5: Implementierung**

```cpp
#include "Features/Skylighting/OcclusionProjection.h"

#include <cmath>

namespace Features::Skylighting
{
	namespace
	{
		constexpr double kTau = 6.283185307179586;

		double Fract(double a_value) noexcept
		{
			return a_value - std::floor(a_value);
		}

		void Normalise(double (&a_v)[3]) noexcept
		{
			const auto length = std::sqrt(a_v[0] * a_v[0] + a_v[1] * a_v[1] + a_v[2] * a_v[2]);
			if (length > 0.0) {
				a_v[0] /= length;
				a_v[1] /= length;
				a_v[2] /= length;
			}
		}

		void Cross(const double (&a_a)[3], const double (&a_b)[3], double (&a_out)[3]) noexcept
		{
			a_out[0] = a_a[1] * a_b[2] - a_a[2] * a_b[1];
			a_out[1] = a_a[2] * a_b[0] - a_a[0] * a_b[2];
			a_out[2] = a_a[0] * a_b[1] - a_a[1] * a_b[0];
		}

		double Dot(const double (&a_a)[3], const double (&a_b)[3]) noexcept
		{
			return a_a[0] * a_b[0] + a_a[1] * a_b[1] + a_a[2] * a_b[2];
		}

		std::int32_t Wrap(std::int32_t a_value, std::int32_t a_size) noexcept
		{
			const auto r = a_value % a_size;
			return r < 0 ? r + a_size : r;
		}
	}

	void SkyDirection(std::uint32_t a_index, double a_seed, double a_maxZenith, double (&a_out)[3]) noexcept
	{
		// The template's R2 sequence: two irrationals stepped per frame.
		const double u = Fract(a_seed + a_index * 0.245122333753);
		const double v = Fract(a_seed + a_index * 0.430159709002);
		// Uniform on a disc of radius sin(maxZenith): sqrt for area, the
		// radius so that asin(r) never exceeds the zenith angle. The template
		// scales u by sin(maxZenith) before the root, which at thirty degrees
		// lets directions out to forty-five; this keeps the promise the
		// setting makes.
		const double r = std::sin(a_maxZenith) * std::sqrt(u);
		const double phi = kTau * v;
		a_out[0] = -r * std::cos(phi);
		a_out[1] = -r * std::sin(phi);
		a_out[2] = -std::sqrt(std::max(0.0, 1.0 - r * r));
		Normalise(a_out);
	}

	void OcclusionMatrix(const double (&a_centre)[3], const double (&a_direction)[3], double (&a_out)[16]) noexcept
	{
		double forward[3]{ a_direction[0], a_direction[1], a_direction[2] };
		Normalise(forward);
		// A reference that is never parallel to the look direction: y when
		// looking nearly straight down, z otherwise.
		const double reference[3] = std::abs(forward[2]) > 0.99 ? double[3]{ 0.0, 1.0, 0.0 } : double[3]{ 0.0, 0.0, 1.0 };
		double right[3]{};
		Cross(reference, forward, right);
		Normalise(right);
		double up[3]{};
		Cross(forward, right, up);

		const double half = kOcclusionDistance * 0.5;
		// Rows: x = right . (p - c) / half, y = up . (p - c) / half,
		// z = (forward . (p - c) + half) / (2 half), w = 1.
		for (int j = 0; j < 3; ++j) {
			a_out[0 * 4 + j] = right[j] / half;
			a_out[1 * 4 + j] = up[j] / half;
			a_out[2 * 4 + j] = forward[j] / kOcclusionDistance;
			a_out[3 * 4 + j] = 0.0;
		}
		a_out[0 * 4 + 3] = -Dot(right, a_centre) / half;
		a_out[1 * 4 + 3] = -Dot(up, a_centre) / half;
		a_out[2 * 4 + 3] = 0.5 - Dot(forward, a_centre) / kOcclusionDistance;
		a_out[3 * 4 + 3] = 1.0;
	}

	GridWindow WindowFor(const double (&a_eye)[3], std::int32_t (&a_previousCell)[3]) noexcept
	{
		GridWindow window{};
		for (int axis = 0; axis < 3; ++axis) {
			const double cellSize = kGridExtent[axis] / kGridSize[axis];
			const auto cell = static_cast<std::int32_t>(std::round(a_eye[axis] / cellSize));
			window.cellOrigin[axis] = cell * cellSize - a_eye[axis];
			window.validMargin[axis] = a_previousCell[axis] - cell;
			window.arrayOrigin[axis] = Wrap(cell - static_cast<std::int32_t>(kGridSize[axis] / 2), static_cast<std::int32_t>(kGridSize[axis]));
			a_previousCell[axis] = cell;
		}
		return window;
	}
}
```

Der Ausdruck `std::abs(forward[2]) > 0.99 ? double[3]{…} : double[3]{…}` ist kein gültiges C++;
schreibe ihn als

```cpp
		double reference[3]{ 0.0, 0.0, 1.0 };
		if (std::abs(forward[2]) > 0.99) {
			reference[1] = 1.0;
			reference[2] = 0.0;
		}
```

-   [ ] **Step 6: Grün sehen**

Test-Ziel bauen, `ctest -R SkylightingProjection`: 18 `ok`-Zeilen. Prüfe die Vorzeichen der
Fensterprüfung „one cell along x: the far x edge is new": `validMargin = previous − current` ist
die Konvention der Vorlage (`cellIDDiff = prevCellID - cellID`), der Shader macht daraus
`validMax = DIM − 1 + min(0, margin)`.

-   [ ] **Step 7: Drei Mutationen**

1. `SkyDirection`: `a_out[2] = +sqrt(…)` → „every direction points down" fällt.
2. `OcclusionMatrix`: `0.5 − Dot(…)` → `−Dot(…)` → „the centre lands in the middle", „the near
   face is depth 0", „the far face is depth 1" fallen.
3. `WindowFor`: `validMargin = cell − previous` → „one cell along x: the far x edge is new" und
   „a leap makes the whole grid new" fallen.

Je Mutation bauen, **Bau prüfen**, laufen lassen, Fall sehen, zurücknehmen.

-   [ ] **Step 8: Commit**

```bash
git add src/Features/Skylighting/OcclusionProjection.h src/Features/Skylighting/OcclusionProjection.cpp tests/SkylightingProjectionTests.cpp CMakeLists.txt
git commit -m "feat: the sky direction, occlusion matrix and grid window"
```

---

## Task 9: Die Shader

**Files:**

-   Create: `package/Shaders/FO4/Common/SphericalHarmonics.hlsli`,
    `package/Shaders/FO4/Common/Reconstruct.hlsli`,
    `package/Features/Skylighting/Shaders/FO4/Skylighting/SkylightingCommon.hlsli`,
    `package/Features/Skylighting/Shaders/FO4/Skylighting/UpdateProbes.hlsl`,
    `package/Features/Skylighting/Shaders/FO4/Skylighting/Skylighting.hlsl`
-   Modify: `tools/verify-package.ps1` (Basis: die zwei Common-Dateien; Addon: drei Dateien)

Die Shader werden hier geschrieben und in Task 10 übersetzt; `ShaderCompilerTests` und
`ShaderSourceTests` prüfen Compiler und Splicer, nicht diese Dateien. Ein Schreibfehler zeigt sich
beim ersten `Setup` als `[E]` mit Zeile und Spalte.

-   [ ] **Step 1: `Common/SphericalHarmonics.hlsli`**

Die geerbte Datei `package/Shaders/Common/Spherical Harmonics/SphericalHarmonics.hlsli` kopieren
und **nur** die Zeile `#include "Common/Math.hlsli"` ersetzen durch

```hlsl
// Copied from the inherited Skyrim shaders for the Fallout 4 build, which
// ships only Shaders/FO4. The one dependency, Math::PI, is inlined; the
// functions are unchanged.
namespace Math
{
	static const float PI = 3.1415926535897932384626433832795f;
	static const float HALF_PI = PI * 0.5f;
}
```

Die Lizenzdatei daneben (`LICENSE`) wird als `SphericalHarmonics.LICENSE` mitkopiert.

-   [ ] **Step 2: `Common/Reconstruct.hlsli`**

```hlsl
// A pixel's position and normal from the depth buffer and the camera's
// basis, the way F3 and F4 reconstruct it: the view ray from the pixel's
// place in the picture, its length from z = 1 - near / d. Everything is
// relative to the camera; world coordinates run to eighty thousand.
//
// The caller provides the depth texture and the constants; this only does
// the arithmetic, so that a pass in half resolution can sample the full
// resolution depth at whatever pixel it likes.
#ifndef FO4CS_RECONSTRUCT_HLSLI
#define FO4CS_RECONSTRUCT_HLSLI

namespace Reconstruct
{
	// uv in [0, 1] with y down (Fullscreen.hlsl), depth the raw buffer value.
	float3 ViewRay(float2 uv, float3 cameraForward, float3 cameraRightOverScale, float3 cameraUpOverScale)
	{
		const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
		return cameraForward + ndc.x * cameraRightOverScale + ndc.y * cameraUpOverScale;
	}

	float ViewDepth(float depth, float nearPlane)
	{
		return nearPlane / max(1.0 - depth, 1e-6);
	}

	float3 Position(float2 uv, float depth, float nearPlane, float3 cameraForward, float3 cameraRightOverScale, float3 cameraUpOverScale)
	{
		return ViewRay(uv, cameraForward, cameraRightOverScale, cameraUpOverScale) * ViewDepth(depth, nearPlane);
	}

	// The normal from the positions of this pixel and the two neighbours to
	// its right and below, turned to face the camera. One pixel wide at
	// edges; good enough for a factor on the ambient light.
	float3 Normal(float3 position, float3 positionRight, float3 positionDown)
	{
		const float3 normal = normalize(cross(positionDown - position, positionRight - position));
		// The view ray points away from the camera; a normal facing it
		// points against the ray.
		return dot(normal, position) > 0.0 ? -normal : normal;
	}
}

#endif
```

-   [ ] **Step 3: `Skylighting/SkylightingCommon.hlsli`**

Die Abtastung der Vorlage (`Skylighting.hlsli`), ohne `SharedData`, ohne Schatten-Sichtbarkeit,
mit unseren Konstanten:

```hlsl
#ifndef FO4CS_SKYLIGHTING_COMMON_HLSLI
#define FO4CS_SKYLIGHTING_COMMON_HLSLI

#include "../Common/SphericalHarmonics.hlsli"

// The template's Skylighting.hlsli, cut to what the Fallout 4 build uses:
// the grid sample and the diffuse evaluation. Constants are the same
// numbers as OcclusionProjection.h; the update shader and the pass both
// read this file so they cannot disagree.
namespace Skylighting
{
	static const uint3 ARRAY_DIM = uint3(256, 256, 128);
	static const float3 ARRAY_SIZE = 10000.0 * float3(1.0, 1.0, 0.5);
	static const float3 CELL_SIZE = ARRAY_SIZE / ARRAY_DIM;
	static const sh2 UNIT_SH = float4(sqrt(4.0 * Math::PI), 0.0, 0.0, 0.0);
	static const float EPSILON_WEIGHT_SUM = 1e-6;

	struct Settings
	{
		row_major float4x4 OcclusionViewProj;
		float4 OcclusionDir;   // xyz the direction the occlusion camera looks along, down
		float4 PosOffset;      // xyz cell origin relative to the eye
		uint4 ArrayOrigin;     // xyz ring buffer origin
		int4 ValidMargin;      // xyz previous cell - current cell
	};

	// Texture cell of a logical cell, and back.
	uint3 TextureCell(uint3 cellID, uint3 arrayOrigin)
	{
		return (cellID + arrayOrigin) % ARRAY_DIM;
	}

	uint3 LogicalCell(uint3 textureCell, uint3 arrayOrigin)
	{
		return (textureCell + ARRAY_DIM - arrayOrigin) % ARRAY_DIM;
	}

	float GetFadeOutFactor(float3 positionMS)
	{
		const float3 uvw = saturate(positionMS / ARRAY_SIZE + 0.5);
		const float3 dists = min(uvw, 1.0 - uvw);
		const float edgeDist = min(dists.x, min(dists.y, dists.z));
		return saturate(edgeDist * 20.0);
	}

	float CosineLobeVisibility(sh2 skylightingSH, float3 normal, float fadeOutFactor)
	{
		const float visibility = SphericalHarmonics::FuncProductIntegral(skylightingSH, SphericalHarmonics::EvaluateCosineLobe(normal)) / Math::PI;
		return lerp(1.0, saturate(visibility), fadeOutFactor);
	}

	// Eight cells trilinearly, each weighted by how much it lies in front
	// of the surface, after a half-cell push along the normal.
	sh2 Sample(Texture3D<sh2> probes, Settings settings, float3 positionMS, float3 normalWS)
	{
		positionMS += normalWS * CELL_SIZE * 0.5;
		const float3 adjusted = positionMS - settings.PosOffset.xyz;
		const float3 uvw = adjusted / ARRAY_SIZE + 0.5;
		if (any(uvw < 0.0) || any(uvw > 1.0))
			return UNIT_SH;
		const float3 cellCoord = uvw * ARRAY_DIM;
		const int3 cell000 = (int3)floor(cellCoord - 0.5);
		const float3 trilinear = cellCoord - 0.5 - cell000;
		sh2 sum = 0.0;
		float weightSum = 0.0;
		[unroll] for (int i = 0; i < 2; i++)
			[unroll] for (int j = 0; j < 2; j++)
				[unroll] for (int k = 0; k < 2; k++)
		{
			const int3 offset = int3(i, j, k);
			const int3 cellID = cell000 + offset;
			if (any(cellID < 0) || any((uint3)cellID >= ARRAY_DIM))
				continue;
			const float3 cellCentre = (cellID + 0.5 - ARRAY_DIM / 2) * CELL_SIZE;
			const float3 weights = 1.0 - abs(offset - trilinear);
			const float triW = weights.x * weights.y * weights.z;
			const float tangentW = dot(normalize(cellCentre - adjusted), normalWS) * 0.5 + 0.5;
			const float w = triW * tangentW;
			sum = SphericalHarmonics::Add(sum, SphericalHarmonics::Scale(probes[TextureCell((uint3)cellID, settings.ArrayOrigin.xyz)], w));
			weightSum += w;
		}
		if (weightSum <= EPSILON_WEIGHT_SUM)
			return UNIT_SH;
		return SphericalHarmonics::Scale(sum, rcp(weightSum));
	}
}

#endif
```

-   [ ] **Step 4: `Skylighting/UpdateProbes.hlsl`**

```hlsl
// One frame's sky direction into the probe grid: for every cell, was the
// cell's centre lit from that direction, as the occlusion depth map says;
// folded into the cell's SH as a running mean over the frames since the
// cell came into view. The template's UpdateProbesCS without the shadow
// cascade part. Dispatched (32, 32, 128) from kBeforeComposite.
#include "SkylightingCommon.hlsli"

Texture2D<float> OcclusionDepth : register(t0);
SamplerComparisonState Comparison : register(s0);

RWTexture3D<sh2> Probes : register(u0);
RWTexture3D<uint> AccumFrames : register(u1);

cbuffer PerFrame : register(b1)
{
	Skylighting::Settings Settings;
	float4 CameraForward;  // unused here, shared layout with the pass
	float4 CameraRight;
	float4 CameraUp;
	float4 Params;
};

static const uint kFadeInFrames = 15;

[numthreads(8, 8, 1)] void main(uint3 dtid : SV_DispatchThreadID)
{
	const uint3 cellID = Skylighting::LogicalCell(dtid, Settings.ArrayOrigin.xyz);
	const uint3 validMin = (uint3)max(int3(0, 0, 0), Settings.ValidMargin.xyz);
	const uint3 validMax = Skylighting::ARRAY_DIM - 1 + (uint3)min(int3(0, 0, 0), Settings.ValidMargin.xyz);
	const bool isValid = all(cellID >= validMin) && all(cellID <= validMax);

	float3 cellCentre = cellID + 0.5 - Skylighting::ARRAY_DIM / 2;
	cellCentre = cellCentre / Skylighting::ARRAY_DIM * Skylighting::ARRAY_SIZE + Settings.PosOffset.xyz;

	// Through the same matrix the casters were drawn with: clip x, y in
	// [-1, 1], depth in [0, 1]. The map's v runs down while clip y runs up.
	const float4 clip = mul(Settings.OcclusionViewProj, float4(cellCentre, 1.0));
	const float2 occlusionUV = float2(clip.x * 0.5 + 0.5, 0.5 - clip.y * 0.5);

	if (all(occlusionUV > 0.0) && all(occlusionUV < 1.0)) {
		const uint accumFrames = isValid ? (AccumFrames[dtid] + 1) : 1;
		// 1 where the cell centre is nearer the sky than what the casters
		// drew there: the sky is visible from this direction.
		const float visibility = OcclusionDepth.SampleCmpLevelZero(Comparison, occlusionUV, clip.z);
		sh2 occlusionSH = SphericalHarmonics::Scale(SphericalHarmonics::Evaluate(-Settings.OcclusionDir.xyz), visibility * 4.0 * Math::PI);
		if (isValid) {
			const float lerpFactor = rcp((float)accumFrames);
			sh2 previous = Skylighting::UNIT_SH;
			if (accumFrames > 1)
				previous += (Probes[dtid] - Skylighting::UNIT_SH) * kFadeInFrames / min(kFadeInFrames, accumFrames - 1);
			occlusionSH = lerp(previous, occlusionSH, lerpFactor);
		}
		occlusionSH = lerp(Skylighting::UNIT_SH, occlusionSH, min(kFadeInFrames, accumFrames) / (float)kFadeInFrames);
		Probes[dtid] = occlusionSH;
		AccumFrames[dtid] = accumFrames;
	} else if (!isValid) {
		Probes[dtid] = Skylighting::UNIT_SH;
		AccumFrames[dtid] = 0;
	}
}
```

`OcclusionDir` ist die Blickrichtung der Okklusionskamera (nach unten); die SH-Richtung ist die
Richtung **zum Himmel**, also negiert — die Vorlage speichert `OcclusionDir` schon negiert
(`OcclusionDir = -direction`), wir negieren im Shader und geben die Blickrichtung roh hinein.

-   [ ] **Step 5: `Skylighting/Skylighting.hlsl`**

```hlsl
// The sky visibility of every pixel, as a factor on the ambient light.
//
// Drawn in kComposite onto RT_028 with ZERO / SRC_COLOR, so that the
// engine's own composite carries our factor onto its ambient term. Half
// resolution: RT_028 is 1280x720, the depth is read at the matching full
// resolution pixel.
//
// The "Debug View" setting, carried in CameraUp.w, draws onto the picture
// in kAfterOpaque instead:
//   1  visibility  the factor, black to white
//   2  occlusion   this frame's depth map from the sky, laid over the screen
//   3  grid        the grid's zenith visibility at the pixel, no normal
#include "../Common/Reconstruct.hlsli"
#include "SkylightingCommon.hlsli"

Texture3D<sh2> Probes : register(t0);
Texture2D<float> DepthTexture : register(t1);
Texture2D<float> OcclusionDepth : register(t2);
SamplerState LinearClamp : register(s0);

cbuffer PerFrame : register(b1)
{
	Skylighting::Settings Settings;
	float4 CameraForward;  // xyz forward, w unused
	float4 CameraRight;    // xyz right / sx, w near plane
	float4 CameraUp;       // xyz up / sy, w debug view
	float4 Params;         // x min visibility, y unused, zw full resolution size
};

struct PixelInput
{
	float4 position: SV_POSITION;
	float2 uv: TEXCOORD0;
};

float3 PositionAt(float2 uv)
{
	const int2 pixel = int2(uv * Params.zw);
	const float depth = DepthTexture.Load(int3(pixel, 0));
	return Reconstruct::Position(uv, depth, CameraRight.w, CameraForward.xyz, CameraRight.xyz, CameraUp.xyz);
}

float4 main(PixelInput input) : SV_TARGET0
{
	const int debugView = (int)(CameraUp.w + 0.5);
	if (debugView == 2)
		return float4(OcclusionDepth.Sample(LinearClamp, input.uv).rrr, 1.0);

	const int2 pixel = int2(input.uv * Params.zw);
	const float depth = DepthTexture.Load(int3(pixel, 0));
	if (depth >= 0.9999)
		return float4(1.0, 1.0, 1.0, 1.0);

	const float2 texel = 1.0 / Params.zw;
	const float3 position = Reconstruct::Position(input.uv, depth, CameraRight.w, CameraForward.xyz, CameraRight.xyz, CameraUp.xyz);
	const float3 normal = Reconstruct::Normal(position, PositionAt(input.uv + float2(texel.x, 0.0)), PositionAt(input.uv + float2(0.0, texel.y)));

	const sh2 sh = Skylighting::Sample(Probes, Settings, position, normal);
	const float fade = Skylighting::GetFadeOutFactor(position);
	const float minVisibility = Params.x;

	if (debugView == 3) {
		const float zenith = Skylighting::CosineLobeVisibility(sh, float3(0.0, 0.0, 1.0), fade);
		return float4(zenith.xxx, 1.0);
	}

	const float visibility = Skylighting::CosineLobeVisibility(sh, normal, fade);
	const float factor = lerp(minVisibility, 1.0, visibility);
	if (debugView == 1)
		return float4(factor.xxx, 1.0);
	return float4(factor.xxx, 1.0);
}
```

-   [ ] **Step 6: `verify-package.ps1`**

Nach der `Fullscreen.hlsl`-Prüfung des Basisarchivs:

```powershell
    foreach ($file in @("Shaders/FO4/Common/SphericalHarmonics.hlsli", "Shaders/FO4/Common/Reconstruct.hlsli")) {
        Check ($baseEntries -contains $file) "and the shared shader piece $file"
    }
```

Ein Block `$sky = @($archives | Where-Object { $_.Name -like "Skylighting-*" })` mit
`Check ($sky.Count -eq 1) "exactly one Skylighting archive"`, in der `if`-Bedingung, und

```powershell
    $skyEntries = @(Get-Entries $sky[0].FullName | Sort-Object)
    Check (
        $skyEntries.Count -eq 3 -and
        $skyEntries[0] -eq "Shaders/FO4/Skylighting/Skylighting.hlsl" -and
        $skyEntries[1] -eq "Shaders/FO4/Skylighting/SkylightingCommon.hlsli" -and
        $skyEntries[2] -eq "Shaders/FO4/Skylighting/UpdateProbes.hlsl"
    ) "the skylighting addon carries its three shader files and nothing else"
    Check (
        -not ($baseEntries -contains "Shaders/FO4/Skylighting/Skylighting.hlsl")
    ) "and the base does not carry them"
```

`$union` um `$skyEntries` ergänzen.

-   [ ] **Step 7: Commit**

```bash
git add package/Shaders/FO4/Common package/Features/Skylighting tools/verify-package.ps1
git commit -m "feat: the skylighting shaders and the shared pieces"
```

---

## Task 10: Das Feature `Skylighting`

**Files:**

-   Create: `src/Features/Skylighting.h`, `src/Features/Skylighting.cpp`
-   Modify: `src/Feature/FeatureSystem.cpp` (vor `CloudShadows`)

**Interfaces:**

-   Consumes: Tasks 1, 2, 6, 7, 8, 9; `Render::FogCameraFromMatrix`, `Render::IsPlausibleViewProjection`,
    `RE::Main::WorldRootCamera`, `RE::Sky::GetSingleton`, `Shader::CompileComputeShader`,
    `Shader::CompilePixelShader`, `Render::PassScope`.

-   [ ] **Step 1: Die vier Konstanten aus Lauf 1**

Oben in `Skylighting.cpp`, mit Kommentar auf das Datum des Laufs und die Logzeile:

```cpp
		/// From the probe run of 2026-09-13: the vertex constant buffer the
		/// shadow casters take their view projection from, and where in it
		/// the 4x4 with the last row 0 0 0 1 sits.
		constexpr std::uint32_t kViewConstantSlot = <aus Lauf 1>;
		constexpr std::size_t kViewProjectionOffset = <aus Lauf 1>;
		/// Whether the casters' positions are relative to the camera (the
		/// probe saw a small translation) or absolute (eighty thousand).
		constexpr bool kGeometryIsCameraRelative = <aus Lauf 1>;
		/// The channel(s) of RT_028 the composite reads the ambient occlusion
		/// from, as the target view showed.
		constexpr std::uint8_t kAmbientWriteMask = <aus Lauf 1: D3D11_COLOR_WRITE_ENABLE_RED, _ALL, …>;
```

Die spitzen Klammern werden mit den Zahlen aus `f5-probe.md` gefüllt, bevor irgend etwas gebaut
wird. Fehlt eine, ist Task 5 nicht fertig.

-   [ ] **Step 2: Header**

```cpp
#pragma once

#include "Feature/Feature.h"
#include "Render/DepthTarget.h"
#include "Render/DrawObserver.h"
#include "Render/PhaseDispatcher.h"
#include "Render/Resources.h"
#include "Util/FileWatch.h"

#include <REX/W32/D3D11.h>

#include <cstdint>
#include <vector>

namespace Features
{
	/// Sky visibility from the scene the engine draws, as a factor on the
	/// ambient light.
	///
	/// Three stages a frame. Capture: every shadow caster draw of
	/// BSUtilityShader is repeated into a depth target of our own, with the
	/// sun's view projection swapped for an orthographic one looking down
	/// from this frame's sky direction. Grid: a compute shader folds that
	/// map into a 3D grid of SH2 probes around the camera, a running mean
	/// over the frames. Pass: in kComposite the factor is multiplied into
	/// RT_028, which the composite applies to its ambient term.
	class Skylighting final : public Feature, public Render::DrawObserver
	{
	public:
		[[nodiscard]] std::string_view Name() const override { return "Skylighting"; }
		void Declare() override;
		[[nodiscard]] bool Setup() override;
		void Frame() override;
		void Shutdown() override;

		[[nodiscard]] bool Wants(const Render::CurrentTechnique& a_current) const noexcept override;
		void BeforeDraw(REX::W32::ID3D11DeviceContext& a_context, const Render::DrawCall& a_call) noexcept override;
		void AfterDraw(REX::W32::ID3D11DeviceContext& a_context, const Render::DrawCall& a_call) noexcept override;

	private:
		static constexpr std::uint32_t kSavedTargets = 8;

		void UpdateProbes();
		void Draw();
		void DrawDebug();
		[[nodiscard]] bool Compile();
		[[nodiscard]] bool EnsureStates();
		[[nodiscard]] bool BeginFrameCapture(REX::W32::ID3D11DeviceContext& a_context) noexcept;
		[[nodiscard]] bool FillConstants(bool a_forPass) noexcept;
		void ResetGrid() noexcept;
		void ReleaseSaved() noexcept;

		Render::TechniqueFilter _casters{};
		Render::DepthTarget _occlusion;
		Render::Texture3D _probes;
		Render::Texture3D _accumFrames;
		Render::ConstantBuffer _constants;
		REX::W32::ID3D11ComputeShader* _update{ nullptr };
		REX::W32::ID3D11PixelShader* _shader{ nullptr };
		REX::W32::ID3D11SamplerState* _comparison{ nullptr };
		REX::W32::ID3D11SamplerState* _linearClamp{ nullptr };
		REX::W32::ID3D11BlendState* _multiply{ nullptr };
		REX::W32::ID3D11BlendState* _plain{ nullptr };
		Render::PhaseDispatcher::Token _probePhase{ Render::PhaseDispatcher::kNoToken };
		Render::PhaseDispatcher::Token _compositePhase{ Render::PhaseDispatcher::kNoToken };
		Render::PhaseDispatcher::Token _debugPhase{ Render::PhaseDispatcher::kNoToken };
		Util::FileWatch _watch;

		// The capture of the current frame.
		REX::W32::ID3D11Buffer* _viewConstants{ nullptr };  // our copy of the casters' view buffer
		std::vector<std::uint8_t> _viewImage;
		std::uint64_t _captureFrame{ 0 };
		bool _captureReady{ false };
		bool _repeating{ false };
		bool _surrounding{ false };
		double _direction[3]{};
		double _matrix[16]{};
		float _eye[3]{};

		// What BeforeDraw put aside for AfterDraw.
		REX::W32::ID3D11RenderTargetView* _savedTargets[kSavedTargets]{};
		REX::W32::ID3D11DepthStencilView* _savedDepth{ nullptr };
		REX::W32::ID3D11Buffer* _savedView{ nullptr };
		REX::W32::D3D11_VIEWPORT _savedViewports[16]{};
		std::uint32_t _savedViewportCount{ 0 };

		// The grid window.
		std::int32_t _previousCell[3]{};
		bool _gridValid{ false };
		bool _wasOutdoors{ false };

		// Counters for the once-a-second line.
		std::uint64_t _frames{ 0 };
		std::uint64_t _castersSeen{ 0 };
		std::uint64_t _castersRepeated{ 0 };
		std::uint64_t _dispatches{ 0 };
		std::uint64_t _passes{ 0 };
		bool _reportedIndoors{ false };
	};
}
```

-   [ ] **Step 3: `Declare`**

```cpp
	void Skylighting::Declare()
	{
		Settings::DeclareFeature("Skylighting", true)
			.Label("feature.skylighting.name", "Skylighting")
			.Help(
				"feature.skylighting.help",
				"Darkens the ambient light where the sky is hidden - under overhangs, in "
				"trenches, beneath trees. Taken from the shadow casters the game draws.");
		Settings::DeclareSlider("Skylighting/maxZenith", 90.0, 30.0, 90.0)
			.Label("feature.skylighting.max_zenith", "Max Zenith")
			.Help(
				"feature.skylighting.max_zenith_help",
				"How far from straight up the sky is sampled, in degrees. Lower keeps light out "
				"of narrow gaps.");
		Settings::DeclareSlider("Skylighting/minVisibility", 0.1, 0.0, 1.0)
			.Label("feature.skylighting.min_visibility", "Min Visibility")
			.Help(
				"feature.skylighting.min_visibility_help",
				"How dark a fully hidden spot gets. 0 is black, 1 switches the effect off.");
		Settings::DeclareChoice(
			"Skylighting/debugView",
			"off",
			std::vector<std::string>{ "off", "visibility", "occlusion", "grid" })
			.Label("feature.skylighting.debug_view", "Debug View")
			.Help(
				"feature.skylighting.debug_view_help",
				"Shows the pass instead of the picture: the factor, this frame's depth map "
				"from the sky, or the grid as seen from straight above.");
	}
```

-   [ ] **Step 4: `Setup`, `Frame`, `Shutdown`**

`Setup`: Zähler nullen; `ClassIndexOf("BSUtilityShader")` → `_casters = { index, kAnyTechnique }`;
`InitFullscreenPass`; `Compile` (beide Shader); `EnsureStates` (Vergleichssampler
`D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR`, `LESS_EQUAL`, Clamp; `_linearClamp` wie F4;
`_multiply` wie F4 mit `renderTargetWriteMask = kAmbientWriteMask`; `_plain`);
`_constants.Create(sizeof(Constants), "FO4CS_CB_Skylighting")` mit

```cpp
		struct alignas(16) Constants
		{
			float occlusionViewProj[16];
			float occlusionDir[4];
			float posOffset[4];
			std::uint32_t arrayOrigin[4];
			std::int32_t validMargin[4];
			float cameraForward[4];
			float cameraRight[4];
			float cameraUp[4];
			float params[4];
		};
		static_assert(sizeof(Constants) == 64 + 8 * 16);
```

`_occlusion.Create(kOcclusionMapSize, "FO4CS_DS_SkyOcclusion")`,
`_probes.Create(256, 256, 128, DXGI_FORMAT_R16G16B16A16_FLOAT, "FO4CS_TEX3D_SkyProbes")`,
`_accumFrames.Create(256, 256, 128, DXGI_FORMAT_R8_UINT, "FO4CS_TEX3D_SkyAccum")`, `ResetGrid()`;
drei Phasen: `kBeforeComposite` → `"Skylighting/Probes"` → `UpdateProbes()`, `kComposite` →
`"Skylighting/Draw"` → `Draw()`, `kAfterOpaque` → `"Skylighting/Debug"` → `DrawDebug()`; jeder
`kNoToken` ist ein Fehler mit Grund. `AddDrawObserver(this)`. Log:
`"Skylighting: repeating BSUtilityShader casters into a {} depth map, grid 256x256x128"`.

`Frame`: Wächter → `Compile`; Innen/Außen: `outdoors = sky && mode == kFull`; beim Wechsel nach
draußen `ResetGrid()`, beim Wechsel nach drinnen einmal `"Skylighting: indoors, standing down"`;
je 180 Frames die Sekundenzeile:

```cpp
			REX::INFO(
				"skylighting: {} caster(s) seen, {} repeated, {} dispatch(es), {} pass(es) in {} frames, direction [{:.3f} {:.3f} {:.3f}], cell [{} {} {}], margin [{} {} {}], composite technique 0x{:04X}",
				…, Render::FramePhaseLastTechnique(Render::Phase::kComposite));
```

`Shutdown`: `RemoveDrawObserver(this)` zuerst, dann die drei Phasen austragen, `ReleaseSaved`,
Shader, States, Sampler, `_viewConstants`, Ziele und Gitter (`Release`). `ResetGrid`:
`_accumFrames.ClearUint(context, 0)`, `_gridValid = false`, `_previousCell` auf die aktuelle Zelle.

-   [ ] **Step 5: Die Erfassung**

```cpp
	bool Skylighting::Wants(const Render::CurrentTechnique& a_current) const noexcept
	{
		return !_repeating && _casters.Matches(a_current);
	}

	bool Skylighting::BeginFrameCapture(REX::W32::ID3D11DeviceContext& a_context) noexcept
	{
		// Once a frame, at the first caster: this frame's direction, the
		// matrix, our copy of the view constants, a clean depth map.
		const auto frame = Render::FrameCount();
		if (_captureFrame == frame) {
			return _captureReady;
		}
		_captureFrame = frame;
		_captureReady = false;

		const auto* const sky = RE::Sky::GetSingleton();
		if (sky == nullptr || sky->mode.get() != RE::Sky::Mode::kFull) {
			return false;
		}
		auto* const world = RE::Main::WorldRootCamera();
		if (world == nullptr) {
			return false;
		}
		const auto& eye = world->GetWorldTransform().translate;
		_eye[0] = eye.x;
		_eye[1] = eye.y;
		_eye[2] = eye.z;

		const auto maxZenith = Settings::GetDouble("Skylighting/maxZenith") * 3.14159265358979323846 / 180.0;
		Skylighting::SkyDirection(static_cast<std::uint32_t>(frame), 0.37, maxZenith, _direction);
		const double centre[3]{
			kGeometryIsCameraRelative ? 0.0 : static_cast<double>(eye.x),
			kGeometryIsCameraRelative ? 0.0 : static_cast<double>(eye.y),
			kGeometryIsCameraRelative ? 0.0 : static_cast<double>(eye.z) };
		Skylighting::OcclusionMatrix(centre, _direction, _matrix);

		// The engine's buffer as it is, with our matrix written over the
		// view projection. Copied through a staging buffer? No: only the
		// matrix changes, and the rest of the buffer is read once from a
		// blocking map the first time - it is per view, not per object.
		REX::W32::ID3D11Buffer* engine = nullptr;
		a_context.VSGetConstantBuffers(kViewConstantSlot, 1, std::addressof(engine));
		if (engine == nullptr) {
			return false;
		}
		if (!EnsureViewCopy(a_context, engine)) {  // legt _viewConstants in der Größe des Engine-Puffers an, liest ihn einmal blockierend in _viewImage
			engine->Release();
			return false;
		}
		engine->Release();

		float rows[16]{};
		for (int i = 0; i < 16; ++i) {
			rows[i] = static_cast<float>(_matrix[i]);
		}
		std::memcpy(_viewImage.data() + kViewProjectionOffset, rows, sizeof(rows));
		a_context.UpdateSubresource(_viewConstants, 0, nullptr, _viewImage.data(), 0, 0);
		_occlusion.Clear(a_context, 1.0f);
		_captureReady = true;
		return true;
	}
```

**Zum blockierenden Lesen:** der Sicht-Puffer der Schattenkarte ändert sich je Frame (Sonne
wandert), aber nur seine Matrix — und die ersetzen wir. Die übrigen Bytes werden einmal beim
ersten Treffer gelesen (`Map` ohne `DO_NOT_WAIT`, ein einmaliger Stall) und danach behalten.
Zeigt Lauf 1, daß sich außer der Matrix weitere Bytes je Sekunde ändern, kommt hier der
Staging-Ring von sechs aus F4 hin; der Plan geht vom einfachen Fall aus.

```cpp
	void Skylighting::BeforeDraw(REX::W32::ID3D11DeviceContext& a_context, const Render::DrawCall&) noexcept
	{
		_surrounding = false;
		REX::W32::ID3D11DepthStencilView* dsv = nullptr;
		a_context.OMGetRenderTargets(kSavedTargets, _savedTargets, std::addressof(dsv));
		const bool shadowCaster = dsv != nullptr && dsv == Render::Targets::DepthStencilView(Render::Targets::kShadowMap);
		if (!shadowCaster || !BeginFrameCapture(a_context)) {
			if (dsv != nullptr) {
				dsv->Release();
			}
			ReleaseSaved();
			return;
		}
		++_castersSeen;
		_savedDepth = dsv;
		a_context.VSGetConstantBuffers(kViewConstantSlot, 1, std::addressof(_savedView));
		_savedViewportCount = 16;
		a_context.RSGetViewports(std::addressof(_savedViewportCount), _savedViewports);
		_surrounding = true;
	}

	void Skylighting::AfterDraw(REX::W32::ID3D11DeviceContext& a_context, const Render::DrawCall& a_call) noexcept
	{
		if (!_surrounding) {
			return;
		}
		_surrounding = false;

		// Our depth map, no colour target, our view constants; the engine's
		// rasterizer stays - the casters carry their own depth bias.
		const REX::W32::D3D11_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(_occlusion.Size()), static_cast<float>(_occlusion.Size()), 0.0f, 1.0f };
		auto* const dsv = _occlusion.DSV();
		a_context.OMSetRenderTargets(0, nullptr, dsv);
		a_context.RSSetViewports(1, std::addressof(viewport));
		a_context.VSSetConstantBuffers(kViewConstantSlot, 1, std::addressof(_viewConstants));

		_repeating = true;
		a_call.Repeat(a_context);
		_repeating = false;
		++_castersRepeated;

		a_context.VSSetConstantBuffers(kViewConstantSlot, 1, std::addressof(_savedView));
		a_context.OMSetRenderTargets(kSavedTargets, _savedTargets, _savedDepth);
		a_context.RSSetViewports(_savedViewportCount, _savedViewports);
		ReleaseSaved();
	}
```

Die Wiederholung läuft **nach** dem Zug der Engine (in `AfterDraw`), damit der Zug der Engine
unberührt bleibt; F4 tat es ebenso.

-   [ ] **Step 6: Gitter und Pass**

```cpp
	bool Skylighting::FillConstants(bool a_forPass) noexcept
	{
		auto* const world = RE::Main::WorldRootCamera();
		if (world == nullptr) {
			return false;
		}
		float matrix[16]{};
		std::memcpy(matrix, world->worldToCam, sizeof(matrix));
		if (!Render::IsPlausibleViewProjection(matrix)) {
			return false;
		}
		const auto camera = Render::FogCameraFromMatrix(world->worldToCam, world->GetWorldTransform().translate.z);

		const double eye[3]{ _eye[0], _eye[1], _eye[2] };
		const auto window = Skylighting::WindowFor(eye, _previousCell);

		Constants data{};
		for (int i = 0; i < 16; ++i) {
			data.occlusionViewProj[i] = static_cast<float>(_matrix[i]);
		}
		for (int i = 0; i < 3; ++i) {
			data.occlusionDir[i] = static_cast<float>(_direction[i]);
			data.posOffset[i] = static_cast<float>(window.cellOrigin[i]);
			data.arrayOrigin[i] = static_cast<std::uint32_t>(window.arrayOrigin[i]);
			data.validMargin[i] = _gridValid ? window.validMargin[i] : 1000;  // alles neu nach einem Reset
		}
		_gridValid = true;
		std::memcpy(data.cameraForward, camera.forward, sizeof(camera.forward));
		std::memcpy(data.cameraRight, camera.rightOverScaleX, sizeof(camera.rightOverScaleX));
		data.cameraRight[3] = camera.near;
		std::memcpy(data.cameraUp, camera.upOverScaleY, sizeof(camera.upOverScaleY));
		data.cameraUp[3] = a_forPass ? DebugViewIndex(Settings::GetString("Skylighting/debugView")) : 0.0f;
		data.params[0] = static_cast<float>(Settings::GetDouble("Skylighting/minVisibility"));
		// Full resolution from the HDR target's description; the depth buffer
		// has the same size, and the pass runs at RT_028's half of it.
		auto* const sceneTexture = Render::Targets::RenderTargetTexture(Render::Targets::kSceneHDR);
		if (sceneTexture == nullptr) {
			return false;
		}
		REX::W32::D3D11_TEXTURE2D_DESC sceneDesc{};
		sceneTexture->GetDesc(std::addressof(sceneDesc));
		data.params[2] = static_cast<float>(sceneDesc.width);
		data.params[3] = static_cast<float>(sceneDesc.height);
		return _constants.Update(std::addressof(data), sizeof(data));
	}
```

`DebugViewIndex` ist die Funktion aus `CloudShadows.cpp`, hier mit `off 0, visibility 1,
occlusion 2, grid 3`. `EnsureViewCopy`:

```cpp
	bool Skylighting::EnsureViewCopy(REX::W32::ID3D11DeviceContext& a_context, REX::W32::ID3D11Buffer* a_engine) noexcept
	{
		REX::W32::D3D11_BUFFER_DESC desc{};
		a_engine->GetDesc(std::addressof(desc));
		if (desc.byteWidth < kViewProjectionOffset + 64) {
			if (!_reportedSmallBuffer) {
				REX::ERROR("Skylighting: the view constants are {} bytes, the matrix would sit at {}", desc.byteWidth, kViewProjectionOffset);
				_reportedSmallBuffer = true;
			}
			return false;
		}
		if (_viewConstants != nullptr && _viewImage.size() == desc.byteWidth) {
			return true;
		}
		// Once: the engine's bytes, read through a staging buffer with a
		// blocking map. A stall of one frame at the first caster of a
		// session; everything after it is our own copy.
		auto* const device = Render::GetDevice();
		REX::W32::D3D11_BUFFER_DESC staging = desc;
		staging.usage = REX::W32::D3D11_USAGE_STAGING;
		staging.bindFlags = 0;
		staging.cpuAccessFlags = REX::W32::D3D11_CPU_ACCESS_READ;
		REX::W32::ID3D11Buffer* copy = nullptr;
		if (device == nullptr || device->CreateBuffer(std::addressof(staging), nullptr, std::addressof(copy)) < 0) {
			return false;
		}
		a_context.CopyResource(copy, a_engine);
		REX::W32::D3D11_MAPPED_SUBRESOURCE mapped{};
		const bool read = a_context.Map(copy, 0, REX::W32::D3D11_MAP_READ, 0, std::addressof(mapped)) >= 0;
		if (read) {
			_viewImage.assign(static_cast<const std::uint8_t*>(mapped.data), static_cast<const std::uint8_t*>(mapped.data) + desc.byteWidth);
			a_context.Unmap(copy, 0);
		}
		copy->Release();
		if (!read) {
			return false;
		}
		REX::W32::D3D11_BUFFER_DESC ours = desc;
		ours.usage = REX::W32::D3D11_USAGE_DEFAULT;
		ours.bindFlags = REX::W32::D3D11_BIND_CONSTANT_BUFFER;
		ours.cpuAccessFlags = 0;
		if (_viewConstants != nullptr) {
			_viewConstants->Release();
			_viewConstants = nullptr;
		}
		if (device->CreateBuffer(std::addressof(ours), nullptr, std::addressof(_viewConstants)) < 0) {
			REX::ERROR("Skylighting: our copy of the view constants could not be created");
			return false;
		}
		REX::INFO("Skylighting: view constants are {} bytes in slot {}, matrix at {}", desc.byteWidth, kViewConstantSlot, kViewProjectionOffset);
		return true;
	}
```

(`_reportedSmallBuffer` als weiteres `bool` im Header; `EnsureViewCopy` als private Methode.)

Wenn der Kameramatrix-Anteil in der Okklusionsmatrix kamerarelativ ist (`kGeometryIsCameraRelative`),
gilt für den Compute-Shader dasselbe: `cellCentre` ist relativ zur Kamera und
`OcclusionViewProj` erwartet kamerarelative Punkte — konsistent. Ist die Geometrie absolut,
addiert `FillConstants` die Kameraposition auf `posOffset` (und die Matrix trägt sie ohnehin).

`UpdateProbes` (in `kBeforeComposite`): nur wenn `_captureReady` für dieses Frame; `FillConstants(false)`;
`CSSetShader(_update)`, SRV 0 = `_occlusion.SRV()`, UAV 0/1 = Gitter, Sampler 0 = `_comparison`,
CB 1; `Dispatch(32, 32, 128)`; alles lösen (`CSSetShaderResources(0, 1, null)`,
`CSSetUnorderedAccessViews(0, 2, nulls, nullptr)`, `CSSetShader(nullptr)`); `++_dispatches`. Ein
`StateGuard` ist hier nicht nötig — Compute-Bindungen setzt die Engine selbst neu; F2 verfährt so.

`Draw` (in `kComposite`): nur wenn `debugView == off`, `_gridValid`, draußen. `FillConstants(true)`;
`StateGuard`; RTV = `Targets::RenderTargetView(28)`; Viewport aus dessen `GetDesc` (1280×720);
Blend `_multiply`; PS-SRVs 0..2 = Gitter, `DepthSRV(kSceneDepth)`, `_occlusion.SRV()`; Sampler 0;
CB 1; `_shader`; `DrawFullscreen`; `++_passes`. Der `StateGuard` sichert die Viewports (alle 16) und setzt sie zurück, also darf der Pass seinen 1280×720-Viewport frei setzen.

`DrawDebug` (in `kAfterOpaque`): nur wenn `debugView != off`; wie `Draw`, aber RTV = `kSceneHDR`,
Blend `_plain`, Viewport voll.

-   [ ] **Step 7: Registrieren, bauen, testen, Paket**

`FeatureSystem.cpp`, vor `CloudShadows`:

```cpp
			// Watches the shadow casters through the draw hook and multiplies
			// into RT_028 right before the composite. Ahead of the cloud
			// shadows so that the two observers read in the order the frame
			// draws them: shadow map first, sky later.
			TheRegistry().Register(std::make_unique<Skylighting>());
```

```pwsh
python tools/extract-i18n.py --write
& "<cmake>\cmake.exe" --build --preset FO4
& "<cmake>\ctest.exe" --test-dir build/FO4 -C Release
& "<cmake>\cmake.exe" --build --preset FO4 --target package
pwsh tools/verify-package.ps1
pwsh tools/verify-plugin.ps1
```

-   [ ] **Step 8: Commit**

```bash
git add src/Features/Skylighting.h src/Features/Skylighting.cpp src/Feature/FeatureSystem.cpp package/F4SE/Plugins/CommunityShadersFO4/Translations/en.json
git commit -m "feat: skylighting from the engine's shadow casters"
```

---

## Task 11: Lauf 2 — Abnahme

Kein Code vor dem Lauf. Prüfliste aus Spec 10.3, an den Nutzer:

1. Overlay: Seite `Skylighting` mit Schalter, `Max Zenith`, `Min Visibility`, `Debug View`.
2. `coc RedRocketExt`, unter das Vordach, Blick auf den Boden: Schalter aus und an. Kein
   Unterschied: Gitter leer oder `RT_028` nicht der Faktor — dann die Sekundenzeile lesen.
3. `Debug View` `occlusion`: eine Höhenkarte, die je Frame die Richtung wechselt. Schwarz/weiß:
   Erfassung leer oder Tiefe verkehrt.
4. `grid`: hell im Freien, dunkel unter dem Dach, weich. Kacheln: Fensterrechnung.
5. `visibility`: wie `grid`, Wände zur offenen Seite hell. Wie `grid`: Normale bleibt Zenit.
6. 200 m laufen: das Gitter baut nach, ohne Sprung in der Mitte.
7. `Max Zenith` 30: flacher. `Min Visibility` 0 und 1 (1 gleicht „aus").
8. Tafel: `Skylighting/Probes`, `Skylighting/Draw`; **F11 an, dann ohne Bewegung aus, F11**.
9. Root Cellar rein: „standing down"; raus: Gitter kommt neu.
10. Pip-Boy, Alt-Tab, Feature aus und an.
11. Hot-Reload: **ich frage vorher**, dann berühre ich `Skylighting.hlsl` und `UpdateProbes.hlsl`.

Log: keine `[W]`/`[E]`; `composite technique 0x0040` in jeder Sekundenzeile; Wiederholungen ≈
Schattenwerfer je Frame; Kosten aus der Differenz der beiden Frame-Zeilen.

Fällt ein Schritt: systematic-debugging, ein Lauf je Hypothese.

---

## Task 12: Roadmap, Erinnerungen, Abschluß

-   [ ] **Step 1: Roadmap**

Statuszeile (F5 abgeschlossen, als Nächstes F6), Tabelle, Abschnitt „Aus Teilprojekt F5 bestätigt"
nach F4.5: die Zahlen (beide Schnappschüsse, Draw-Hook-Anteil), der Sondenbefund (Slot, Offset,
Translationskonvention, `RT_028`, Regen), der gewählte Weg und die Gabelungen (welche eintraten),
die Abnahme mit dem Ungeprüften, Zugeständnisse (Alter, spekular, Speicher, Schattenwerfer der
Sonne), was F6 aufwärts erbt. `frame-order.md`: die Phase `kComposite` und der Platz der
SSAO-Pässe, falls die Sonde ihn zeigte. `render-targets.md`: `RT_028` benannt.

-   [ ] **Step 2: Commit**

```bash
git add docs/fallout4-port/ROADMAP.md docs/fallout4-port/frame-order.md docs/fallout4-port/render-targets.md
git commit -m "docs: record what F5 confirmed"
```

-   [ ] **Step 3: Erinnerungen**

`fallout4-engine-facts.md`: Utility-Konstantenpuffer, `RT_028`, zwei Anker auf einer Klasse.
`fallout4-port-roadmap.md`: F5 fertig, F6 als Nächstes, was F6 bis F11 erben (`kComposite`,
`DepthTarget`, `Texture3D`, `TargetView`, `Reconstruct.hlsli`). Eine `fallout4-f5-stand.md` nur,
falls etwas offen bleibt.

-   [ ] **Step 4: Branch abschließen**

finishing-a-development-branch: Tests, Paket, Prüfungen, Menü per AskUserQuestion, Fast-Forward
nach `dev` und Push nur auf Ansage.

---

## Reihenfolge

```
1 Beobachterliste ─┐
2 Targets ─────────┼─► 3 TargetView ─► 4 Sonde ─► 5 Lauf 1 ─┐
                   │                                          ├─► 10 Feature ─► 11 Lauf 2 ─► 12 Abschluß
6 Phase ───────────┤                                          │
7 Ziele ───────────┼─► 9 Shader ──────────────────────────────┘
8 Projektion ──────┘
```

Tasks 6 bis 9 hängen nicht an Lauf 1 und werden gebaut, während der Nutzer ihn macht. Zwei
Spielstarts.
