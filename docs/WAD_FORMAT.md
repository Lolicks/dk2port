# DWFB WAD container — reverse-engineered format specification

Target: the proprietary WAD archives shipped with *Dungeon Keeper 2* (1999, GOG release).
All facts below were derived from the bytes of the five shipped WADs, the sibling
`DK2TextureCache/EngineTextures.{dat,dir}` cache and strings in `DKII.EXE`.
No third-party DK2 implementation source was consulted.

Endianness: **little-endian** for every integer. Offsets are **absolute, 0-based file
offsets**. Multipliers/sizes are in **bytes**.

---

## 1. Files analysed

| File | Size (bytes) | Entries | First data offset | Name pool offset | Name pool size |
|---|---:|---:|---:|---:|---:|
| `Data/EngineTextures.WAD` | 2 208 282 | 102 | 4 168 (`0x1048`) | 2 206 863 (`0x21AC8F`) | 1 419 |
| `Data/FrontEnd.WAD` | 22 599 650 | 445 | 17 888 (`0x45E0`) | 22 591 731 (`0x158B8F3`) | 7 919 |
| `Data/Meshes.WAD` | 32 026 721 | 2 015 | 80 688 (`0x13B30`) | 31 990 798 (`0x1E8240E`) | 35 923 |
| `Data/Paths.WAD` | 1 933 348 | 81 | 3 328 (`0xD00`) | 1 931 905 (`0x1D7A81`) | 1 443 |
| `Data/Sprite.WAD` | 818 184 | 24 | 1 048 (`0x418`) | 817 822 (`0xC7A9E`) | 362 |

All five have magic `44 57 46 42` (`"DWFB"`) and version `u32 = 2`.

---

## 2. Overall layout

```
+----------------------------------------------------------+ 0x0000
| magic "DWFB"                  4 bytes                     |
| version u32 = 2               4 bytes                     |
| reserved (all zero observed)  64 bytes (0x08..0x47)       |
| entry_count        u32 @ 0x48                            |
| name_pool_offset   u32 @ 0x4C                            |
| name_pool_size     u32 @ 0x50                            |
| reserved           u32 @ 0x54 (zero)                     |
+----------------------------------------------------------+ 0x0058
| entry[0]  : 40 bytes (0x28)                              |
| entry[1]  : 40 bytes                                     |
| ...                                                      |
| entry[N-1]: 40 bytes                                     |
+----------------------------------------------------------+ 0x0058 + 40*N
| data blob 0 | data blob 1 | ... | data blob N-1          |
| (contiguous, in index order, no padding/gaps)            |
+----------------------------------------------------------+ name_pool_offset
| name pool: NUL-terminated strings, in index order        |
+----------------------------------------------------------+ EOF == pool_offset + pool_size
```

**Confidence: HIGH.** Every structural equation below was verified on all five
files; data regions are contiguous with zero gaps and zero overlaps in all of them.

---

## 3. Header (0x00 .. 0x57, 88 bytes)

| Offset | Size | Type | Meaning | Confidence |
|---:|---:|---|---|---|
| `0x00` | 4 | ASCII | magic `DWFB` | HIGH |
| `0x04` | 4 | u32 LE | format version, `2` in all files | HIGH |
| `0x08` | 64 | bytes | all zero in all five files; reserved / future fields | MEDIUM (observed only v2) |
| `0x48` | 4 | u32 LE | number of entries `N` | HIGH |
| `0x4C` | 4 | u32 LE | absolute offset of the name pool | HIGH |
| `0x50` | 4 | u32 LE | byte length of the name pool (sum of all name lengths incl. NULs) | HIGH |
| `0x54` | 4 | u32 LE | zero in all files; reserved | MEDIUM |

Evidence (`Data/Sprite.WAD`, `xxd -g1`):

