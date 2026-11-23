# KMSH codec — SOLVED (historical report below)

**UPDATE 2026-11-23:** the codec HAS been recovered. See
`docs/format-research/KMSH_FORMAT.md` and `tools/kmsh_decompress.py`.
The algorithm was ported from OpenKeeper `WadFile.decompressFileData` and
dk2-tools `Scripts/wad_extract.1sc`. All 2015 meshes decompress to exact-size
valid KMF files; 85 project tests pass. The "UNSOLVED" verdict in the old
report below is superseded; it is kept only as a provenance record.

---

# KMSH codec — derivation report (UNSOLVED, second pass)

Target: the compressed mesh payloads in `Data/Meshes.WAD` (Dungeon Keeper 2,
GOG build, 1999). Work was confined to `port/`. No code from OpenKeeper or any
fan engine was used. Format descriptions from public documentation (010-Editor
binary templates, Compression wiki pages) were read only to establish target
structure and candidate algorithms; none were copied into the deliverable.

**Verdict: the exact codec is STILL NOT recovered.** A new, concrete candidate —
**Rob Northen Compression (RNC), the Bullfrog House codec** — was identified and
tested against four independent reference implementations, but none decoded any
of the 2015 bodies. In accordance with the task rules, nothing is fabricated:
`tools/kmsh_decompress.py` remains an honest envelope reader/raw extractor.

---

## 1. What the bytes prove (unchanged from pass 1)

All 2015 payloads share this envelope:

```
10 fb <variant:u8> <u16 BE size> <trailer:u8> "KMSH" <u32 LE size> <body...>
```

* `10 fb 00`: 2015/2015; variant `00`:1839, `01`:144, `02`:29, `03`:2, `05`:1.
* u16 at +3 (big-endian) = declared size low 16 bits: 2015/2015.
* u32 at +10 = WAD entry's `uncompressed_size`: 2015/2015.
* trailer `e3`:2008, `e1`:7. The seven `e1` entries are 151, 345, 355, 448,
  851, 1122, 1667; their body prefixes differ from the 2008 normal bodies.
* body starts at payload offset 14; normal bodies begin `11 00 00 00 48 45 41 44`
  (version 17 + literal `"HEAD"`).

Aggregate statistics over the compressed streams (payload offset 6..end,
31 898 020 bytes):

| quantity | value |
|---|---|
| total declared uncompressed bytes | 53 622 297 |
| bytes `< 0xe0` | 29 279 246 (91.8 %) |
| bytes `>= 0xe0` | 2 618 774 (8.2 %) |
| implied output per high byte, if low bytes are 1:1 literals | 9.30 |

Byte-frequency scan shows a sharp discontinuity at `0xdf -> 0xe0`
(`0xdf`: 32 671 occurrences, `0xe0`: 433 143). All 32 high bytes are present,
so this is a structured alphabet, not a single escape byte.

---

## 2. NEW: the RNC hypothesis

### 2.1 Why RNC was investigated

Public format references state that Rob Northen Compression ("RNC ProPack",
1991) is used by Bullfrog games, explicitly including Dungeon Keeper:
* ModdingWiki, *Rob Northern Compression*: "used by several Bullfrog games
  including Magic Carpet, Theme Hospital, and Dungeon Keeper".
* CorsixTH wiki, *RNC*: "Many of the original Theme Hospital data files
  (along with Dungeon Keeper data files) are compressed using ... RNC".
* nviewer wiki, *RNC methods 1 and 2*: full C decoder for methods 1 and 2.

The observed streams are a plausible match in *shape*: RNC1 is a Huffman-coded
bitstream interleaved with verbatim literal bytes, which explains why clear-text
KMF chunk tags (`HEAD`, `MATL`, `SPRS`, ...), mesh names (`cyllinder`,
`DefaultEnvMap`, `G_Torture_PotG_t`) and long runs of triangle-index bytes
appear unmodified in the compressed data while other data expands ~1.7x.

### 2.2 RNC reference algorithm (from the public specs)

Standard RNC file: 18-byte header (`"RNC"` + method 1/2 + BE32 decoded size +
BE32 packed size + BE16 decoded CRC + BE16 packed CRC + 2 reserved bytes),
then:

* method 1: LSB-first 16-bit bit words; per-block three canonical Huffman
  tables (5-bit count, 4-bit code lengths); each tuple = Huffman(raw literal
  count) + raw bytes + Huffman(distance-1) + Huffman(length-2), with a
  32 KiB overlapping LZ window.
* method 2: MSB-first control bits; literal / 2-byte-match / longer match /
  raw-run (12..72) tokens.

The DK2 payload has **no `"RNC"` signature and only a 6-byte wrapper**, so the
hypothesis requires the game to use a stripped/custom header.

### 2.3 Exact tests performed

Four independent decoders were applied to the smallest body (entry 851,
`Group_imparms.kmf`, 77-byte stream, declared 104) and to entries 0 and 1:

1. In-house Python port of the nviewer RNC1 decoder (16-entry tables),
   candidate stream starts 0, 5, 6, 14 and flag-bit skips 0..3 — no stream
   produced `outlen == declared` or a sane KMF prefix.
2. Same with 18-entry tables (CorsixTH value range 0..17) — no hit.
3. In-house Python port of the nviewer RNC2 decoder — `bad=True` immediately.
4. Brute-force over stream starts 0..41 × flag skips 0..3 for both methods —
   no hit.
5. `propack` (PyPI RNC ProPack implementation) with a synthesized standard
   header and correct packed CRC for stream starts 0, 6, 7, 14 and methods
   1/2: errors `failed to decode huffman value`, `invalid match offset`,
   `file is locked`, `file is encrypted but no key provided`. Forcing the
   first two flag bits to zero/one in both bit orders also failed.

Trace of the most plausible case (entry 851, stream at offset 6, method 1):
the first Huffman table decoded 9 used symbols, but the second and third
tables decoded **zero** symbols, and the first "literal run" was 8 bytes
followed by an immediate out-of-range back-reference. This is a hard mismatch,
not a near miss.

**Conclusion:** the data is *not* standard RNC1/RNC2 at any of the obvious
offsets. Either DK2 uses a modified RNC variant (different table layout,
pre-shared tables, or an extra transform) or the codec is a different Bullfrog
scheme. RNC remains the best *lead*, not a result.

---

## 3. NEW: the decompiled-code investigation (Ghidra 12.2 export)

Export: `re/ghidra/export_summary.txt` →
`functions=8260 decompiled=8260 failed=0 heavy=3`.
Decompiled C in `re/ghidra/decomp/<address>.c`, ASM in `re/ghidra/asm/`.

### 3.1 The chunk-tag registry and the compression-method selector

The string table at VA `0x6be130` (file offset `0x2bc730`) was dumped directly
from `DKII.EXE`:

```
HEAD\0\0\0\0 KMSH\0\0\0\0 TEXTURE_SET\0 MAT2\0\0\0\0 MATL\0\0\0\0
GEOM\0\0\0\0 SPHD\0\0\0\0 SPRS\0\0\0\0 CTRL\0\0\0\0 MESH\0\0\0\0
VGEO\0\0\0\0 ITAB\0\0\0\0 VERT\0\0\0\0 POLY\0\0\0\0 ANIM\0\0\0\0
ELEM\0\0\0\0 GROP\0\0\0\0
```

`DAT_006be130 = "HEAD"`, `DAT_006be138 = "KMSH"`. The **only** decompiled
function referencing `DAT_006be138` is `FUN_0057d270` (verified with
`grep -rl 006be138 decomp/`). Its relevant tail:

```c
uVar4 = FUN_0059a500(local_400);
FUN_0057a800(uVar4,&DAT_006be138,local_404);
FUN_0057a1e0(&DAT_006be130);
iVar2 = *(int *)(DAT_00766648 + DAT_0076622c);
DAT_0076622c = DAT_0076622c + 8;
FUN_0057a310();
piVar8 = (int *)0x0;
if (iVar2 == 1) { FUN_0057d390(param_3); piVar8 = (int *)FUN_0057d5b0(); }
else if (iVar2 == 2) { FUN_0057d390(param_3); piVar8 = (int *)FUN_0057dad0(); }
else if (iVar2 == 3) { piVar8 = (int *)FUN_0057e110(); }
FUN_0057a150();
(**(code **)(*piVar8 + 8))(param_2);
return piVar8;
```

