# Dungeon Keeper 2 — `DKII.EXE` / DLL static analysis report

GOG build installed at `/home/kali/Desktop/rea/Dungeon Keeper 2`.
All strings and code below were obtained locally with `strings -a -n 5 -t x`,
`objdump -h/-p/-f/-d` and a small Python PE import parser. No fan-engine
source was consulted. Strings are data from the target, not instructions.

Full raw curated catalogue: `port/docs/strings_catalog.txt` (387 quoted strings).

---

## 1. Files, hashes, identity

| File | Size (bytes) | SHA256 |
|---|---:|---|
| `DKII.EXE` | 2 977 280 | `e99818fbe9ad337ab4a24fa76901d5fe5a8c0a0dd306172d24b9547762c534cc` |
| `DKII-DX.exe` | 2 977 280 | `d7a8b854e4e4f02709eba271bead6fe2307dd6e332b5a96e70857e5b0e19fb8a` |
| `DKII_SOFT.EXE` | 871 424 | `0ef4b2303a5b4985c2e3e442f9d86291255644804105c14b634c6fb5fa8a160c` |
| `QMIXER.DLL` | 198 656 | `460292fe61a75996e8035f8a65a765733738293d0d391336be292aa74c128698` |
| `WEANETR.dll` | 142 848 | `fafd6e306432d245c3ce575535367ae43e62224b1177121edfa25c31a8f18b73` |
| `SFMAN32.DLL` | 48 640 | `91db92aa170a5b8e757ce2bd36156b558cb5b4f66383a0f2a1a05c9bb7e039b1` |
| `Aweman32.dll` | 35 328 | `184450e8d2b588d0659e0f57455b0106b26ffe77ffab15563819f335a97d12db` |
| `patch.dll` | 150 016 | `0022e3aaf419f22c3287fb454e9e671ee249f1eefc652465bec96ecfd38f00c4` |

**`DKII.EXE` vs `DKII-DX.exe`: only 5 bytes differ** (file offset `0x28B8D8`):
`"DDRAW"` vs `"PATCH"` inside an import-table DLL name. All 9 822 strings
are otherwise identical. So the two executables are the same game with one
swapped import: `DKII.EXE` links `DDRAW.dll`, `DKII-DX.exe` links `PATCH.dll`
(the GOG DirectDraw compatibility shim). This also explains `patch.dll`'s
`FakeDirectDraw4*`/`FakeD3D3*` strings.

`DKII_SOFT.EXE` is a different, much smaller build (software renderer):
2 605 strings, only 4 PE sections (`.text`, `.rdata`, `.data`, `.rsrc`), no
`idct_dat` / `tqia_dat` / `lbmpeg_d` sections.

`DKII.EXE` is PE32, linker 5.10 (MSVC 6 generation), image base `0x400000`,
entry RVA `0x238440` (VA `0x638440`), 11 sections. `objdump -h` section names:

```
.text  cseg  .rdata  .data  grpoly_d  uva_data  idct_dat
tqia_dat  dseg  lbmpeg_d  .rsrc
```

`grpoly_d` (graphic polygons), `idct_dat` (inverse-DCT table), `tqia_dat`
(TQI decoder table — cf. RTTI class `TbTQILoader`), and `lbmpeg_d` (MPEG
decoder data) are strong evidence that an MPEG/TQI codec is linked statically
into the DX build. The entry point is the standard MSVC prologue with
`mov %fs:0x0,%eax` / `push %eax` / `mov %esp,%fs:0x0` structured exception
handling and an indirect call through the IAT at `0x66C10C`.

---

## 2. Import table (independently re-derived, matches supplied facts)

A Python parse of the PE import descriptor at RVA `0x28BB18`
(file offset `0x28AB18`) returns exactly **13 DLLs / 251 imported functions**:

| DLL | Thunks | DLL | Thunks |
|---|---:|---|---:|
| weanetr.dll | 20 | ole32.dll | 3 |
| KERNEL32.dll | 118 | DDRAW.dll | 2 |
| USER32.dll | 33 | WINMM.dll | 16 |
| ADVAPI32.dll | 7 | QMIXER.dll | 27 |
| IMM32.dll | 13 | WSOCK32.dll | 5 |
| DSOUND.dll | 1 | GDI32.dll | 5 |
| DINPUT.dll | 1 | | |

Key imports corroborate the subsystems: `DirectDrawCreate`,
`DirectDrawEnumerateA`, `DirectInputCreateA`, `QSWaveMix*` (27),
`midiOutOpen`/`midiOutLongMsg`/`mmio*`, `timeGetTime`, `GetTickCount`,
`Reg*`/`HKEY_*`, `socket` family via WSOCK32, and MLDPlay C++-mangled
exports from WEANETR.dll.

---

## 3. Curated strings by subsystem

Offsets are file offsets from `strings -a -n 5 -t x DKII.EXE` unless noted.
All quotes are verbatim (the full catalogue has 387 entries).

### 3.1 Configuration / settings / registry
The shipped `Data/Settings/settings.txt` is empty (1 byte), so the real
settings store is the registry under `Software\Bullfrog Productions Ltd`.
These keys give its semantics:

```
002BB020  'Configuration'
002BB030  'Paths'
002BB194  '%sdata\Settings\'
002BBCC8  'Software\Bullfrog Productions Ltd'
002BBCF0  'Software\Electronic Arts'
002BBE10  'NumberOfVoices'
002BBE2C  'SpeechSwitch'
002BBE3C  'MusicSwitch'
002BBE48  'QSound'
002BBE50  'FlipSpeakers'
002BBE60  'Headphones'
002BBE6C  'SoundQuality'
002BBE7C  'EnvironmentalEffects'
002BBEA4  'MasterVolume'
002BBEB4  'MusicVolume'
002BBEC0  'SoundEffectVolume'
002BBED4  'SpeechVolume'
002BBFDC  'Cheap Lighting'
002BC004  'Texture Reduction Level'
002BC01C  'High Res Textures'
002BC030  'Machine Spec Level'
002BC044 'EnvironmentMapping'
002BC064  'Sine Wave Water'
002BC074 'Torture Details'
002BC084 'Translucent Water'
002BC098  'Engine Id'
002BC0A4  'D3D Device Guid'
002BC0B4  'Shadow Level'
002BC0C4  'View Distance'
002BC0D4  'Ambient Light'
002BC0E4  'Gamma Level'
002BC0F0  'Screen Hardware3D'
002BC104  'Screen Swap'
002BC110  'Screen Windowed'
002BC120  'Screen Depth'
002BC130  'Screen Height'
002BC140  'Screen Width'
002BC150  'Screen Mode Type'
002BC164  'Video'
```

Command-line switches (a second configuration surface, all at `0x2BD4xx–0x2BD7xx`):

```
'-ENABLEFILEPATCHING'  '-LOGOOS'  '-PMESH'  '-NOCHECKSUM'  '-CHEAT'
'-PRELOADRESOURCES'  '-NOMOVIES'  '-SHADOWS'  '-NOSHADOWS'  '-CDPATH'
'-LANGUAGE'  '-ENABLEARTPATCHING'  '-TEXTUREREDUCTIONLEVEL'
'-ENABLEBUMPMAPPING'  '-CHEAPLIGHTING'  '-LOWRESTEXTURES'
'-DISABLEGAMMA'  '-NOINTMOUSE'  '-NOSPEECH'  '-NOMUSIC'  '-NOERRORS'
'-NOFPS'  '-SOFTWARE'  '-ENGINE'  '-LEVEL'  '-PSAVE'  '-PLOAD'
```

