# EA XA ADPCM audio decoder — C11 port and validation

Date: 2026-11-23. Scope: `port/` only. This document records the algorithm as
implemented in `port/decoder/audio/`, the source line mapping, and the exact
commands/results used to validate it.

## 0. Sources

* Authoritative algorithm: `port/research/OpenKeeper/src/main/java/toniarts/openkeeper/video/tgq/EAAudioFrame.java`
  (`decodeFrame`, lines 52-109; `signExtend`, lines 118-122; coefficient table,
  lines 31-37). The task statement names the file without the `tgq/` directory;
  the path above is its actual location in this checkout.
* Container and `PT` audio header: `.../video/tgq/TgqFile.java` (tags 55-64;
  chunk dispatch 119-181; `readAudioHeader` 204-218; TLV parsing 220-285;
  defaults 287-297; `getValue` 355-366).
* Byte order: `.../tools/convert/BufferedResourceReader.java:59-69` sets
  `ByteOrder.LITTLE_ENDIAN` on every chunk buffer; `TgqFile.java:150` passes that
  buffer straight to `EAAudioFrame`. This is why `buf.getInt()` at
  `EAAudioFrame.java:63` reads a little-endian u32 (`80 03 00 00` = 896 in the
  first `SCDl` of `BullfrogIntro.tgq`).
* Format research: `port/docs/format-research/TQI_TGQ_FORMAT.md` §6 and §8.3
  (audio blocks `SCDl`; 16087/16109 shipped blocks satisfy
  `(payload-12)/30*28`).

No OpenKeeper source file was modified. The new harnesses
`port/tools/TgqAudioDump.java` and `port/tools/tgq_audio_dump_openkeeper.sh`
call the **unmodified** Java decoder to produce an independent PCM reference.

## 1. Algorithm mapping (Java line -> C)

| Java (`EAAudioFrame.java`) | C implementation |
|---|---|
| 53-60: reject non-`EA_XA_ADPCM`, require exactly 2 channels | `dk2_xa_decode_block` returns `DK2_ERR_UNSUPPORTED` for `channels != 2`; `parse_pt` in `tgq_audio.c` requires compression id 7 (`TgqFile.java:335-337`) and 2 channels before decoding anything |
| 31-37: `EA_ADPCM_TABLE[20]` | `dk2_xa_adpcm_table[20]` in `audio/xa.c`, identical values |
| 62-69: 12-byte header: `codedSamples = getInt(); codedSamples -= codedSamples % 28;`, then four signed i16 predictors (left current/previous, right current/previous) | `dk2_xa_read_block_header` — LE u32 via `dk2_u32le`, round down by `DK2_XA_GROUP_SAMPLES`, four `dk2_u16le` reads converted with two's-complement `u16 -> i16` |
| 65: `numberOfSamples = (capacity-12)/30*28` | used only as a structural cross-check in the research; **Java never uses this value to size or fill the output** — output length is `codedSamples * 4` bytes (line 72). The C decoder mirrors the actual behaviour: output frames = sum of rounded-down `codedSamples` |
| 72-74: output `byte[codedSamples*4]`, LE `ByteBuffer` | `int16_t` output, interleaved L/R; WAV writer emits each sample explicitly LE |
| 77-82: coefficient/shift byte: `b>>4` selects left table pair, `b&0x0F` right | `idx_l`, `idx_r`, `c1l/c2l/c1r/c2r` |
| 84-86: `shiftLeft = 20 - (b>>4)`, `shiftRight = 20 - (b&0x0F)` | `shift_l`, `shift_r` |
| 88-91: 28 nibbles; `signExtend(b>>4,4) << shiftLeft` and `signExtend(b,4) << shiftRight` | `sx4()` (portable equivalent of lines 118-122: mask low 4 bits, then sign-extend) and `nibble_shift()` (shifts through `uint32_t` to avoid C UB on negative `<<`) |
| 93-98: `(next + cur*coeff1 + prev*coeff2 + 0x80) >> 8` | identical expression using `sar32()` (portable arithmetic right shift, Java `>>` semantics) |
| 100-105: `previous <- current`, `current <- (short)next`, `putShort(L)`, `putShort(R)` | `prv_l = cur_l; cur_l = out_l; ...; pcm_out[2*i] = out_l; pcm_out[2*i+1] = out_r` |

All Java `short` values are promoted to signed `int` before arithmetic
(lines 94-98); the C code keeps predictors as sign-extended `int32_t` and
truncates back to `int16_t` only where Java casts with `.shortValue()`
(lines 101, 103) or calls `putShort` (104-105). Truncation and `int` wraparound
are implemented without implementation-defined casts
(`u16_to_i16_local`, `u32_to_i32`).

## 2. Public API (`port/decoder/audio/`)

`xa.h` / `xa.c`:

```c
extern const int dk2_xa_adpcm_table[20];
Dk2Result dk2_xa_decode_block(const uint8_t *block28, int16_t *pcm_out, int channels,
                              int32_t *predictor_left, int32_t *predictor_right);
Dk2Result dk2_xa_read_block_header(const uint8_t *payload, size_t payload_size,
                                   int32_t *coded_samples,
                                   int32_t *predictor_left, int32_t *predictor_right);
```

`block28` is one **30-byte packed group** (2 control bytes + 28 nibble bytes)
and produces 28 interleaved stereo frames (56 `int16_t`). Java carries four
predictor shorts; each `int32_t *predictor_*` therefore packs one channel's
pair: **low 16 bits = current**, **high 16 bits = previous**, both sign-extended.
Use `dk2_xa_predictor_pack()`, `dk2_xa_predictor_current()` and
`dk2_xa_predictor_previous()` from `xa.h`; seed from `dk2_xa_read_block_header()`
for the first group and pass the state through subsequent groups.

`tgq_audio.h` / `tgq_audio.c`:

```c
typedef struct { ... } Dk2TgqAudioInfo;   /* blocks, sample_frames, declared_total,
                                             expected_blocks, sample_rate, channels,
                                             bits_per_sample, clip_min, clip_max */
Dk2Result dk2_tgq_audio_decode(const uint8_t *tgq, size_t n,
                               int16_t **pcm_out, size_t *frames_out, Dk2TgqAudioInfo *info);
void dk2_tgq_audio_free(int16_t *pcm);
Dk2Result dk2_tgq_audio_extract(const uint8_t *tgq, size_t n, const char *out_wav);
```

`dk2_tgq_audio_extract` walks all IFF-style chunks, parses the `SCHl` `PT` TLV
(channels/compression/total-samples), validates `SCCl == number of SCDl chunks`,
decodes every `SCDl` (accepting the `SHEN/SCEN/SDEN/SEEN` aliases of
`TgqFile.java:55-64`), and writes a canonical 44-byte-header RIFF/WAVE file:
PCM format 1, 2 channels, 22050 Hz (the Java default at `TgqFile.java:294` —
the `PT` TLV has no sample-rate tag), 16 bits (`TgqFile.java:291`), 88200 bytes/s,
4-byte block align. Sample rate is therefore a **source-cited default**, not an
invented value.

CLI: `dk2tool tgqaudio <movie.tgq> <out.wav>` (added in `tools/dk2tool.c`).

## 3. Validation — exact commands and results

Worked from `/home/kali/Desktop/rea/Dungeon Keeper 2`.

### 3.1 Build and C self-tests

```bash
cd port/decoder && bash build.sh
```

```
C DECODER TESTS: OK (0 failures)
C TQI TESTS: OK (0 failures)
audio: blocks=187 frames=164220 declared=164228 rate=22050 ch=2 clip_min=0 clip_max=0 pcm_fnv1a=d9f24506aa44b243
window[0:8] = 1280 1280 1200 1712 869 1349 1327 753
audio: deterministic redecode hash=d9f24506aa44b243
wav: build/BullfrogIntro_audio.wav data_bytes=656880 riff_size=656916
C AUDIO TESTS: OK (0 failures)
C DECODER BUILD OK
```

`tests/test_audio.c` checks: the 20 table constants; argument/2-channel error
paths; `signExtend`/shift arithmetic on hand-computed groups; predictor
carry-over across the 28 samples; an **independent** chunk walk that re-counts
187 `SCDl`, `SCCl = 187`, raw declared samples 164228 and rounded 164220;
the decoded info/frame count; zero `INT16_MIN`/`INT16_MAX` samples (no clipping;
NaN is impossible for `int16_t`); deterministic re-decode (identical FNV-1a
hash); and canonical WAV header fields.

### 3.2 WAV extraction via the CLI

```bash
cd "/home/kali/Desktop/rea/Dungeon Keeper 2"
./port/decoder/build/dk2tool tgqaudio Data/Movies/BullfrogIntro.tgq \
    port/decoder/build/BullfrogIntro_audio.wav
python3 -c "import wave; w=wave.open('port/decoder/build/BullfrogIntro_audio.wav'); print(w.getparams())"
xxd -l 48 port/decoder/build/BullfrogIntro_audio.wav
```

```
tgqaudio: Data/Movies/BullfrogIntro.tgq blocks=187 frames=164220 declared=164228 rate=22050 ch=2 bits=16 clip_min=0 clip_max=0
tgqaudio: wrote port/decoder/build/BullfrogIntro_audio.wav (164220 sample frames)
_wave_params(nchannels=2, sampwidth=2, framerate=22050, nframes=164220, comptype='NONE', compname='not compressed')
00000000: 5249 4646 1406 0a00 5741 5645 666d 7420  RIFF....WAVEfmt
00000010: 1000 0000 0100 0200 2256 0000 8858 0100  ........"V...X..
00000020: 0400 1000 6461 7461 f005 0a00 0005 0005  ....data........
```

### 3.3 Independent OpenKeeper Java reference