Interpretation: `FUN_0057d270` is the mesh *object builder*. It opens a KMSH
chunk, reads a u32 `iVar2` from the decompressed stream buffer
(`DAT_00766648` + cursor `DAT_0076622c`), skips 8 bytes, and dispatches to one
of three **KMF layout parsers**, not to codecs:

| selector | function | behaviour (from decompiled C) |
|---|---|---|
| 1 | `FUN_0057d5b0` | reads name, counts, tri-strip sizes (`uVar5 = iVar7 * 3`), vertex arrays, polygon tables; allocates via `FUN_0057ceb0`/`FUN_0057e860` |
| 2 | `FUN_0057dad0` | similar, different field widths (`0x12`-stride records, `uStack_...` 2-byte fields); allocates via `FUN_0057e2a0` |
| 3 | `FUN_0057e110` | group/anim loader; reads child mesh names, calls `FUN_0057ef30` per element |

These functions **consume** an already-decompressed IFF-style stream; none of
them contains a byte-decompression loop.

### 3.2 Where the decompression must live

`FUN_0057ef30` (caller of `FUN_0057d270`) does:

```c
iVar1 = FUN_0058df40(param_1);            /* lookup mesh by hashed name */
if (iVar1 < 0) {
  puVar2 = (undefined4 *)FUN_0057ceb0(0x18);
  uVar3 = FUN_0058de00(param_1,puVar2);   /* register name in mesh table */
  if (param_2 != '\0') { uVar3 = FUN_0057d270(param_1,param_3,param_4); ... }
  ...
}
```

`FUN_0059a500` (setting up the stream) ends with a virtual call:

```c
(**(code **)(*DAT_00795700 + 0x118))(local_104);
```

`DAT_00795700` is the FileSystem/FileManager object (RTTI names present in the
EXE: `.?AVTbWadFileStorage@FileSystem@@`, `.?AVCFileManager@@`,
`.?AVCIFFFile@@`). The WAD-read + decompression logic therefore lives in that
virtual method's implementation, **not** in the mesh-manager functions. A grep
of all 8260 decompiled files for `DAT_00766648`/`DAT_0076622c` returns only the
five mesh functions above; the producer of the decompressed buffer is reached
through the virtual call and is not named in the decompilation.

### 3.3 Shape-based searches that found no decompressor

* `grep -rln "006be138" decomp/` → only `0057d270.c`.
* `grep -rnE "0xe0\s*<=|<=?\s*0xe0|0xe0\s*<|>\s*0xe0" decomp/` → **zero hits**:
  no function compares a byte against `0xe0`. The high-byte alphabet is
  therefore detected by a table, by bit tests, or is part of a bitstream —
  consistent with an entropy coder (RNC) and inconsistent with the simple
  `if (c >= 0xe0)` LZ decoders assumed in pass 1.
* `grep -rnE "0x10fb|0xfb10" decomp/` → only unrelated constants; no function
  checks the `10 fb` wrapper signature as an immediate.
* `grep -rn "a001"` in `asm/` → 8 files, all unrelated (CRC usage elsewhere;
  no RNC-header CRC check in the mesh path).
* `strings -a DKII.EXE | grep -i rnc` → no RNC-identifying string (RNC decode
  is likely inlined/static in the file-storage layer).
* The strings `Decompression error` (VA `0x6c436c`) and
  `unknown compression method` (VA `0x6c4a2c`) belong to libpng inflate code,
  as established in pass 1; their xrefs do not lead to the mesh codec.

### 3.4 Other functions read during this pass

`0057a800`, `0057a1e0`, `0057a150`, `0057a310`, `0057a3a0`, `0057d390`,
`0057d5b0`, `0057dad0`, `0057e110`, `0057e2a0`, `0057ef30`, `0057ed30`,
`0058df40`, `0058de00`, `0059a500`, `0059a4b0`, `0059a570`, `0059a600`,
`0059a630`, `0055c020`, `0055be80`, `0057b0e0`, `0057b6d0`, `005b3eb0`,
`005b3dd0`, `005b3ce0`, `00598880`. None contains a byte decompressor; they
are mesh/resource bookkeeping, IFF chunk navigation, rendering, and file-system
wrappers.

