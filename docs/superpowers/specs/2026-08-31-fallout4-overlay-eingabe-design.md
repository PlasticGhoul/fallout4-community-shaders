# Teilprojekt E1 — Overlay und Eingabe

Spec, Stand 2026-08-31. Teil der Portierung nach `docs/fallout4-port/ROADMAP.md`.
Setzt A, B1, B2, C, D1 und D2 voraus, insbesondere
`2026-08-30-fallout4-feature-framework-design.md`.

## 1. Kontext und Ziel

D1 hat Features an- und abschaltbar gemacht, D2 hat sie auslieferbar gemacht. Bedient werden sie
bis heute, indem der Nutzer eine JSON-Datei von Hand bearbeitet und Alt-Tab drückt. Es gibt nichts
auf dem Schirm.

**Ziel:** Ein ImGui-Overlay, das sich per Taste öffnet, mit der Maus bedienen lässt, und das die
Spieleingabe anhält, solange es offen ist.

**Abnahmekriterium der Roadmap für E:** Overlay im Spiel bedienbar, Einstellungen überleben einen
Neustart. E1 deckt die erste Hälfte; die zweite gehört E2.

### Warum E geteilt wurde

E bündelte fünf Dinge: Overlay-Rendering, Eingabe, Einstellungsoberfläche, Themes und Schriften,
und i18n. Die ersten beiden bilden ein Subsystem — „können wir zeichnen und Eingaben nehmen" — und
tragen sämtliche riskanten Unbekannten. Die übrigen drei setzen darauf auf und beantworten eine
andere Frage: „was zeigen wir". E wurde deshalb am 2026-08-31 in **E1** (diese Spec) und **E2**
(Einstellungsoberfläche, Themes, Schriften, i18n) geteilt.

### Was die Vorerkundung ergeben hat

| Sachverhalt               | Befund                                                                                                                                                                                                                              |
| ------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ImGui                     | `1.92.6` in der Baseline, mit den Features `dx11-binding` und `win32-binding`                                                                                                                                                       |
| `REX::W32` USER32         | dünn: `GetClientRect`, `GetKeyNameText*`, `GetKeyState`, `GetWindowRect`, `MessageBox*`, `SetWindowLongPtrA`, `ShowCursor`. **Kein `CallWindowProc`, kein `SetWindowLongPtrW`**                                                     |
| Fenstermeldungen allein   | reichen **nicht**: Fallout 4 liest Tastatur und Maus über sein eigenes Eingabesystem, nicht über `WM_`-Meldungen                                                                                                                    |
| Engine-Weg                | `RE::BSInputEnableManager` mit `AllocateNewLayer`, `EnableUserEvent`, dazu `BSInputEnableLayer::DecRef`. `USER_EVENT_FLAG::kAll` schaltet alles ab                                                                                  |
| Adressbibliothek 1.11.240 | Format V0, 652.306 Einträge. Die drei auf AE aufgelösten IDs sind **vorhanden**: `2268244` (`0x16688C0`), `2268263` (`0x166A620`), `2268272` (`0x166AD20`)                                                                          |
| Auffüllregel              | `VariantID{og, ng}` füllt den AE-Slot mit dem NG-Wert (`while (i < COMMONLIB_RUNTIMECOUNT) m_offs[i++] = lastValue`). Beim Singleton fehlt der NG-Wert `2689007` in der Datenbank, der eigens eingetragene AE-Wert `4796297` ist da |
| Fensterhandle             | `IDXGISwapChain::GetDesc().outputWindow`                                                                                                                                                                                            |
| `Features::Settings`      | kennt heute nur `<Name>/enabled` als `bool`                                                                                                                                                                                         |

## 2. Umfang

### In E1 enthalten

-   `imgui` als Abhängigkeit, mit `dx11-binding` und `win32-binding`.
-   `src/Menu/` mit dem Overlay-System: Aufsetzen, Zeichnen, Abbauen.
-   Verkettung der Fensterprozedur, samt eigener Deklaration von `CallWindowProcW` und
    `SetWindowLongPtrW`.
