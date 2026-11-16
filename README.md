# Ordinary Day

Symulator zupełnie zwyczajnego tygodnia introwertyka. Godot 4 + C++ (GDExtension), otoczenie z wokseli, postacie w pixel arcie, widok z boku z budynkami w przekroju.

Tydzień od poniedziałku do niedzieli. Czas płynie tylko wtedy, gdy coś robisz. Każda akcja otwiera jedne drzwi i zamyka inne: siłownia w poniedziałek → Kuba → domówka → Ola → pokój do wynajęcia → wyprowadzka. Albo komputer → gildia → zlecenie → pieniądze, ale mama zaczyna się martwić → czwartkowa kłótnia.

## Struktura

```
src/core/          czysta symulacja w C++ (bez Godota) – testowalna z linii poleceń
  sim.hpp/.cpp     zegar, fakty, reguły, harmonogramy NPC, ekwipunek, telefon, graf przyczyn, zapis
  json.hpp/.cpp    mały parser JSON bez wyjątków
src/godot/         GDExtension
  game_sim         węzeł GameSim – most do symulacji, sygnały
  world_director   buduje świat z world.json: teren, panorama, budynki, propsy, NPC
  cutaway_building wokselowy budynek; fasada znika, gdy budynek jest otwarty (przekrój)
  voxel_mesher     mesher wokseli z AO + loader MagicaVoxel .vox
  voxel_model      węzeł do ręcznego stawiania modeli .vox w edytorze
  animated_door    drzwi na zawiasach ze sprężystą animacją
  player_controller ruch boczny i sekwencje animacji: drzwi, schody, autobus; kamera
  tone_director    pora dnia + ukryte „ciepło” → słońce, mgła wolumetryczna, kurz, deszcz, mokre ulice
game/              projekt Godota
  content/*.json   CAŁA treść gry: świat, postacie, akcje, reguły, zakończenia
  assets/          vox / sprites / icons (generowane przez tools/gen_assets.py)
  shaders/         voxel.gdshader (emisja, kałuże, dithering fasad), post.gdshader (ziarno, winieta, aberracja)
  ui/hud.gd        interfejs: upływ czasu, unoszące się liczby, karty zmian i konsekwencji
  ui/dev_tools.gd  sterowanie grą z linii poleceń (testy, zrzuty ekranu)
tests/
  sim_fuzz.cpp     fuzzer tygodni + uruchamianie skryptów rozgrywki
  scripts/         skrypty ścieżek fabularnych (test regresji efektu motyla)
tools/gen_assets.py  generator placeholderowych modeli .vox i sprite'ów PNG
```

## Budowanie

Wymagania: Godot 4.5+ (testowane na 4.7.2), Python 3, SCons (`pip3 install --user scons`), kompilator C++17.

```sh
git submodule update --init                               # godot-cpp (gałąź 4.5)
scons platform=macos arch=arm64 target=template_debug     # → game/bin/
python3 tools/gen_assets.py                               # assety (już są w repo)
godot --path game --import                                # pierwszy import zasobów
godot --path game                                         # menu → gra
```

Na Windows/Linux: `scons platform=windows` / `platform=linux` – ścieżki są już w `ordinary_day.gdextension`.

## Testy efektu motyla

Rdzeń symulacji kompiluje się bez Godota:

```sh
clang++ -std=c++17 -O2 -o build/sim_fuzz tests/sim_fuzz.cpp src/core/*.cpp

build/sim_fuzz game/content 3000                     # 3000 losowych tygodni: rozkład zakończeń,
                                                     # martwe reguły, nieużyte akcje
build/sim_fuzz game/content 1 --verbose 42           # jeden tydzień z pełnym logiem i grafem przyczyn
build/sim_fuzz game/content --script tests/scripts/warm_moveout.txt   # scenariusz z asercjami
```

Ładowanie treści waliduje odwołania: nieznane przedmioty, lokacje, NPC, a także **fakty czytane, ale nigdy nieustawiane** (najczęstszy błąd w grafie przyczyn).

