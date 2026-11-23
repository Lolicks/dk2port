# Dungeon Keeper 2 `KWD` / `KLD` chunk format — reverse-engineered specification

Target: the proprietary data files shipped with *Dungeon Keeper 2* (Bullfrog,
1999, GOG release), in `Data/editor/*.kwd` and `Data/editor/maps/*.kld`.
All facts below were derived from the bytes of the shipped files. No source from
OpenKeeper or another DK2 fan engine was consulted. All integers are
**little-endian (u32)**; all offsets are absolute, 0-based file offsets.

Spec produced by `port/tools/kwd_tool.py`; machine-readable recovered data in
`port/docs/game_data.json`; assertions in `port/tests/test_kwd.py`.

---

## 1. Files analysed

| File | Root tag | Version | Declared size = file size | Records | Record bytes |
|---|---:|---:|---:|---:|---:|
| `Data/editor/Rooms.kwd` | `0x78` | 4 | 27 486 | 26 | 1 055 |
| `Data/editor/Traps.kwd` | `0x82` | 4 | 9 320 | 16 | 579 |
| `Data/editor/Doors.kwd` | `0x8C` | 4 | 4 984 | 8 | 616 |
| `Data/editor/KeeperSpells.kwd` | `0x96` | 4 | 5 334 | 13 | 406 |
| `Data/editor/CreatureSpells.kwd` | `0xA0` | 4 | 10 962 | 41 | 266 |
| `Data/editor/Creatures.kwd` | `0xAA` | 4 | 250 710 | 46 | 5 449 |
| `Data/editor/Terrain.kwd` | `0x6E` | 4 | 22 688 | 41 | 552 |
| `Data/editor/Objects.kwd` | `0xF0` | 4 | 129 686 | 145 | 894 |
| `Data/editor/EffectElements.kwd` | `0xFA` | 4 | 108 528 | 596 | 182 |
| `Data/editor/Effects.kwd` | `0x10E` | 4 | 91 076 | 370 | 246 |
| `Data/editor/Shots.kwd` | `0x104` | 4 | 15 591 | 65 | 239 |
| `Data/editor/GlobalVariables.kwd` | `0xE6` | 4 | 9 992 (see §7) | 621 | 16 |
| `Data/editor/maps/*Map.kld` (66 files) | `0x64` | 4 | varies | n/a | n/a |

Root tags are assigned in steps of `0x0A` across related editor data sets
(`0x78` rooms, `0x82` traps, `0x8C` doors, `0x96` keeper spells, `0xA0`
creature spells, `0xAA` creatures). **Confidence: HIGH** (observed on all files).

---

## 2. Overall layout

```
+-----------------------------------------------------------+ 0x0000
| root tag u32     (e.g. 0xAA for Creatures.kwd)             |
| version u32      (4 in every file examined)               |
| declared size u32 (= total byte length, except §7)        |
+-----------------------------------------------------------+ 0x000C
| chunk[0] : [tag u32][payload_len u32][payload ...]        |
| chunk[1] : [tag u32][payload_len u32][payload ...]        |
| ...                                                       |
+-----------------------------------------------------------+ EOF
```

Chunks are contiguous: the next chunk begins immediately after the current
payload. There is no padding and no terminating chunk. Walking all chunk headers
lands exactly on EOF for all 66 `*Map.kld` files and all 11 single-root KWD
files. **Confidence: HIGH.**

Evidence — `Data/editor/Traps.kwd`, first 64 bytes (`xxd -g1`):

```
00000000: 82 00 00 00 04 00 00 00 68 24 00 00 83 00 00 00  ........h$......
00000010: 1c 00 00 00 10 00 00 00 01 00 00 00 ce 07 05 01  ................
00000020: 00 00 0a 2c 04 74 d0 07 04 02 00 00 0a 25 10 00  ...,.t........%..
00000030: 84 00 00 00 30 24 00 00 47 61 73 20 54 72 61 70  ....0$..Gas Trap
```

Root: tag `0x82`, version `4`, declared size `0x2468 = 9 320` = file length.
Chunk 0: tag `0x83`, length `0x1C = 28`. Chunk 1: tag `0x84`, length `0x2430`
= 9 264. Check: `12 + (8+28) + (8+9264) = 9 320` = EOF.

