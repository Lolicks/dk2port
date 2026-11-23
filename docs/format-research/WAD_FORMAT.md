# DWFB (`.WAD`) archive specification — source-cited research

Scope: the five `DWFB` archives shipped with *Dungeon Keeper 2* in this checkout
(`Data/{EngineTextures,FrontEnd,Meshes,Paths,Sprite}.WAD`). This document was
written by **reading the referenced source files**, not from README summaries,
and every structural/codec claim carries a `path:line` citation. Statements that
could be checked against real bytes were checked; the commands and their output
are in §8. Where sources disagree, the disagreement is recorded rather than
smoothed over.

Paths are relative to the repository root. Line numbers are 1-based and refer to
the files as they exist in this checkout (verified clean:
`git -C port/research/OpenKeeper status --short` and
`git -C port/research/dk2-tools status --short` both produced no output; HEADs
`e2bfe95d…` and `b4e09728…`).

## 0. Source inventory

| Source | Lines | Role |
|---|---:|---|
| `port/research/dk2-tools/Formats/Archives/wad_struct.bt` | 58 | 010 Editor template: header, entry table, name pool view, flag enum |
| `port/research/dk2-tools/Scripts/wad_extract.1sc` | 178 | 010 Editor script: extraction + complete decoder `decompressFileData` |
| `port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/wad/WadFile.java` | 386 | Java reader/extractor + independent decompressor port |
| `port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/wad/WadFileEntry.java` | 148 | Entry POJO, flag enum, identity |
| `port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/WadExtractor.java` | 56 | CLI wrapper; hardcodes `Meshes.WAD` |
| `port/docs/WAD_FORMAT.md` | 289 | Our earlier byte-level spec (container only; its §7 codec claim is now superseded) |
| `port/docs/wad_inventory.json` | 182 | Our generated per-WAD inventory |
| `port/tools/wad_tool.py` | 315 | Our stdlib reader/extractor (raw payloads only) |
| `port/tools/kmsh_decompress.py` | 400 | Our stdlib decoder, ported from the two sources above |
| `port/tests/test_wad.py`, `port/tests/test_kmsh.py` | 157 / — | Our executable checks |

The 010 template header says "World Asset Directory Structure" and declares
`CompressType { UNCOMPRESSED = 0, COMPRESSED = 4 }`
(`port/research/dk2-tools/Formats/Archives/wad_struct.bt:12-13`). The Java
reader's own Javadoc says the file is little-endian and that the code was
converted from C by Tomasz Lis and an anonymous author
(`WadFile.java:40-48`). All integers below are unsigned little-endian unless
explicitly marked otherwise.

## 1. Header (88 bytes, `0x00..0x57`)

Sources: `wad_struct.bt:15-31` (`struct WadHeader`), `WadFile.java:68-90`
(`WadFile(Path file)`), `port/tools/wad_tool.py:84-99` (`WadFile._parse`),
`port/docs/WAD_FORMAT.md:56-79`.

| Offset | Size | Type | Meaning | Citation |
|---:|---:|---|---|---|
| `0x00` | 4 | ASCII | magic `DWFB` (`44 57 46 42`) | `wad_struct.bt:13,16-20`; `WadFile.java:55,71-75`; `wad_tool.py:22,89-90` |
| `0x04` | 4 | u32 LE | version, must be `2` | `wad_struct.bt:21-25`; `WadFile.java:56,78-81`; `wad_tool.py:91-96` |
| `0x08` | 64 | bytes | reserved/zero in v2 | `wad_struct.bt:26`; `WadFile.java:84` skips straight to `0x48`; `wad_tool.py:98-99` reads no field there; `docs/WAD_FORMAT.md:62,264-265` (observation) |
| `0x48` | 4 | u32 LE | entry count `N` | `wad_struct.bt:27`; `WadFile.java:84,87`; `wad_tool.py:98` |
| `0x4C` | 4 | u32 LE | absolute name-pool offset | `wad_struct.bt:28`; `WadFile.java:88`; `wad_tool.py:98` |
| `0x50` | 4 | u32 LE | name-pool byte length | `wad_struct.bt:29`; `WadFile.java:89`; `wad_tool.py:98` |
| `0x54` | 4 | u32 LE | reserved/zero in v2 (read but unused in both readers) | `wad_struct.bt:30`; `WadFile.java:90` names it `unknown`; `wad_tool.py:98-99` names it `header_reserved` |