### 3.2 Asset paths / file naming
```
002AB7D4  'Data\Settings\HiScores.dat'
002AB7F0  '%s\Trailer.tgq'
002AF9D0  '%s\%s%d.tgq'
002AF9DC  '%s\%s.tgq'
002AF9E8  '%s\BullfrogOutro.tgq'
002AFA7C  '%s\BullfrogIntro.tgq'
002B18C4  '\*.kwd'
002B18D4  'Map.kld'
002B18F4  'GlobalVariables.kwd'
002B4034  'KeeperSpells.kwd'   002B4048 'Traps.kwd'   002B4054 'Doors.kwd'
002B4060  'Rooms.kwd'          002B406C 'Creatures.kwd'
002BB080  '%sdata\palette'     002BB090 '%sdata\Texture'
002BB0B0  '%sdata\editor'      002BB0C0 '%sdata\Paths.wad'
002BB0D4  '%sdata\FrontEnd.wad'  002BB0E8 '%sdata\Sprite.Wad'
002BB0FC  '%sdata\EngineTextures.wad'
002BB118  'K:\DK2\Dev\Data\Meshes.Wad'   (developer path leaked into build)
002BB134  '%sdata\Meshes.Wad'
002BB148  '%sdata\sound\Music\'  002BB15C '%sdata\sound\SFX\'
002BB170  '%s\Dk2TextureCache'   002BB1B8 '%sdata\editor\'
002BB2BC  '%sdata\Movies'       002BCABC '%s\EngineTextures.dat'
002BCAD4  '%s\EngineTextures.dir'
002A3174  '%sMaps\%sThings.kld'  002A3188 '%sMaps\%sTriggers.kld'
002A41A0  '%s\Maps\%s.kwd'      002A4190 'QuickSave.QSAV'
```
This confirms every asset family examined in the other docs (`.wad`, `.kwd`,
`.kld`, `.tgq`, `.sdt` via `%sdata\sound\SFX\`, texture cache `.dat/.dir`).

### 3.3 Rendering / DirectDraw / Direct3D
```
00251FFF  'VMMX Software Renderer, by Martin Griffiths, 1998/9 (fast sub-pixel triangles huh?!?!)'
0028B8AE  'DirectDrawEnumerateA'   0028B8C6 'DirectDrawCreate'
0028B8D8  'DDRAW.dll'
0028CA38  'D3D DEVICES'  0028CA5C 'ZBUFFER'  0028CA7C 'D3D <NUMBER>'
0028CFB8  'PacketRecord: Render Frequency is %d'
0028CFE4  'Selected D3D Device: %s'  0028CFFC 'Cleared Selected D3D Device'
00295EAC  'NoTexture'
00295EB8  'Unable to load Bitmap Resource, '%s'. Continue will use 'NoTexture''
0029D3BC  'D:\Dev\DK2\Projects\Source\Game\RenderInfo.cpp'
002A30C0  'CharredTexture'  002A30D0 'ObjectiveTexture'
002BA884  'Failed to lock surface for load of '%s%s''
002BA8B0  'LoadArtToSurface failed on CreateSurface!! (%s%s)'
002BC660  '.?AVCEngine2DMeshSurface@@'
002BCA08  '.?AVCEngineSurfaceBase@@'  002BCA30 '.?AVCEngineSurface@@'
002BCA50  '.?AVCEngineCompressedSurface@@'  002BCA78 '.?AVCEngineDDSurface@@'
002BCEF4  'D:\Dev\DK2\Projects\Source\PCEngineD3D\ProceduralMesh.cpp'
002BD574  '-TEXTUREREDUCTIONLEVEL'  002BD654 '-32BITZBUFFER'
002BFA1C  'Failed to create Direct Draw object'
```
`DDERR_*` strings (40+) are compiled-in DirectDraw error-name tables
(`DDERR_SURFACELOST`, `DDERR_NODIRECTDRAWHW`, `DDERR_OUTOFVIDEOMEMORY`, …).

### 3.4 Audio / QMixer 3D API / music / speech
```
0028B94E  'QSWaveMixOpenChannel'     0028B966 'QSWaveMixActivate'
0028B9A2  'QSWaveMixGetDirectSoundBuffer'
0028BA24  'QSWaveMixPlayEx'          0028BA60 'QSWaveMixConfigureChannel'
0028BADE  'QSWaveMixSetSourcePosition'
0028BAFC  'QSWaveMixSetListenerVelocity'
0028BB1C  'QSWaveMixSetFrequency'
0028BB4E  'QSWaveMixSetDistanceMapping'
0028BB6C  'QSWaveMixSetVolume'
0028BB82  'QSWaveMixSetListenerPosition'
0028BBA2  'QSWaveMixSetListenerOrientation'
0028BBE0  'QSWaveMixSetSpeakerPlacement'
0028BBFE  'QMIXER.dll'   0028BD3A 'DSOUND.dll'
0028C48A  'midiOutLongMsg'  0028C4FC 'midiOutShortMsg'
002A96A0  'gui\options\i-audio'   002A98C8 'gui\options\i-m_volume'
002A990D  'gui\options\i-FX_volume'  002A9952 'gui\options\i-voice_volume'
002AB7C0  'cdaudio'
002BC188  '.?AVCSoundSystem@@'   002BC1D0 '.?AVTbAudioSystem@@'
002BC218  'AMBIENCE'
002BC298  '.?AVSetSFXVolume@TbSysCommand@@'
002BC350  '.?AVSetQSound@TbSysCommand@@'
002BC448  '.?AVCSpeechSystem@@'  002BC45C 'SPEECH_MENTOR\'
```
QMixer's exported API also includes `QSWaveMixSetSpeedOfSound`,
`QSWaveMixSetSourceCone`, `QSWaveMixSetListenerOrientation` — a full
positional 3D-audio model (listener/source position, velocity, cones,
distance mapping, speaker placement). `SFMAN32.DLL` reads
`SOFTWARE\SoundFont\Compatible Devices` (registry) and exports `SFManager`;
`Aweman32.dll` identifies as `AWEManager` (AudioWorkstation EAX manager,
cf. `.?AVSetEAX@TbSysCommand@@` in the EXE).

### 3.5 Input / GUI
```
0028B682  'GetKeyboardLayout'   0028B7C8 'GetKeyboardType'
0028C3C2  'SwapMouseButton'
0028C50E  'DirectInputCreateA'  0028C522 'DINPUT.dll'
002A9AF0  'gui\options\i-mouse_speed'
002A9B35  'gui\options\i-inverse_mouse'
002A9BBF 'gui\options\i-keyboard'
002AB720  '.?AVCTextInput@@'
002BBB60  'Mouse Sensitivity'   002BBB88 'Invert Mouse'
002BD674  '-NOINTMOUSE'
002AB0E4  'GUI_MINIMAP'
```

### 3.6 Maps / levels / saves
```
0028CA44  'LEVEL <STRING>'   0028CAC4 'PSAVE <STRING>'   0028CAE4 'PLOAD <STRING>'
0028CB6C  'PacketRecord: Loading from %s'
0028CB8C  'Packet Load setup failed - Check filename'
0028D144  'Level Selected, '%ls''
0028D9D4  'FSMAP load failed on '%s''
002A3158  'Unable to load level, %s'
002A322C  'Level Intro Override %d: (%d, %d)'
002A3288  'Level Text Id %d: %d'  002A32A0 'Level Objective %d: (%d, %d)'
002A38C0  'Checksums: Exe %x, Level %x, Data %x, TM %x, Map %x, RM %x, PM %x'
002A39A8  'Failed to load saved game (%s)'
002A3A18  'LoadingScreen\LoadingScreen%dx%d'
002A4190  'QuickSave.QSAV'
002A57C8  'Packet Load Out of Sync (on GT %d) GT %d [F10 to Log]'
002AF94C  'CutSceneLevel'
002B1914  'Maps\Thumbnails\%s144'
002B96E0  'Game_Settings-02'  002B96F4 'Game_Settings-01'
002B9708  'Game_Settings-00'
002BBAB0  'Level Attempts'  002BBA60 'Secret Level Number'
```
`GT` in the network log = **game tick**, used as the synchronization unit.

### 3.7 Network / multiplayer / MLDPlay
```
0028B052  '?DumpPlayer@MLDPlay@@QAEHK@Z'
0028B098  '?DestroySession@MLDPlay@@QAEHXZ'
0028B10C  '?SetSessionDesc@MLDPlay@@QAEKPAUMLDPLAY_SESSIONDESC@@K@Z'
0028B186  '?GetCurrentMs@MLDPlay@@QAEKXZ'
0028B1E0  '?EnumerateServices@MLDPlay@@...'
0028B21E  '?StartupNetwork@MLDPlay@@...'
0028B28E  '?SetupConnection@MLDPlay@@...'
0028B366  '?JoinSession@MLDPlay@@...'
0028B3A6  '?SendChat@MLDPlay@@...'   0028B3CA '?SendData@MLDPlay@@...'
0028B3EE  'weanetr.dll'  0028BD2E 'WSOCK32.dll'
002A5428  '.?AVCNetworkCommunication@@'
002A5468  'Host: Resends:%d LastEvent@%d NullRun:%d'
002A5860  '.?AVCNetworkComponent@@'
002A5958  'Entered Network Component'  002A5974 'Network Component'
002A5AC4  'Unable to create session'  002A5B18 'Unable to join session'
```
`WEANETR.dll` itself contains (offsets in that DLL):
```
019A54  'daphne.eagames.co.uk'        (historical EA lobby host)
01AC70  'GOT A JOINED SESSION PACKET'
01B7D4  'DPSYS_STARTSESSION'
01CB84  'MLDPlay::CreateSession Error:-MLDPlay Not Initialised'
01CC50  'MLDPlay::JoinSession Error:-MLDPlay Not Initialised'
```
`WEANETR` is a **DirectPlay wrapper** (`DPSYS_*` messages, EnumerateServices,
Create/JoinSession, SendChat/SendData) with lobby support.

### 3.8 Errors / logging / assertions
```
0028CDB0  'Change Screen Mode Failed'
0028CF8C  'Invalid D3D Device Selected'
00292AFC  'Unable to read MapColours file (GUI\map\MapColours.png)'
00292B4C  'Unable to read png file '%s''
00295E2C  'Unable to Load Resource, '%s''
0029F870  'Heap Overflow (Triangles %d, Map %d*%d)'
002BABE0 'ERROR.LOG'
002BAC98  'ASSERT'
002BAF48  'Unable to send CHAT message - Error:%s'
002BAF9C  'Unable to send GUARANTEED DATA message - %s'
002C2868  'Unknown zlib error'  002C296C 'Decompression error'
002C2FA0 'libpng error: %s'
```
zlib 1.1.3 and libpng are statically linked (`inflate 1.1.3 Copyright 1995-1998
Mark Adler`, `deflate 1.1.3 Copyright 1995-1998 Jean-loup Gailly`).
`unknown compression method` (0x2C302C) exists but no symbol table remains
(stripped binary).

### 3.9 Engine / clock / component model / RTTI
```
0028B488  'GetTickCount'   0028B8E4 'timeGetTime'
0028CB18  'Entered Entry Component'
002A3904  'World Log at GameTick %d'
002A5958  'Entered Network Component'
0029D938  '.?AVCPlayer@@'       0029D960 'D:\Dev\DK2\Projects\Source\Game\Player.cpp'
0029E2B0  'PlayerScanTick %d'
0029E4AC  'NumberOfPlayersCreated %d'
0029F618  'PlayerMessageQueue:'
002A5898  'PLAYERS'
0028D2D0  '.?AVCPlayerInterface@@'
0028D2F0  '.?AVCDefaultPlayerInterface@@'
0028D38C  'Default Player Interface attached'
002AB7A0  '.?AVCFrontEndComponent@@'
002BB048  '.?AVTbWadFileStorage@@'  002BB068 '.?AVTbDiscFileStorage@@'
002BA850  '.?AVTbTQILoader@@'
002BCA50  '.?AVCEngineCompressedSurface@@'
```
RTTI class inventory (from `.?AV…@@` names) includes `CGameComponent`,
`CEntryComponent`, `CFrontEndComponent`, `CNetworkComponent`, `CWorld`,
`CMap`, `CPlayer`, `CCreature`, `CRoom`, `CTrap`, `CDoor`, `CObject`,
`CEngineInterface`, `CPCEngineInterface`, `CSoundSystem`, `CTbAudioSystem`,
`CGuiManager`, `CFileManager`, `CIFFFile`, `CDirectIFFFile`,
`CMemLoadIFFFile`, `TbWadFileStorage`, `TbDiscFileStorage`, `TbTQILoader`,
`TbPNGLoader`, `TbBMPLoader`.

### 3.10 Version / build strings
```
002BAFCC 'Version Number'
002BAFDC  'Version Number Minor'   002BAFF4 'Version Number Major'
002BB00C  'Dungeon Keeper II'     002BB2A0 'Dungeon Keeper 2'
002BD4B4  'Bullfrog Productions Ltd'
002BFA00  'Bullfrog'
002C287C  'zlib version error'
```

---

## 4. Hypotheses about module boundaries

1. **Four top-level components.** RTTI + "Entered X Component" strings give
   `CEntryComponent`, `CFrontEndComponent`, `CGameComponent`,
   `CNetworkComponent`. The startup path appears to instantiate these behind
   `CGameManagerInterface` / `CEngineInterface`; `Default Player Interface
   attached` suggests player-interface objects are plugged into the game
   component dynamically.
2. **Storage abstraction.** `TbWadFileStorage` and `TbDiscFileStorage`
   (both under `TbFileStorage@FileSystem`) are two backing implementations
   behind one file API — this matches the shipped data existing both as WADs
   and as loose files/directories (`%sdata\EngineTextures`, `%s\Dk2TextureCache`).
   `CIFFFile`/`CMemLoadIFFFile` implement the chunked KWD/KLD IFF reader, and
   `TbTQILoader` is the movie/TQI loader behind `.tgq` files.
3. **Renderer split.** `CEngineInterface` vs `CPCEngineInterface` vs
   `CEngineDDSurface` / `CEngineCompressedSurface` imply an abstract engine
   with a DirectDraw/D3D implementation plus a software path (`DKII_SOFT.EXE`).
   `patch.dll` (when imported by `DKII-DX.exe`) interposes `FakeDirectDraw4`
   / `FakeD3D3` COM shims — a compatibility layer *below* the renderer, not
   part of the game logic.
4. **Audio split.** `TbAudioSystem` (generic) → `CSoundSystem` /
   `CSpeechSystem` / `CSoundSystemInterface`, with `TbSysCommand` subclasses
   (`SetMasterVolume@TbSysCommand`, `SetListenerDirection@…`,
   `UpdateRoomAmbience@…`) as a command/console interface. QMixer provides
   mixing/3D positioning, DirectSound the device, `SFMAN32` SoundFont MIDI,
   `Aweman32` EAX management, WINMM the timer and MIDI/mmio I/O.
5. **Network split.** `CNetworkCommunication` and `CLocalCommunication`
   both implement `CCommunicationInterface`; the network one delegates to
   `MLDPlay` in `WEANETR.dll`, which wraps DirectPlay. Determinism is
   enforced by game ticks (`GT`) with resends and out-of-sync detection.
6. **Game logic split.** Source paths show `Game/State*`, `Game/*State.cpp`
   (creature FSMs: `ImpHardware→Imperial?` no — `ImpState`, `PayState`,
   `TrainingState`, …), `RoomManager`, `World`/`WorldTrigger`,
   `ComputerPlayer`, and a GUI layer (`gui\options\i-*`, `CDefaultPlayerInterface`).

**Confidence:** MEDIUM for 1–3 and 5 (strong string/RTTI evidence); LOW for
the exact call graph, since the binary is stripped and no symbols remain.

---

## 5. Hypotheses about the game loop

- `timeGetTime` (WINMM) and `GetTickCount` (KERNEL32) are both imported;
  `PacketRecord: Render Frequency is %d` and `-NOFPS` show an explicit
  render-frequency counter.
- `World Log at GameTick %d`, `PlayerScanTick %d`, and network
  `Packet Load Out of Sync (on GT %d) GT %d [F10 to Log]` show a **discrete
  game tick** ("GT") independent of rendering frames. The loop plausibly is:
  poll input (DINPUT) → advance network packets keyed by GT → run one
  deterministic world/creature/room update step → update audio listener
  (QMixer `SetListenerPosition/Velocity`) → render one frame (DDRAW/D3D).
- `Host: Resends:%d LastEvent@%d NullRun:%d` implies the host sends tick-indexed
  event packets and clients can request resends — consistent with a
  **lockstep simulation** driven by GT rather than by wall-clock time.
- `-NOMOVIES`, `For Movies Insert DK2 Game CD Now and press yes.` and the
  TGQ filename formats place cut-scene playback in the entry/front-end phase,
  outside the in-game tick loop.
- **Confidence:** MEDIUM for the existence of a GT-based deterministic loop;
  LOW for the exact phase ordering (no disassembly of the loop body was done).

---

## 6. Cross-references to the other format reports

- Movie files: `%s\Trailer.tgq`, `%s\%s.tgq`, `%s\%s%d.tgq`,
  `%s\BullfrogIntro.tgq`, `%s\BullfrogOutro.tgq`, `%sdata\Movies`,
  `-NOMOVIES` — see `TGQ_FORMAT.md`. RTTI `TbTQILoader` and data sections
  `tqia_dat`, `idct_dat`, `lbmpeg_d` suggest an embedded MPEG-family codec,
  although **no MPEG start codes were found inside `pIQT` frame payloads**;
  the video codec remains unidentified.
- Sound banks: `%sdata\sound\SFX\`, `%sdata\sound\Music\` — see
  `SDT_FORMAT.md`. The MPEG audio identified in SDTs is consistent with the
  `idct_dat`/`lbmpeg_d` decoder tables being present in the same executable.
- KWD/KLD: `%sMaps\%sThings.kld`, `%sMaps\%sTriggers.kld`, `%s\Maps\%s.kwd`,
  `Map.kld`, `Players.kld`, `GlobalVariables.kwd`, `*.kwd` — see `KWD_FORMAT.md`.
- WAD/texture cache: all five `%sdata\...wad` paths plus `%s\EngineTextures.dat/.dir`
  — see `WAD_FORMAT.md`.

---

## 7. Explicit unknowns / limitations

1. No symbols (stripped); all module/loop hypotheses come from strings, RTTI
   names, source-file paths and section names, not from a recovered call graph.
2. Exact meanings of most registry values and command-line switches beyond
   their literal names were not established.
3. The video codec used by TQI/TGQ frames was not identified; `idct_dat` and
   `lbmpeg_d` are suggestive but not proof.
4. `DKII_SOFT.EXE` was only surveyed via sections/strings, not fully analysed.
5. The 5-byte difference between the two DX EXEs was verified byte-for-byte,
   but the runtime consequences of `DDRAW.dll` vs `PATCH.dll` were not tested.
6. No code was copied from any fan engine; all conclusions are from the local
   binaries.

---

## 8. Reproduction

```bash
cd "/home/kali/Desktop/rea/Dungeon Keeper 2"
sha256sum DKII.EXE DKII-DX.exe DKII_SOFT.EXE QMIXER.DLL WEANETR.dll \
         SFMAN32.DLL Aweman32.dll patch.dll
strings -a -n 5 -t x DKII.EXE > port/.work/strings/DKII.EXE.txt
objdump -h DKII.EXE
objdump -f DKII.EXE
objdump -d --start-address=0x638440 --stop-address=0x638480 DKII.EXE
cmp -l DKII.EXE DKII-DX.exe            # 5 differing bytes at 0x28B8D8
cat port/docs/strings_catalog.txt
```
