# Audio pipeline — SDT banks to iOS runtime assets

Status: **tooling ready, local AAC encoding unavailable** (see §8). This document
specifies `tools/build_audio.py` and the manifest it emits. It does not claim
that a full conversion has been run or that the runtime plays the result.

## 1. Input and decisions

`Data/Sound/Sfx/Global` of the original game contains 108 SDT banks / 5512
samples. Measured with `tools/sdt_probe.py`:

| Source codec | Samples | Payload bytes |
|---|---:|---:|
| `mpeg-layer1` (64 kbps / 22.05 kHz mono) | 4014 | ~49.3 MiB |
| `mpeg-layer2` (112 kbps / 22.05 kHz stereo) | 288 | ~53.3 MiB |
| `pcm16le` (22.05 kHz, channel count unproven; assumed mono) | 211 | ~9.9 MiB |
| `empty/blank` (`data_size == 0`) | 999 | 0 |
| **Total** | **5512** | **~112.5 MiB** |

The previous importer (`tools/import_assets.py`) wrote raw payloads (~112 MiB)
plus ~4512 decoded WAVs (~619 MiB). That is too large for the IPA. User
decision implemented here:

- music/long SFX → AAC-LC, 192 kbps, `.m4a` container;
- short/UI/speech sounds → lossless PCM16 WAV;
- blank entries keep a manifest row with `path: null`;
- original SDT payloads stay in the private LFS repo, never re-shipped.

## 2. Output layout

`--out` is the **Assets** directory (the same directory used by
`tools/import_assets.py`). The audio manifest deliberately does not overwrite
that importer's `Assets/manifest.json`:

```
<out>/audio/manifest.json                  runtime audio manifest (schema below)
<out>/audio/.build_audio_state.json        idempotency cache (not shipped as content)
<out>/audio/<BANK>/sNNNN_<name>.m4a        AAC, non-critical
<out>/audio/<BANK>/sNNNN_<name>.wav        PCM16, critical
<out>/audio/.wav-cache/<BANK>/sNNNN_<name>.wav   intermediate PCM (deleted by default)
```

`<BANK>` is the SDT stem, `NNNN` the zero-padded sample index, `<name>` the
sanitized sample name. Manifest paths are POSIX-style and relative to `--out`.

## 3. CLI

```bash
python3 tools/build_audio.py --game-root <dir> --out <dir> [--ffmpeg <path>]
```

Exact required invocation on a machine with ffmpeg installed:

```bash
python3 tools/build_audio.py \
  --game-root "/path/to/Dungeon Keeper 2" \
  --out ios/DK2Port/Assets \
  --ffmpeg "$(command -v ffmpeg)"
```

Optional flags:

| Flag | Effect |
|---|---|
| `--afconvert <path>` | explicit macOS `afconvert` path |
| `--mpg123 <path>` | explicit Linux MPEG decoder path |
| `--keep-wavs` | retain `.wav-cache` intermediates for inspection/re-encode |
| `--limit N` | process only the first N SDT banks (smoke test) |
| `--force` | ignore the state cache and re-encode every artifact |
| `--self-test` | run built-in tests on synthetic SDT data and exit |

The tool exits non-zero with a clear message **before writing anything** if a
non-critical sample exists but no AAC encoder is available, or if an MPEG
sample exists but no decoder is available. It never silently substitutes WAV
for AAC.

## 4. External commands (exact)

Tool selection is deterministic: decoder = `ffmpeg` (or `--ffmpeg`) if present,
else `mpg123`; AAC encoder = `ffmpeg` if present, else macOS `afconvert`.
Selection and tool versions are recorded in the manifest's `tools` section.

Decode MPEG elementary stream to canonical 16-bit PCM WAV:

```bash
# ffmpeg path (preferred)
ffmpeg -nostdin -hide_banner -loglevel error -y \
  -i <raw.mp1|raw.mp2> -vn -map_metadata -1 -c:a pcm_s16le <tmp.wav>

# mpg123 fallback (Linux)
mpg123 -q -w <tmp.wav> <raw.mp1|raw.mp2>
```