```bash
bash port/tools/tgq_audio_dump_openkeeper.sh \
     Data/Movies/BullfrogIntro.tgq port/.work/audio/BullfrogIntro_openkeeper.raw
```

```
audioHeader rate=22050 bits=16 channels=2 compression=EA XA ADPCM
movie=Data/Movies/BullfrogIntro.tgq blocks=187 frames=164220 bytes=656880
```

### 3.4 PCM comparison (MAD / exact match)

```bash
python3 port/decoder/tests/compare_audio.py \
    port/decoder/build/BullfrogIntro_audio.wav \
    port/.work/audio/BullfrogIntro_openkeeper.raw 4096
```

```
c_samples=328440 java_samples=328440 compared=328440
full: exact=328440/328440 (100.000000%) MAD=0.000000 max_abs=0
first_4096: exact=4096/4096 (100.000000%) MAD=0.000000 max_abs=0
first_16_c = [1280, 1280, 1200, 1712, 869, 1349, 1327, 753, 988, -62, 926, -1082, 356, -1526, -946, -1687]
first_16_java = [1280, 1280, 1200, 1712, 869, 1349, 1327, 753, 988, -62, 926, -1082, 356, -1526, -946, -1687]
first_mismatch_index = None
```

The PCM payloads are byte-identical:

```bash
tail -c +45 port/decoder/build/BullfrogIntro_audio.wav | sha256sum
sha256sum port/.work/audio/BullfrogIntro_openkeeper.raw
```

```
3a5400f2ba383a2c43de437f728878fd2c64273ecbdfac9d6eb7410e42c0917b  -
3a5400f2ba383a2c43de437f728878fd2c64273ecbdfac9d6eb7410e42c0917b  port/.work/audio/BullfrogIntro_openkeeper.raw
```

Cross-check on two more movies (same commands, different inputs):

```
trailer.tgq        750 blocks, 661500 frames, declared 661500 -> full exact=1323000/1323000, MAD=0, max_abs=0
CutSceneLevel8.tgq 803 blocks, 708232 frames, declared 708245 -> full exact=1416464/1416464, MAD=0, max_abs=0
```

`CutSceneLevel8.tgq` exercises the terminal-block edge case: declared total
708245, decoded 708232 (13 samples dropped by the Java `codedSamples %= 28`
rounding), and the Java reference still matches bit-for-bit.

### 3.5 Robustness over all shipped movies

```bash
for f in Data/Movies/*.tgq; do \
  ./port/decoder/build/dk2tool tgqaudio "$f" /dev/null; \
done
```

All 24 movies: exit 0, `clip_min=0 clip_max=0`, `rate=22050 ch=2 bits=16`.
Block counts match the `SCDl` column in
`port/docs/format-research/TQI_TGQ_FORMAT.md` §8.1 (e.g. 187 for
BullfrogIntro, 3289 for INTRO.TGQ, 803 for CutSceneLevel8).

## 4. Limitations / honest scope

* The reference is OpenKeeper's Java reimplementation, which is the algorithm
  the task designates as authoritative. No recording from the running DK2 game
  was captured, so this validates agreement with that source, not with the
  game binary's actual audio path. No audio codec code in `DKII.EXE` was
  reversed for this task.
* Every shipped file happens to use 22050 Hz / 16-bit / 2 channels. A movie
  whose `PT` header declares PCM or a different channel count is rejected with
  `DK2_ERR_UNSUPPORTED` (the same restriction as `EAAudioFrame.java:53-60`).
* Terminal blocks whose declared count is not a multiple of 28 are rounded
  **down**, exactly as `EAAudioFrame.java:64` does; their trailing nibble bytes
  are ignored. `EAAudioFrame.java:65`'s `numberOfSamples` is computed but never
  used, and the C port does not emit that many samples either.
* Pre-existing quirk (not changed): `dk2_tgq_parse` in `port/decoder/tgq/tgq.c`
  increments `audio_count` for `SCEl` as well as `SCDl`, so it reports 188 for
  BullfrogIntro. The extractor counts only real audio blocks (187) and
  cross-checks against `SCCl`.
* No external decoder (`ffmpeg`, `mplayer`, Python `av`) is installed in this
  environment; the Java reference was therefore the obtainable reference and
  was used.

## 5. Files added / modified under `port/`

Added:
`decoder/audio/xa.h`, `decoder/audio/xa.c`, `decoder/audio/tgq_audio.h`,
`decoder/audio/tgq_audio.c`, `decoder/tests/test_audio.c`,
`decoder/tests/compare_audio.py`, `tools/TgqAudioDump.java`,
`tools/tgq_audio_dump_openkeeper.sh`, `docs/format-research/XA_ADPCM_PORT.md`.

Modified:
`decoder/build.sh` (audio sources in `SRC`, build/run `test_audio`),
`decoder/tools/dk2tool.c` (include, `cmd_tgqaudio`, dispatch, usage).
