# REVERSE ENGINEERING — Dungeon Keeper 2

Источник: оригинальная установка GOG (Bullfrog, 1999). Все выводы проверены
тестами в `port/tests/` или цитатами декомпилированного кода в
`port/re/ghidra/decomp/`. Где доказательств нет — прямо сказано «неизвестно».

## 1. Инструменты и ограничения

- **REA 6.10.1**: `inspect_pe_imports` дал 13 DLL / 251 импорт, imphash
  `3c5a546374b48c7a84dee5d13fdd1803`; собраны 387 строк в
  `docs/strings_catalog.txt`, отчёт `docs/EXE_ANALYSIS.md`.
- **Ghidra 12.2 headless (напрямую, минуя дедлайн MCP)**: полный анализ
  DKII.EXE с `-Xmx3G`, `-analysisTimeoutPerFile 2400`. Результат:
  **8260 функций, 8260 декомпилировано, 0 ошибок** (`re/ghidra/export_summary.txt`).
  Выгрузки: `functions.json`, `symbols.json`, `memory.json`, `decomp/*.c`, `asm/*.asm`.
  Два вызова API в экспортном Java-скрипте были исправлены в процессе.
- Попытка через REA-провайдеров Ghidra/Hopper срывалась из-за 512M heap и
  клиентского дедлайна; напрямую та же машина (2 CPU / 5.6 GB) справилась.
- ** Hopper ** доступен как провайдер REA, но также упирался в клиентский
  дедлайн; использован как вторичный источник, не как основной.

## 2. Function-level находки (декомпилированный код)

| Адрес | Что делает (доказательство) | Значение для порта |
|---|---|---|
| `FUN_00448d80` | `if (timeGetTime() - DAT_006da460 > 0x31)` → цикл по 2D-сеткам `short`/`int`; усреднение 4 соседей, деление на 4, кламп `[-0x1000, +0x1000]`. | Периодическая **grid-релаксация ~49 мс (≈20 Гц)**._semantics поля точно не доказана (кандидаты — жидкости/тепло/шум). В порте пока нет аналога. |
| `FUN_00575700` | `DAT_0075ca70 = 1000 / (timeGetTime() - DAT_00765208)`, затем `FUN_0058e580(DAT_0075ca70)`. | Оригинал считает FPS как `1000/delta` и передаёт дальше; подсказка для HUD/профилирования. |
| `FUN_005b5e00` | `QueryPerformanceCounter` с сохранёнными base/frequency/divisor/offset, fallback на `timeGetTime`. | Двухуровневый таймер: точный счётчик + миллисекундный резерв. |
| `FUN_00555b10` | Цикл `do { rdtsc(); timeGetTime(); } while (...)`; возвращает `(delta_cycles)/(delta_ms)`. | Калибровка CPU (cycles/ms) при старте. |
| `FUN_0057d270` | Читает "TEXTURE_SET", вызывает `FUN_0059a500`, затем `FUN_0057a800(..., &DAT_006be138 /* "KMSH" */, ...)`; переключает `iVar2` 1/2/3 на `FUN_0057d5b0`, `FUN_0057dad0`, `FUN_0057e110`. | Это **выбор парсера/сериализатора KMF**, не декомпрессор. Три варианта формата. |
| `FUN_005b26f0` | Читает 8-байтовый заголовок кадра TGQ, затем `FUN_00659580(param_1+8)`. | Точка входа декодера TGQ (см. `TGQ_CODEC_ANALYSIS.md`). |

## 3. Восстановленные форматы

### 3.1 DWFB/WAD (полностью)
`docs/WAD_FORMAT.md`, `tools/wad_tool.py`, 11 тестов OK.
`"DWFB"` + u32 v2 + 0x48 reserved; @0x48 `count`, `pool_offset`, `pool_size`;
@0x58 записи по 40 B: `hash_id, name_off, name_len, data_off, data_size,
flags, uncompressed_size, 0,0,0`. Пул имён заканчивается в EOF.
Итог: EngineTextures 102 PNG, Sprite 24, Paths 81, FrontEnd 445, Meshes 2015.

