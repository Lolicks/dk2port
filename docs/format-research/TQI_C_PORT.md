# TQI C port — implementation and validation

Date: 2026-11-23. Scope: `port/` only. This document records what was actually
implemented, the exact validation numbers, the deliberate divergence from the
game executable, and the remaining limitations.

## 1. What was ported

A C11, stdlib-only TQI **intra-frame** decoder lives in `port/decoder/tqi/`:

| File | Contents |
|---|---|
| `tqi.h` | public API: frame struct, `dk2_tqi_decode_frame`, `dk2_tqi_frame_to_rgb24`, `dk2_tqi_frame_write_ppm`, `dk2_tqi_find_frames` |
| `tqi.c` | bit reader, VLC tree walk, dequantisation, zigzag/AC escape handling, integer IDCT, 4:2:0 planes, YCbCr→RGB, container walk |
| `tqi_tables.h` | static VLC/base tables, generated mechanically from `TgqFrame.java` |

The generation script is `port/tools/gen_tqi_tables.py`. It reads the Java
source and emits the header; no Java code is executed and no values were
hand-copied. It asserts 64/64/64/19/19/227 table row counts.

Algorithms ported from
`port/research/OpenKeeper/src/main/java/toniarts/openkeeper/video/tgq/TgqFrame.java`:

* `BitReader` (`.../tools/convert/BitReader.java`) — 32-bit **little-endian**
  words consumed MSB-first, including the `readIntSafe()` zero-padded tail and
  the exact `deficit` bookkeeping.
* `calculateDequantizationTable` — `qScale = (215 - 2*quantizer) * 5`,
  integer shift form.
* `decodeVlc` — binary-tree walk over the luma/chroma DC trees and the
  227-row run/level tree, with the leaf detection consuming one lookahead bit
  that is **not** counted in the consumed length.
* `mpeg1DecodeBlock` / `decodeDc` — DC prediction, AC run/level, `DCT_ESCAPE`
  (6-bit run + 8-bit level, with the `-128`/`0` special cases), zigzag placement.
* `eaIdctPut` / `eaIdctCol` / `idctTransform` / `clip` — the fixed-point IDCT
  with constants `ASQRT=181`, `A4=669`, `A2=277`, `A5=196`, plus the `block[0] += 4`
  DC bias and the zero-column shortcut.
* `getImage` — nearest-neighbour chroma upsampling and the JPEG YCbCr
  coefficients, including OpenKeeper's packed-int colour behaviour (see §4).
* `TgqFile.readFrame` — EA IFF chunk walk (`[tag][u32 LE size incl. 8-byte
  header][payload]`), collecting `pIQT` payloads.

Two implementation details were found by differential debugging and are worth
recording:

1. `decodeVlc` in Java starts `readLength` at **-1**. The final tree step reads
   one lookahead bit that selects the `-1` child; it is not part of the code.
   Skipping one bit too many desynchronised the stream after ~1.3 KB and made
   frame 0 fail with an AC run past coefficient 63.
2. Java packs `color = (r<<16)|(g<<8)|b` into a single 32-bit int and
   `TYPE_INT_RGB`/ImageIO keep the low 24 bits. A negative `g` sign-extends into
   the red byte (e.g. `r=137, g=-3, b=14` → `0xFFFFFD0E` → RGB `255,253,14`).
   Masking each channel separately produced 352 wrong pixels per frame; masking
   the packed word fixed all of them.

`dk2tool` gained:

```
dk2tool tgqdecode <movie.tgq> <outdir> [--frames N]
```

It writes `frame_%03d.ppm` (binary P6) using the frame's `pIQT` ordinal and
prints width/height, quantizer, the three extra header bytes and the byte count.
`port/decoder/build.sh` now compiles `tqi/tqi.c` into all three binaries and runs
`tests/test_tqi.c`.

## 2. Build and test (exact commands and results)

From `/home/kali/Desktop/rea/Dungeon Keeper 2`:

```
$ bash port/decoder/build.sh
mesh0: chunks=3 tris=1396 mats=3 verts=0
g_Maiden-Torture.kmf: type=2 tris=1396 tex=3
G_Maiden-electrocute.kmf: type=2 tris=1 tex=1
3DFE_GemHolder.kmf: type=2 tris=2922 tex=3
DHeartPlug4.kmf: type=1 tris=270 tex=4
PORTAL4.kmf: type=1 tris=10 tex=1
tgq: frames=189 audio=188 320x480
C DECODER TESTS: OK (0 failures)
C TQI TESTS: OK (0 failures)
C DECODER BUILD OK
```

`test_tqi.c` checks the generated tables, the container walk (189 frames, first
payload at offset 48, size 6484, trailing `SCEl`), decodes **all 189 frames**
without error, verifies the 8-byte header on every frame, checks the PPM writer,
checks FNV-1a hashes of the first 10 RGB images against the OpenKeeper PNGs, and
checks malformed-input error paths.

Header facts confirmed on every one of the 189 frames: `320x480`, quantizer
`0x64`, extra bytes `14 1e 03`, strides `320/160/160`.

## 3. Decoding and validation (exact commands and results)

Reference PNGs were regenerated with the **unmodified** OpenKeeper Java decoder:

```
$ mkdir -p port/.work/tqi_c_port/reference
$ bash port/tools/tgq_decode_openkeeper.sh \
      Data/Movies/BullfrogIntro.tgq port/.work/tqi_c_port/reference 3
audioHeader rate=22050 bits=16 channels=2 compression=EA XA ADPCM
frame=0 size=320x480 png=frame_000.png bytes=35413 lumaLineSize=320 cbLineSize=160 crLineSize=160
frame=1 size=320x480 png=frame_001.png bytes=36269 lumaLineSize=320 cbLineSize=160 crLineSize=160
frame=2 size=320x480 png=frame_002.png bytes=36836 lumaLineSize=320 cbLineSize=160 crLineSize=160
movie=Data/Movies/BullfrogIntro.tgq decoded=3 videoFramesSeen=3

$ sha256sum port/.work/tqi_c_port/reference/*.png
a8c4184d589d9cc5db65f672ec08fc70c4e58df5fcc3a8f2035f0ec7fd0be4f1  frame_000.png
0da6fda1ada14922cd0b2e3e312fadb7a718bf722388ed5b770ae21d1c37131b  frame_001.png
a5f47d5900bc5d653184fc4e8a6419a2da1160afbc6e9b135f139b97eb6e07b8  frame_002.png
```

These are the same hashes recorded in `TQI_TGQ_FORMAT.md` §9, confirming the
reference is reproducible.

C decode:

```
$ mkdir -p port/.work/tqi_c_port/c_ppm
$ ./port/decoder/build/dk2tool tgqdecode \
      Data/Movies/BullfrogIntro.tgq port/.work/tqi_c_port/c_ppm --frames 3
frame=0 size=320x480 quantizer=0x64 extra=141e03 ppm=.../frame_000.ppm bytes=460815
frame=1 size=320x480 quantizer=0x64 extra=141e03 ppm=.../frame_001.ppm bytes=460815
frame=2 size=320x480 quantizer=0x64 extra=141e03 ppm=.../frame_002.ppm bytes=460815
movie=Data/Movies/BullfrogIntro.tgq frames=189 decoded=3
```

Comparison with `port/decoder/tests/compare_ppm_png.py` (stdlib PPM reader,
ImageMagick PNG→PPM; reports per-pixel mean absolute difference):

```
$ for i in 0 1 2; do
    python3 port/decoder/tests/compare_ppm_png.py \
      port/.work/tqi_c_port/c_ppm/frame_00$i.ppm \
      port/.work/tqi_c_port/reference/frame_00$i.png --expect-exact
  done
```

| Frame | Pixels | Exact bytes | MAD (all) | MAD R | MAD G | MAD B | max abs diff |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 153600 | 460800/460800 | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 | 0 |
| 1 | 153600 | 460800/460800 | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 | 0 |
| 2 | 153600 | 460800/460800 | 0.000000000 | 0.000000000 | 0.000000000 | 0.000000000 | 0 |

**Result: byte-exact match, not merely a low difference.** The same comparison
was repeated for the first **10** frames (10 OpenKeeper PNGs, 10 C PPMs):
`exact frames compared: 10/10`, each `460800/460800 exact bytes`, `MAD=0`.

