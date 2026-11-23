# Dungeon Keeper 2 `Data/Movies/*.tgq` — codec identification and feasibility analysis

Target: the 24 shipped `Data/Movies/*.tgq` movies in
`/home/kali/Desktop/rea/Dungeon Keeper 2` (213 445 428 bytes total).
All byte facts come from those files and from `DKII.EXE` in that directory.
The container was already solved in `docs/TGQ_FORMAT.md`; this document
addresses the **codec** and whether a decoder can be written in reasonable
scope. Ghidra 12.2 headless decompiled all 8 260 functions of `DKII.EXE` into
`re/ghidra/decomp/*.c` (no source code from any fan engine was used).

---

## 1. Verdict

**Video codec: Electronic Arts TQI ("Texture Quantized Image"), an MPEG-1-derived
intra/inter DCT codec with EA-specific VLC tables, zigzag ordering, IDCT and
motion compensation.** Confidence: **HIGH**.

**Container: Electronic Arts multimedia IFF-style chunk stream** (`SCHl`,
`pIQT`, `SCCl`, `SCDl`, `SCEl`). Confidence: **HIGH**.

**Audio codec: EA-specific compressed audio in `SCDl` chunks.** The exact
algorithm (ADPCM family / sub-band) was **not** identified. Confidence that
`SCDl` is compressed audio: HIGH; confidence in any particular codec: LOW.

**Decoder implementation: NOT feasible in reasonable scope from the available
evidence without reusing an existing implementation.** The game's decoder is a
large, hand-optimised fixed-point/float pipeline (~4 900 + 6 200 bytes of x86
across `FUN_00659b90` / `FUN_0065e280` / `FUN_0065aeb0` / `FUN_0065ca50`) that
builds four 1 024-entry VLC lookup tables at run time, uses a custom IDCT with
clipping tables and packs 4 pixels per 32-bit word. Reconstructing all of that
faithfully — and validating it without a reference decoder — exceeds the scope
of a small stdlib-only script. A partial tool (`tools/tgq_decode.py`) that
parses the container, the corrected 8-byte TQI frame header and the raw
bitstream is provided instead, with honest tests in `tests/test_tgq_codec.py`.
**No image decoder is claimed and none is fabricated.**

---

## 2. Evidence for the TQI identification

### 2.1 Chunk tag `pIQT` is "TQIp" — TQI picture

All video chunks in all 24 files carry the tag bytes `70 49 51 54` = `pIQT`.
Read with the EA constant byte order reversed this is `TQIp` — the fourcc for a
**TQI picture**. This is corroborated independently:

* `DKII.EXE` RTTI string `.?AVTbTQILoader@@` at file offset `0x002BA850`
  (`strings -a -n 5 -t x DKII.EXE`) — the movie loader class is literally the
  **TQI** loader.
* `DKII.EXE` PE section names (from `objdump -h` / `re/ghidra/memory.json`):
  `tqia_dat` (VA `0x007AF000`, 4 096 B), `idct_dat` (VA `0x007AD000`, 8 192 B)
  and `lbmpeg_d` (VA `0x007B2000`, 4 096 B). These are decoder tables linked
  into the executable: **TQI** data, inverse-DCT data and MPEG-family data.
* Public format documentation (MultimediaWiki, "Electronic Arts TQI",
  fetched 2026-11-23): *"Texture Quantized Image (TQI) is a video codec used
  by some Electronic Arts games for the PC. The format is an evolution of the
  TGQ codec, and draws heavily from MPEG-1."* It lists the video chunk as
  `pIQT` and describes 16x16 macroblocks with 4:2:0 luma/chroma blocks,
  MPEG-1-style DC/AC VLC with EA-specific values, an EA zigzag scan, a
  quantizer-dependent dequantization table and an IDCT. The same page and the
  OpenTPW format index both state that TQI movies are wrapped in the TGQ
  file extension — exactly the situation here (`.tgq` files containing
  `pIQT` frames).