Important alignment note: the 010 template models the 64 reserved bytes and
then reads `files/nameOffset/namesSize/unk2` consecutively from `FTell()`
(`wad_struct.bt:26-31`); Java explicitly seeks `0x48` first
(`WadFile.java:84`). Both agree on `0x48/0x4C/0x50/0x54`. Our earlier doc records
an alternative `0x54`-header alignment that is indistinguishable with only v2
files (`port/docs/WAD_FORMAT.md:117-124`); nothing in the two implementations
resolves it, so it remains an honest unknown.

Version checks differ: the template prints an error and `return -1`
(`wad_struct.bt:22-25`), Java throws `RuntimeException`
(`WadFile.java:79-81`), our Python tool only appends a warning and continues
with the v2 layout (`wad_tool.py:92-96`).

## 2. Entry table (`0x58 + 40*i`, 40 bytes per entry)

Sources: `wad_struct.bt:33-54` (`struct WadEntry`), `WadFile.java:93-110`,
`WadFileEntry.java:29-52`, `port/tools/wad_tool.py:108-127`,
`port/docs/WAD_FORMAT.md:83-98`.

| Rel. | Size | Type | Meaning | Citation |
|---:|---:|---|---|---|
| `+0x00` | 4 | u32 | opaque per-entry id (our doc calls it `hash_id`; semantics unknown) | `wad_struct.bt:34` (`unk1 // index? sequence?`); `WadFile.java:97` (`setUnk1`); `WadFileEntry.java:45`; `docs/WAD_FORMAT.md:228-240` |
| `+0x04` | 4 | u32 | absolute name offset into name pool | `wad_struct.bt:35,48-49`; `WadFile.java:98`; `WadFileEntry.java:46` |
| `+0x08` | 4 | u32 | name length including terminating NUL | `wad_struct.bt:36,49`; `WadFile.java:99`; `docs/WAD_FORMAT.md:89,113-114` |
| `+0x0C` | 4 | u32 | absolute payload offset | `wad_struct.bt:37,50-51`; `WadFile.java:100`; `WadFileEntry.java:48` |
| `+0x10` | 4 | u32 | stored payload byte count | `wad_struct.bt:38,51`; `WadFile.java:101`; `WadFileEntry.java:49` |
| `+0x14` | 4 | u32 enum | compression flag: `0` raw, `4` compressed | `wad_struct.bt:12,39-42`; `WadFile.java:102`; `WadFileEntry.java:29-44,94-100,123-125` |
| `+0x18` | 4 | u32 | uncompressed size; `0` when flag `0` | `wad_struct.bt:43`; `WadFile.java:103`; `WadFileEntry.java:51`; `docs/WAD_FORMAT.md:93` |
| `+0x1C` | 4 | u32 | reserved (always 0 in shipped v2) | `wad_struct.bt:44`; `WadFile.java:104-108`; `WadFileEntry.java:52` |
| `+0x20` | 4 | u32 | reserved (always 0 in shipped v2) | same |
| `+0x24` | 4 | u32 | reserved (always 0 in shipped v2) | same |

Java reads exactly 40 bytes per entry (`rawWad.readChunk(40 * files)`,
`WadFile.java:93`) and the template declares `entry[header.files]`
(`wad_struct.bt:54`). The template additionally seeks to each name and payload
to materialize `char name[nameSize]` and `ubyte file[compressedSize]`, then
restores the index position (`wad_struct.bt:46-52`). Our Python tool unpacks all
ten u32s with one `<10I` (`wad_tool.py:110-111`).

## 3. Name pool