The decoder WAV is parsed and rewritten with a canonical 44-byte RIFF header
(`<H` sample rate, actual channel count from `fmt `), so decoder-specific
header/metadata differences cannot affect output bytes.

Encode AAC (non-critical):

```bash
# ffmpeg (Linux and macOS via brew)
ffmpeg -nostdin -hide_banner -loglevel error -y \
  -i <canonical.wav> -vn -map_metadata -1 \
  -c:a aac -b:a 192k -movflags +faststart <out.m4a>

# afconvert fallback (macOS only)
afconvert -f m4af -d aac -b 192000 -q 127 <canonical.wav> <out.m4a>
```

Critical samples are copied byte-for-byte from the canonical WAV (no encoder
call). Original PCM payloads get a canonical WAV header directly.

## 5. Classification rules

`classify()` in `tools/build_audio.py` uses **SDT metadata only** — never
decoder output — so the decision is identical on Linux and macOS:

1. `codec == "empty/blank"` → not critical, reason `blank`, no artifact.
2. `codec == "pcm16le"` → critical, reason `pcm16-source-lossless` (already
   lossless; re-encoding to AAC would only add loss).
3. Sample name contains one of the keywords (case-insensitive substring):
   `speech`, `voice`, `ui`, `click`, `menu`, `button`, `select`, `beep`,
   `alarm`, `confirm`, `cancel`, `hint` → critical, reason
   `name-keyword:<word>`.
4. Estimated duration `< 0.6 s` → critical, reason `short-duration`.
5. Otherwise → non-critical (AAC), reason `music-sfx`.

Duration estimate: `field_20 / (rate * 2 * channels)` where `field_20` is the
SDT's decoded-length hint, `channels = 2` for `mpeg-layer2` else 1; PCM uses
`data_size / (rate * 2)`. If the hint is zero, MPEG frame headers are parsed
(Layer I / Layer II bitrate tables) or a codec-default bitrate is used. `field_20`
is approximate (per `docs/SDT_FORMAT.md` §4.2), so the 0.6 s boundary is a
policy boundary, not a measured sample length; the **measured** decoded length
is recorded separately as `duration_ms`.

Deterministic dry classification of all 108 banks (read-only, no encoding):

| Output | Samples | Reason breakdown |
|---|---:|---|
| `.m4a` AAC 192k | 3546 | `music-sfx` 3546 |
| `.wav` PCM16 | 967 | `pcm16-source-lossless` 211, `short-duration` 730, `name-keyword` 26 |
| blank (no artifact) | 999 | `blank` 999 |

## 6. Manifest

Schema: `tools/audio_manifest.schema.json` (JSON Schema draft 2020-12).
Required per-entry fields:

| Field | Type | Meaning |
|---|---|---|
| `id` | string | Stable `<bank>/<index:04d>`, e.g. `AmbienceHD/0007` |
| `bank` | string | SDT stem |
| `index` | int | Sample index inside the bank |
| `codec` | enum | `aac`, `pcm16`, `blank` (**output** codec) |
| `rate` | int | Effective sample rate in Hz |
| `path` | string or null | Runtime artifact path relative to `--out` |
| `wav_source` | string or null | Intermediate WAV path under `audio/.wav-cache`; regenerable, removed by default |
| `critical` | bool | `true` = lossless PCM shipped |

Additional informational fields: `name`, `source_codec` (original SDT codec),
`channels`, `duration_ms`, `estimated_duration_ms`, `reason`, `bytes`,
`source_sha256`, `output_sha256`. Top level carries `source`, `policy`,
`tools`, `counts`, `total_bytes`, `wav_cache_retained` and `entries`.

`path: null` on blanks means "do not load"; the runtime should skip such rows.
The iOS `DK2AudioController` already opens files with `AVAudioFile(forReading:)`,
which accepts both the `.m4a` and `.wav` artifacts; wiring it to enumerate
`audio/manifest.json` (instead of the old `audio.json`) is separate work and is
**not** changed by this task.