A single-command reproduction is:

```
$ bash port/decoder/tests/validate_tqi.sh 3
...
VALIDATED 3 frames: byte-exact vs OpenKeeper reference
```

All 189 frames of `BullfrogIntro.tgq` also decode without error
(`frames=189 decoded=189`, 0.9 s wall clock on this host).

## 4. Known differences from the game executable

This is a faithful port of the **OpenKeeper Java intra decoder**, not of the
`DKII.EXE` codec. The Ghidra research in `TQI_TGQ_FORMAT.md` §7 shows that the
game differs in ways this decoder deliberately does **not** reproduce:

* **No motion compensation / prediction.** `FUN_005B26F0` (`0x005B26F0`) reads
  header bits 0/4/7 and packed bytes 5..7; `FUN_00659B90` (`0x00659B90`) has a
  motion-vector branch, and `FUN_0065AEB0` / `FUN_0065CA50` form an alternate
  predicted family. OpenKeeper ignores header bytes 5..7 and always decodes
  intra blocks, so this C port does too. The shipped frames used here still
  decode exactly to OpenKeeper's output; we have no in-game capture to compare
  against, so **no claim of game-exactness is made**.
* **IDCT.** The game uses a floating-point IDCT with clipping tables
  (`FUN_005B1D7E` setup at `0x005B1D7E`, float path in `0x0065E280`). We port
  OpenKeeper's integer IDCT.
* **VLC tables.** The game builds four 1024-entry tables at runtime via
  `FUN_005B2450` (`0x005B2450`) and `FUN_005B21E0` (`0x005B21E0`); the four
  input masks are passed through an indirect call and are not statically
  recoverable. We use OpenKeeper's hard-coded 19+19+227-row trees instead.
* **Bitstream start.** Both agree that the bitstream begins at payload `+8`
  (`FUN_005B26F0` calls `FUN_00659580(param_1 + 8)`, `TgqFrame.java:177-183`).

The RGB output intentionally reproduces a Java quirk (§1, item 2): negative
intermediate `g` leaks into the red byte. This is what makes the comparison
byte-exact; it is not "correct" video maths.

## 5. Remaining limitations

* Audio (`SCHl`/`SCCl`/`SCDl` EA XA ADPCM) is skipped; the container walk only
  collects `pIQT`. `EAAudioFrame`/`EAAudioHeader` were read as spec sources but
  not ported.
* Only the `pIQT` (TQI) video chunk is handled. The alternate `SHEN`/`SCEN`/
  `SDEN`/`SEEN` aliases are skipped harmlessly as unknown tags.
* Dimensions are capped at 8192 and total coded area at 64 Mpixels; malformed
  headers return `DK2_ERR_SIZE` rather than allocating unbounded memory.
* The dequantisation computation uses 64-bit intermediates. For quantizers far
  outside the observed `0x62..0x64` range, Java's 32-bit `int` overflow would
  not be reproduced. This does not affect any shipped frame.
* `getImage()` indexes luma as `y*width+x` (Java does the same) rather than
  using `coded_width`. For non-16-aligned widths the C code returns
  `DK2_ERR_BOUNDS`/uses the padded plane exactly as Java does with its Buffer;
  all shipped frames are 320x480 so this path is unexercised.
* Validation reference is the OpenKeeper Java PNG, not a frame captured from
  the running game. Byte-exact agreement with OpenKeeper therefore proves a
  faithful port of that implementation, not of the game's float/predicted path.
* `port/tools/gen_tqi_tables.py` requires Python 3; the C decoder itself has no
  Python or third-party runtime dependency.

## 6. Files added or modified

Added:
`port/decoder/tqi/tqi.c`, `port/decoder/tqi/tqi.h`,
`port/decoder/tqi/tqi_tables.h`, `port/decoder/tests/test_tqi.c`,
`port/decoder/tests/compare_ppm_png.py`, `port/decoder/tests/validate_tqi.sh`,
`port/tools/gen_tqi_tables.py`, `port/docs/format-research/TQI_C_PORT.md`.

Modified:
`port/decoder/build.sh`, `port/decoder/tools/dk2tool.c`.
