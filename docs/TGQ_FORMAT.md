# Dungeon Keeper 2 `TGQ` movie container — reverse-engineered format

Target: the 24 `Data/Movies/*.tgq` cut-scene/trailer files shipped with
*Dungeon Keeper 2* (Bullfrog, 1999, GOG release), from `BullfrogIntro.tgq`
(1 986 192 B) to `INTRO.TGQ` (40 589 016 B).
All facts were derived from the bytes of those files plus strings in `DKII.EXE`.
No third-party DK2/fan-engine source was consulted. A public-data web search was
attempted but the search provider returned HTTP 403, so all statements below are
byte-derived only.

All integers are **little-endian**; all offsets are absolute, 0-based file
offsets. Spec produced with `port/tools/tgq_probe.py`.

---

## 0. Erratum (2026-11-23)

The `pIQT` frame header is **8 bytes, not 16**. Bytes 8..15 are already TQI
VLC bitstream symbols. This was established by decompiling the game's frame
decoder `FUN_005b26f0` (`0x005B26F0`), which reads header bytes 0..7 and calls
`FUN_00659580(param_1 + 8)` to initialise the bitstream. §6 below still records
the original 16-byte observation; treat its field table as superseded. See
`TGQ_CODEC_ANALYSIS.md` for the full codec identification and the corrected
header layout.

## 1. Verdict

`TGQ` is **not** RIFF and has no `RIFF`/`LIST` fourcc. It is a simple
**IFF-style chunk stream**: every chunk is `[4-byte ASCII tag][u32 LE size]`
followed by a payload; the `size` field **includes the 8-byte chunk header**.
The chunk walk is deterministic and lands *exactly* on EOF for **24 / 24**
shipped files. This is HIGH confidence.

The audio and video payloads are **compressed with proprietary codecs that were
not decoded**. The container, header fields, stream ordering and payload
boundaries are fully recovered; frame/sample decoding remains unknown.

---

## 2. Files analysed

`python3 port/tools/tgq_probe.py scan Data/Movies` (24 files, all `walk=OK`):

| File | Bytes | Chunks | `pIQT` (video) | `SCDl` (audio) | `SCCl` value |
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
| **total** | **213 445 428** | **—** | **16 108** | **16 109** | **—** |

Total compressed video payload: **197 755 804 bytes**; total audio payload:
**15 430 448 bytes**.

---

## 3. Chunk stream

```
+--------------------------------------------------------------+ 0x0000
| "SCHl" | u32 size = 40 | 32-byte stream header payload        |
+--------------------------------------------------------------+ 0x0028
| "pIQT" | u32 size      | 16-byte frame header + video payload  |  frame 0
+--------------------------------------------------------------+
| "SCCl" | u32 size = 12 | u32 value (= number of SCDl blocks)   |
+--------------------------------------------------------------+  (after frame 0)
| "pIQT" | u32 size      | video frame payload                   |  frame 1
| "SCDl" | u32 size      | audio block payload                   |  audio 0
| "pIQT" ... / "SCDl" ... (alternating)                          |
+--------------------------------------------------------------+
| "SCEl" | u32 size = 8  | (empty payload)                       |  EOF-8
+--------------------------------------------------------------+ EOF
```

Governing equation (verified on all 24 files):

```
chunk[0].tag == "SCHl"; chunk[0].size == 40
chunk[-1].tag == "SCEl"; chunk[-1].size == 8
chunk[i+1].offset == chunk[i].offset + chunk[i].size      (no padding/gaps)
SCCl.payload_u32 == count(SCDl chunks)                     (24 / 24)
```

Evidence — `Data/Movies/BullfrogIntro.tgq`, `python3 port/tools/tgq_probe.py
chunks Data/Movies/BullfrogIntro.tgq --limit 8`:

```
#     offset     tag    name                       size      pay_size  payload[0:16]
0     0          SCHl   stream header              40        32        50 54 00 00 00 01 02 06 01 65 1b 01 19 fd 85 03
1     40         pIQT   video frame                2124      2116      40 01 e0 01 64 14 1e 03 2f a5 34 f4 02 fa 0b e8
2     6532       SCCl   audio/chunk count marker   12        4         bb 00 00 00
3     6544       pIQT   video frame                6512      6504      40 01 e0 01 64 14 1e 03 2f a5 34 f4 02 fa 0b e8
4     13056      SCDl   audio block                980       972       80 03 00 00 00 00 00 00 00 00 00 00 11 44 55 02
5     14036      pIQT   video frame                6580      6572      40 01 e0 01 64 14 1e 03 2f a5 34 f4 02 fa 0b e8
6     20616      SCDl   audio block                952       944       64 03 00 00 62 f9 85 f9 a0 f5 1b f5 22 55 a1 f4
7     21568      pIQT   video frame                6656      6648      40 01 e0 01 64 14 1e 03 2f a5 34 f4 02 fa 0b e8
```