## 7. Determinism and idempotency

- Banks are processed in sorted filename order, samples by index; JSON is
  written with sorted keys, fixed indentation and LF newlines via atomic
  replace.
- WAV bytes are canonicalized, so decoder/header differences do not leak.
- `audio/.build_audio_state.json` stores, per `id`, source SHA-256, output
  SHA-256 and the full entry. A rerun reuses an entry only when the source hash,
  output file existence and output hash all still match; otherwise that entry
  is rebuilt. `--force` bypasses reuse.
- Tools used are persisted in the state so fresh and fully-cached runs emit a
  byte-identical manifest.
- AAC bytes are deterministic for a fixed ffmpeg/afconvert build and fixed
  arguments; tool identity/version is recorded, but cross-version byte
  equality is not claimed.

## 8. Local verification status (honest)

Environment used to author this tool:

| Tool | Status |
|---|---|
| `python3` | available; `python3 -m py_compile tools/build_audio.py` passes |
| `mpg123` | available (`/usr/bin/mpg123`) — decoder path ready |
| `ffmpeg` / `ffprobe` | **missing** locally |
| `afconvert` | **missing** locally (macOS only) |

No AAC artifacts were produced locally and no size measurements from a real
run are claimed. `python3 tools/build_audio.py --self-test` passes **22 checks**
on synthetic SDT fixtures: classification rules, canonical WAV parsing,
command construction, end-to-end PCM output, schema validation, deterministic
IDs/paths, byte-identical manifest on rerun, and the AAC path exercised with an
injected fake encoder (clearly a test stub, not a real AAC result). The tool is
ready for CI, where ffmpeg or afconvert exists.

## 9. Expected output sizes (metadata estimates, not measured)

| Set | Estimate |
|---|---:|
| AAC, 3546 samples, ~10 045 s total @ 192 kbps CBR | ~230 MiB |
| PCM16 WAV, 967 critical samples (~field_20 sum) | ~23 MiB |
| Manifest + state | < 1 MiB |
| **New audio total** | **~253 MiB** |
| Old importer (WAV 619 MiB + raw 112 MiB) | ~731 MiB |
| **Reduction** | **~65 %** |

`field_20` is off by ~0.3 % on sampled banks, and CBR AAC size depends on the
encoder; treat these as planning figures and publish measured numbers only
after a CI run with a real encoder.

## 10. How CI should call it on macOS (not applied here)

The current `.github/workflows/build.yml` runs `import_assets.py --skip-audio`;
keep that so the bundle manifest and WAD/map data are unchanged, then add an
audio step writing into the same Assets tree:

```bash
# macOS runner; afconvert is built in, ffmpeg is optional
brew install ffmpeg   # optional: afconvert fallback exists without it

python3 tools/import_assets.py --game-root game \
  --out ios/DK2Port/Resources/Assets --skip-audio

python3 tools/build_audio.py \
  --game-root game \
  --out ios/DK2Port/Resources/Assets \
  --ffmpeg "$(brew --prefix ffmpeg)/bin/ffmpeg"

test -s ios/DK2Port/Resources/Assets/audio/manifest.json
python3 -c 'import json; m=json.load(open("ios/DK2Port/Resources/Assets/audio/manifest.json")); assert m["source"]["samples"] == 5512'
```

Because the audio files are `.m4a`/`.wav`, ensure the Xcode target copies
`Resources/Assets/audio` (the existing Asset catalog/Resources copy phase
covers `Resources/Assets` as a folder reference). CI workflow files, Xcode
project files, status files and engine/runtime code were intentionally **not**
modified by this task.

## 11. Verification commands

```bash
python3 -m py_compile tools/build_audio.py
python3 tools/build_audio.py --self-test
python3 -m json.tool tools/audio_manifest.schema.json >/dev/null
```

Original SDT payloads are copyrighted game data: they remain in the private
LFS repo. Only the derived runtime artifacts described here are intended for
the app bundle.