-   Anhalten der Spieleingabe über eine `BSInputEnableManager`-Schicht.
-   Die Umschalttaste, Standard `VK_END`, konfigurierbar über die Einstellungsdatei.
-   **Ein** Fenster auf dem Schirm, das Zustand zeigt und einen Knopf hat — mehr nicht.
-   Eine Erweiterung von `Features::Settings` um Einstellungen, die keine Feature-Schalter sind.

### Nicht in E1 enthalten

-   Die eigentliche Einstellungsoberfläche, die Featureliste, das Schreiben von Einstellungen
    (**E2**).
-   Themes, Schriften, i18n (**E2**). E1 nimmt ImGuis eingebaute Schrift.
-   Docking, mehrere Viewports, Gamepad-Bedienung.
-   Änderungen an der Paketierung. Schriften und Themes werden nicht ausgeliefert, weil E1 keine
    braucht.

## 3. Vorentscheidungen

| Frage                                | Entscheidung                                         | Begründung                                                                                                                                                                                 |
| ------------------------------------ | ---------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Wo das Menü lebt                     | eigenes System `src/Menu/`, **kein** Feature         | Es ist das Ding, das Features steuert. Als Feature wäre es das einzige, das man über die JSON abschalten müsste, die es bedienen soll, und jedes Umschalten risse ImGuis D3D-Ressourcen ab |
| Eingabe an ImGui                     | verkettete Fensterprozedur, ImGuis Win32-Backend     | Der Backend existiert genau dafür. Eigene Übersetzung von Tasten, Rad und Textzeichen wäre viel Code mit vielen stillen Fehlern                                                            |
| Eingabe **weg vom Spiel**            | `BSInputEnableManager`-Schicht                       | Fenstermeldungen zu schlucken hilft nicht, weil das Spiel gar nicht daraus liest. Das ist der Weg, den die Engine für ihre eigenen Menüs nimmt                                             |
| `SetWindowLongPtr`                   | die **W**-Fassung, selbst deklariert                 | REX deklariert nur `A`. Die auf einem Unicode-Fenster zu benutzen stellt die Meldungsübersetzung auf ANSI um — für E1 folgenlos, für E2 mit Texteingabe nicht                              |
| Umschalttaste                        | `VK_END`, über die Einstellungen änderbar            | Unter FO4-Mods verbreitet und im Spiel unbelegt. Änderbar, weil eine Kollision sonst einen neuen Bau bräuchte                                                                              |
| Mauszeiger                           | ImGui zeichnet seinen eigenen                        | `io.MouseDrawCursor` vermeidet den Streit mit dem Spiel um den Zustand des Systemzeigers                                                                                                   |
| Aufsetzen                            | verzögert, beim ersten `Frame` auf dem Render-Thread | Gerät, Kontext und Fenster sind erst dann sicher da, und alles D3D bleibt auf einem Thread — dieselbe Regel wie in C                                                                       |
| Verworfen: Menü als Feature          | —                                                    | siehe oben                                                                                                                                                                                 |
| Verworfen: eigene Eingabeübersetzung | —                                                    | siehe oben                                                                                                                                                                                 |

## 4. Architektur

### 4.1 `Menu::System`

```cpp
namespace Menu
{
	/// Once per Present, on the render thread. Sets itself up on the first
	/// call and draws from then on.
	void Tick() noexcept;

	/// True while the overlay is open. The window procedure asks, so that it
	/// knows whether to keep a message or pass it on.
	[[nodiscard]] bool IsOpen() noexcept;
}
```

Gerufen aus `Render::Present`, unmittelbar **nach** `Features::TickSystem()` — das Overlay gehört
zuoberst, und die Features sollen ihren Frame vorher gehabt haben.