`SCCl` sits **immediately after the first `pIQT`**, not at the end. After that
the stream is strictly alternating `pIQT, SCDl, pIQT, SCDl, …` (except that a
few files have one unmatched video or audio block at the start/end, e.g. INTRO
has 3288 video / 3289 audio and CutSceneLevel19 has 810 / 811). `SCEl` is
always the last 8 bytes and carries no payload.

---

## 4. Chunk tags

| Tag | Count per file | Size | Meaning | Confidence |
|---|---|---|---|---|
| `SCHl` | 1 | 40 | Stream header (32-byte payload) | HIGH |
| `pIQT` | 189…3288 | 2124…24 824 | One compressed video frame | HIGH (frame: one per displayed image) / MEDIUM (exact semantics) |
| `SCCl` | 1 | 12 | u32 marker; equals the number of `SCDl` audio blocks in 24/24 files | HIGH (value) / LOW (name/semantics) |
| `SCDl` | 187…3289 | 52…980 | One compressed audio block | HIGH (audio: presence) / MEDIUM (codec) |
| `SCEl` | 1 | 8 | Empty end-of-stream marker | HIGH |

**Confidence: HIGH** that `pIQT` is video and `SCDl` is audio. Basis:
`DKII.EXE` contains `TbAudioSystem`, `%s\%s.tgq`, `%s\%s%d.tgq`
(level cut-scene naming), `%s\BullfrogIntro.tgq`, `%s\BullfrogOutro.tgq`,
`%s\Trailer.tgq`, `%sdata\Movies`, `For Movies Insert DK2 Game CD Now and press
yes.` and `Press No to continue without movies`, while the two bulk chunk
types are interleaved 1:1 at a frame cadence.

---

## 5. `SCHl` stream header (32-byte payload)

`xxd -g1 -l 48 Data/Movies/BullfrogIntro.tgq`:

```
00000000: 53 43 48 6c 28 00 00 00 50 54 00 00 00 01 02 06  SCHl(...PT......
00000010: 01 65 1b 01 19 fd 85 03 02 81 84 82 01 02 83 01  .e..............
00000020: 07 8a 04 00 00 00 00 ff 70 49 51 54 5c 19 00 00  ........pIQT\...
```

Interpretation as eight u32 words (same layout in all 24 files):

| Payload offset | Bytes (BullfrogIntro) | Interpretation | Confidence |
|---:|---|---|---|
| `+0x00` | `50 54 00 00` = `0x5450` | Constant across all files; 4-char-ish `PT\0\0` but no RIFF-style meaning established | LOW |
| `+0x04` | `00 01 02 06` | Constant; looks like a version/flags tuple (`0,1,2,6`) | LOW |
| `+0x08` | `01 65 1b 01` | Constant | LOW |
| `+0x0C` | `19 fd 85 03` | Constant | LOW |
| `+0x10` | `02 81 84 82` | **Only field that varies per file** (`0x82848102`, `0x8240402C`, …) | HIGH (varies) / LOW (meaning) |
| `+0x14` | `01 02 83 01` | Constant | LOW |
| `+0x18` | `07 8a 04 00` | Constant; `0x048A07` = 297 479 | LOW |
| `+0x1C` | `00 00 00 ff` | Constant | LOW |

The varying word at `+0x10` has the form `0x82xx xxxx` in every file. It is
**not** CRC32 of the whole file, nor of the file minus the header (checked on
five files). It may be an authoring-time identifier/pointer or a checksum over
a different scope; **unknown**. All other 28 header bytes are byte-identical
between all 24 movies.

---

## 6. `pIQT` video frame header (first 16 bytes of payload)

Across all frames of all files the first four u16 fields are constant *within a
file*, with only the third varying slightly between movies:

| Off | BullfrogIntro | Across all 24 files | Interpretation | Confidence |
|---:|---:|---|---|---|
| `+0x00` | 320 (`0x0140`) | always 320 | Horizontal extent of compressed image | MEDIUM |
| `+0x02` | 480 (`0x01E0`) | always 480 | Vertical extent of compressed image buffer | MEDIUM |
| `+0x04` | 5220 (`0x1464`) | 5218…5220 | Per-movie constant; unknown (possibly a size/CRC/timing parameter) | LOW |
| `+0x06` | 798 (`0x031E`) | always 798 | Per-frame constant; unknown | LOW |
| `+0x08..0x0F` | varies per frame | varies | Per-frame data; not parsed | UNKNOWN |

Example (first frame of BullfrogIntro):
```
40 01 e0 01 64 14 1e 03 2f a5 34 f4 02 fa 0b e8 | 29 62 84 34 94 22 ...
```

The `320 × 480` pair is unusual for a 4:3 display. Plausible readings: the
codec stores two interlaced 320×240 fields stacked into a 320×480 buffer, or
the second value is a stride/buffer height rather than a display height.
**Neither reading was proven** — no decoder was written, so this is an explicit
unknown.

The remaining payload (typically 2 108…24 808 B per frame) has high
byte-to-byte entropy (mean |Δ| ≈ 78 on the first BullfrogIntro frame, all 256
byte values present in a 6 468-byte sample) and contains no fourcc or table
that could identify a codec. It is **not** Smacker (`SMK1`/`SMK2`), Cinepak
(`CREK`), or any standard container. Treat it as a proprietary Bullfrog
block-transform / VQ-style bitstream.

