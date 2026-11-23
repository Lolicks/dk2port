# Архитектура независимого C-декодера

## 1. Принципы

- Чистый C99/C11, только стандартная библиотека.
- Никакого JVM/OpenKeeper/jME3 в рантайме: исходники используются только как
  *спецификация*, скопирован алгоритм, а не код со ссылками на чужие классы.
- Каждый модуль — отдельная статическая библиотека с минимальным API.
- Все чтения через `reader` с проверкой границ; никаких невыровненных чтений
  `u32*` напрямую из буфера.
- Явная обработка little-endian (WAD/KMF/KWD) и big-endian (3-байтовый размер
  в KMSH, 2-байтовый в оболочке).
- Никакой аллокации по размеру из файла без проверки лимита.

## 2. Структура

```
decoder/
  core/
    reader.h/.c        # bounds-checked read_u8/u16le/u32le/u16be, seek, tell
    buffer.h/.c        # owning byte buffer, grow with overflow checks
    endian.h/.c        # LE/BE helpers
    error.h            # enum DK2_ERR_*, human-readable messages
    crc.h/.c           # (опционально) контроль целостности
  wad/
    wad.h/.c           # parse header/entries, flags, name pool, extract(raw)
  kmsh/
    kmsh.h/.c          # decompress_stream(): 4 token branches, exact size check
  kmf/
    kmf.h/.c           # IFF chunk walker; HEAD/MATL/MAT2/MESH/ANIM/GROP;
                       # GEOM/SPRS/POLY/VERT/ITAB/VGEO/ELEM
  tgq/
    tgq.h/.c           # IFF container: SCHl/pIQT/SCCl/SCDl/SCEl, frame headers
  tqi/
    tqi.h/.c           # VLC tables, dequant, float IDCT, motion compensation
    tables.h/.c        # таблицы, построенные по FUN_005B21E0 / OpenKeeper
  audio/
    xa.h/.c            # EA XA ADPCM (SCDl blocks)
    sdt.h/.c           # SDT bank/sample headers
  textures/
    dk2tex.h/.c        # Dk2TextureDecoder / EngineTextureDecoder порт
  tools/
    dk2unwad.c         # CLI: list/extract
    dk2kmf.c           # CLI: kmf -> obj/JSON
    dk2tgq.c           # CLI: movie -> PNG sequence (PPM на первом этапе)
  tests/
    test_wad.c, test_kmsh.c, test_kmf.c, test_tgq.c
```

## 3. Ключевые API (контракты)

```c
/* core */
typedef struct { const uint8_t *data; size_t size, pos; } Dk2Reader;
int dk2_read_u8(Dk2Reader*, uint8_t*);
int dk2_read_u16le(Dk2Reader*, uint16_t*);
int dk2_read_u32le(Dk2Reader*, uint32_t*);
int dk2_read_bytes(Dk2Reader*, void*, size_t);

/* wad */
typedef struct { char name[256]; uint32_t off, size, flags, uncompressed; } Dk2WadEntry;
int dk2_wad_parse(Dk2Reader*, Dk2WadEntry **out, size_t *count);
int dk2_wad_get(const uint8_t *wad, size_t wad_size, const Dk2WadEntry*,
                uint8_t **out, size_t *out_size);

/* kmsh */
int dk2_kmsh_decompress(const uint8_t *stored, size_t stored_size,
                        uint8_t **out, size_t *out_size);
/* внутренний контракт: 3-байтовый BE размер, поток со смещения 5 (или 8),
   ветви 0x80/0x40/0x20, do-while => counter+1 */

/* kmf */
typedef struct { uint32_t tag, size; size_t off; } Dk2Chunk;
int dk2_kmf_walk(const uint8_t*, size_t, Dk2Chunk**, size_t*);

/* tgq / tqi */
int dk2_tgq_parse(const uint8_t*, size_t, Dk2TgqInfo*);
int dk2_tqi_decode_frame(const uint8_t *bitstream, size_t len,
                         uint8_t *rgb, int width, int height);
```

## 4. Порядок чтения и выравнивание

- WAD: запись 40 байт, индекс начинается с `0x58`, имена — NUL-строки в пуле.
- KMSH: `u16 BE` на смещении 3; `u32 LE` на 10; поток с 5.
- KMF: все `u32 LE`; каждый чанк = `char[4] + u32 size + payload`, где `size`
  включает 8 байт заголовка (проверено `kmf_struct.bt` и `KmfFile.java`).
- TGQ: IFF-chunk, `size` включает 8-байтовый заголовок; кадр — 8 байт + битпоток.
- Выравнивание: в шаблонах 010 Editor явно `<optimize=false>` для массивов —
  это подсказка интерпретатору, а не требование к выравниванию в файле.
  В C читаем последовательно, без выравнивания.

## 5. Защита от повреждённых файлов

1. Проверять magic и версию до любой аллокации.
2. Проверять `offset + size <= file_size` для каждой записи/чанка.
3. Ограничивать `count` сверху (WAD ≤ 100000, KMF chunks ≤ 100000).
4. В KMSH: проверять `k` back-reference в `[0, out_len)`, иначе `DK2_ERR_CORRUPT`.
5. В KMF: проверять, что вложенный `size` не выходит за родительский чанк.
6. В TQI: проверять размер VLC-таблицы и выходной буфер до записи.
7. Никаких рекурсий по вложенным чанкам без глубины ≤ 8.

## 6. Кросс-платформенность

- Сборка под Windows: MSVC/clang-cl, `cmake` опционально; `stdint.h` из C11.
- Сборка под iOS: тот же C-код компилируется как Objective-C-совместимый C;
  экспорт в Swift через bridging header, без C++.
- Тесты — на хосте (Linux/macOS/Windows) на фикстурах из реальных файлов.

## 7. Что НЕ входит в декодер

Игровая логика, рендеринг, Metal, AVAudioEngine, загрузка миссий — это
уровень порта, а не декодера resources. См. `port/engine/`.