```
00000000: 44 57 46 42 02 00 00 00 00 00 00 00 00 00 00 00  DWFB............
...
00000040: 00 00 00 00 00 00 00 00 18 00 00 00 9e 7a 0c 00  .............z..
00000050: 6a 01 00 00 00 00 00 00 c4 0d 00 00 9e 7a 0c 00  j............z..
00000060: 0e 00 00 00 18 04 00 00 9e 60 00 00 00 00 00 00  .........`......
```

`0x48` = `0x18` = 24 entries; `0x4C` = `0xC7A9E`; `0x50` = `0x16A` = 362;
`0xC7A9E + 362 = 0xC7C08` = exact file size. `0x54` = 0.

---

## 4. Entry record (40 bytes each, from `0x58`)

| Rel. | Abs. (entry 0) | Size | Type | Meaning | Confidence |
|---:|---:|---:|---|---|---|
| `+0x00` | `0x58` | 4 | u32 LE | unknown per-entry id/hash (`hash_id`), see §8 | LOW (semantics) / HIGH (layout) |
| `+0x04` | `0x5C` | 4 | u32 LE | absolute `name_offset` into name pool | HIGH |
| `+0x08` | `0x60` | 4 | u32 LE | `name_length` **including the terminating NUL** | HIGH |
| `+0x0C` | `0x64` | 4 | u32 LE | absolute `data_offset` | HIGH |
| `+0x10` | `0x68` | 4 | u32 LE | `data_size`, stored byte count | HIGH |
| `+0x14` | `0x6C` | 4 | u32 LE | flags: `0` = stored raw; `4` = compressed KMSH payload | HIGH (value) / MEDIUM (meaning) |
| `+0x18` | `0x70` | 4 | u32 LE | uncompressed size; `0` when flags = 0 | HIGH |
| `+0x1C` | `0x74` | 4 | u32 LE | always 0 | HIGH (observation) |
| `+0x20` | `0x78` | 4 | u32 LE | always 0 | HIGH (observation) |
| `+0x24` | `0x7C` | 4 | u32 LE | always 0 | HIGH (observation) |

Entry *i* starts at `0x58 + 40*i`.

Example — `Sprite.WAD` entry 0 (bytes at `0x58`):

```
c4 0d 00 00  -> hash_id 3524
9e 7a 0c 00  -> name_offset 0xC7A9E
0e 00 00 00  -> name_length 14
18 04 00 00  -> data_offset 0x418
9e 60 00 00  -> data_size 24734
00 00 00 00  -> flags 0
00 00 00 00  -> uncompressed size 0
00 00 00 00 / 00 00 00 00 / 00 00 00 00 -> reserved zeros
```

At `0xC7A9E` the pool contains `54 72 61 70 49 63 6f 6e 73 2e 73 70 72 00`
= `"TrapIcons.spr\0"` — 13 characters + NUL = 14, exactly `name_length`.
At `0x418` the payload starts with `50 53 46 42` (`"PSFB"`), a sprite-atlas magic.

**Record-boundary ambiguity.** The last three u32 fields are always zero, and the
u32 at `0x54` is also always zero. An equally consistent model treats the header
as 0x54 bytes and each record as starting at `0x54` with a leading zero u32
(`+0x00 = 0`, `+0x04 = hash_id`, `+0x08 = name_offset`, …, `+0x1C = uncompressed
size`, `+0x20 = +0x24 = 0`). Only version-2 files are available; no field was
observed to distinguish the two alignments. The decoder uses the `0x58 / 40-byte`
model because it makes the first data offset fall exactly at the end of the index.

---

## 5. Name pool

- Starts at `name_pool_offset`, length `name_pool_size`, ends exactly at EOF.
- Contains `N` NUL-terminated byte strings in **the same order as the entries**.
- `name_length[i]` counts the NUL; `sum(name_length[i]) == name_pool_size`
  verified on all five WADs.
- Names are ASCII in practice; the decoder decodes them as Latin-1 to preserve
  every byte. Backslash is the path separator (e.g.
  `GUI\Creatures\Evil\warlock.png`). Case is significant and preserved
  (`MapBack.PNG`, `EnginePath67.KCS`).
- `FrontEnd.WAD` contains 31 duplicated names; an extractor must disambiguate
  output paths (the supplied tool appends `__<index>` to the stem).

Tail of `Sprite.WAD` (`xxd -g1 -s 818104`):

```
000c7be8: 41 6e 64 54 72 61 70 49 63 6f 6e 73 2e 73 70 72  AndTrapIcons.spr
000c7bf8: 00 50 69 63 6b 41 78 65 54 61 67 2e 70 6e 67 00  .PickAxeTag.png.
```

---

## 6. Data region and governing equations

With `N = entry_count`, `E = 0x58 + 40*N` (end of index):

```
data_offset[0]      = E
data_offset[i+1]    = data_offset[i] + data_size[i]        (0 <= i < N-1)
name_pool_offset    = data_offset[N-1] + data_size[N-1]
file_size           = name_pool_offset + name_pool_size
name_length[i]      = strlen(pool[name_offset[i] - name_pool_offset]) + 1
```

All five equations hold exactly for all five WADs. Consequence: the payload byte
range for entry *i* is `[data_offset[i], data_offset[i] + data_size[i])` and its
content can be extracted byte-for-byte with a single slice of the file.

**Observed payload types** (by magic/extension):

| WAD | Payload types |
|---|---|
| `EngineTextures.WAD` | 102 × PNG (all begin `89 50 4E 47 0D 0A 1A 0A`) |
| `Sprite.WAD` | 15 × PNG, 9 × `PSFB` sprite atlas |
| `Paths.WAD` | 80 × KCS binary path data (`EnginePathNN.KCS`), 1 × `.txt` |
| `FrontEnd.WAD` | 371 × PNG, 42 × BMP, 29 × `.444`, 1 × GIF, 1 × PSD, 1 × `.dat` |
| `Meshes.WAD` | 2015 × compressed KMF (`flags = 4`) |

The KCS blobs start with a u32 element count followed by little-endian float
triples, e.g. `Sprite`/`Paths` entry 0: `aa 00 00 00 00 00 00 00 ... 74 f7 c4 be
5f da be 3f ...` — 0xAA = 170 records, then float3 values. (Inner KCS semantics
were not fully reverse-engineered.)

---

## 7. Compression (Meshes.WAD) — partially determined

Every one of the 2 015 mesh entries has `flags = 4`, `uncompressed_size > 0`, and
a 6-byte wrapper before an embedded KMF file:

```
10 fb <u8 variant> <u16 LE uncompressed_size> e3 | "KMSH" | <u32 LE uncompressed_size> | compressed body ...
```

Verification: in **2015 / 2015** mesh entries, bytes `[6:10]` are exactly `KMSH`
and the u32 at payload offset `10` equals the entry's `uncompressed_size`
field. Byte 2 of the wrapper is `00` in 1839 entries, `01` in 144, `02` in 29,
`03` in 2 and `05` in 1 — likely a sub-variant/chunk count. Byte 5 is `0xE3` in
every entry. Observed compression ratios `stored/uncompressed` range from
0.201 to 0.798.

Evidence (`Meshes.WAD` entry 0, payload at `0x13B30`; header at `0x58`):

```
00000058: cd 15 00 00 0e 24 e8 01 15 00 00 00 30 3b 01 00
00000068: 0f 16 00 00 04 00 00 00 27 53 00 00 00 00 00 00
   -> hash_id 5581, name "g_Maiden-Torture.kmf" (21 bytes), data 0x13B30,
      stored 5647, flags 4, uncompressed 21287 (0x5327)