### 2.2 Ghidra decompiled decoder call chain

Export: `re/ghidra/export_summary.txt` →
`functions=8260 decompiled=8260 failed=0`.

| Address | Function | Evidence from decompiled C |
|---|---|---|
| `0x0053C020`, `0x0053BFD0` | movie setup | pass `s__s_BullfrogIntro_tgq_...` / `s__s__s_tgq_...` to `FUN_0053BCD0` |
| `0x0053BCD0` | movie loader/player setup | builds the IFF reader/player objects, then calls `FUN_00559690(&LAB_0053C2E0, param_1)` |
| `0x005B2450` | VLC configuration | takes four `u32` bitmasks, computes popcounts (`DAT_007982B0..BC`), trailing-zero counts (`DAT_007982C4..CC`) and calls `FUN_005B21E0()` |
| `0x005B21E0` | VLC table build | fills four 1 024-entry tables at `0x00798F90`, `0x00799F90`, `0x0079AF90`, `0x0079BF90` with canonical codes derived from those counts |
| `0x005B1D7E` | IDCT setup | writes `DAT_007AD000 = param_2 << 2` (the `idct_dat` section) and walks `&DAT_007AD02C..0x007AD048` calling `FUN_005B1B11()`/`FUN_005B1C74()` |
| `0x005B26F0` | **frame decoder entry** | reads the 8-byte frame header, initialises the bitstream at `param_1 + 8`, then dispatches to the four block decoders below. One caller (vtable). |
| `0x00659580` | bitstream init | sets `DAT_007AF058 = param_1` and zeroes five state words |
| `0x00659B90`, `0x0065E280` | block decode + IDCT (path A) | 4 877 B / 6 214 B of x86; coefficient arrays at `0x00798710..0x00798E10`, clipping lookups at `0x0079A590`, `0x0079B590`, `0x0079C590`, `0x00799590`; float IDCT expressions and 4-pixel-per-`u32` writes |
| `0x0065AEB0`, `0x0065CA50` | block decode + IDCT (path B) | 4 690 B / 6 182 B; selected when `DAT_007AF0D0 != 0` (header bit 7) |

Frame-header facts from `0x005B26F0` (decompiled):

```c
DAT_007af0d0 = (uint)(*(byte *)(param_1 + 4) >> 7);   /* header bit 7  */
DAT_007af0cc = (byte)~*(byte *)(param_1 + 4) & 1;     /* header bit 0  */
DAT_007af0d4 = (*(byte *)(param_1 + 4) & 0x10) >> 4;  /* header bit 4  */
FUN_00659580(param_1 + 8);                            /* bitstream @ +8 */
```

This **corrects** the earlier provisional "16-byte frame header" in
`docs/TGQ_FORMAT.md`: only the first **8** bytes are the TQI frame header;
bytes 8..15 are already bitstream symbols (their byte values come from the
same small alphabet as the frame body). The three bytes `14 1E 03` at offsets
5..7 are constant in all 24 movies (they equal 20, 30, 3 — the macroblock
column/row counts for 320x480 and a small format/flags value).

### 2.3 Byte-level evidence

`pIQT` payload header, first frame of every file (24/24):

```
40 01 e0 01 XX 14 1e 03      XX = 0x62 (2 files), 0x63 (20 files), 0x64 (2 files)
```

* `0x0140` = 320, `0x01E0` = 480 — frame width/height in pixels (20x30
  macroblocks of 16x16).
* byte 4 (`0x62`/`0x63`/`0x64`) is the per-frame quantizer/flags field read by
  `0x005B26F0`.
* bytes 5..7 = `14 1E 03` in 24/24 files.
* bitstream begins at byte 8, e.g. `pIQT` payload of `BullfrogIntro.tgq`
  frame 0 starts `40 01 e0 01 64 14 1e 03 | 2f a5 34 f4 02 fa 0b e8 | 29 62 ...`.

