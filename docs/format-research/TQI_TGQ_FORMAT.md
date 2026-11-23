# Dungeon Keeper II `TGQ` / TQI video — deep, source-cited format research

Date: 2026-11-23. Scope: `port/` only. All numbers below were measured from the
24 shipped `Data/Movies/*.tgq` files, from the OpenKeeper sources under
`port/research/OpenKeeper/`, and from the Ghidra export under `port/re/ghidra/`.
Where a claim is not proved by one of those, it is explicitly marked
**HYPOTHESIS** or **UNKNOWN**. No decoder was invented for this report: the only
pixel decode shown in §9 is produced by compiling the *unmodified* OpenKeeper
Java decoder that this task asked us to read.

Companion documents: `port/docs/TGQ_FORMAT.md` (container, byte-derived),
`port/docs/TGQ_CODEC_ANALYSIS.md` (codec identification from `DKII.EXE`),
`port/docs/format-research/KMF_FORMAT.md` (unrelated).

---

## 0. Verdict / executive summary

1. **There is no `jlibtqi` dependency in the inspected OpenKeeper checkout.** The
   premise that OpenKeeper delegates TQI decoding to a native/third-party
   library is **false for this source snapshot (commit `e2bfe95`, shallow clone)**.
   `TgqFrame.java` is a complete Java TQI decoder: hard-coded VLC trees,
   quantiser/dequantisation, zigzag, 4:2:0 macroblock decode and an integer IDCT
   (`port/research/OpenKeeper/src/main/java/toniarts/openkeeper/video/tgq/TgqFrame.java:39-495`).
   `TgqFile.java` only parses the IFF container and the 8-byte frame header;
   `TgqPlayer.java` is a threading/audio/video synchronisation shell and does no
   codec work. See §2.
2. **The container is EA multimedia IFF-style chunks**, `[4-byte tag][u32 LE size
   including the 8-byte header][payload]`; this is confirmed independently by
   MultimediaWiki (fetched 2026-11-23) and by all 24 files walking exactly to EOF.
3. **`pIQT` is the TQI video chunk.** Reversal to `TQIp` is EA's documented
   fourcc convention for these video chunks (the wiki lists `pIQT` for TQI and
   `pQTG` for TGQ), but the reversal itself is **not** established by the local
   bytes or by OpenKeeper source; treat it as a naming convention, not a semantic
   proof. `DKII.EXE` RTTI `.?AVTbTQILoader@@` and sections `tqia_dat`/`idct_dat`/
   `lbmpeg_d` are independent local corroboration (quoted in
   `port/docs/TGQ_CODEC_ANALYSIS.md:48-64`).
4. **The real game decoder differs from OpenKeeper's Java reimplementation.**
   The game builds four 1024-entry VLC lookup tables at runtime
   (`FUN_005b21e0` @ `0x005B21E0`) and uses a **floating-point** IDCT with
   clipping tables; OpenKeeper uses hard-coded binary VLC trees and an **integer**
   IDCT. The game also has macroblock prediction/motion-vector paths gated by
   frame-header bits 0/4/7 (`0x005B26F0`, `0x00659B90`); OpenKeeper ignores those
   bits (it skips header bytes 5..7 and always decodes intra blocks). A faithful
   C port therefore cannot be a transcription of `TgqFrame.java` alone. §7.
5. **Audio is identified in the OpenKeeper source as EA XA ADPCM** (`EAAudioFrame.java`),
   22.05 kHz stereo, 16-bit PCM output, 12-byte block header + 30-byte packed
   groups of 28 samples. This corrects the "audio codec unknown" statement in
   the two earlier docs. 16087/16109 shipped audio blocks satisfy the exact
   length equation (§8.3). §6.
6. **At least one real frame was decoded to PNG** through the OpenKeeper Java
   source itself (§9); the images are visually plausible DK2 intro/logo frames
   and are byte-identical across independent recompilations.

---

## 1. Sources and method

| Source | Role in this report |
|---|---|
| `port/research/OpenKeeper/src/main/java/toniarts/openkeeper/video/tgq/TgqFile.java` (367 lines) | container/chunk parser, audio `PT` header TLV parser |
| `.../video/tgq/TgqFrame.java` (495 lines) | full Java TQI video decoder (reference algorithm) |
| `.../video/TgqPlayer.java` (469 lines) | playback threads, A/V sync, PCM volume; **no codec logic** |
| `.../video/tgq/EAAudioFrame.java`, `EAAudioHeader.java` | EA XA ADPCM decoder, audio header enums |
| `.../tools/convert/BitReader.java`, `ConversionUtils.java` | bit reader used by `TgqFrame`; `toUnsignedByte` at `ConversionUtils.java:149-151` |
| `port/research/OpenKeeper/build.gradle:12,87-106` | dependency inventory (no TQI library) |
| `port/re/ghidra/decomp/{005b26f0,005b2450,005b21e0,005b1d7e,005b0890,00659580,00659b90,0065e280}.c` | game-binary cross-check |
| `port/re/ghidra/functions.json` | function sizes/caller counts (8260 records) |
| `port/tools/tgq_probe.py`, `port/tools/tgq_decode.py` | container/header probes, run against all 24 movies |
| MultimediaWiki `Electronic_Arts_Formats` and `Electronic_Arts_TQI` (fetched 2026-11-23) | external corroboration of chunk names and frame layout |

Method: read each Java file line by line; cite exact line ranges; run the probes
and capture stdout into `port/.work/tqi_research/`; compile the *unmodified*
OpenKeeper decoder with Java 25 and the locally cached `jme3-core`/`vecmath`
jars; compare game-binary decompilation to the Java algorithm; mark anything
not directly observed as hypothesis/unknown.

---

## 2. Third-party dependency audit (the "jlibtqi" question)

