# Dungeon Keeper 2 — нативный iOS-порт (рабочий каркас)

**Актуальный статус что реализовано, а что нет: [STATUS.md](STATUS.md)** (обновляется автоматически при каждой сборке).

**ВНИМАНИЕ:** это не законченная игра. См. `docs/PROGRESS.md` — там честно
перечислено, что работает, а что нет. Проект не заявляет соответствие критериям
завершения.

## Что есть

- RE-артефакты и отчёты по оригинальной игре — `docs/`.
- Декодеры форматов (WAD, KWD/KLD, TGQ, SDT, KMSH-envelope) — `tools/`.
- Воспроизводимый экспорт Ghidra (8260 декомпилированных функций) —
  `tools/run_ghidra_export.sh`, результаты в `re/ghidra/`.
- Импортёр подлинных ресурсов — `tools/import_assets.py`.
- Переносимое C++17-ядро (карты, A*, экономика, существа, камера, сохранения) — `engine/`.
- iOS-слой: SwiftUI + UIKit + Metal + жесты — `ios/DK2Port/`.
- XcodeGen-проект и CI — `ios/project.yml`, `.github/workflows/build.yml`.

## Быстрый старт (Linux)

```bash
cd port
python3 -m unittest discover -s tests -v
python3 tools/import_assets.py --game-root .. --out build/Assets
clang++ -std=c++17 -O2 -o build/engine_tests \
    tests/engine_tests.cpp engine/DK2.cpp
./build/engine_tests build/Assets/maps/Alcatraz.bin
```

## Импорт ресурсов

Ресурсы **не** копируются в репозиторий (авторские права). Запустите импортёр
с путём к своей легальной установке DK2:

```bash
python3 tools/import_assets.py --game-root "/path/to/Dungeon Keeper 2" \
    --out ios/DK2Port/Assets
```

Для полного импорта звука (~5512 сэмплов) требуется `mpg123`; без него
MPEG-сэмплы извлекаются в сыром виде.

## Сборка iOS (нужен macOS + Xcode)

```bash
cd port/ios
brew install xcodegen
xcodegen generate
xcodebuild -project DK2Port.xcodeproj -scheme DK2Port \
  -configuration Release -sdk iphoneos CODE_SIGN_IDENTITY="-" \
  CODE_SIGN_STYLE=Manual archive -archivePath build/DK2Port.xcarchive
```

`xcodebuild`/`xcodegen` недоступны на Linux — поэтому IPA здесь **не собран**.
CI на `macos-14` выполняет сборку и ad-hoc подпись автоматически.
Приложение ищет ассеты в `Bundle/Assets`, затем в `Documents/Assets`.

## Main known gaps

Декомпиляция функций движка, числовой баланс KWD, кодек 3D-мешей KMSH,
видеокодек TGQ, рантайм-звук, AI/бои/эффекты, скрипты кампании.