Search for MPEG-1 start codes `00 00 01 xx` across all 189 `pIQT` payloads of
`BullfrogIntro.tgq`: **zero matches**. Across all 24 movies a small number of
coincidental `00 00 01` byte triples occur deep inside frames (~100 in
16 108 frames, i.e. far fewer than one per frame on average), and **none** of
them is followed by a valid MPEG start-code suffix (`0x00` picture,
`0xB3`/`0xB5`/`0xB7`/`0xB8`, `0xC0..0xCF`, `0xE0..0xEF`). This rules out a raw
MPEG-1/2 elementary stream inside the frames (consistent with TQI, whose
bitstream has no start codes). No `SMK1`/`SMK2` (Smacker), `CREK` (Cinepak), `RIFF`/`WAVE`,
`OggS` or `ID3` magic occurs at the start of any frame or audio payload.

Bitstream statistics (`BullfrogIntro.tgq` frame 0, bitstream from offset 8):
the first ~240 bytes use only 21 distinct byte values, dominated by
`0x22, 0x29, 0x4A, 0x52, 0x94, 0xA5`; exact repeated substrings of length
30..37 recur at a period of **140 bytes**. This is the signature of a VLC /
run-length bitstream over a largely uniform source image, not of raw pixels
or of byte-aligned PCM. Whole-frame Shannon entropy is 7.42 bits/byte and all
256 byte values occur, consistent with a dense Huffman-coded payload.

### 2.4 Tables inside `tqia_dat`

The extracted raw section `port/.work/pe/tqia_dat.bin` (2 560 bytes) contains,
in order:

1. IEEE-754 constants at `0x00`..`0xEB` (e.g. `0x3F95A1CB`, `0xBF5020C5`,
   `0x3FCD0E56`, `0x4001999A`) — dequantization / IDCT scale factors.
2. A 64-entry `u32` permutation table starting near `0xEC` with
   `3F 37 3E 3D 36 2F 27 2E ...` — the **EA zigzag scan**.
3. A run/level/length VLC table from about `0x1E0`: 4-byte records such as
   `00 04 01 02`, `00 03 02 01`, `00 02 41 00`, `00 07 03 02`,
   `00 0A 11 01`, `00 0C 01 0B`, with code lengths 2..16.
4. Nibble-packed code-length tables near `0x448`..`0x600`
   (`12 12 12 ... 22 22 ... 43 43 ... 76 76 ...`).

`lbmpeg_d` holds a 16-bit decreasing sequence `3FEC 3F4F 3E15 3C42 39DB 36E5
3368 2F6C 2AFB 2620 20E7 1B5D 1590 0F8D 0964 0324` repeated in permuted
order — a transform/quantization matrix, consistent with the MPEG-1 heritage.

---

## 3. Hypotheses tested