### 3.2 KWD/KLD (контейнер полностью, часть семантики)
`docs/KWD_FORMAT.md`, `tools/kwd_tool.py`, 20 тестов OK.
Root `[tag,version=4,size]`, чанки `[tag,byte_len,payload]`; `0x65` = w/h,
`0x66` = `u32 tiles[w*h]`. Все 66 карт удовлетворяют `size == 36 + w*h*4`.
Извлечено: 46 существ, 26 комнат, 16 ловушек, 13 заклинаний хранителя,
41 заклинание существа, 8 дверей, 145 объектов, 41 местность, 66 карт.

**Тайл:** `[terrain:8][surface:8][owner:8][flags:8]`; `byte0` — индекс в
`Terrain.kwd` (1=Rock, 2=Dirt Path, 5=Gold, 9=Treasury, 11=Portal…).

### 3.3 Миссии (новое)
`docs/missions.json`, `tools/mission_tool.py`, 13 тестов OK.
Разобраны все 330 sidecar-файлов (66 `.kwd` + 264 `.kld`). Извлечено 1359 строк:
внутренние имена карт (UTF-16LE @0x38), тексты брифингов 5 миссий, имена
главных врагов 16 уровней (`level1` → Antonius, `level3` → Avaricious,
`Level20` → Reginald), имена игроков, 15 файловых ссылок на карту.
Категории: campaign (уровни 1–20 с вариантами 6a/6b, 11a/b/c, 15a/b),
secret1–5, multiplayer `mpd1..7`, прочие skirmish. Стартовые gold/mana
остались `null`: семантика полей не доказана.

### 3.4 Баланс из документации (новое)
`docs/balance.json`, `engine/Balance.generated.hpp`, `tools/build_balance.py`.
Цены комнат из `manual.pdf`: Treasury 200, Lair 300, Hatchery 300,
Training 500, Combat Pit 750, Temple 750, Workshop 600, Casino 2000,
Graveyard 2000, Portal 750, Torture 1500, Prison 750, Library 1500,
Guard Room 600, Wooden Bridge 200, Stone Bridge 500.
Требования к suществам и их заклинания — из `reference_card.pdf` (14 строк).
Это **документированные** значения, в отличие от реконструкционных констант.

### 3.5 TGQ / SDT
- `docs/TGQ_FORMAT.md` + `docs/TGQ_CODEC_ANALYSIS.md`: 24/24 файла — IFF
  (`SCHl`, `pIQT`, `SCCl`, `SCDl`, `SCEl`). **`pIQT` — это обратный порядок `TQIp` →
  Electronic Arts TQI**, MPEG-1-производный DCT-кодек; подтверждено RTTI
  `TbTQILoader`, секциями `tqia_dat`/`idct_dat`/`lbmpeg_d` и
  `FUN_005B26F0`. Заголовок кадра — 8 байт, `40 01 e0 01 62..64 14 1e 03`
  (320×480). Полный декодер не построен: VLC-таблицы строятся в рантайме.
- `docs/SDT_FORMAT.md`: 108 банков, 5512 сэмплов. MPEG Layer I 64 kbps
  22.05 kHz mono, MPEG-2 Layer II stereo, PCM16. `.map` — 170 BANK + 171 SFX.

### 3.6 KMSH — РЕШЁН
`docs/format-research/KMSH_FORMAT.md`, `tools/kmsh_decompress.py`. Оболочка
`10 fb <variant> <u16 BE size> e3 "KMSH" <u32 LE size>` + тело (2015/2015).
Алгоритм найден в открытых реализациях: OpenKeeper
`WadFile.java:292 decompressFileData` и dk2-tools `Scripts/wad_extract.1sc:74`.
Ключ: поток начинается на смещении 5, 4 ветви по битам `flag`, back-reference
`do/while (counter-- != 0)` даёт `counter+1` копий. Проверено:
**2015/2015 мешей → точный размер, валидный KMF v17, 53 622 957 байт**.
Спецификация — `docs/format-research/KMSH_FORMAT.md`, KMF — `KMF_FORMAT.md`.

## 4. Итог

Восстановлено: контейнеры, вся таксономия, геометрия карт, звук, интерфейсные
изображения, 1359 строк миссий, цены из документации, границы подсистем и
пять function-level фактов движка, форматы KMSH/KMF/TGQ/WAD. Не восстановлено:
собственный C-декодер TQI (есть рабочий Java-эталон), числовые поля KWD,
скрипты триггеров, формат оригинальных сохранений.