* `port/research/OpenKeeper/build.gradle:88` declares `implementation fileTree(dir: 'lib', include: ['*.jar'])`.
  The checkout has **no `lib/` directory** (verified with `ls`), so there is no
  bundled TQI jar.
* `port/research/OpenKeeper/build.gradle:89-100` lists only jME 3.9.0-stable
  (`jme3-core`, `-desktop`, `-plugins`, `-effects`, `-networking`, `-lwjgl3`,
  `-niftygui`), `gdx-ai:1.8.2`, `vecmath:1.5.2`, `zay-es`, `sio2`,
  `sim-ethereal`, plus JUnit for tests. **No `jlibtqi`, no `libtqi`, no native
  codec dependency.** `build.gradle:12` sets `jmonkeyengine_version = '3.9.0-stable'`.
* `gh search code jlibtqi` returned only unrelated base64 certificate strings;
  `gh api search/code?q=jlibtqi+repo:tonihele/OpenKeeper` returned `total_count: 0`;
  `gh api search/commits?q=jlibtqi+repo:tonihele/OpenKeeper` returned `0`.
  Upstream history for `TgqFrame.java` (queried through `gh api`) starts with
  `f363b3973d` "Beginnings of TGQ video decoder with working audio stream
  encoding" (2015-03-25), followed by `d5d14964f3` "Implemented calculation of
  the dequantization table", `dee01bcb43` "Actually working block decoding",
  `a3005a52ef` "EA IDCT implemented", `32d1e2a724` debug RGB conversion,
  `db9e7cfda1` "Do proper clipping" (2015-05-04) — i.e. the decoder was written
  in-tree, not imported from a library.
* The only "third-party" relationship is **attribution of ideas**, visible in
  the Javadoc: `TgqFrame.java:30-35` cites FFmpeg `eatgi.c`/`mpeg12.c`, "JAVA
  MPEG Player by J.Anders" and JCodec; `EAAudioFrame.java:20-27` cites FFmpeg
  `adpcm.c`; `TgqPlayer.java:43` says the example is taken from JCodec. These
  are **references, not linked code or build dependencies**. We did not copy any
  of them into `port/`.
* Licence/runtime implications: OpenKeeper is GPL-3.0 (`gh api repos/tonihele/OpenKeeper`
  → `GPL-3.0`; header of every cited Java file states GPL-3-or-later). jME 3.9.0-stable
  is only needed because `ConversionUtils.java:19` imports `com.jme3.math.Vector3f`
  for unrelated general-purpose conversion helpers; the only method `TgqFrame`
  needs is `ConversionUtils.toUnsignedByte` (`ConversionUtils.java:149-151`).
  If a future port depended on an OpenKeeper JAR at runtime it would inherit
  GPL-3 obligations and the jME/vecmath classpath; this research only *reads*
  that source and compiles it in a scratch directory for verification, and adds
  **no runtime dependency** to our port. The decoder harness added in §9 is a
  verification tool, not a shipped runtime component.

---

## 3. Container: chunk names, reversal and payload contents

EA block layout, confirmed by MultimediaWiki (`Electronic_Arts_Formats`):

```
bytes 0-3   block type FourCC
bytes 4-7   block size (including this 8-byte preamble)
bytes 8..   block payload
```

The wiki lists audio chunks `SCHl`, `SCCl`, `SCDl`, `SCLl`, `SCEl` and the
alternate spellings `SHEN`, `SCEN`, `SDEN`, `SEEN`; video `pIQT` = TQI,
`pQTG` = TGQ. It lists "Dungeon Keeper 2 | tgq | TQI (no typo) | SCHl/PT/adpcm_ea | le".
This exactly matches what OpenKeeper parses and what our probe sees.

`TgqFile.java:55-64` defines the tags it accepts:
`pIQT`, `SCHl`, `SHEN`, `SCCl`, `SCEN`, `SCDl`, `SDEN`, `SCEl`, `SEEN`, plus the
audio `PT` patch marker. `readFrame()` (`TgqFile.java:119-181`) reads an 8-byte
chunk header, `tag = reader.readString(4)` and `frameSize = reader.readInteger()`
(lines 123-129), then dispatches:

| Observed tag | OpenKeeper case (`TgqFile.java`) | Payload contents (source-cited) |
|---|---|---|
| `SCHl` / `SHEN` | lines 133-140 → `readAudioHeader(frameSize - 8)` | `PT` patch header; see §6 |
| `SCCl` / `SCEN` | lines 141-146 → `numberOfAudioStreamChunks = readChunk(4).readUnsignedInteger()` | exactly one `u32` = number of `SCDl` blocks (24/24 files) |
| `SCDl` / `SDEN` | lines 147-154 → `new EAAudioFrame(...)` on `frameSize - 8` bytes | EA XA ADPCM block, see §6 |
| `SCEl` / `SEEN` | lines 155-159 | empty end marker (size 8) |
| `pIQT` | lines 160-174 → `new TgqFrame(file.readChunk(frameSize - 8).getByteBuffer(), videoFrameIndex)` | 8-byte TQI frame header + VLC bitstream, §4-§5 |

Notes:
* OpenKeeper supports *both* fourcc spellings, which is why the wiki lists both
  sets; our DK2 files use only the `SCHl/SCCl/SCDl/SCEl` set.
* `videoFrameIndex` increments once per `pIQT` and `audioFrameIndex` once per
  `SCDl` (`TgqFile.java:54,153,173`); width/height are latched from the first
  decoded frame (`TgqFile.java:167-170`).
* Unknown tags are logged and skipped (`TgqFile.java:175-177`).
* `pIQT` reversed is `TQIp` ("TQI picture"). **HYPOTHESIS** (external convention,
  not proved locally): the reversal follows EA's video-fourcc convention. It is
  not a claim that the file stores the ASCII bytes in reverse.