## Grafika

Woksele mają 1/8 m (budynki) i 1/16 m (meble), z szumem koloru na każdym wokselu i zapieczonym AO. Jasność wokseli w nocy i ekranów koduje kanał alfa koloru: palety `.vox` z alfą 96 świecą zawsze, z alfą 200 – tylko nocą.

Na oświetlenie składają się SDFGI i SSIL, mgła wolumetryczna ograniczona do `FogVolume` wokół budynków (smugi światła przez okna), światła pokojów i latarń z cieniami, bloom, SSR na mokrych kałużach, głębia ostrości tła, ziarno, winieta i lekka aberracja.

Kamera jest perspektywiczna (34°, ok. 20 m od sceny, lekko z góry), więc pokoje mają widoczne podłogi, sufity i zbiegające się ściany boczne. Tło ma trzy warstwy: bloki z wielkiej płyty z balkonami i kolorowymi pasami, wieżowce z kominem elektrociepłowni i wieżą kościoła oraz linię drzew. Niebo rysuje własny shader (`sky.gdshader`): gradient, słońce, chmury i gwiazdy.

## Interfejs

- **Menu główne** to osobna, lekka scena (`scenes/menu.tscn`); gra ładuje się dopiero po „Nowy tydzień” albo „Kontynuuj” (z podsumowaniem zapisu).
- **Ustawienia** (menu i pauza): jakość grafiki Niskie/Średnie/Wysokie, limit klatek, pełny ekran; zapis w `user://settings.cfg`. Niskie i Średnie wyłączają SDFGI, SSIL, SSR i cienie lamp w pokojach oraz renderują 3D w 55%/77% rozdzielczości z FSR. Ma to znaczenie na ekranach Retina, gdzie domyślnie renderuje się w 2×.
- **Pauza (Esc):** zapis i wczytanie; gra zapisuje się sama na początku każdego dnia.
- **Podpowiedzi w świecie:** nad drzwiami, schodami (▲/▼) i przystankiem etykieta z klawiszem (kłódka i powód, gdy zamknięte). Nakładające się etykiety układają się jedna nad drugą, a aktywna ma ramkę. W/S działa do 1,6 m od przejścia – postać sama podchodzi. Nad NPC, z którym można coś zrobić, „!” z daleka i „E · akcja” z bliska.
- **Pasek skrótów:** licznik akcji w miejscu i odznaka nieprzeczytanych wiadomości.
- **Karty:** baner miejsca przy wejściu, „Nowa możliwość”, gdy w trakcie pobytu pojawi się nowa akcja, oraz zmiany relacji, przedmioty i konsekwencje.
- **Menu akcji:** akcje pogrupowane, z kim je robisz, koszt czasu i powód blokady. Sterowanie strzałkami, W/S, 1–9 i E.

## Testy wizualne

```sh
godot --always-on-top --path game scenes/main.tscn -- "--do=key:help,walk:street:6,delay:2,key:move_up,delay:0.5,shot:res://../build/door.png" --shot=res://../build/end.png
```

Komendy: `act:`, `hud:` (akcja z animacją HUD), `pause`, `nohud`, `quality:0-2`, `nolimit`, `wait:HH:MM`, `go:link:a|b`, `walk:seg:x`, `take:item`, `key:akcja`, `delay:s`, `shot:ścieżka`. `--always-on-top` jest potrzebne na macOS, bo zasłonięte okno przestaje się renderować.

## Sterowanie

| Klawisz | |
|---|---|
| A / D | chodzenie |
| W / S | drzwi, schody, autobus |
| E | menu akcji (każda kosztuje czas) |
| Q | szuflada w twoim pokoju (6 slotów ekwipunku) |
| P | telefon |
| 1–6 | slot ekwipunku (w pokoju: odłóż) |
| F5 / F9 | zapis / wczytanie |
| Esc | menu pauzy |
| F1 | pomoc |

## Pisanie treści

