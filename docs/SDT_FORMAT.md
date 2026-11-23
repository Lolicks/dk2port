# Dungeon Keeper 2 `SDT` sound banks and `.map` indexes — reverse-engineered format

Target: `Data/Sound/Sfx/Global/*.sdt` (108 files, 5 512 samples total) and all
341 `.map` files in that directory (170 `*BANK.map`, 171 `*SFX.map`; 409 `.map`
files exist in the whole `Data/Sound` tree).
All facts were derived from the shipped bytes. No OpenKeeper or other fan-engine
source was consulted. Little-endian integers; absolute, 0-based file offsets.
Spec produced with `port/tools/sdt_probe.py`.

**The audio codecs were positively identified** by decoding the MPEG headers
and corroborated with the local `file(1)` magic and `mpg123` (see §5).

---

## 1. SDT container

```
+--------------------------------------------------------------+ 0x0000
| u32 N = sample count                                          |
| u32 offsets[N]        (absolute file offsets)                  |
+--------------------------------------------------------------+ 0x0004 + 4N
| sample[0] : [40-byte header][data data_size]                   |
| sample[1] : ...                                               |
| ...                                                           |
+--------------------------------------------------------------+ EOF
```

Verified on **108 / 108** files: `offsets[0] == 4 + 4*N`, offsets strictly
increasing, and for every sample `header_size == 40` and
`offset[i] + 40 + data_size == offset[i+1]` (last sample ends at EOF).

Evidence — `CasinoHD.sdt`, `xxd -g1 -l 64`:

```
00000000: 05 00 00 00 18 00 00 00 cd a0 00 00 f5 a0 00 00   ................
00000010: 9e b3 00 00 1b c4 00 00 28 00 00 00 8d a0 00 00   ........(.......
00000020: 52 4f 55 4c 45 54 54 45 20 57 48 45 45 4c 20 00  ROULETTE WHEEL .
00000030: 22 56 10 24 00 00 00 00 00 72 03 00 00 00 00 00  "V.$.....r......
```

`N = 5`; offsets `0x18, 0xA0CD, 0xA0F5, 0xB39E, 0xC41B`;
`4 + 4*5 = 24 = 0x18` = first sample offset. Sample 0: `header_size = 0x28 = 40`,
`data_size = 0xA08D = 41 101`, name `"ROULETTE WHEEL "`.

**Confidence: HIGH.**

---

## 2. 40-byte sample header

| Rel. off | Size | Type | Meaning | Confidence |
|---:|---:|---|---|---|
| `+0x00` | 4 | u32 | Header size, **always 40** (`0x28`) in 5 512/5 512 samples | HIGH |
| `+0x04` | 4 | u32 | Audio payload byte length | HIGH |
| `+0x08` | 32 | bytes | NUL-terminated ASCII sample name, zero-padded to 32 | HIGH |
| `+0x18` | 2 | u16 | Sample rate in Hz: `22050` (5 494 samples) or `44100` (18) | HIGH |
| `+0x1A` | 2 | u16 | Codec/format tag (see §4) | HIGH (mapping) / LOW (bit semantics) |
| `+0x1C` | 4 | u32 | Always 0 in all 5 512 samples | HIGH |
| `+0x20` | 4 | u32 | 0 for PCM/blank; for compressed samples ≈ decoded 16-bit PCM byte length (see §5.3) | MEDIUM |
| `+0x24` | 4 | u32 | Always 0 | HIGH |

Names include `bd_charge.mp2`, `ROULETTE WHEEL `, `wetcreak.mp2`,
`Jb_1.wav`, `Blank`, `3pt2-001.mp2`, `gascloud3d.mp2`. The `.mp2` suffix is a
**misnomer** for most samples (see §5); it is the game's own naming convention.

---

## 3. Codec/format tag mapping (exhaustive over 5 512 samples)

| `format_u16` | Samples | Observed payload | Codec | Confidence |
|---:|---:|---|---|---|
| `0x0010` | 999 | `data_size == 0`, names usually `Blank` | empty / placeholder | HIGH |
| `0x0210` | 211 | little-endian 16-bit samples near zero at start, names `*.wav` | uncompressed PCM16 | HIGH |
| `0x2410` | 4 014 | starts `FF F7 42 C0` (3 980×), `FF FF 20 C0` (24×) or `FF F5 62 C0` (10×) | **MPEG Layer I**, mono | HIGH |
| `0x2510` | 288 | starts `FF F5 B2 00` in 288/288 | **MPEG Layer II**, stereo | HIGH |

No `OggS`, `RIFF`/`WAVE`, `ID3` or ADPCM magic occurs inside any sample
payload. The only RIFF/WAVE in an SDT is produced by the extractor's
optional `--wav` wrapper.

---

## 4. MPEG audio framing (the key result)

The 22050 Hz mono samples begin with a valid MPEG audio frame header. Decoding
`FF F7 42 C0` bit by bit:

```
11111111 1111 0111 0100 0010 1100 0000
-------- ---  --  --  ---- -- - -- ----
sync(11) ver lay pr  br   sr  pad priv mode ...
ver=10 -> MPEG-2
layer=11 -> Layer I
protection=1 -> no CRC
bitrate_index=4 -> 64 kbps (MPEG-2 Layer I table)
sample_rate_index=0 -> 22050 Hz
padding=1
channel_mode=11 -> mono
```

MPEG Layer I frame length is
`((12 * bitrate) / sample_rate + padding) * 4` bytes:

| Header | padding | Computed frame length | Observed stride |
|---|---:|---:|---:|
| `FF F7 42 C0` | 1 | `(12*64000/22050 = 34) + 1) * 4` = **140** | 140 |
| `FF F7 40 C0` | 0 | `(34 + 0) * 4` = **136** | 136 |

In `CasinoHD.sdt` sample 0, the `FF F7` sync positions are
`0, 140, 280, 416, 556, 696, …` — the 140/136-byte alternation matches the
padding bit of each header exactly (`python3 port/tools/sdt_probe.py info
Data/Sound/Sfx/Global/CasinoHD.sdt` prints the histogram `[(140, 52), (136, 11)]`).
Across all 108 SDTs, 305 168 headers are `FF F7 42 C0` and 62 577 are
`FF F7 40 C0`.

Other MPEG variants found:

| Header | `file(1)` verdict | Samples |
|---|---|---|
| `FF F7 42 C0` | `MPEG ADTS, layer I, v2, 64 kbps, 22.05 kHz, Monaural` | 3 980 |
| `FF F5 B2 00` | `MPEG ADTS, layer II, v2, 112 kbps, 22.05 kHz, Stereo` | 288 (music tracks, e.g. `Track_3_2HD.sdt`) |
| `FF F5 62 C0` | `MPEG ADTS, layer II, v2, 48 kbps, 22.05 kHz, Monaural` | 10 |
| `FF FF 20 C0` | MPEG-**1** Layer I, 64 kbps, 44.1 kHz, mono; 68/72-byte frames (padding 0/1) | 24 (e.g. `gascloud3d.mp2`) |

Independent verification (extracted payload written by the tool):

```bash
$ file port/.work/sdt_extract/s0000_ROULETTE_WHEEL_.mp1
MPEG ADTS, layer I, v2,  64 kbps, 22.05 kHz, Monaural
$ mpg123 -q -w /tmp/out.wav port/.work/sdt_extract/s0000_ROULETTE_WHEEL_.mp1
$ ls -l /tmp/out.wav      # 226604 bytes ≈ 5.14 s of 16-bit mono @ 22050 Hz
```

**Confidence: HIGH** — the container's `format_u16`, leading magic bytes,
frame-stride math, `file(1)` identification and a successful `mpg123` decode
all agree.

### 4.1 PCM samples

`JackinBoxHW.sdt` sample 0 (`Jb_1.wav`, 77 718 bytes, `format=0x0210`) begins
`00 00 00 00 FF FF 00 00 00 00 FF FF …` — consecutive little-endian 16-bit
words forming a fade-in from silence; no MPEG sync. These are stored raw.
`--wav` wraps them in a 44-byte canonical RIFF/WAVE header (mono assumed —
**channel count for PCM samples remains unproven**).

### 4.2 Field `+0x20`

For `CasinoHD.sdt` sample 0 the stored value is `225 792`, close to the
226 592 bytes of 16-bit PCM produced by `mpg123`. For the 112 kbps stereo
track `Track_3_2HD.sdt` sample 0 it is `940 452`, again close to the decoded
PCM size. It is therefore best read as an approximate decoded-length hint,
**not** an exact byte count (off by ~0.3 %). **Confidence: MEDIUM.**

---

## 5. `.map` files — BANK flavour

GUID first byte `0x01`; GUID bytes: `01 2C 61 E9 D0 31 D2 11 B4 09 00 A0 C9 93 F2 03`.

```
0x00  16 bytes  GUID
0x10   8 bytes  all zero
0x18  u32       N = number of bank references
0x1C  N x 11-byte records:
           u32  0xFFFFFFFF
           u32  0x0066F318
           u24  0x73215B (e.g. "5b 22 73" / "5b 21 73")
       -> N x [u32 length][ASCII string incl. terminating NUL]
EOF
```

**170 / 170** `*BANK.map` files in `Global/` parse exactly to EOF with this
model. Example — `creature_bile_demonBANK.map` (91 bytes):

```
MAP: creature_bile_demonBANK.map (BANK)
count           : 2
bank names      : ['Global\\Bile', 'Global\\MiscCreatures']
record f0/f1/f2 : {'f0': 4294967295, 'f1': 6746904, 'f2': '5b 21 73'}
ends at EOF     : True
```

