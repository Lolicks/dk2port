# ARCHITECTURE — нативный порт Dungeon Keeper 2

## 1. Принципы

- Игровая логика написана на переносимом C++17 и **не зависит от Windows API**.
- Платформенный слой — Swift + Metal (iOS 15+, ARM64).
- Все визуальные/звуковые данные приходят из оригинальной установки через
  импортёр; генерируется только геометрия quad'ов для уже существующих тайлов.
- Никакие готовые реализации DK2 (OpenKeeper и т.п.) не используются.

## 2. Слои

```
┌───────────────────────────────────────────────────────┐
│ SwiftUI: меню миссий, HUD, настройки                   │
├───────────────────────────────────────────────────────┤
│ UIKit: MTKView, CADisplayLink, gesture recognizers     │
│   pan / pinch / rotation / tap → DK2Bridge             │
├───────────────────────────────────────────────────────┤
│ Swift Metal renderer: 8×8 atlas, instance draw,        │
│ fixed-timestep frame pump                              │
├───────────────────────────────────────────────────────┤
│ Objective-C++ DK2Bridge: NSDictionary snapshot API     │
├───────────────────────────────────────────────────────┤
│ C++17 core (engine/DK2.*):                             │
│  World, Tile, Game, Economy, Creature, Room, Camera,   │
│  A* pathfinding, LoopDriver, GridSimulation, Clock,    │
│  save/load                                            │
├───────────────────────────────────────────────────────┤
│ AssetBundle (импортёр): PNG, WAV/MP1, DK2M, JSON       │
└───────────────────────────────────────────────────────┘
```

## 3. Поток данных

1. `tools/import_assets.py` читает WAD/KLD/KWD/SDT исходной игры и пишет
   `build/Assets/`: `manifest.json`, `maps/*.bin` (`DK2M`), `maps/maps.json`,
   `data/game_data.json`, `textures/**`, `audio/**`.
2. Приложение ищет ассеты в Bundle, затем в `Documents/Assets` (переменная
   окружения `DK2_ASSET_ROOT` имеет наивысший приоритет).
3. `DK2GameViewController` загружает выбранную карту через мост, порождает
   существо и запускает `CADisplayLink`.
4. `LoopDriver` в C++ гарантирует фиксированный шаг 30 Гц; рендер — 60 кадр/с.
5. Metal-рендерер строит 8×8 атлас из **оригинальных** PNG и рисует каждый
   тайл и каждое существо как instance quad. UV-координаты берутся из ID местности.

## 4. Формат карты DK2M

```
"DK2M" | u32 version=1 | u32 width | u32 height | u32 tiles[w*h]
```
Tile word: `[terrain:8][surface:8][owner:8][flags:8]`.

## 5. Состояние и сохранение

`Game::save()/load()` пишет версионированный снимок (`DK2S` v1): заголовок,
все тайлы, существа, комнаты, экономика, время, счётчик тиков. Тест
round-trip проходит на C++-ядре. Это **собственный** формат порта, не формат
сохранений оригинальной игры (последний не восстановлен).

## 6. Тесты

- 45 Python-тестов: WAD (11), KWD/KLD (20), KMSH (14) — запускаются против
  реальных файлов игры; при отсутствии оригинальной игры пропускаются.
- C++ `tests/engine_tests.cpp`: загрузка карты, A*, 600 тиков fixed-step,
  экономика, заклинания, save/load, камера. Проходит и на реальной карте, и на
  синтетическом фикстуре CI.

## 7. Сборка

`port/.github/workflows/build.yml`:
- `core-tests` (ubuntu-latest): Python + C++;
- `build-ios` (macos-14): XcodeGen → xcodebuild archive → ad-hoc IPA.

Локально на macOS: `cd port/ios && brew install xcodegen && xcodegen generate
&& open DK2Port.xcodeproj`. На Linux собирается только ядро:
`clang++ -std=c++17 -o build/engine_tests tests/engine_tests.cpp engine/DK2.cpp`.

## 8. Известные разрывы с оригиналом

См. `PROGRESS.md` — порт **не является** законченной игрой.