| # | Hypothesis | Result | Discriminator |
|---|---|---|---|
| H1 | Smacker (`SMK1`/`SMK2`) | **Rejected** | no fourcc; chunk tags are EA IFF; no 8-byte Smacker frame header |
| H2 | Cinepak (`CREK`) | **Rejected** | no fourcc; no codebook strips |
| H3 | Raw MPEG-1/2 video in `pIQT` | **Rejected** | 0 occurrences of `00 00 01 xx` in 189 frames; TQI VLC has no start codes |
| H4 | EA **TGQ** (older codec) | **Rejected for these files** | TGQ frames use chunk tag `pQGT` (= `TGQp` reversed); these use `pIQT` (= `TQIp`). TGQ also has per-macroblock `[u8 length][data]` records; `pIQT` bodies are a continuous VLC bitstream |
| H5 | EA **TQI** | **Accepted** | `pIQT` fourcc + `TbTQILoader` RTTI + `tqia_dat`/`idct_dat`/`lbmpeg_d` sections + Ghidra decoder at `0x005B26F0` + MPEG-1-derived structure matching public TQI documentation |
| H6 | `SCDl` = MPEG audio (as in the game's `.sdt` banks) | **Rejected** | no `FF Fx` MPEG sync at audio-block starts; leading `u32` length field and 8/12-byte prefix instead |
| H7 | `SCDl` = uncompressed PCM | **Rejected** | consecutive-byte deltas large (~81 mean), no WAVE header |
| H8 | `SCDl` = EA-specific ADPCM-family codec | **Plausible but unproven** | EA TQI movies are documented to carry SHI/PT/adpcm_ea audio; no decoder identified in `DKII.EXE` strings |

---

## 4. Why a full decoder was not implemented

A faithful decoder would require all of:

1. The four VLC bitmask parameters passed to `FUN_005B2450`. The only call is
   indirect (vtable), so the masks are not recoverable as immediate constants
   from the decompiled C without dynamic tracing.
2. Reimplementation of `FUN_005B21E0`'s 1 024-entry canonical-code table
   builder, including the separate `DAT_007AFC0 == 1` path.
3. The bit reader and DC/AC run-level decoder inside the 4.9 kB
   `FUN_00659B90` / 6.2 kB `FUN_0065E280` (and their 4.7/6.2 kB alternates).
4. The runtime-built IDCT state (float scale factors from `tqia_dat` + cosine
   tables from `idct_dat`) and four clipping lookup tables.
5. Motion compensation for inter macroblocks (header bits 0/4/7 select among
   the four decoder paths).
6. YUV 4:2:0 → RGB conversion matching the game's palette/colour conventions.

There is also **no local reference decoder** to validate against: `ffmpeg` /
`ffprobe` are not installed, and using a third-party TQI implementation would
violate the "no fan-engine code" constraint. A decoder that merely emits a
plausible-looking image without byte-level agreement to the game's algorithm
would be a fabrication, so it was not written.

---

## 5. What was delivered

* `tools/tgq_decode.py` — stdlib-only **probe**: walks the IFF container,
  validates the corrected 8-byte TQI header on every frame, reports the
  quantizer/flags byte and the first bitstream bytes, and can dump raw
  bitstreams verbatim. It explicitly refuses to claim pixel output.
* `tests/test_tgq_codec.py` — honest assertions over all 24 movies:
  container walk, tag/order invariants, 8-byte header invariants, absence
  of MPEG start codes, and exact bitstream prefix bytes.
* This report.

---

## 6. Reproduction

```bash
cd "/home/kali/Desktop/rea/Dungeon Keeper 2"

# Container facts
python3 port/tools/tgq_probe.py scan Data/Movies

# Codec probe (new tool)
python3 port/tools/tgq_decode.py info Data/Movies/BullfrogIntro.tgq
python3 port/tools/tgq_decode.py headers Data/Movies/BullfrogIntro.tgq --limit 6
python3 port/tools/tgq_decode.py scan Data/Movies

# Tests
python3 -m pytest -q port/tests/test_tgq_codec.py

# EXE evidence
strings -a -n 5 -t x DKII.EXE | grep -E 'TbTQILoader|tgq|Movies'
objdump -h DKII.EXE | grep -E 'tqia_dat|idct_dat|lbmpeg_d'
grep -n 'FUN_005b26f0\|FUN_005b2450\|FUN_00659b90' \
     port/re/ghidra/decomp/*.c | head
```

The Ghidra re-export used to obtain the decompilation was:

```bash
# initial analysis (already completed)
analyzeHeadless /home/kali/ghidra-dk2-project dk2 \
  -import DKII.EXE -analysisTimeoutPerFile 2400 \
  -scriptPath "port/re" -postScript GhidraExportScript.java

# post-script had two API errors, fixed in port/re/GhidraExportScript.java:
#   Reference[] -> ReferenceIterator; r.isCall() -> r.getReferenceType().isCall()
analyzeHeadless /home/kali/ghidra-dk2-project dk2 \
  -process -noanalysis -scriptPath "port/re" \
  -postScript GhidraExportScript.java     # -> 8260/8260 decompiled
```