* The first `pIQT` chunk is followed by `SCCl`, then `SCDl`/`pIQT` alternate
  (`port/docs/TGQ_FORMAT.md:119-123`); this is container-level interleaving, not
  codec-level information.

---

## 4. The corrected 8-byte TQI frame header

Game binary: `FUN_005b26f0` reads header bytes 4..7 and calls
`FUN_00659580(param_1 + 8)` (`port/re/ghidra/decomp/005b26f0.c:21-31`), so the
bitstream begins at payload offset 8.

Java source, same layout: `TgqFrame.java:158-175` reads `u16 width` and
`u16 height` (lines 162-163), then `decodeFrame()` reads one quantiser byte and
skips three bytes (`TgqFrame.java:177-180`). Coded buffer geometry
`codedWidth = (width+15) & ~0xf`, `codedHeight = (height+15) & ~0xf` and three
4:2:0 planes (`luma`, `cb`, `cr`) are allocated at `TgqFrame.java:166-171`.

| Offset | Field | Proven by | Notes |
|---:|---|---|---|
| `+0x00` | `u16 LE width` | `TgqFrame.java:162`; wiki frame table | 320 in all 16108 frames (§8.2) |
| `+0x02` | `u16 LE height` | `TgqFrame.java:163`; wiki frame table | 480 in all 16108 frames |
| `+0x04` | `u8 quantizer` | `TgqFrame.java:178`; `005b26f0.c:28-30,94-97` | 0x62/0x63/0x64 observed; game also reads bits 0,4,7 of this byte |
| `+0x05..0x07` | 3 bytes | `TgqFrame.java:179` skips them; `005b26f0.c:21-25` uses them as packed shift/mask state | DK2: always `14 1e 03` (16108/16108). Wiki says "always zero" for its samples — DK2 differs. |
| `+0x08..` | 32-bit LE most-to-least-significant VLC bitstream | `FUN_00659580` @ `005b26f0.c:31,92`; `TgqFrame.java:183` | no MPEG start codes (§8.2) |

The earlier 16-byte "frame header" in `port/docs/TGQ_FORMAT.md:177-199` is
superseded: bytes 8..15 are VLC symbols. The stale `VIDEO_HEADER_SIZE = 16` in
`port/tools/tgq_probe.py` was corrected to 8 in this task; `--strip-video-header`
now strips the true 8-byte header and `video_header_u16` is retained only as an
explicitly labelled legacy preview of the first 16 bytes.

---

## 5. OpenKeeper video decoder — exact source walkthrough

All references are to
`port/research/OpenKeeper/src/main/java/toniarts/openkeeper/video/tgq/TgqFrame.java`.

| Stage | Lines | What the source actually does |
|---|---|---|
| Signature / references | 28-38 | Javadoc names FFmpeg `eatgi.c`, `mpeg12.c`, JAVA MPEG Player, JCodec (attribution only) |
| Dequant state | 46, 193-201 | `dequantizationTable[64]`; `qScale = (215 - 2*quantizer) * 5`; `dequant[0] = (BASE_TABLE[0]*BASE_TABLE2[0]) >> 11`; `dequant[i] = (BASE_TABLE[i]*BASE_TABLE2[i]*qScale + 32) >> 14` |
| Zigzag | 47-64 | `SCAN_TABLE` (MPEG-like zigzag) and `SCAN_TABLE_PERMUTATED` (identity copy at lines 59-64) |
| Base tables | 65-82 | `BASE_TABLE[64]` (starts 4096, 2953, 3135, …) and `BASE_TABLE2[64]` (starts 8, 16, 19, …), identical to the table quoted on MultimediaWiki's TQI page |
| VLC definitions | 83-139 | `DC_VLC_BITS = 9`, `TEX_VLC_BITS = 28`, two DC Huffman trees (luma/chroma, 19 rows each), `DCT_ESCAPE = -2`, `EOB = -5`, and the 227-row `DCT_COEFF` run/level tree |
| Macroblock loop | 185-190 | `(height+15)/16 × (width+15)/16` macroblocks → 20×30 = 600 for 320×480; each becomes `idctPut(decodeBlock(bitReader), x, y)` |
| Block decode | 203-209 | 6 blocks per macroblock (4 luma + Cb + Cr) via `mpeg1DecodeBlock` |
| DC | 217-228, 273-293 | component 0..3 → luma VLC, 4..5 → chroma VLC; `lastDc[3]` predictor; `block[0] = dc * dequantizationTable[0]` |
| AC run/level | 231-270 | `decodeVlc(TEX_VLC_BITS, DCT_COEFF)`; escape uses 6-bit run + 8-bit level with two special cases (`twosSigned`); dequant `(level*dequant[j]) >> 4`, oddify `(level-1)|1`, sign from one bit; `EOB` ends block |
| Bit reading helpers | 295-309, 311-329 | `mpegSigned`, `toSigned`, `twosSigned`; `decodeVlc` forks the reader, reads `maxLength+1` bits and walks the binary tree `tab[idx][bit]` from the MSB, then skips the consumed length |
| IDCT placement | 336-350 | 4 luma blocks at (0,0),(8,0),(0,8),(8,8) and Cb,Cr at 8×8 offsets in the three planes |
| IDCT | 352-414 | `eaIdctPut` adds 4 to DC, column pass (`eaIdctCol`) then row pass; `idctTransform` uses fixed-point constants `ASQRT=181`, `A4=669`, `A2=277`, `A5=196` (lines 150-156) |
| Clipping | 416-419 | `(val >> 4)` clamped to [0,255] |
| Colour conversion | 427-446 | `getImage()` samples Y at `y*width+x`, U/V at `(y/2, x/2)` (nearest neighbour) and applies JPEG YCbCr coefficients 1.402, −0.34414, −0.71414, 1.772; used only for debugging/PNG, GPU path uses the raw planes via `getBufferForPlane` (455-472) |