Das Aufsetzen beim ersten `Tick` beschafft Gerät, Kontext und Fensterhandle, richtet den ImGui-
Kontext ein, ruft `ImGui_ImplWin32_Init` und `ImGui_ImplDX11_Init` und verkettet die
Fensterprozedur. Scheitert eines davon, wird einmal protokolliert und nie wieder versucht — ein
Overlay, das sich nicht aufsetzen ließ, wird auch beim tausendsten Frame nicht besser.

### 4.2 Das Renderziel

ImGui zeichnet in das, was gerade gebunden ist, und was das zum Zeitpunkt von `Present` ist, hat
niemand versprochen. Das System bindet deshalb selbst: Puffer `0` der Swapchain holen, eine
Renderzielansicht darauf erzeugen, setzen, zeichnen. Die Ansicht wird gehalten und nur dann neu
erzeugt, wenn `GetDesc` eine andere Größe meldet als beim letzten Mal — eine Auflösungsänderung
oder ein Fensterwechsel macht die alte ungültig.

### 4.3 Die Fensterprozedur

Das Handle kommt aus `IDXGISwapChain::GetDesc().outputWindow`. Verkettet wird mit
`SetWindowLongPtrW(hwnd, GWLP_WNDPROC, …)`; der zurückgegebene Zeiger ist das Original und wird für
`CallWindowProcW` gemerkt. Beide Funktionen fehlen in `REX::W32` und werden in `src/Menu/Win32.h`
selbst deklariert — `user32.lib` linkt `commonlib-shared` bereits `PUBLIC`.

Die Prozedur tut drei Dinge, in dieser Reihenfolge:

1.  Ist es die Umschalttaste als `WM_KEYDOWN`, wird ein Umschaltwunsch vermerkt und die Meldung
    geschluckt. Umgeschaltet wird sie nicht — das tut `Tick`, siehe Abschnitt 5.
2.  Ist das Overlay offen, geht die Meldung an `ImGui_ImplWin32_WndProcHandler`. Meldet der
    zurück, dass er sie haben wollte, ist sie erledigt und wird geschluckt; sonst geht sie weiter.
3.  Sonst unverändert an das Original.

**Warum geschluckt wird, obwohl das Spiel nicht aus Meldungen liest:** Nicht das Spiel liest sie,
aber andere Plugins und Überlagerungen tun es. Eine Meldung, die für uns bestimmt war, soll nicht
zusätzlich woanders wirken.

### 4.4 Die Eingabeschicht

Beim Öffnen:

```cpp
auto* const manager = RE::BSInputEnableManager::GetSingleton();
manager->AllocateNewLayer(_layer, "CommunityShadersFO4");
manager->EnableUserEvent(
	_layer->layerID,
	RE::UserEvents::USER_EVENT_FLAG::kAll,
	false,
	RE::UserEvents::SENDER_ID::kMenu);
```

Beim Schließen wird derselbe Aufruf mit `true` gemacht und die Schicht losgelassen. `kAll` ist
`-1`, schaltet also jedes Ereignis ab — Bewegung, Umsehen, Kämpfen, VATS und den Rest.

Die drei dahinterliegenden IDs sind gemessen vorhanden (Abschnitt 1). Gemessen ist damit, dass sie
**auflösen** — ob sie auf die richtige Funktion zeigen, kann nur das Spiel zeigen. Das ist der
erste Aufruf einer Engine-Funktion im ganzen Port; bis hierher wurde nur Speicher gelesen.

### 4.5 Was gezeichnet wird

Ein Fenster mit dem Titel `Community Shaders`, darin die Bildnummer, der Zustand der
Eingabeschicht, und ein Knopf, der das Overlay schließt. Das ist keine Oberfläche, sondern der
Beweis, dass Zeichnen, Maus und Tastatur ankommen. Die Liste der Features kommt in E2.

### 4.6 Erweiterung von `Features::Settings`

Heute kennt der Einstellungsspeicher nur Feature-Schalter. Dazu kommt:

```cpp
namespace Features::Settings
{
	/// a_path is a two segment path, "Block/Key" - the store addresses its
	/// values as JSON pointers, one object per segment.
	void DeclareBool(std::string_view a_path, bool a_default);
	[[nodiscard]] bool GetBool(std::string_view a_path) noexcept;

	void DeclareUInt32(std::string_view a_path, std::uint32_t a_default);
	[[nodiscard]] std::uint32_t GetUInt32(std::string_view a_path) noexcept;
}
```

`DeclareFeature` und `IsEnabled` bleiben als die feature-nahe Fassung bestehen und werden intern zu
`DeclareBool("<Name>/enabled", …)` beziehungsweise `GetBool`. Der Schreiber der
Standarddatei gruppiert künftig nach dem ersten Pfadsegment, sodass aus `Menu/toggleKey` und
`ImagespaceTint/enabled` die erwartete Verschachtelung entsteht. Zwei Segmente sind Pflicht und
werden geprüft.

**`float` gibt es weiterhin nicht** — `TJsonSetting` ist dafür nicht instanziiert. E2 nimmt
`double`, wenn es Gleitkommawerte braucht.

## 5. Zustände und Ablauf

| Zeitpunkt        | Was geschieht                                                                        |
| ---------------- | ------------------------------------------------------------------------------------ |
| `kGameDataReady` | `Menu/toggleKey` wird angemeldet, bevor `Settings::Init()` läuft                     |
| erstes `Present` | ImGui wird aufgesetzt, die Fensterprozedur verkettet                                 |
| jedes `Present`  | ein ImGui-Frame; gezeichnet wird nur, wenn offen                                     |
| Taste gedrückt   | die Fensterprozedur setzt ein Flag; geöffnet und geschlossen wird im nächsten `Tick` |

**Umgeschaltet wird nicht in der Fensterprozedur**, obwohl die Taste dort ankommt: die Prozedur
läuft auf dem Fenster-Thread, die Eingabeschicht und ImGui gehören dem Render-Thread. Die Prozedur
setzt deshalb nur ein `std::atomic<bool>`, und `Tick` handelt es ab — dieselbe Trennung wie beim
Watcher in C.

## 6. Fehlerbehandlung

| Fall                                 | Verhalten                                                                 |
| ------------------------------------ | ------------------------------------------------------------------------- |
| Gerät, Kontext oder Fenster fehlen   | einmal protokollieren, Overlay bleibt aus, Spiel läuft weiter             |
| `ImGui_ImplDX11_Init` scheitert      | dito, und der ImGui-Kontext wird wieder abgebaut                          |
| Verketten der Prozedur scheitert     | Overlay bleibt aus — ohne Eingabe wäre es ein unbedienbares Bild          |
| `BSInputEnableManager` ist `nullptr` | Overlay öffnet trotzdem, ohne Eingabesperre, mit einer Zeile im Log       |
| Renderzielansicht nicht erzeugbar    | dieser Frame wird übersprungen, beim nächsten neu versucht                |
| Umschalttaste ist `0`                | Overlay lässt sich nicht öffnen; eine Zeile beim Aufsetzen nennt den Wert |

Wie in D1 wird jeder Aufruf in das Overlay in `try`/`catch` gefasst: eine Ausnahme aus ImGui liefe
sonst durch unseren Present-Hook in die Engine.

## 7. Tests und Abnahme

### 7.1 Ohne Spiel prüfbar

| Test                   | Prüft                                                                                                                                                                                                                                                                                    |
| ---------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `MenuGateTests`        | Der Zustandsautomat: Tastendruck öffnet, erneuter schließt; die Eingabesperre wird genau einmal je Übergang angefordert und genau einmal aufgehoben; eine Taste, die nicht die konfigurierte ist, tut nichts; zwei Tastendrücke vor dem nächsten Tick ergeben einen Übergang, nicht zwei |
| `FeatureSettingsTests` | erweitert: ein `uint32` überlebt Schreiben und Lesen; die Standarddatei gruppiert `Menu/toggleKey` und `<Feature>/enabled` richtig; ein Pfad ohne genau zwei Segmente wird abgelehnt                                                                                                     |

