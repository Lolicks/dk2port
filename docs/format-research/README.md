# Format research — Dungeon Keeper II

Этот каталог — техническая база для разработки **собственного** C-декодера
ресурсов DK2. Исследование выполнено по исходникам OpenKeeper (GPLv3),
010-шаблонам dk2-tools, реальным игровым файлам и 8260 декомпилированным
функциям DKII.EXE (Ghidra 12.2).

## Статус форматов

| Формат | Статус | Документ | Проверка |
|---|---|---|---|
| WAD / DWFB | Контейнер **решён**; распаковка flags=4 **решена** | `WAD_FORMAT.md`, `KMSH_FORMAT.md` | 5 WAD, 2667 записей; 2015/2015 распаковано |
| KMSH → KMF | **Решён** | `KMSH_FORMAT.md`, `KMF_FORMAT.md` | 53 622 957 байт KMF, 2015/2015 валидны |
| KMF v17 | **Решена** структура (MESH/ANIM/GROP) | `KMF_FORMAT.md` | 2015/2015 распарсено, 0 ошибок |
| TGQ / TQI | Контейнер и кодек **идентифицированы**; рабочий декодер есть в OpenKeeper (Java) | `TQI_TGQ_FORMAT.md` | 24 файла, 16 108 кадров; реальные PNG получены |
| SDT / звук | Структура **решена**; MPEG Layer I / PCM | `WAD_FORMAT.md`, `OPENKEEPER_ANALYSIS.md` | 108 банков, 5512 сэмплов |
| KWD / KLD | Контейнер решён, часть семантики неизвестна | `../KWD_FORMAT.md` | 46 существ, 66 карт |
| Декомпиляция | 8260 функций, сопоставление выполнено | `DECOMPILED_FUNCTIONS.md` | см. документ |

## Ключевой результат

`KMSH` — это не «формат модели», а **сжатый поток**, который раскладывается в
IFF-подобный `KMF`. Поток начинается на смещении **5** WAD-записи; токенный
алгоритм имеет 4 ветви по битам `flag` и `do/while (counter-- != 0)` (тело
выполняется `counter+1` раз). Источник алгоритма — OpenKeeper `WadFile.java:292` и
dk2-tools `wad_extract.1sc:74`.

## Документы

1. `README.md` — этот файл.
2. `KMF_FORMAT.md` — полная спецификация KMF v17 с таблицами полей.
3. `KMSH_FORMAT.md` — сигнатура, оболочка, токенный алгоритм.
4. `TQI_TGQ_FORMAT.md` — EA TQI, IFF-контейнер, VLC/IDCT, XA ADPCM.
5. `WAD_FORMAT.md` — DWFB, таблица файлов, flags 0/4, распаковка.
6. `OPENKEEPER_ANALYSIS.md` — разбор готовых реализаций с цитатами.
7. `DECOMPILED_FUNCTIONS.md` — сопоставление 8260 функций.
8. `DECODER_ARCHITECTURE.md` — архитектура независимого C-декодера.
9. `IMPLEMENTATION_PLAN.md` — план по этапам.
10. `REFERENCES.md` — все источники.

## Воспроизводимость

```bash
cd port
python3 tools/wad_tool.py list ../Data/Meshes.WAD --json
python3 tools/kmsh_decompress.py batch ../Data/Meshes.WAD build/KMF --json
python3 tools/kmf_tool.py validate build/KMF --json
python3 tools/tgq_probe.py info ../Data/Movies/BullfrogIntro.tgq --json
python3 -m unittest discover -s tests
```

Все выходные файлы и артефакты лежат в `port/`. Оригинальные игровые ресурсы
**не** копируются в репозиторий и не должны распространяться.