00013b30: 10 fb 00 53 27 e3 4b 4d 53 48 27 53 00 00 11 00
00013b40: 00 00 48 45 41 44 01 07 10 01 03 02 01 03 01 e0
00013b50: 4d 41 54 4c 01 08 12 0d 0b 03 02 07 32 5b e3 47
   -> wrapper 10 fb 00 53 27 e3, "KMSH", u32 0x5327 = 21287,
      then chunk tags "HEAD", "MATL", ...
```

The body contains clear-text chunk tags (`HEAD`, `MATL`, `SPRS`, `SPHD`, `POLY`,
`EOM`, `VE`, `RT`, `IT`, `AB`, `OM`) interleaved with bytes in the `0xE0..0xE4`
range that appear to be LZ control tokens (e.g. `e3` immediately before the
literal `"DefaultEnvMap\0"`). This strongly suggests a custom token/LZ scheme,
**not** zlib/LZ77-with-header. The algorithm was **not** determined; the supplied
tool therefore extracts the payload **verbatim** (raw compressed blob) and labels
it accordingly. `DKII.EXE` contains the strings `unknown compression method`,
`Decompression error` and `.?AVCEngineCompressedSurface@@`, but no usable symbol
or table for this codec was identified.

**Confidence:** HIGH that flags=4 marks compression and that `uncompressed_size`
is the output size; LOW on the exact codec.

---

## 8. The unknown field at `+0x00` (`hash_id`)

- 32-bit value, observed range over all 2 222 entries: **1 107 .. 11 219**,
  mean ≈ 4 322. It is *not* uniformly distributed over 16 bits, so it is not a
  conventional CRC32/CRC16/FNV/djb2 hash of the name or of the payload.
- Tested and rejected: CRC32 (low/high 16 bits) of name and of data, Adler32,
  djb2 with seeds 0/1/5381, FNV-1a, simple byte sums with small multipliers,
  rotations, and a broad linear `h = h*m + c` sweep over common multipliers and
  case variants.
- It is stable per entry and remains unexplained. **Preserve it verbatim; do not
  use it as a filename or an index.**

**Confidence:** HIGH that it is an opaque identifier; LOW on what it means.

---

## 9. Cross-checks with sibling files

- `DK2TextureCache/EngineTextures.dir`: magic `"TCHC"`, u32 `0x21D70` = its own
  file size (138 608), u32 `4`, u32 `0x1687` (5 767), followed by records
  `name\0 + u32 offset` pointing into `EngineTextures.dat`. Names match the WAD
  conventions (e.g. `GUI\C-Spells\c-windMM2`). This is a *separate* decoded
  texture-cache index and not part of the DWFB container; it corroborates the
  naming convention only.
- `DKII.EXE` strings confirm the game uses these exact WAD filenames
  (`%sdata\EngineTextures.wad`, `%sdata\FrontEnd.wad`, `%sdata\Meshes.Wad`,
  `%sdata\Paths.wad`, `%sdata\Sprite.Wad`), the `DWFB` signature and a class
  named `TbWadFileStorage`.
- `Data/EngineTextures/*.png` are already-extracted textures; entries such as
  `MapRock.png` begin with the standard PNG signature and IHDR, confirming that
  uncompressed WAD payloads are complete, standalone image files.

---

## 10. Explicit unknowns / limitations

1. Meaning of the 64-byte reserved header block (`0x08..0x47`) and of the u32 at
   `0x54`; all are zero in version 2 only.
2. Exact algorithm of `hash_id` at entry `+0x00` (§8).
3. The record-boundary ambiguity described in §4 (header 0x58 vs 0x54).
4. The exact mesh compression codec and the meaning of wrapper byte 2 and byte
   `0xE3` (§7). Only the container-level metadata is reliable.
5. Inner structures of KMF, KCS, `PSFB` and `.444` payloads were not decoded.
6. Version 1 (or any other version) WADs were not available; the decoder assumes
   version 2 and warns on other versions.
7. Name encoding is assumed Latin-1/ASCII; no non-ASCII names occur in the five
   samples, so code-page handling is unverified.

---

## 11. Reproduction

```bash
cd "/home/kali/Desktop/rea/Dungeon Keeper 2"
python3 port/tools/wad_tool.py list Data/EngineTextures.WAD --json
python3 port/tools/wad_tool.py extract Data/EngineTextures.WAD /tmp/out 'MapRock.png'
python3 port/tests/test_wad.py
```

The tool is stdlib-only. Extraction always writes the unmodified stored bytes;
for `Meshes.WAD` these are the compressed KMSH blobs, explicitly labelled as
such in `--json` output.