Der Zustandsautomat wird dafür von ImGui und der Engine getrennt: er bekommt die Eingabesperre als
zwei Rückrufe herein, genau wie die Registrierung in D1 ihre `EnabledQuery` bekommt. Das ist der
Grund für den Schnitt, nicht Eleganz.

### 7.2 Nur im Spiel prüfbar

Alles Übrige: dass ImGui zeichnet, dass die Maus ankommt, und vor allem, ob die drei
Adressbibliotheks-IDs auf die Funktionen zeigen, für die wir sie halten.

### 7.3 Abnahmekriterien

1.  Das Log nennt beim Aufsetzen Fensterhandle, ImGui-Version und die Umschalttaste.
2.  **Ende** öffnet das Overlay, der Knopf lässt sich mit der Maus drücken, **Ende** schließt es.
3.  **Bei offenem Overlay bewegt sich der Spieler nicht**, die Kamera dreht sich nicht, und ein
    Mausklick löst keinen Angriff aus. Nach dem Schließen geht alles wieder.
4.  Der Farbstich aus C bleibt sichtbar, während das Overlay offen ist.
5.  Alt-Tab hinaus und zurück, danach ist das Overlay weiter bedienbar.
6.  Die Einstellungsdatei enthält nach dem ersten Start einen Block `Menu` mit `toggleKey`; ein
    anderer Wert dort schaltet nach einem Neustart auf die andere Taste um.
7.  Die acht Host-Tests aus A bis D2 bleiben grün.

Kriterium 3 ist der eigentliche Prüfstein: es ist der einzige, der die Engine-Aufrufe belegt.

## 8. Annahmen, die E1 bestätigen muss

-   Dass `imgui_impl_dx11.h` seine D3D-Typen vorwärtsdeklariert. Täte es das nicht, bräuchte unsere
    Übersetzungseinheit `<d3d11.h>`, das im Projekt verboten ist — dann müsste der Aufruf hinter
    eine eigene Übersetzungseinheit wandern, die sonst nichts von `REX::W32` sieht.
-   Dass sich `REX::W32::ID3D11Device*` per `reinterpret_cast` an ImGui reichen lässt. Beide sind
    Zeiger auf dasselbe COM-Objekt; die Deklaration unterscheidet sich, das Objekt nicht.
-   Dass die drei IDs auf `AllocateNewLayer`, `EnableUserEvent` und `DecRef` zeigen. Vorhanden sind
    sie gemessen; ihre Identität zeigt erst Kriterium 3.
-   Dass `BSInputEnableManager` vom Render-Thread aus gerufen werden darf. Der Manager hält eine
    `BSSpinLock`, was dafür spricht, beweist es aber nicht.
-   Dass ImGuis Win32-Backend mit einem Fenster zurechtkommt, das ihm nicht gehört, und dass
    `SetWindowLongPtrW` auf dem Spielfenster erlaubt ist.

## 9. Übergabe

-   **Roadmap:** Zeile E in E1 und E2 teilen; E1 abgeschlossen, E2 offen mit dem Kriterium
    „Einstellungen im Overlay ändern, sie überleben einen Neustart".
-   **`CLAUDE.md`:** Ein Abschnitt zum Menü — wo es lebt, warum es kein Feature ist, und dass die
    Fensterprozedur nur ein Flag setzt. Die Zeile zu i18n bleibt ruhend und wandert auf E2.
-   **Für E2:** Die Featureliste hängt an `Features::Registry`; `EnabledQuery` ist die Naht, und
    zum Schreiben kommt `FJsonSettingStore::Save()` dazu — das funktioniert nur, weil D2 die Datei
    mit allen Schlüsseln anlegt. Themes und Schriften unter `package/Interface` und `package/SKSE`
    sind dort zu entscheiden.
-   **Für F:** Jedes Feature, das eine Oberfläche bekommen will, braucht dafür eine Naht in der
    Basisklasse. Die zu entwerfen ist Sache von E2, nicht von E1.