Każdy plik `game/content/*.json` jest scalany. Wygodnie jest trzymać jeden wątek fabularny w jednym pliku (`chain_ola.json`, `chain_halina.json`…).

### Warunki (`when`, `requires[].if`, `check`)

Lista = AND. Wewnątrz jednego wpisu `|` = OR. `!` neguje.

| Zapis | Znaczenie |
|---|---|
| `met_kuba` / `!met_kuba` | fakt ustawiony / nieustawiony |
| `mom_worry>=3` | porównanie faktu |
| `$battery<20`, `$money>=900`, `$warmth>=50`, `$rel.ola>=15` | statystyki i relacje |
| `$day<=1`, `$time>=17:00`, `day:wed`, `day:weekend` | czas |
| `item:umbrella`, `stash:book` | ekwipunek / szuflada |
| `at:kitchen,room` | gdzie jest gracz |
| `npc:kuba@gym`, `npc:mama@here` | gdzie jest NPC (wg harmonogramu) |
| `weather:rain`, `fired:rule_id`, `chance:0.3` | pogoda, reguła już odpaliła, deterministyczny los |
| `read_kuba_party` | wiadomość przeczytana (wartość = czas przeczytania) |

### Efekty

`set:fakt`, `set:fakt=3`, `add:fakt=-1`, `clear:fakt`, `$energy-=10`, `$rel.mama+=5`, `give:item`, `take:item`, `stash:item`, `msg:tekst`, `phone:id|Od kogo|treść`, `time+=2`, `goto:lokacja`, `ending:auto`.

`phone:` dociera tylko, gdy gracz ma przy sobie telefon. Inaczej czeka, aż weźmie go z szuflady, i dopiero wtedy ustawia `read_<id>`. Zostawienie telefonu w domu to więc decyzja z konsekwencjami.

### Akcje

```json
{ "id": "train", "label": "Trenuj (1 h)", "loc": "gym", "cost": 4, "limit": "daily",
  "when": [...], "requires": [{ "if": ["$energy>=20"], "why": "Nie masz siły." }],
  "effects": [...],
  "outcomes": [ { "when": ["npc:kuba@here", "!met_kuba"], "effects": [...], "text": "..." } ],
  "text": { "cold": "…", "warm": "…" } }
```

`cost` liczy się w kwadransach. `when` ukrywa akcję, a `requires` pokazuje ją wyszarzoną z powodem (`why`). `outcomes` działa jak `switch`: wygrywa pierwszy pasujący wariant.

### Reguły (świat reaguje)

```json
{ "id": "halina_cake", "label": "Pani Halina przynosi sernik",
  "when": ["helped_halina"], "delay": { "days": 1, "at": "10:00" },
  "check": ["at:kitchen,room"], "effects": ["give:cake"], "else_effects": ["stash:cake"] }
```

Reguły są sprawdzane co kwadrans gry. `delay` odkłada skutek w czasie, a `check` sprawdza warunek dopiero w momencie skutku. `repeat` może mieć wartość `daily` albo `always`. `label` pojawia się na końcowym grafie „Dlaczego tak wyszło”.

### Ton

Każdy tekst może być obiektem `{ "cold": …, "warm": … }`. Wariant wybiera ukryta statystyka `warmth` (próg 50). Ta sama wartość steruje też nasyceniem barw, kolorem światła i kolorem narracji w HUD-zie.

## Stan prac

Zrobione: rdzeń symulacji, pełny tydzień z 8 NPC, 52 akcjami i 36 regułami, 5 zakończeń z epilogami, graf przyczyn, zapis i odczyt, świat (blok z 4 piętrami, Żabka, siłownia, przystanek, park, kampus z wydziałem i biblioteką, kawiarnia), cykl dnia i nocy, deszcz, światła w mieszkaniach, HUD.

Następne kroki:
- dźwięk i muzyka sterowane `warmth`;
- ręcznie robione modele w MagicaVoxel w miejsce generowanych;
- więcej wątków (ok. 200 reguł);
- edytor grafu przyczyn;
- presety eksportu (filtr `*.json, *.vox`).