**What OpenKeeper does NOT implement:** motion compensation / prediction. Header
bits 0, 4 and 7 are ignored (byte 5..7 are skipped at line 179), and there is no
reference-frame buffer; every macroblock is decoded as intra. The game binary
*does* have these paths (§7). This is why `TgqFrame` is a useful algorithm
reference for intra frames but not a byte-exact specification of the game
decoder.

Dequantisation vs MultimediaWiki: the wiki gives `A = (107.5 - quant) * 0.625`,
`dequant[0] = base[0]^2`, `dequant[i] = (base[i]*base2[i]*A + 4) / 8`;
OpenKeeper uses the integer form above. The two are not directly comparable
because the wiki's `base_table` and OpenKeeper's `BASE_TABLE` use different
scales; we verified the **OpenKeeper** formula empirically by decoding real
frames (§9), not the wiki formula.

---

## 6. Audio: EA XA ADPCM (identified, not guessed)

`TgqFile.readAudioHeader` (`TgqFile.java:204-218`) requires the first two bytes to
be `PT` (line 64 constant, check at 208-211), then reads a 2-byte platform
identifier (`TgqFile.java:214-215`, mapping at 299-326: 0=PC, 3=Mac, 5=PS2,
6=GameCube, 7=Xbox, 9=Xbox360, 10=PSP), then length-prefixed big-endian TLV tags
(`getValue`, `TgqFile.java:347-366`). Sub-tags `0xFC/0xFE/0xFD` open a substream;
`0x82` = channels, `0x83` = compression identifier, `0x85` = sample count,
`0x8A` = exit substream, `0xFF` = end (`TgqFile.java:220-285`). Defaults are
16-bit, EA_XA_ADPCM, 1 channel, 22050 Hz (`TgqFile.java:287-297`); compression
IDs 0x00→PCM16LE, 0x07→EA_XA_ADPCM (`TgqFile.java:328-345`). Our real files
decode to 22050 Hz / 16-bit / **2 channels** / EA XA ADPCM (§9 log).

`EAAudioFrame.java` decodes each `SCDl` payload:
* Javadoc line 20-27 cites FFmpeg `adpcm.c`;
* `EA_ADPCM_TABLE[20]` at lines 28-33 (filter coefficients);
* 12-byte block header: `codedSamples = buf.getInt()` then
  `codedSamples -= codedSamples % 28`, followed by four `u16` predictor samples
  (left/right current+previous) at lines 43-55;
* `numberOfSamples = (capacity - 12) / 30 * 28` (line 51);
* each 30-byte group = 2 coefficient/shift bytes + 28 nibbles, two stereo
  channels per nibble, predictor update with `+0x80` and `>> 8`, output
  interleaved signed 16-bit LE PCM (lines 60-102);
* only EA_XA_ADPCM and exactly 2 channels are supported (lines 45-52).

Cross-check on shipped bytes: `SCDl` #0 of BullfrogIntro begins
`80 03 00 00` (896) and its payload is 972 bytes; `(972-12)/30*28 = 896`,
matching `EAAudioFrame.java:51`. Across all files, **16087/16109** audio blocks
satisfy `codedSamples == (payload_size-12)/30*28`; the 22 exceptions are
terminal blocks whose declared sample count is not a multiple of 28 (OpenKeeper
rounds it down at line 50). Evidence: `port/.work/tqi_research/audio_check.txt`.

---

## 7. Ghidra cross-check: what is proved and what is assumed

All paths below are relative to `port/re/ghidra/decomp/`. Function sizes/caller
counts are from `port/re/ghidra/functions.json`.

### 7.1 Proved from the decompilation

| Address | Size / callers | Proved fact |
|---|---|---|
| `0x005B26F0` (`005b26f0.c`) | 767 B / 1 caller (vtable) | Reads header bytes 5,6,7 into packed shift state (lines 21-25), byte 4 bits 7/0/4 into `DAT_007af0d0/cc/d4` (lines 28-30, 94-97), calls `FUN_00659580(param_1 + 8)` (31, 92). Branches on `DAT_007982d4 == 0x10` (27, 91) into two decoder families; dispatches `FUN_00659b90`/`FUN_0065e280` (75/78) or `FUN_0065aeb0`/`FUN_0065ca50` (121/124), selected by `DAT_007af0d4` (74, 120). |
| `0x00659580` (`00659580.c`) | 63 B / 2 callers | Bitstream init: `DAT_007af058 = param_1` and five state words zeroed (lines 5-10). |
| `0x005B2450` (`005b2450.c`) | 658 B / 1 caller | VLC configuration from four 32-bit masks: popcounts into `DAT_007982b0..bc` (lines 36-69), trailing-zero counts into `c4..cc` (79-96), `DAT_007982d4` computed from their sum (97-98), then `FUN_005b21e0()` (116). Also builds 259-byte table at `DAT_007981a8` via `FUN_006359f0` and a 64-entry fixed-point table from `DAT_006c10c0` (16-35). |
| `0x005B21E0` (`005b21e0.c`) | 621 B / 1 caller | **VLC table builder**: four 1024-entry tables at `DAT_00798f90`, `DAT_00799f90`, `DAT_0079af90`, `DAT_0079bf90`; loop `iVar7 < 0x1000` step 4 (lines 39-51, 56-91), canonical code values derived from popcount/trailing-zero globals, with a separate `DAT_007af0c0 == 1` variant (70-78). |
| `0x005B1D7E` (`005b1d7e.c`) | 403 B / 6 callers | **IDCT table setup**: `DAT_007ad000 = param_2 << 2` (line 5), eight calls over `DAT_007ad02c..048` through `FUN_005b1b11` (6-21), then eight `FUN_005b1c74` calls advancing by `DAT_007ad000` (22-37). Called six times from `FUN_005b0890` (`005b0890.c:33,46,59,72` with `0x10`; `:85,98` with `8`). |
| `0x00659B90` (`00659b90.c`) | 4877 B / 1 caller | Block decode path A: four passes of 8 DC reads through `FUN_00659930` and 8 AC reads through `FUN_00659a90` (32 calls of each in the whole function; the first pass writes coefficient arrays `DAT_00798710..0x00798e10`, lines 17-49); **motion-vector branch** at lines 152-168 when `DAT_007af0cc != 0`, reading two byte-swapped 32-bit words from `DAT_007af058`; clip lookups at `DAT_0079a590/0x0079b590/0x0079c590` (178-184). |
| `0x0065E280` (`0065e280.c`) | 6214 B / 1 caller | Float IDCT path: e.g. lines 16-29 `fVar1 = ((float)(int)(&DAT_00798710)[iVar2*0x40] - _DAT_007af000) * _DAT_007af008 + ...`, writes packed 16-bit pixels via the clipping tables. |
| `0x0065AEB0` / `0x0065CA50` | 4690 B / 6182 B | Alternate decoder family used by `FUN_005b26f0` in the other branch; caller steps output by `0x20` instead of `0x10` (`005b26f0.c:126` vs `:80`). |