---

## 3. Root header (12 bytes)

| Offset | Size | Type | Meaning | Confidence |
|---:|---:|---|---|---|
| `0x00` | 4 | u32 LE | Root tag / data-set id (`0xAA` = creatures, `0x78` = rooms, …) | HIGH |
| `0x04` | 4 | u32 LE | Format version, always `4` in examined files | HIGH (only v4 observed) |
| `0x08` | 4 | u32 LE | Declared total byte length of this root's section | HIGH |

---

## 4. Chunk header (8 bytes) and tag numbering

| Rel. | Size | Type | Meaning | Confidence |
|---:|---:|---|---|---|
| `+0x00` | 4 | u32 LE | Chunk tag | HIGH |
| `+0x04` | 4 | u32 LE | Payload byte length (excludes the 8-byte header) | HIGH |
| `+0x08` | N | bytes | Payload | HIGH |

Tags are small positive integers, normally consecutive per file:
chunk 0 tag = root+1, chunk 1 tag = root+2. The info chunk's payload is always
28 bytes. The data chunk's payload is always `record_count * record_size`.

Evidence — `Data/editor/Creatures.kwd`:

```
00000000: aa 00 00 00 04 00 00 00 56 d3 03 00 ab 00 00 00
00000010: 1c 00 00 00 2e 00 00 00 01 00 00 00 ce 07 05 01
00000020: 00 00 0a 2c 04 00 d0 07 04 02 00 00 0a 25 10 00
00000030: ac 00 00 00 1e d3 03 00 49 6d 70 00 00 00 00 00
          ^tag 0xAC  ^len 0x3D31E (250 654)  ^"Imp" (first record)
```

The prompt's example `aa… 56d30000 ab… 1c… 2e…` matches this header; the
readable `Imp` follows marker `0xAC` exactly as described.

---

## 5. Info chunk (tag = root+1, length 28)

| Payload offset | Size | Meaning | Confidence |
|---:|---:|---|---|
| `+0x00` | 4 | Number of records in the data chunk | HIGH |
| `+0x04` | 4 | Always `1` in all files examined | HIGH (observation) |
| `+0x08 .. +0x1B` | 20 | Small numeric/flag block; a few bytes vary per file (e.g. byte `+0x10`: `0x74` traps/keeper spells, `0x55` rooms, `0x00` creatures) | LOW (semantics unknown) |

The varying byte at `+0x10` is **not** a checksum of the record count and does
not correlate with record size in a way derived so far.

---

## 6. Data chunk (tag = root+2) and records

The payload is a flat array of `N` records, each exactly
`payload_length / N` bytes (the division is exact for all 11 single-root KWD
files). Record 0 begins at the first payload byte; record *i* at
`payload_offset + i * record_size`.

Record layout (high confidence for the first field only):

| Record offset | Size | Meaning | Confidence |
|---:|---:|---|---|
| `+0x00` | 28 | Display name: NUL-terminated ASCII, zero-padded to 28 bytes | HIGH |
| `+0x1C ..` | varies | Typed fields / binary property data (see below) | MEDIUM/LOW |

Evidence — `Data/editor/Traps.kwd`, record 0 at `0x38`:

```
00000038: 47 61 73 20 54 72 61 70 00 00 ... 00  ; "Gas Trap" + zero padding
00000058: 54 52 41 50 5f 47 41 53 00 ...        ; "TRAP_GAS"  (code token)
000000AC: 67 75 69 5c 74 72 61 70 73 5c 67 61 73  ; "gui\traps\gas"
00000100: 44 6f 6f 72 41 6e 64 54 72 61 70 49 63 6f 6e 73 23 30
                                                ; "DoorAndTrapIcons#0"
00000154: 47 75 69 5c 4d 6f 6f 64 73 5c 53 54 2d 47 61 73
000001A8: 54 52 41 50 5f 47 41 53 28 41 63 74 69 76 65 29
                                                ; "TRAP_GAS(Active)"
```

### 6.1 Observed field-slot pattern (partial reconstruction)