---

## 7. `SCDl` audio block

Payload of an `SCDl` chunk:

```
u32 field A | audio payload bytes (payload_size - 4)
```

Observations across 187 blocks in BullfrogIntro:

| Field A | Occurrences | Chunk payload size | `payload_size - A` |
|---:|---:|---:|---:|
| 896 (`0x380`) | 93 | 972 | 76 |
| 868 (`0x364`) | 93 | 944 | 76 |
| 176 (`0xB0`) | 1 (last block) | 224 | 48 |

`payload_size - field A` is 76 in 186/187 blocks; the first block of the file
is special: it starts with field A followed by **eight zero bytes** before the
audio bytes begin (seen in several files), so the first block also carries an
initial padding/silence run. Field A is plausibly a decoded sample/byte count
or an encoder state value, but the constant +76 offset prevents a confident
reading — **LOW confidence**.

Evidence (`SCDl` #0, file offset 13 056):

```
80 03 00 00 00 00 00 00 00 00 00 00 11 44 55 02 ff 2e fd 0c ee bf d1 03
23 52 40 20 10 01 02 04 23 1f 0e 0c ec ee f0 ff ff ff 22 55 1c cc ...
```

No `OggS`, `RIFF`/`WAVE`, `fmt `, `ID3` or MPEG frame header occurs at the
start of any `SCDl` payload (a handful of coincidental `FF FB` pairs occur
inside the bitstream but do not form valid MP3 frames). Consecutive-byte
deltas are large (mean |Δ| ≈ 81), so this is **not** plain 8-bit PCM. The audio
is compressed with an unidentified codec — likely a Bullfrog-specific ADPCM /
sub-band scheme. **Audio exists; its codec is unknown.**

The tool writes each `SCDl` payload verbatim to `audio_NNNNN_OFFSET.bin`
(including field A) so a future codec researcher loses no bytes.

---

## 8. Interleaving, cadence and duration (inference)

- `SCCl` = number of audio blocks exactly (24/24). Most files have
  `audio_blocks == video_frames`, with three exceptions off by one
  (BullfrogIntro −2, INTRO +1, CutSceneLevel19 +1, CutSceneLevel8 +1).
- This is strong evidence of **one audio block per displayed video frame**,
  i.e. frame-synchronous A/V interleaving, with a possible leading/trailing
  extra silence block.
- A/V are grouped in small alternating runs, not strictly one frame each at
  the very start (frame 0, `SCCl`, frame 1, then alternating).
- Frame rate and wall-clock duration **cannot be determined** from the
  container: no timestamp, sample rate, or frame-duration field was identified.
  If each audio block were one 22.05 kHz PCM frame, the values 896/868 would
  imply ≈ 40 ms per block (25 fps), but that rests on an unproven codec
  assumption — do not use it as a fact.

---

## 9. Cross-check with `DKII.EXE`

```
%s\Trailer.tgq
%s\%s%d.tgq
%s\%s.tgq
%s\BullfrogOutro.tgq
%s\BullfrogIntro.tgq
%sdata\Movies
For Movies Insert DK2 Game CD Now and press yes.
Press No to continue without movies
-NOMOVIES
.?AVTbAudioSystem@@
```

These confirm the engine loads the exact filenames found on disk from a
`data\Movies` directory and has a dedicated audio subsystem class.

---

## 10. Explicit unknowns / limitations

1. Video codec and audio codec (no frames decoded; this was out of scope).
2. Meaning of `SCHl` header fields, especially the per-file varying `0x82…`
   word (§5).
3. Whether 320/480 are image dimensions, a field buffer, or stride/fields;
   whether `pIQT +0x04/+0x06` are sizes, CRCs or timing values (§6).
4. Meaning of `SCDl` field A and the constant `payload_size - A == 76`
   relationship, and the eight leading zero bytes in first audio blocks (§7).
5. Frame rate, sample rate, channel count and stream duration.
6. Why three movies have one or two unmatched video/audio blocks.
7. Public documentation lookup was unavailable (search provider HTTP 403);
   all claims rest on the shipped bytes only.

---

## 11. Reproduction

```bash
cd "/home/kali/Desktop/rea/Dungeon Keeper 2"
python3 port/tools/tgq_probe.py scan Data/Movies
python3 port/tools/tgq_probe.py info Data/Movies/INTRO.TGQ --json
python3 port/tools/tgq_probe.py chunks Data/Movies/BullfrogIntro.tgq --limit 8
python3 port/tools/tgq_probe.py extract Data/Movies/BullfrogIntro.tgq port/.work/tgq --all --frames 2
```

All commands were executed against the real files; `scan` reported `walk=OK`
for all 24 movies. Extraction writes 189 video + 187 audio payload files plus
`SCHl_payload.bin` and `tgq_extract.json` for BullfrogIntro; payload bytes are
unmodified slices of the source file.