Short quotes (verbatim from the decompiled C):

```c
/* 005b26f0.c:21-31 */
DAT_007af0c8 = (uint)(*(byte *)(param_1 + 6) >> (DAT_007af088 & 0x1f)) << ... ;
DAT_007af0d0 = (uint)(*(byte *)(param_1 + 4) >> 7);
DAT_007af0cc = (byte)~*(byte *)(param_1 + 4) & 1;
DAT_007af0d4 = (*(byte *)(param_1 + 4) & 0x10) >> 4;
FUN_00659580(param_1 + 8);

/* 005b21e0.c:39-46 */
iVar8 = iVar7 + 4;
*(int *)((int)&DAT_00798f90 + iVar7) =
     (iVar6 >> (8U - cVar3 & 0x1f)) << ((byte)DAT_007982c0 & 0x1f);
*(int *)((int)&DAT_00799f90 + iVar7) =
     (iVar6 >> (8U - cVar4 & 0x1f)) << ((byte)DAT_007982c4 & 0x1f);

/* 005b1d7e.c:5-7 */
DAT_007ad000 = param_2 << 2;
DAT_007ad004 = &DAT_007ad02c;
FUN_005b1b11();

/* 00659b90.c:152-162 */
_DAT_007af0d8 = 0;
if ((DAT_007af0cc != 0) && ... ) {
  uVar3 = *(int *)(DAT_007af058 + uVar3 * 4) << bVar1 | ... ;
  _DAT_007af0d8 = uVar3 >> 0x18 | (uVar3 & 0xff0000) >> 8 | ... ;
```

### 7.2 Assumptions / limitations of the Ghidra evidence

* `FUN_005b2450` has **no direct caller in the decompiled output** (grep for
  `FUN_005b2450(` finds only its definition) and `FUN_005b26f0` likewise. Both
  are reached through a vtable, so the **four VLC masks are not recoverable from
  static decompilation**. Do not claim their values.
* `DAT_007982d4` is 0x10 or 0x20 depending on `popcount(param_2)+popcount(param_3)+popcount(param_1) > 0x10` (`005b2450.c:97-98`). The two decoder families differ in output stride (0x10 vs 0x20, `005b26f0.c:80,126`). **ASSUMPTION** (consistent with the code, not proved at runtime): this selects 8-bit vs packed-16-bit output paths.
* The game's IDCT is **float** (`fVar1`, `_DAT_007af0xx` in `0065e280.c:7-29`) with
  clipping tables; OpenKeeper's is **integer** (`TgqFrame.java:379-419`).
  **Proved**: the implementations differ. **Not proved**: that OpenKeeper's
  pixel output is byte-identical to the game's.
* Bits 0/4/7 of header byte 4 and the packed bytes 5..7 demonstrably influence
  the game decoder (`005b26f0.c:21-31`, `00659b90.c:152-168`). **ASSUMPTION**:
  bit 0 marks predicted/macroblock motion, bit 4/7 select decoder variants;
  the exact semantics need dynamic tracing or a reference implementation with
  the four masks.
* `FUN_00659b90`'s motion-vector branch reads 8 bytes only when a bit is set;
  `FUN_0065aeb0` path may use a different prediction layout. Both are beyond
  what the Java source models.

---

## 8. Probe evidence — exact measured numbers

All commands were run from `/home/kali/Desktop/rea/Dungeon Keeper 2`; stdout was
captured under `port/.work/tqi_research/`.

### 8.1 Container scan (all 24 files, `walk=OK`)

`python3 port/tools/tgq_probe.py scan Data/Movies` → `port/.work/tqi_research/scan.txt`:

| File | Bytes | Chunks | `pIQT` | `SCDl` | `SCCl` |
|---|---:|---:|---:|---:|---:|
| BullfrogIntro.tgq | 1 986 192 | 379 | 189 | 187 | 187 |
| BullfrogOutro.tgq | 5 937 644 | 845 | 421 | 421 | 421 |
| CutSceneLevel1.tgq | 5 656 304 | 773 | 385 | 385 | 385 |
| CutSceneLevel2.tgq | 4 378 048 | 589 | 293 | 293 | 293 |
| CutSceneLevel3.tgq | 6 545 528 | 1109 | 553 | 553 | 553 |
| CutSceneLevel4.tgq | 3 454 392 | 705 | 351 | 351 | 351 |
| CutSceneLevel5.tgq | 5 960 656 | 537 | 267 | 267 | 267 |
| CutSceneLevel6.tgq | 5 541 728 | 979 | 488 | 488 | 488 |
| CutSceneLevel7.tgq | 4 727 324 | 703 | 350 | 350 | 350 |
| CutSceneLevel8.tgq | 9 449 524 | 1608 | 802 | 803 | 803 |
| CutSceneLevel9.tgq | 5 509 608 | 1085 | 541 | 541 | 541 |
| CutSceneLevel10.tgq | 3 770 548 | 609 | 303 | 303 | 303 |
| CutSceneLevel11.tgq | 5 980 112 | 529 | 263 | 263 | 263 |
| CutSceneLevel12.tgq | 6 580 968 | 1051 | 524 | 524 | 524 |
| CutSceneLevel13.tgq | 8 344 152 | 1151 | 574 | 574 | 574 |
| CutSceneLevel14.tgq | 11 844 368 | 1605 | 801 | 801 | 801 |
| CutSceneLevel15.tgq | 8 464 724 | 1311 | 654 | 654 | 654 |
| CutSceneLevel16.tgq | 12 824 976 | 1621 | 809 | 809 | 809 |
| CutSceneLevel17.tgq | 2 867 780 | 597 | 297 | 297 | 297 |
| CutSceneLevel18.tgq | 4 848 720 | 727 | 362 | 362 | 362 |
| CutSceneLevel19.tgq | 12 862 104 | 1624 | 810 | 811 | 811 |
| INTRO.TGQ | 40 589 016 | 6580 | 3288 | 3289 | 3289 |
| Outro.tgq | 28 535 944 | 4069 | 2033 | 2033 | 2033 |
| trailer.tgq | 6 785 068 | 1503 | 750 | 750 | 750 |

Totals: 213 445 428 bytes, 16 108 video frames, 16 109 audio blocks. The
`SCCl` u32 equals the `SCDl` count in **24/24** files. Three files have one extra
audio block (CutSceneLevel8, CutSceneLevel19, INTRO); BullfrogIntro has two
fewer audio blocks than video frames.

First chunks of `BullfrogIntro.tgq` (`tgq_probe.py chunks … --limit 8`,
`port/.work/tqi_research/chunks_BullfrogIntro.txt`):

```
#  offset  tag   size  pay_size  payload[0:16]
0  0       SCHl  40    32        50 54 00 00 00 01 02 06 01 65 1b 01 19 fd 85 03
1  40      pIQT  6492  6484      40 01 e0 01 64 14 1e 03 2f a5 34 f4 02 fa 0b e8
2  6532    SCCl  12    4         bb 00 00 00
3  6544    pIQT  6512  6504      40 01 e0 01 64 14 1e 03 2f a5 34 f4 02 fa 0b e8
4  13056   SCDl  980   972       80 03 00 00 00 00 00 00 00 00 00 00 11 44 55 02
5  14036   pIQT  6580  6572      40 01 e0 01 64 14 1e 03 2f a5 34 f4 02 fa 0b e8
6  20616   SCDl  952   944       64 03 00 00 62 f9 85 f9 a0 f5 1b f5 22 55 a1 f4
7  21568   pIQT  6656  6648      40 01 e0 01 64 14 1e 03 2f a5 34 f4 02 fa 0b e8
```

`0x195c = 6492` (verified with `xxd -g1 -l 64 Data/Movies/BullfrogIntro.tgq`).
**Correction:** `port/docs/TGQ_FORMAT.md:110` records this chunk as 2124 bytes;
that is a stale value (2124 *is* the minimum `pIQT` size, see below).

### 8.2 Frame header statistics (16 108 frames)

`port/.work/tqi_research/global_headers.txt`:

* width = 320 in 16108/16108 frames; height = 480 in 16108/16108.
* header bytes 5..7 = `14 1e 03` in 16108/16108.
* quantizer histogram: `0x62` → 5321 frames, `0x63` → 9882, `0x64` → 905.
* per-file first-frame quantizer: `0x62` for INTRO.TGQ and Outro.tgq, `0x64`
  for BullfrogIntro.tgq and trailer.tgq, `0x63` for the other 20 files.
* `pIQT` chunk sizes range 2124..32452 bytes.
* `tgq_decode.py info Data/Movies/BullfrogIntro.tgq` → 379 chunks / 189 frames,
  quantizer `0x64=189`, extra `14 1e 03` ×189, 0 MPEG-like triples.
* First six frame bitstream prefixes (`tgq_decode.py headers … --limit 6`) are in
  `port/.work/tqi_research/headers_BullfrogIntro.txt`; frame 0 starts
  `2f a5 34 f4 02 fa 0b e8 29 62 84 34 94 22 52 4a`.
* The first 38 frames of `INTRO.TGQ` are byte-identical blank/near-black frames
  (bitstream `bf 94 52 e8 0a e8 2f a0 …`, chunk size 2124); frames 38/39 begin
  to differ and decode to visible content (§9). `trailer.tgq` starts with the
  same constant blank bitstream and quantizer `0x64`.

### 8.3 Audio block equation