Many string fields are arranged in 84-byte (`0x54`) slots: a small u32 tag
followed by an 80-byte fixed char array. For `Traps.kwd` record 0 the tags at
record offsets `+0x1C, +0x70, +0xC4, +0x118, +0x16C` are `0, 5, 1, 1, 2`, with
strings at `+0x20, +0x74, +0xC8, +0x11C, +0x170`. For `KeeperSpells.kwd` record
0 the same holds for the first two slots (`0 → "gui\spells\sight_of_evil"`,
`1 → "KeeperSpellIcons#0"`), but later regions use different lengths (strings
at `+0xF0, +0x120, +0x174`) and contain packed numeric data (e.g. `5000` at
`+0x11C`). Creature records contain binary/float-like data before their
animation-name strings, and the slot pattern is not universal.

**Confidence: MEDIUM** that 4+80 slots are a real recurring encoding for the
first string fields; **LOW** that this is the complete grammar. The decoder
therefore does not attempt to decode individual numeric properties; it exposes
raw records plus all NUL-terminated strings. Deriving the complete field
grammar would need the original struct definitions or calibrated comparisons
against engine behaviour.

### 6.2 String encoding

Strings are ASCII, NUL-terminated and zero-padded inside fixed-size fields.
Letters, digits, spaces, backslashes, parentheses and punctuation occur. Some
records contain short binary bytes immediately before a string, so naive
scanning can produce strings with 1–2 junk lead bytes (e.g. `:(TRAP_GAS` at
`0x245` in `Traps.kwd`); these are artefacts of adjacent binary data, not a
different string encoding. **Confidence: HIGH for ordinary strings.**

Code tokens recovered include `CREATURE_IMP`, `CREATURE_KNIGHT`,
`TRAP_GAS`, `ROOM_TREASURE`, `KEEPER_SPELL_SIGHT_OF_EVIL`,
`GUI_BUTTON_SIGHT_OF_EVIL`.

---

## 7. Known layout anomalies

1. **`GlobalVariables.kwd` is a concatenation of two KWD roots.** Root `0xE6`
   declares 9 992 bytes (chunks `0xE7`/`0xE8`, 621 records × 16 bytes); at
   offset 9 992 a second root `0xBE` begins with chunks `0xBF`/`0xC0`, ending at
   the real file length 13 636. `KwdFile` reports the size mismatch as a warning
   and still exposes all chunks. **Confidence: HIGH** (direct bytes).

2. **`*Triggers.kld` files contain nested chunks whose extent exceeds the
   declared size of the parent chunk.** Example `AlcatrazTriggers.kld`:
   root `0xD2`, chunk `0xD3` (32 bytes) at `0x0C`, chunk `0xD4` (declares 32
   bytes) at `0x34`, followed by four nested 24-byte chunks `0xD5, 0xD5, 0xD6,
   0xD6` starting at `0x3C` and ending exactly at EOF (`0x9C`). The `0xD4`
   size field does not bound its payload. The tool recognises this pattern and
   attaches the nested chunks with an explicit anomaly note. **Confidence:
   HIGH for the observation; LOW for the meaning of the 0xD4 size field.**

3. **Map-settings `*.kwd` files have a third chunk in non-sequential tag order.**
   Example `Alcatraz.kwd`: root `0xDC`, chunks `0xDD` (28 bytes), `0xDF` (25 683
   bytes), `0xDE` (1 180 bytes). The flat chunk walk still reaches EOF.

---

## 8. KLD map files (`*Map.kld`)

Three chunks after the root header:

| Chunk tag | Payload | Meaning | Confidence |
|---:|---:|---|---|
| `0x65` | 8 | `width u32`, `height u32` (tile counts) | HIGH |
| `0x66` | `4*width*height` | Tile array, row-major, one LE u32 per tile | HIGH |

Evidence — `Data/editor/maps/Level4Map.kld`:

```
00000000: 64 00 00 00 04 00 00 00 24 32 00 00 65 00 00 00
00000010: 08 00 00 00 32 00 00 00 40 00 00 00 66 00 00 00
00000020: 00 32 00 00 1e 02 01 01 1e 02 01 01 1e 02 01 01
```