---

## 4. Pass-1 negative results (still valid)

Fixed byte→output-length substitution (256 and 32-unknown least squares):
mean |error| 1545 / 2944 bytes, **0/2015** exact. PackBits, LZ4, zlib/raw
deflate at offsets 0/6/14, `0xE0+n` literal substitution, repeat-previous-byte,
~100 LZ parameterisations, and a 414 720-point C brute force over LZ token
layouts (length from token bits or following byte; distance from token bits,
1 or 2 following bytes, byte/word units; overlapping and non-overlapping copy)
— **zero** bodies decoded to the declared length.

---

## 5. What a future decoder must satisfy

1. `len(output) == declared_size` exactly for all 2015 bodies.
2. Output starts `11 00 00 00 48 45 41 44` for the 2008 normal bodies; explain
   the seven `e1`-trailer anomalies (151, 345, 355, 448, 851, 1122, 1667).
3. Output parses as KMF per the 010 templates: `KMSH` → `HEAD` → `MATL` →
   `MESH`/`ANIM`/`GROP`, with consistent u32 chunk sizes.
4. Reproduce literal strings such as `DefaultEnvMap`, `cyllinder`, mesh names.
5. Explain the `0xe0`-before-4-byte-tag and `0xe1`/`0xe2`-before-name patterns.

**Most promising next steps:**
* Locate the virtual method at vtable slot `0x118` of the `DAT_00795700`
  FileSystem object (RTTI `TbWadFileStorage`); that method reads the WAD entry
  and fills `DAT_00766648`. Static vtable recovery from the data section, or a
  debugger breakpoint on the virtual call at `FUN_0059a500+0x4c`, will expose
  the actual decompressor.
* Alternatively, run the game (or a faithful loader harness) and dump the
  decompressed KMF from memory for one mesh, then align it against the raw
  body — this supplies the known plaintext that all statistical attempts
  lacked.

---

## 6. Reproduction — exact commands and results

Run from `/home/kali/Desktop/rea/Dungeon Keeper 2`. Analysis scripts are in
`port/tmp/` (not part of the shipped tool).

```bash
# statistics + envelope
python3 - <<'PY'
# totals: 53 622 297 uncompressed / 31 898 020 compressed; high bytes 2 618 774
PY

# grep the Ghidra export
grep -rl 006be138 port/re/ghidra/decomp/                  # -> 0057d270.c only
grep -rnE "0xe0\s*<=|<=?\s*0xe0" port/re/ghidra/decomp/   # -> no hits

# LZ brute force
gcc -O2 -o port/tmp/bf port/tmp/bf.c && ./port/tmp/bf     # tested 414720, found 0

# RNC reference decoders
python3 port/tmp/rnc_test.py        # in-house RNC1/RNC2: all bad
python3 port/tmp/rnc_bf.py          # start offsets 0..41 x flags: no hit
python3 -m venv port/tmp/venv --system-site-packages
port/tmp/venv/bin/pip install propack
port/tmp/venv/bin/python port/tmp/propack_test3.py        # done, no hit

# shipped honest tool
python3 port/tools/kmsh_decompress.py inspect Data/Meshes.WAD --limit 5
python3 port/tools/kmsh_decompress.py decompress Data/Meshes.WAD \
        port/tmp/raw --limit 5
python3 port/tests/test_kmsh.py
```

---

## 7. Confidence

| Claim | Confidence |
|---|---|
| Envelope layout, duplicated size fields, wrapper byte meanings (except variant semantics) | HIGH (2015/2015) |
| Body starts at payload offset 14; version is 17 for 2008 bodies | HIGH (direct bytes) |
| Clear-text tags/names are literal bytes, not entropy-coded | HIGH |
| `FUN_0057d270` selects KMF *layout parsers*, not the codec | HIGH (decompiled C) |
| The decompressor is reached via the FileSystem virtual method at slot `0x118` | MEDIUM (call graph) |
| RNC is the codec family | MEDIUM–LOW (public DK2 usage + stream shape; all direct decode tests failed) |
| Exact decompression algorithm | NONE (unresolved) |
| Seven `e1` anomalies are codec variants rather than corruption | MEDIUM |