The names are **SDT bank stems**: `Global\Bile` → `BileHD.sdt` / `BileHW.sdt`;
`Global\Track_Zero` → `Track_Zero*.sdt`; `Global\Imp` → `ImpHW.sdt`, etc.
The 11-byte record's three fields are identical in every record of every file
inspected — they look like unused/placeholder serialized data.

The `u32 length` + string pairs are **unaligned** (they start at `0x1C + 11N`,
which is not 4-byte aligned for most N). The length includes the NUL.

---

## 6. `.map` files — SFX flavour

GUID first byte `0x00`; GUID: `00 2C 61 E9 D0 31 D2 11 B4 09 00 B0 C9 93 F2 03`.

```
0x00  16 bytes GUID
0x10   8 bytes zero
0x18  u32    = 1 (marker)
0x1C  u32    = N (number of sound entries)
0x20  u32    = 0
0x24  u16,u16 (e.g. 9,23 / 1,9 / 9,1) — unknown
0x28  3 x f32 (commonly 0.1, 2.0, 0.5) — likely 3D-audio rolloff params (LOW)
0x34  N x 20-byte records:
          u32  sound_id        (49…845 across all files)
          u32  field1          (mostly 1)
          u32  field2          == 0 in 171/171 maps
          u32  field3          (0…6000; often 320/310/280/40/20 …)
          u32  field4          (mostly 0)
      -> variable-length detail block with per-sound playback parameters
```

**171 / 171** `*SFX.map` files have a well-formed 20-byte record table at
`0x34` with `field2 == 0` for every record, and a non-empty trailing detail
block. Example — `creature_bile_demonSFX.map`:

```
count           : 37
floats @0x28    : [0.5, 2.0, 0.2]
detail block    : offset 792, 2786 bytes (not decoded)
#      sound_id  field1  field2  field3  field4
0      195       1       0       320     0
1      196       1       0       310     0
2      199       1       0       280     0
...
36     841       1       0       145     0
```

Across all SFX maps, recovered `sound_id` values span **49…845**. Total
samples across all 108 SDTs is **5 512**, so a sound id is plausibly a **global
sample index into the concatenated SDT bank set**, resolved through the
corresponding `*BANK.map` names. This is a **hypothesis (MEDIUM)**: the exact
concatenation order and the 11-byte BANK records' role are not established.

The trailing detail block (e.g. 2 786 bytes in the example) contains repeated
40-byte-ish groups with values `0x6464` (100,100), `0x3232` (50,50),
`0xFFFF`, floats like 20.0/5.0, and byte patterns `47 04 74 f0` / `a0 41 47 04
74 f0`. These look like per-sound 3D/volume envelopes but were **not decoded**.

**Confidence: HIGH** for GUID/header/record table; LOW for float semantics,
field1/3/4 semantics and the detail block.

---

## 7. HD/HW pairs

Most banks have two variants, e.g. `BileHD.sdt` (323 148 B) and `BileHW.sdt`
(514 432 B), both with 73 samples at 22050 Hz and identical sample names.
They are alternative encodes of the same sample set, **not** different sample
counts. `HW` files are usually larger in this GOG build; the quality/target
distinction is **unknown** (possibly different MPEG bitrates or an
uncompressed fallback).

---

## 8. Explicit unknowns / limitations

1. Semantics of `format_u16` bits (the mapping in §3 is empirical).
2. Exact meaning of sample-header `+0x20` (§4.2) and of `+0x1A` bit layout.
3. PCM channel count (mono assumed in the optional WAV wrapper).
4. `field1/3/4` in SFX records, the two u16 at `0x24`, the three floats at
   `0x28`, and the entire trailing detail block.
5. The 11-byte BANK record's three constant fields.
6. How a `sound_id` is resolved to a specific SDT sample in engine order.
7. Whether the 999 "blank" entries are deliberate placeholders.

---

## 9. Reproduction

```bash
cd "/home/kali/Desktop/rea/Dungeon Keeper 2"
python3 port/tools/sdt_probe.py info Data/Sound/Sfx/Global/CasinoHD.sdt
python3 port/tools/sdt_probe.py list Data/Sound/Sfx/Global/BileHD.sdt
python3 port/tools/sdt_probe.py map Data/Sound/Sfx/Global/creature_bile_demonBANK.map
python3 port/tools/sdt_probe.py map Data/Sound/Sfx/Global/creature_bile_demonSFX.map
python3 port/tools/sdt_probe.py extract Data/Sound/Sfx/Global/CasinoHD.sdt port/.work/sdt --name 'ROULETTE*'
python3 port/tools/sdt_probe.py scan Data/Sound/Sfx/Global
file port/.work/sdt/s0000_ROULETTE_WHEEL_.mp1
python3 -c 'import port.tools.sdt_probe'  # stdlib-only
```

`scan` reports 108 files, 5 512 samples; all offsets, headers and record
boundaries validated. The extractor writes raw audio bytes — no transcoding —
so the produced `.mp1`/`.mp2` files are byte-identical slices of the SDT.