`0x65` payload = `0x32, 0x40` = 50 × 64 tiles; `0x66` size = `0x3200` =
12 800 = 50 × 64 × 4. Root declared size `0x3224` = 12 836 = file size.
Therefore for every map:

```
file_size == 12 + (8 + 8) + (8 + 4*width*height) == 36 + 4*width*height
```

This equation was verified on **all 66** `*Map.kld` files (e.g. Circlet
80×80, Caverns 101×101, Secret1 50×50, FrontEnd3DLevel 7×14). The user's
initial guess that `Level4Map.kld` is 80×80 or 50×50 is **not** supported by
the bytes: it is **50×64**. The 80×80 maps are Circlet, Level17, Level19, MPD6
and Onslaught; the 50×50 maps are Secret1 and level3.

Each tile u32 has bytes `[terrain_id, variant, 0x01, 0x01]` in little-endian
byte order (observed values e.g. `1E 02 01 01`, `08 01 01 01`). The leading
byte is a terrain/material id; the second is a small variant/flags byte;
bytes 2–3 are `0x01` in all observed tiles. **Confidence: HIGH for layout,
MEDIUM for the semantic labels.**

`Data/editor/maps/Thumbnails/*.bmp` are fixed 144×144 images, so they cannot
confirm per-map dimensions; they were checked and excluded as evidence.

Other KLD families (same 12-byte root header, different tags): `*Players.kld`
root `0xB4` (chunks `0xB5`/`0xB6`), `*Things.kld` root `0xBE` (`0xBF`/`0xC0`),
`*Variables.kld` root `0xE6` (`0xE7`/`0xE8`), `*Triggers.kld` root `0xD2`
(`0xD3`/`0xD4` + nested; see §7). Their record grammars are not decoded here.

---

## 9. Content cross-check

Recovered names match the known DK2 content: `Imp`, `Bile Demon`, `Mistress`,
`Warlock`, `Dark Elf`, `Goblin`, `Vampire`, `Skeleton`, `Troll`, `Salamander`,
`Firefly`, `Knight`, `Dwarf`, `Giant`, `Wizard`, `Elven Archer`, `Thief`,
`Monk`, `Fairy`, `Black Knight`, `Dark Angel`, `Horny`, `Maiden Of The Nest`
among 46 creature records; rooms include `Treasury`, `Lair`, `Portal`,
`Hatchery`, `Dungeon Heart`, `Library`, `Training Room`; traps include
`Gas Trap`, `Boulder Trap`, `Sentry Trap`, `Fireburst Trap`; keeper spells
include `Sight Of Evil`, `Possession`, `Create Imp`, `Thunderbolt`, `Heal`.
Full lists are in `game_data.json`.

`Data/Text/Default/*.str` files begin with `BFST` and contain a u32 offset
table plus strings in a **non-ASCII custom glyph encoding** (no literal ASCII
tokens such as `CREATURE_IMP` occur in them). That encoding was not decoded and
is an explicit unknown. The cross-check was therefore performed against the
KWD code tokens embedded in the same records (`CREATURE_*`, `TRAP_*`,
`ROOM_*`, `KEEPER_SPELL_*`) and the well-known DK2 roster.

---

## 10. Confidence summary and explicit unknowns

| Item | Confidence |
|---|---|
| Root header, chunk headers, size field, chunk walk | HIGH |
| Info-chunk record count and fixed-size record array | HIGH |
| 28-byte NUL-padded ASCII name field | HIGH |
| KLD 0x65 dimensions, 0x66 tile array, size equation (66/66 maps) | HIGH |
| Tile byte layout (terrain/variant/two 0x01 bytes) | MEDIUM |
| 4+80 field-slot pattern for some string fields | MEDIUM |
| Meaning of the varying info-chunk bytes, numeric record fields | LOW / unknown |
| Meaning of `0xD4` size field in trigger files | LOW / unknown |
| BFST `.str` glyph encoding | unknown (not decoded) |
| Whether 4 is a format version or a constant tag | MEDIUM (version is the simpler reading) |

The decoder is deliberately conservative: it reconstructs the chunk tree, the
record table, names and strings exactly, and reports numeric property bytes as
raw data instead of guessing semantics.