`16087/16109` blocks match `codedSamples == (payload_size-12)/30*28` exactly;
22 terminal blocks differ (examples: BullfrogIntro last block declared 176 vs
computed 196; many files' last block declared 881 vs computed 896), consistent
with OpenKeeper rounding `codedSamples` down to a multiple of 28
(`EAAudioFrame.java:50`).

---

## 9. Successful pixel decode — using the researched OpenKeeper source, not a new decoder

Environment check (`command -v`): `ffmpeg`, `ffprobe`, `mplayer`, `mpv`, `vlc`,
`gst-launch-1.0` are **absent**; Python `av`, `imageio`, `cv2`, `ffmpeg`,
`moviepy` are **absent**; `PIL`, `numpy` are present; `java`/`javac` 25.0.5 are
present; `convert`/`magick` are present but cannot decode TQI. Therefore no
external TQI decoder was available — but the exact Java decoder named in the
task was.

Reproducible harness added: `port/tools/TgqDecodeOne.java` (calls the unmodified
`TgqFile`/`TgqFrame` API and writes PNGs with `javax.imageio`) and
`port/tools/tgq_decode_openkeeper.sh` (compiles 12 **unmodified** OpenKeeper
source files plus the harness with `-cp jme3-core-3.9.0-stable.jar:vecmath-1.5.2.jar`
from `/home/kali/.gradle/caches`, then runs it). No network, no new codec code.

```bash
bash port/tools/tgq_decode_openkeeper.sh Data/Movies/BullfrogIntro.tgq \
     port/.work/tqi_research/frames_script 2
```

Output (`port/.work/tqi_rescript.log`):

```
audioHeader rate=22050 bits=16 channels=2 compression=EA XA ADPCM
frame=0 size=320x480 png=frame_000.png bytes=35413 lumaLineSize=320 cbLineSize=160 crLineSize=160
frame=1 size=320x480 png=frame_001.png bytes=36269 lumaLineSize=320 cbLineSize=160 crLineSize=160
movie=Data/Movies/BullfrogIntro.tgq decoded=2 videoFramesSeen=2
```

SHA-256 of the PNGs (identical when produced by a separate inline compile,
`port/.work/tqi_research/decode_bullfrog.log`):

```
a8c4184d589d9cc5db65f672ec08fc70c4e58df5fcc3a8f2035f0ec7fd0be4f1  frame_000.png
0da6fda1ada14922cd0b2e3e312fadb7a718bf722388ed5b770ae21d1c37131b  frame_001.png
a5f47d5900bc5d653184fc4e8a6419a2da1160afbc6e9b135f139b97eb6e07b8  frame_002.png
```

Pixel statistics (PIL/numpy, `port/.work/tqi_research/png_stats.txt`):
320×480 RGB; frame 0 mean ≈ (45.9, 32.0, 32.0), std ≈ (38.4, 12.5, 10.9),
6128 unique colours. Visual inspection of `frame_000.png` shows the red
Bullfrog "M" logo on a dark background — i.e. a real, non-synthetic image.
`INTRO.TGQ` frames 0..37 decode to a constant dark image `(16,16,16)`; frame 39
decodes to a visible DK2 temple/corridor scene (SHA
`532eab257886a847413f3e96ba3d2fd9308bc142ab7a4696bd215703c4253e7e`,
40950-byte PNG), confirming that the constant prefix is source content, not a
decoder failure at those frames.

**What this does and does not prove:** it proves the OpenKeeper Java algorithm
can convert shipped `pIQT` bitstreams to plausible pixels without any external
codec library. It does **not** prove bit-exact agreement with the game's float
IDCT/prediction paths; no reference frame from the running game was captured.

---

## 10. Confirmed facts / hypotheses / unknowns

### Confirmed (source + bytes + Ghidra)
* Container layout and chunk set; `SCHl`=32-byte `PT` audio header, `SCCl`=u32
  count, `SCDl`=EA XA ADPCM block, `SCEl`=empty, `pIQT`=video frame.
* 8-byte TQI frame header: `u16` W, `u16` H, `u8` quantizer, 3 unknown bytes;
  VLC bitstream begins at `+8`.
* OpenKeeper parses only the container/header; all video VLC/dequant/IDCT and
  all audio ADPCM decoding are implemented in its own Java classes.
* No `jlibtqi`/native TQI library dependency in this checkout.
* Game decoder addresses and roles (§7.1); game uses four runtime-built VLC
  tables and a float IDCT; OpenKeeper uses static trees and an integer IDCT.
* Audio codec EA XA ADPCM, 22.05 kHz, stereo; exact block equation holds for
  16087/16109 blocks.
* Real frames decode to PNG via the OpenKeeper source (§9) with reproducible
  hashes.

### Hypotheses (labelled, not asserted)
* `pIQT` ↔ `TQIp` reversal follows EA's video-fourcc naming convention
  (external wiki + local RTTI corroboration; reversal not proved by bytes).
* Header byte 4 bits 0/4/7 select prediction/alternate IDCT paths (game code
  reads them; exact meaning untraced). Bytes 5..7 are packed bit-shift state for
  those paths (`005b26f0.c:21-25`), not "always zero" as the wiki says for its
  samples.
* `DAT_007982d4` 0x10 vs 0x20 selects 8-bit vs packed-16-bit output.
* One audio block per video frame; the few off-by-one files have leading or
  trailing silence blocks.
* Frame rate: wiki says TQI is 15 fps, but OpenKeeper hard-codes 25 fps for
  playback (`TgqPlayer.java:52`), with the comment "The specs say 15 FPS, but
  with this they are totally in sync". Neither value is proved from the DK2
  files (no timestamps in the container).

### Unknowns
* The four VLC bitmasks passed to `FUN_005b2450` (indirect call; not in
  decompilation) and therefore exact equivalence of OpenKeeper's hard-coded
  trees to the game's runtime tables.
* Exact semantics of header bytes 5..7 in DK2 (`14 1e 03`) and the meaning of
  the per-file varying `SCHl` word at `+0x10` (`0x82xxxxxx`).
* Exact motion-compensation/prediction algorithm for the inter paths
  (`0x0065AEB0`/`0x0065CA50`) and reference-frame management.
* Whether the game's float IDCT output is bit-identical to OpenKeeper's integer
  IDCT; no in-game frame capture used as reference.
* Why 22 terminal audio blocks break the length equation; likely tail padding.
* Whether the earlier TGQ wiki page (request timed out on two fetches) uses the
  same `BASE_TABLE` scale as OpenKeeper.

---

## 11. Requirements for a future independent C decoder

1. **Do not copy OpenKeeper code verbatim.** It is GPL-3.0; reimplement the
   algorithm from the cited line ranges and cite it, or keep the decoder in a
   separately licensed component with the required notices. No jME/vecmath
   runtime dependency is needed: only `toUnsignedByte` is used from
   `ConversionUtils`.
2. **Container reader**: little-endian 8-byte chunk header, accept all five
   tags plus the `SHEN/SCEN/SDEN/SEEN` aliases; validate `SCCl == count(SCDl)`;
   stop at `SCEl`/EOF. Read the `PT` TLV header before audio blocks.
3. **Frame reader**: consume exactly 8 header bytes; retain quantizer and the
   three extra bytes; treat bytes ≥8 as the VLC bitstream (32-bit LE words,
   MSB-first symbol walk). 20×30 macroblocks for 320×480; 6 blocks per MB in
   standard order (4 Y, Cb, Cr).
4. **VLC**: implement both the DC luma/chroma trees and the 227-entry run/level
   tree with escape handling from `TgqFrame.java:83-139,217-329`. For a
   byte-exact game-compatible decoder, instead recover the four VLC masks and
   reproduce `FUN_005B2450`/`FUN_005B21E0` (four 1024-entry tables); the masks
   require dynamic tracing of the indirect caller.
5. **Dequantisation and IDCT**: start from the integer reference
   (`TgqFrame.java:193-201,352-419`) for a functional decoder; for game-exact
   output implement the float IDCT and clipping tables evidenced in
   `0x0065E280.c:7-29` and the setup at `0x005B1D7E`.
6. **Prediction**: implement header-bit-gated motion vector decoding from
   `00659b90.c:152-168` and the alternate `0x0065AEB0`/`0x0065CA50` family;
   OpenKeeper has no reference-frame store, so it cannot be the sole spec.
7. **Colour/YUV**: output three planes (Y at full resolution, Cb/Cr half
   resolution, strides 320/160/160) and either reproduce the JPEG coefficients
   in `TgqFrame.java:437-442` for RGB, or feed planes to a GPU shader the way
   `MovieMaterial.java:68-69,171-176` does.
8. **Audio (if in scope)**: EA XA ADPCM per `EAAudioFrame.java:28-102`; 12-byte
   header, 30-byte stereo groups, 28 samples/group, 22050 Hz.
9. **Validation fixtures**: use the PNG SHA-256 values in §9 (OpenKeeper intra
   reference) plus a captured game frame for the float/prediction paths; assert
   the 16108-frame header histogram and the 16087/16109 audio equation. Note
   that no pixel-output reference exists yet, so do not claim game-exactness
   without one.

---

## 12. Corrections to earlier documents and tools

| Item | Earlier claim | Corrected value / status |
|---|---|---|
| `pIQT` header size | `port/docs/TGQ_FORMAT.md:177-199` says 16 bytes | 8 bytes; bytes 8..15 are VLC data (`005b26f0.c:31`; `TgqFrame.java:178-183`) |
| `tgq_probe.py` | `VIDEO_HEADER_SIZE = 16`; `--strip-video-header` removed 16 bytes | corrected to 8; 16-byte values kept only as labelled `legacy_preview_u16` (this task) |
| First `BullfrogIntro` `pIQT` chunk | `TGQ_FORMAT.md:110` says 2124 bytes | 6492 bytes (`0x195c`), `xxd` and probe agree |
| Audio codec | "unknown / ADPCM-family plausible" (`TGQ_CODEC_ANALYSIS.md:22-24,165`) | identified as EA XA ADPCM by `EAAudioFrame.java` and the length equation |
| Decoder availability | "no decoder implemented; not feasible in reasonable scope" (`TGQ_CODEC_ANALYSIS.md:26-36,169-190`) | true for an *independent* decoder, but a complete Java reference exists in the researched OpenKeeper source; compiling it decoded real PNGs (§9) |
| `pIQT`↔`TQIp` / TGQ fourcc | `TGQ_CODEC_ANALYSIS.md:161` says TGQ uses `pQGT` | MultimediaWiki lists TGQ as `pQTG`; our files are TQI `pIQT` either way |
| Wiki frame header tail | wiki says bytes 5..7 "always zero" | DK2 has `14 1e 03` in all 16108 frames |

---

## 13. Reproduction

```bash
cd "/home/kali/Desktop/rea/Dungeon Keeper 2"

python3 port/tools/tgq_probe.py scan Data/Movies
python3 port/tools/tgq_probe.py info Data/Movies/BullfrogIntro.tgq
python3 port/tools/tgq_probe.py chunks Data/Movies/BullfrogIntro.tgq --limit 8
python3 port/tools/tgq_decode.py scan Data/Movies
python3 port/tools/tgq_decode.py headers Data/Movies/INTRO.TGQ --limit 5

bash port/tools/tgq_decode_openkeeper.sh Data/Movies/BullfrogIntro.tgq \
     port/.work/tqi_research/frames_script 3
sha256sum port/.work/tqi_research/frames_script/*.png

python3 -m pytest -q port/tests/test_tgq_codec.py
python3 -m pytest -q port/tests/test_tgq_research.py
```

External corroboration used (fetched 2026-11-23):
`https://wiki.multimedia.cx/index.php/Electronic_Arts_Formats` and
`https://wiki.multimedia.cx/index.php/Electronic_Arts_TQI`.
`https://wiki.multimedia.cx/index.php/Electronic_Arts_TGQ` timed out twice and
was not used as evidence.
