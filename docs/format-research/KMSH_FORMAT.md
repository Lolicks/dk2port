# KMSH — сжатие мешей Dungeon Keeper II (формат и алгоритм)

**Статус: РЕШЕНО.** Алгоритм восстановлен из открытых реализаций и проверен
на всех 2015 мешах `Meshes.WAD`: 2015/2015 декодируются в KMF точной длины.

## 1. Источники (точные цитаты)

| Источник | Что дал |
|---|---|
| `research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/wad/WadFile.java:292` — `decompressFileData(byte[] src, String fileName)` | Полный Java-порт токенного декодера: 4 ветви по битам управляющего байта, 3-байтовый размер, `do/while (counter-- != 0)` |
| `research/dk2-tools/Scripts/wad_extract.1sc:74` — `void decompressFileData(ubyte src[], uint size, string filename)` | Независимая реализация того же алгоритма на 010 Script; комментарии «Correct decrement», «Correct postfix decrement» |
| `research/dk2-tools/Formats/Model/kmf_struct.bt:37` — `local string tKmsh = "KMSH"` | Подтверждает, что результат — KMF с магическим `KMSH`, чанки HEAD/MATL/MESH/… |
| `research/OpenKeeper/.../kmf/KmfFile.java:69` — `KMF_HEADER_IDENTIFIER = "KMSH"` | Парсер ожидает распакованный KMF; сам распаковщик в KmfFile НЕ находится |
| `tools/kmsh_decompress.py` | Наша реализация; `decompress_stream()` — построчный порт Java-версии |

## 2. Оболочка WAD-записи (flags = 4)

```
00 10        Заголовок (0x10, 0xfb)
02    variant       u8   0..5 (1839×0, 144×1, 29×2, 2×3, 1×5)
03    low_size      u16  BE, младшие 16 бит размера распакованных данных
05    trailer       u8   0xe3 (2015/2015 в нашем наборе)
06    "KMSH"        4 байта
10    full_size     u32  LE, полный размер распакованных данных
12    variant2      u8   == variant
13    ...           поток сжатых токенов
```

**Ключевое открытие:** поток сжатия начинается со смещения **5**, а не 14.
Байты 5..13 (`e3 4b 4d 53 48 27 53 00 00`) — это уже первый литеральный
пробег(output: `KMSH` + u32 размера). Поэтому `KMSH` видно в «сжатом теле»:
он не является заголовком потока, а является его первыми выходными байтами.

Проверка заголовка (из Java):
```
i = 0
if (src[i++] & 1) != 0: i += 3        # опциональное поле
i += 1                                # пропустить второй байт
decsize = (src[i]<<16) | (src[i+1]<<8) | src[i+2]   # BE!
i += 3                                # <- i == 5 для наших 2015 записей
```

## 3. Алгоритм токенов

Цикл читает `flag = src[i++]`; дальше four ветви:

### Ветвь 0 — `(flag & 0x80) == 0`
```
tmp = src[i++]
n_lit = flag & 3                    # 0..3 литералов
копировать n_lit байт буквально
k = j - ((flag & 0x60) << 3) - tmp - 1
n_ref = ((flag >> 2) & 7) + 2
do { out[j++] = out[k++]; } while (n_ref-- != 0)   # n_ref+1 копий!
```

### Ветвь 1 — `(flag & 0x40) == 0`
```
tmp  = src[i++]; tmp2 = src[i++]
n_lit = tmp >> 6                    # 0..3
копировать литералы
k = j - ((tmp & 0x3F) << 8) - tmp2 - 1
n_ref = (flag & 0x3F) + 3           # +1 копия => 4..67
```

### Ветвь 2 — `(flag & 0x20) == 0`
```
a = src[i++]; b = src[i++]; d = src[i++]
n_lit = flag & 3
копировать литералы
k = j - ((flag & 0x10) << 12) - (a << 8) - b - 1
n_ref = d + ((flag & 0x0C) << 6) + 4 # +1 копия
```

### Ветвь 3 — terminal / длинный литерал
```
n = (flag & 0x1F) * 4 + 4
if n > 0x70: finished = true; n = flag & 3
копировать n байт
```

**Критическая деталь:** `do … while (counter-- != 0)` выполняет тело
`counter + 1` раз (сравнение использует значение до декремента). Именно эта
dеталь отсутствовала в предыдущих неудачных попытках.

## 4. Результат

Выход — распакованный KMF v17 (IFF-подобные чанки):
`KMSH` → `HEAD`, `MATL`/`MAT2`, `MESH`/`ANIM`/`GROP`, `SPRS`/`SPHD`, `GEOM`,
`CTRL`, `POLY`, `VERT`, `ITAB`, `VGEO`, `ELEM`. Спецификация — `KMF_FORMAT.md`.

Проверка (реальные команды, 2026-11-23):
```
$ python3 tools/kmsh_decompress.py batch ../Data/Meshes.WAD build/KMF --limit 2015 --json
decoded 2015 total bytes 53622297
$ python3 tools/kmf_tool.py validate build/KMF --json
{"clean": 2015, "with_issues": 0, "errors": 0}
$ python3 -m unittest discover -s tests
Ran 85 tests ... OK
```

Пример: `g_Maiden-Torture.kmf` — 5647 байт сжатого → **21287 байт** точного выхода;
первыйLiteral token `0xe3` даёт 16 байт: `KMSH`, u32 21287, `11 00 00 00`, `HEAD`.

## 5. Ограничения

- Сжатие (обратная операция) в найденных инструментах **не реализовано** — только распаковка.
- Повреждённые записи: наш декодер проверяет границы и генерирует `KmshError`;
  оригинальные реализации этого не делают.
- Поле `variant` влияет только на заголовок; разницы в потоке для variant>0
  не обнаружено (все 2015 декодируются одним алгоритмом).