- The pool starts at `+0x4C`, has `+0x50` bytes, ends exactly at EOF, and holds
  `N` NUL-terminated names in entry order
  (`wad_struct.bt:28-29,48-49`; `WadFile.java:112-133`;
  `docs/WAD_FORMAT.md:127-138`; verified in §8).
- Java resolves each name by
  `nameOffset[i] - name_pool_offset` and copies `name_size` bytes
  (`WadFile.java:115-121`), then converts `\` separators and, for a bare name
  with no separator, **inherits the previous entry's directory**
  (`WadFile.java:112-130`). The comment there is explicit: "The file names
  itself aren't unique, but with the path they are".
- Our tool decodes Latin-1, strips the trailing NUL, sanitizes path components,
  and refuses `..`/absolute escapes (`wad_tool.py:113-120,200-211,226-228`).

### Duplicate names — three different behaviours

1. **Java collapses them.** Entries are stored in a `LinkedHashMap` and
   `wadFileEntries.put(name, entry)` overwrites a previous equal key
   (`WadFile.java:117,132`); `getWadFileEntryCount()` therefore returns the map
   size, not the header's entry count (`WadFile.java:155-157`). Path inheritance
   (`WadFile.java:124-130`) can make otherwise equal bare names distinct keys.
2. **The 010 script has no de-duplication.** It loops `i = from .. limit` and
   derives the destination solely from `entry[i].name`
   (`wad_extract.1sc:32-50`), so repeated names map to the same output path;
   the script contains no index suffix logic.
3. **Our tool keeps every entry.** It iterates by index and appends
   `__<index>` to the stem on a case-insensitive output-path collision
   (`wad_tool.py:213-236`). Measured: `FrontEnd.WAD` has 31 duplicate names out
   of 445 entries; the other four archives have none (§8).

## 4. Flags `0` vs `4`

- `0` = `UNCOMPRESSED`/`NOT_COMPRESSED`: stored bytes are the complete payload.
  The 010 script copies `entry[i].file` verbatim (`wad_extract.1sc:54-56`);
  Java writes the bytes read at `data_offset` unchanged
  (`WadFile.java:252-256`).
- `4` = `COMPRESSED`: payload is a compressed KMSH/KMF blob. The script calls
  `decompressFileData(entry[i].file, entry[i].size, filename)` — note it passes
  the **entry's uncompressed-size field** as the expected size
  (`wad_extract.1sc:52-53`); Java checks `fileEntry.isCompressed()`
  (`WadFile.java:252-253`, `WadFileEntry.java:123-125`) and calls its own
  decompressor.
- The template only **prints** a warning for any other flag value and keeps
  parsing (`wad_struct.bt:40-42`); Java's enum conversion would fail on an
  unknown value (`WadFile.java:102`); our Python tool treats *any* non-zero
  flag as compressed (`wad_tool.py:53-55`), which is looser than both sources
  and should be tightened if a third flag ever appears.
- Measured distribution (§8): `0` in 102+445+81+24 entries, `4` in all 2015
  `Meshes.WAD` entries. No other value occurs in this checkout.

## 5. Compressed payload envelope

Every `flags=4` entry in `Meshes.WAD` starts with a 14-byte envelope
(`port/tools/kmsh_decompress.py:74-104`, `parse_payload`; corroborated by our
earlier byte survey in `port/docs/WAD_FORMAT.md:182-211`):

```
offset  0..1 : 10 FB                      (signature)
offset  2    : variant u8                 (script's "byte 2")
offset  3..4 : uncompressed size & 0xFFFF, BIG-ENDIAN u16
offset  5    : trailer (0xE3 in every shipped v2 mesh)
offset  6..9 : "KMSH"
offset 10..13: uncompressed size, little-endian u32
offset 14..  : remaining compressed bytes (body)
```

`kmsh_decompress.py` verifies `u16BE == u32 & 0xFFFF` and `data[12] == data[2]`
(`kmsh_decompress.py:90-99`). `iter_wad_payloads` additionally requires the KMSH
u32 to equal the WAD entry's `uncompressed_size` when flag is 4
(`kmsh_decompress.py:119-129`). **The earlier doc's claim that the codec was
undetermined (`port/docs/WAD_FORMAT.md:216-224`) is obsolete**; the algorithm is
given next and was verified on all 2015 entries.

## 6. The exact token algorithm (both independent sources agree)

Sources, line by line:

* `port/research/dk2-tools/Scripts/wad_extract.1sc:74-178` —
  `void decompressFileData(ubyte src[], uint size, string filename)`.
* `port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/wad/WadFile.java:292-385` —
  `private byte[] decompressFileData(byte[] src, String fileName)`.
* `port/tools/kmsh_decompress.py:148-244` — our Python port
  (`_ref`, `decompress_stream`).

### 6.1 Stream header / start offset 5

Both reference implementations do the same pointer dance:

```
i = 0
if ((src[i++] & 1) != 0): i += 3        # 1sc:82-84; Java:294-296
i++                                     # "skip second byte" — 1sc:85; Java:297
decsize = (src[i]<<16) | (src[i+1]<<8) | src[i+2]   # 1sc:88; Java:300
i += 3                                   # 1sc:94; Java:302
```

For the shipped envelopes `src[0] = 0x10` (bit 0 clear), so `i` ends at **2**;
the three size bytes are `src[2..4]` = big-endian `0x5327` for entry 0
(21287), and the first token byte is `src[5]`
(`kmsh_decompress.py:167-185`, whose two `i += 1` statements reproduce the
post-increment plus the explicit skip). Consequences, verified in §8:

* the size is read from **payload offset 2, big-endian**, not from the u32 at
  offset 10 (those two fields agree, which is why the confusion is harmless on
  shipped data);
* the token stream starts at **payload offset 5**; the `0xE3` trailer, the
  literal `"KMSH"` and the little-endian u32 at 10..13 are *not* a header to
  skip — they are consumed as literals by the first token. For entry 0 the
  first flag byte is `0xE3`: bits 7/6/5 are all set, so branch 4 runs with
  `(0xE3 & 0x1F)*4+4 = 16` literals, covering bytes 6..21.
* Because of this, `decompress_body()` (body starting at offset 14) is
  deliberately rejected: `kmsh_decompress.py:247-254`.

`decsize` is the exact output length. The 010 script checks it against its
`size` argument (i.e. the entry's `uncompressed_size`) and aborts on mismatch
(`wad_extract.1sc:88-92`). Java instead allocates `new byte[decsize]`
(`WadFile.java:300-301`) and never compares it with the entry field. Our port
compares the stream's 3-byte value with the envelope u32 and with the final
output length (`kmsh_decompress.py:184-185,242-244`).

### 6.2 Main loop and the `do/while` off-by-one

`flag = src[i++]`, then a four-way branch in this exact order
(`1sc:95-100`; `Java:306-311`; `Python:190-195`).

All literal loops are `while (counter-- != 0)`, i.e. **exactly `counter`
bytes** (`1sc:103-107,122-126,141-145,164-168`;
`Java:314-318,333-337,352-356,375-378`; `Python:201-204,213-216,226-229,237-240`).

All back-reference loops are

```c
do { dest[j] = dest[k++]; j++; } while (counter-- != 0);
```

(`1sc:114-117,132-135,152-155`; `Java:325-328,343-346,363-366`;
`Python:148-164` in `_ref`). The body executes **before** the test and the
test compares the value *before* decrement, therefore the number of copied
bytes is **`counter + 1`**. This is the single most important detail of the
codec; implementing it as a normal `for`/`while` loses one byte per back
reference and produces a short output. Refs are overlapping `dest[k++]`
copies, so they implement run/LZ semantics over the output buffer, not over
the input.

### 6.3 The four branches

`j` is the output index; `k` is the source index in the output buffer. All
extra bytes are unsigned.

| # | Flag test (evaluated in order) | Extra bytes | Literal count | Back-ref distance `k` | Back-ref length (do/while ⇒ +1) | Citation |
|---:|---|---:|---:|---|---:|---|
| 0 | `(flag & 0x80) == 0` | `tmp` (1) | `flag & 3` (0..3) | `j - ((flag & 0x60) << 3) - tmp - 1` | `((flag >> 2) & 7) + 2 + 1` = **3..10** | `1sc:100-117`; `Java:311-328`; `Python:195-206` |
| 1 | else `(flag & 0x40) == 0` | `tmp, tmp2` (2) | `tmp >> 6` (0..3) | `j - ((tmp & 0x3F) << 8) - tmp2 - 1` | `(flag & 0x3F) + 3 + 1` = **4..67** | `1sc:118-135`; `Java:329-346`; `Python:207-218` |
| 2 | else `(flag & 0x20) == 0` | `a, b, d` (3) | `flag & 3` (0..3) | `j - ((flag & 0x10) << 12) - (a << 8) - b - 1` | `d + ((flag & 0x0C) << 6) + 4 + 1` = **5..452** | `1sc:136-155`; `Java:347-366`; `Python:219-231` |
| 3 | else (bits 7,6,5 all set) | none | `(flag & 0x1F)*4 + 4`; if `> 0x70` → terminal, then `flag & 3` literals and stop | — | — | `1sc:156-169`; `Java:367-379`; `Python:232-240` |

Notes on branch 3: the 010 script casts the counter to `ubyte`
(`1sc:157`) whereas Java casts the int to `byte` then to unsigned
(`Java:369`); for the attainable range 4..128 both yield the same comparison
with `0x70`. The terminal threshold is therefore `(flag & 0x1F) >= 28`
(116 > 112). There is exactly one terminal token per payload (measured: 2015
in 2015 payloads, §8). A block counter of `i >= sizeof(src)` breaks the loop
in both references (`1sc:96-98`; `Java:307-309`) but is treated as failure, not
success.

## 7. Error handling for corrupt entries — honest comparison

| Situation | 010 template / script | OpenKeeper Java | Our Python |
|---|---|---|---|
| Bad magic / version | `Printf` + `return -1` (`wad_struct.bt:17-25`); extractor aborts if `RunTemplate < 0` (`wad_extract.1sc:11-14`) | `RuntimeException` (`WadFile.java:73-81`) | `WadError` for magic, warning for version (`wad_tool.py:87-96`) |
| Unrecognised flag | warning only, keeps parsing (`wad_struct.bt:40-42`) | enum conversion fails (`WadFile.java:102`) | treats any non-zero as compressed (`wad_tool.py:53-55`) |
| Name/data range outside file | template seeks blindly; no explicit range check | unchecked; `IOException` wrapped as `RuntimeException` (`WadFile.java:134-138`) | explicit `WadError` (`wad_tool.py:113-122`) |
| `decsize != size` | prints "Uncompressed size error" and returns `-1` (`wad_extract.1sc:88-92`) | no comparison; allocates from stream's value (`WadFile.java:300-301`) | `KmshError` if stream/env/u32 disagree (`kmsh_decompress.py:184-185`, `119-129`) |
| Truncated token/literal | raw array access; loop break only at top of main loop (`1sc:96-98`) | raw array access; any `ArrayIndexOutOfBoundsException` is caught and re-wrapped (`WadFile.java:258-262`) | explicit `KmshError` with the branch name (`kmsh_decompress.py:196-240`) |
| Back-reference before start of output | no check | no check | explicit `KmshError` (`kmsh_decompress.py:148-156`) |
| Stream ends without terminal token | prints "might not be successfully extracted" + `-1` (`1sc:172-175`) | logs `WARNING` and **still returns the buffer** (`WadFile.java:381-384`) | raises `KmshError` (`kmsh_decompress.py:191-192,242-244`) |
| Save failure | prints error + `-1` (`1sc:64-68`) | `RuntimeException` (`WadFile.java:198-203`) | `OSError` propagates from `open(...,"wb")` (`wad_tool.py:235-236`) |

Consequence: both reference decoders are permissive on malformed streams; our
Python decoder is strict and fails closed. Neither the template nor Java
validates an entry's `name_size` against the pool before slicing, so a corrupt
entry can produce garbage names or an unchecked exception.

## 8. Verification — exact commands and results

All commands run from `/home/kali/Desktop/rea/Dungeon Keeper 2`.

### 8.1 Container inventory

```
$ for w in EngineTextures FrontEnd Meshes Paths Sprite; do \
    python3 port/tools/wad_tool.py list "Data/$w.WAD" --json \
      > "port/tmp/wad_list_$w.json"; done
$ python3 - <<'PY'   # collections.Counter over the five JSON lists
...
PY
EngineTextures  entries=  102 flags={0: 102} dup_names=0 warnings=[] file_size=2208282 pool_end=2208282
FrontEnd        entries=  445 flags={0: 445} dup_names=31 warnings=[] file_size=22599650 pool_end=22599650
Meshes          entries= 2015 flags={4: 2015} dup_names=0 warnings=[] file_size=32026721 pool_end=32026721
Paths           entries=   81 flags={0: 81} dup_names=0 warnings=[] file_size=1933348 pool_end=1933348
Sprite          entries=   24 flags={0: 24} dup_names=0 warnings=[] file_size=818184 pool_end=818184
```

`pool_end == file_size` for all five, and `len(warnings) == 0` for all five
(meaning `pool_offset+pool_size`, first-data-offset, per-entry contiguity,
last-data-end and `sum(name_length)` all matched — `wad_tool.py:131-157`).
These numbers agree with `port/docs/wad_inventory.json:4-181` and
`port/docs/WAD_FORMAT.md:15-23`.

### 8.2 Decoding real WAD entries

```
$ python3 port/tools/kmsh_decompress.py inspect ../Data/Meshes.WAD --limit 5 --json
... "payloads": 5, "codec": "recovered", all 5 "envelope_ok": true, "problems": []
    index 0 g_Maiden-Torture.kmf    variant=0 declared=21287 stored=5647 body=5633
    index 1 G_Maiden-electrocute.kmf variant=0 declared= 510 stored= 317 body= 303
    index 2 g_Maiden-elec.kmf       variant=0 declared=24377 stored=11804 body=11790
    index 3 Maiden-Torture.kmf      variant=0 declared=46222 stored=23197 body=23183
    index 4 Maiden-electrocute.kmf variant=0 declared=33380 stored=19420 body=19406

$ mkdir -p tmp/wad_kmf_check
$ python3 tools/kmsh_decompress.py decompress ../Data/Meshes.WAD tmp/wad_kmf_check --limit 3 --json
{"extracted": 3, "entries": [
  {"index":0,"name":"g_Maiden-Torture.kmf","bytes":21287,"status":"ok"},
  {"index":1,"name":"G_Maiden-electrocute.kmf","bytes":510,"status":"ok"},
  {"index":2,"name":"g_Maiden-elec.kmf","bytes":24377,"status":"ok"}]}

$ wc -c tmp/wad_kmf_check/*.kmf
24377 g_Maiden-elec.kmf
  510 G_Maiden-electrocute.kmf
21287 g_Maiden-Torture.kmf

$ for f in tmp/wad_kmf_check/*.kmf; do xxd -l 16 -g1 "$f"; done
00000000: 4b 4d 53 48 39 5f 00 00 11 00 00 00 48 45 41 44   ("KMSH", u32 24377, "HEAD")
00000000: 4b 4d 53 48 fe 01 00 00 11 00 00 00 48 45 41 44   ("KMSH", u32 510, "HEAD")
00000000: 4b 4d 53 48 27 53 00 00 11 00 00 00 48 45 41 44   ("KMSH", u32 21287, "HEAD")
```

Full-corpus run (this is the strongest check of the `counter + 1` semantics;
an off-by-one in any back reference would make the output length wrong):

```
$ time python3 tools/kmsh_decompress.py batch ../Data/Meshes.WAD tmp/wad_kmf_all \
    --json > tmp/wad_kmf_all.json 2> tmp/wad_kmf_all.err
43.35s user 1.97s system 87% cpu 51.642 total ; exit=0
$ python3 -c "..."
extracted 2015 statuses {'ok'}
first  index 0    g_Maiden-Torture.kmf   -> 21287 bytes
last   index 2014 WORKSHOP9.kmf          ->  1474 bytes
files 2015 non-KMSH 0 size-mismatch 0
```

A token census was then taken by importing `iter_wad_payloads` and walking the
same four branches, accumulating `j` without writing bytes:

```
payloads= 2015 total_decompressed_bytes= 53622297
token_counts= {'b3': 831250, 'b0': 2387386, 'b1': 761971, 'b2': 37770, 'terminal': 2015}
literal_bytes= 23857020 backrefs= 3187127 backref_bytes= 29765277
```

Every payload ended with `j == declared_size`; all four branches and the
terminal token occur in real data. 21,287 + 510 + 24,377 bytes in the three
spot-checked entries match both the WAD entry `uncompressed_size` and the
decompressed file size.

Cross-check with the independent KMF parser:

```
$ python3 tools/kmf_tool.py info tmp/wad_kmf_check/g_Maiden-Torture.kmf --json
type=ANIM version=17 chunks=25 name="torttest" material_count=3
head: frame_count=101 frame_factor_function=0 (CLAMP) geom_count=200 index_count=100 lod_count=16
geometry: itab_chunks=1 itab_entries=100 vgeo_entries=10100 anim_geom_count=200
sprites=3 extended_vertices=270 issues=[]

$ python3 tools/kmf_tool.py validate tmp/wad_kmf_all --json
{'clean': 2015, 'with_issues': 0, 'errors': 0}
```

This confirms that the decoded bytes are not merely the right length: they
parse as the KMF chunk trees described in
`port/research/dk2-tools/Formats/Model/kmf_struct.bt:109-314` and
`port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/kmf/KmfFile.java:123-157`.

## 9. Repacking — honest answer

**No tool in this checkout writes DWFB archives.**

* `port/tools/wad_tool.py` exposes only `list` and `extract`
  (`wad_tool.py:253-311`); `extract` opens files `"wb"` only for output files
  (`wad_tool.py:235-236`). There is no `pack`/`write`/`create` subcommand and no
  header/entry serializer.
* `port/research/dk2-tools/Scripts/wad_extract.1sc` only creates new hex
  documents and saves extracted payloads (`1sc:58-70`). Its template is read
  only (`wad_struct.bt:46-54`).
* `WadFile.java` has only readers and extractors (`WadFile.java:146-283`); all
  setters on `WadFileEntry` are `protected` and used only while parsing
  (`WadFileEntry.java:58-116`). `WadExtractor.main` only extracts
  (`WadExtractor.java:34-55`). A search of `OpenKeeper/src/main/java` found no
  WAD writer; the only "save the wad entry" strings are in
  `tools/convert/conversion/task/ConvertTextures.java:190-217`, which writes
  extracted PNGs, not WADs.
* Our own earlier doc states extraction "always writes the unmodified stored
  bytes" (`port/docs/WAD_FORMAT.md:287-289`); `kmsh_decompress.py` likewise
  only writes decompressed KMF files.

Nothing here proves a DWFB writer cannot be built — the container equations in
`port/docs/WAD_FORMAT.md:149-163` are sufficient in principle — but the
opaque `+0x00` field (`docs/WAD_FORMAT.md:228-240`) and the unexamined v1
layout (`docs/WAD_FORMAT.md:271-272`) mean a faithful *game-compatible*
repacker is not currently supported by any tool in this checkout.

## 10. Reproduction

```bash
cd "/home/kali/Desktop/rea/Dungeon Keeper 2"
python3 port/tools/wad_tool.py list Data/Meshes.WAD --json
python3 port/tools/kmsh_decompress.py inspect Data/Meshes.WAD --limit 5 --json
python3 port/tools/kmsh_decompress.py decompress Data/Meshes.WAD /tmp/kmf --limit 3 --json
python3 tools/kmf_tool.py validate /tmp/kmf --json   # from port/
python3 -m unittest discover -s port/tests -v
```
