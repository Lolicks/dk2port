# Build report — DK2Port IPA

Run: https://github.com/Lolicks/dk2port/actions/runs/45244837079
Repository: private `Lolicks/dk2port` (original installation stored in Git LFS under `game/`).

## Results

| Check | Result |
|---|---|
| `core-tests` (Ubuntu) | PASS |
| `build-ios` (macos-latest) | PASS |
| C/C++/Objective-C++/Swift/Metal compilation | PASS |
| Archive + IPA packaging | PASS |
| IPA as GitHub Actions artifact | `DK2Port-IPA` (uploaded) |
| IPA size | 492 544 759 bytes (~470 MiB) |

## IPA contents verified with `unzip -l`

- `Payload/DK2Port.app/Original/Data/Meshes.WAD` — OK
- `Payload/DK2Port.app/Original/Data/FrontEnd.WAD` — OK
- `Payload/DK2Port.app/Original/Data/Movies/INTRO.TGQ` — OK
- `Payload/DK2Port.app/Assets/data/{stats,triggers,game_data,missions}.json` — OK
- `Payload/DK2Port.app/Assets/maps/maps.json` — OK
- `Payload/DK2Port.app/Assets/integrity.json` — OK
- Mandatory `Documents/Original` dependency — absent (checked in CI)

The app embeds the original Data archive (WAD/KWD/KLD/SDT/TGQ), verifies an
integrity manifest on first launch and creates no Files Picker / download step.

## Signing

Unsigned IPA (`CODE_SIGNING_ALLOWED=NO`). Sideloadly re-signs it. No signing
identity or provisioning profile is stored in the repository.

## Honest limitations — build success is NOT gameplay confirmation

- The IPA was **not installed or launched on a real iPhone**; no device claim is made.
- Runtime hooking of WAD→PNG textures, KMF meshes, ANIM frames, TQI frames and
  EA XA ADPCM into the Metal/UI layer remains incomplete. The menu and basic
  simulation compile, but a full faithful game is not claimed.
- AAC/PCM audio transcoding requested for size reduction is **not implemented**;
  original SDT banks are embedded, but no iOS audio conversion pipeline exists yet.
- No commercial resources are in the public artifact: the repository is private
  and the game files live in LFS. Do not redistribute the IPA publicly.
