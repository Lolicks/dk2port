# OpenKeeper / dk2-tools implementation analysis (source-read)

This document analyses **existing code**, not README advertising. Every entry
below was read from the checked-out sources; citations are `path:line` (1-based)
and include the method/declaration name. Nothing here is inferred from a
project description alone. Where a capability was actually executed against
the shipped DK2 data, the command and output are recorded in §8 and the
format→verification table in §7.

Checked-out revisions (working trees clean):
`port/research/OpenKeeper` HEAD `e2bfe95d15627bb200261d66d2984576355dafda`,
`port/research/dk2-tools` HEAD `b4e0972847561ac0657ebdbeb8245d676455e7eb`.

## 1. Licence and dependency facts (cited, not guessed)

### OpenKeeper

* Source files carry an in-file GPL header, e.g.
  `WadFile.java:1-16`, `KmfFile.java:1-16`, `Dk2TextureDecoder.java:1-16`,
  `TgqFile.java:1-16`: "OpenKeeper is free software … either version 3 of the
  License, or (at your option) any later version."
* `port/research/OpenKeeper/license.txt:3-16` states the same GPLv3-or-later
  terms; `port/research/OpenKeeper/LICENSE:1-3` is the full GNU GPL v3 text;
  `README.md` "License" section says "GNU GPLv3 or later".
* `port/research/OpenKeeper/build.gradle:12` sets
  `jmonkeyengine_version = '3.9.0-stable'`; `build.gradle:89-101` declares
  `jme3-core/desktop/plugins/effects/networking/lwjgl3/niftygui`,
  `com.badlogicgames.gdx:gdx-ai:1.8.2`, `javax.vecmath:vecmath:1.5.2`,
  `com.simsilica:zay-es`, `zay-es-net`, `sio2`, `sim-ethereal`, JUnit 5.
  `README.md:8` says JME 3.9 + Java 25.
* The declared **libGDX dependency is `gdx-ai` only**; it is used by 27 source
  files under `src/main/java/toniarts/openkeeper/game/**` (e.g.
  `game/navigation/NavigationService.java`, `game/navigation/pathfinding/MapPathFinder.java`,
  `game/task/TaskManager.java`), **not** by any file in
  `tools/convert/**` or `video/tgq/**`. `grep -rl 'badlogic' src/main/java`
  returned 0 hits in `tools/convert` and `video/tgq`. A standalone C decoder
  therefore never needs libGDX.

### dk2-tools

* The checkout has **no LICENSE/COPYING file**:
  `find port/research/dk2-tools -maxdepth 2 -iname '*licen*' -o -iname '*copying*'`
  returned nothing, and `port/research/dk2-tools/README.md` has no licence
  section. Authorship is per-file (`wad_struct.bt:5-8` "Author: ufdada";
  `kmf_struct.bt:5-9` "Authors: ufdada … Original Code by Hoenir";
  `texturecache_dir_struct.bt:5-9` "Original code by Hoenir";
  `sdt_struct.bt:5-8` "Author: ufdada", rev 1.1).
  **Do not assume a permissive licence**; verbatim template/script code should
  not be copied into this project without resolving that. Format facts
  observed in the bytes are a separate matter.
* These are **010 Editor Binary Templates / Scripts**, not a compiled library:
  the two scripts call `RunTemplate(...)`, `CopyBytesToClipboard`, `FileNew`,
  `FileSave` (`port/research/dk2-tools/Scripts/wad_extract.1sc:11-14,51-70`;
  `Scripts/sdt_extract.1sc:12-15,65-115`). Running them requires proprietary
  010 Editor; it is not present here, so no template execution was attempted.

## 2. WAD / DWFB reader

Sources: `.../tools/convert/wad/WadFile.java`, `WadFileEntry.java`,
`.../tools/convert/WadExtractor.java`; templates `Formats/Archives/wad_struct.bt`,
`Scripts/wad_extract.1sc`. Full byte-level analysis and citations are in
`port/docs/format-research/WAD_FORMAT.md`.

| Aspect | Finding with citation |
|---|---|
| Key methods | `WadFile(Path file)` `WadFile.java:64-139`; `getWadFileEntries()` `:146-148`; `getWadFileEntryCount()` `:155-157`; `extractFileData(String,String)` `:164-177`; private `extractFileData(String,String,ISeekableResourceReader)` `:186-206`; private `getFileData(String,ISeekableResourceReader)` `:234-265`; `getFileData(String)` `:273-283`; `decompressFileData(byte[],String)` `:292-385`. `WadExtractor.main(String[])` `WadExtractor.java:34-55`. |
| What it reads | Magic/version/entry table/name pool (`WadFile.java:68-133`); payload bytes only on demand (`:246-247`). `WadExtractor.main` hardcodes `Data/Meshes.WAD` (`WadExtractor.java:47`). |
| Input format / structures | DWFB v2, `LinkedHashMap<String,WadFileEntry>` (`WadFile.java:54,117,132`); entry fields in `WadFileEntry.java:45-52`; enum `NOT_COMPRESSED(0)/COMPRESSED(4)` at `:29-44`. |
| Decompression | Only for flag 4, `WadFile.java:252-253,292-385` — LZ-style token codec (4 branches, `counter+1` back refs, 3-byte BE size at payload offset 2, stream start offset 5). Full algorithm in `port/docs/format-research/WAD_FORMAT.md:§6`. |
| Transformation | Names converted with `PathUtils.convertFileSeparators`, directory inherited from previous entry (`WadFile.java:112-130`); output written to `destination/<name>` (`:186-206`). |
| What the engine/converter receives | Raw or decompressed `byte[]` (`WadFile.java:273-283`), consumed by `ConvertModels` (`port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/conversion/task/ConvertModels.java:119`), `ConvertTextures` (`ConvertTextures.java:98-99,190-217`), `ConvertPaths` (`ConvertPaths.java:83`), `ConvertMouseCursors` (`ConvertMouseCursors.java:65`). |
| Dependencies | Only project helpers (`FileResourceReader`, `ConversionUtils`, `PathUtils`) + JDK; no jME, vecmath or AWT imports in either WAD file. A C reader needs no third-party library. |
| Not supported | No writer/repacker (verified in `port/docs/format-research/WAD_FORMAT.md:§9`); duplicate names collapse in the Java map (`WadFile.java:117,132`); the decompressor tolerates truncated streams (`WadFile.java:307-309,381-384`). |

## 3. KMF models

Sources: `.../tools/convert/kmf/KmfFile.java` (650 lines), the 16 data classes in
`.../tools/convert/kmf/`, `.../tools/convert/KmfModelLoader.java` (898 lines),
`port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/KmfAssetInfo.java:31-55` (55 lines), template
`Formats/Model/kmf_struct.bt` (331 lines).

| Aspect | Finding with citation |
|---|---|
| Key methods | `KmfFile(Path/InputStream/byte[])` `KmfFile.java:87-121`; `parseKmfFile(IResourceReader)` `:123-157`; `parseHead` `:165-169`; `parseMatl` `:177-187`; `parseMat2` `:195-216`; `parseMesh` `:224-259`; `parseMeshControls` `:267-282`; `parseMeshSprites` `:290-357`; `parseMeshGeoms` `:365-378`; `parseAnim` `:386-478`; `parseAnimControls` `:486-502`; `parseAnimSprites` `:510-580`; `parseGrop` `:588-612`; `checkHeader` `:620-625`. `Type.toType(int)` `:50-61` maps 1=MESH, 2=ANIM, 3=GROP. |
| What it reads | Uncompressed IFF-style chunks `KMSH` → `HEAD`, `MATL/MAT2`, `MESH/CTRL/SPRS/SPHD/GEOM` or `ANIM/POLY/VERT/ITAB/VGEO` or `GROP/ELEM` (`KmfFile.java:69-85,123-157`). It **does not decompress** — it is handed already-decompressed bytes (`WadFile.java:252-253` → `ConvertModels`). |
| Data structures | `Material` (`Material.java:30-60`, flags `:32-54`), `Mesh` (`Mesh.java:27-38`), `Anim` (`Anim.java:27-67`, `FrameFactorFunction` `:29`), `Sprite<T>` (`Sprite.java:32-41`), `MeshSprite`/`AnimSprite` (`MeshSprite.java:24`, `AnimSprite.java:24`), `MeshVertex`/`AnimVertex` (`MeshVertex.java:24-26`, `AnimVertex.java:24-26`), `Triangle` (`Triangle.java:24-26`), `Uv` (`Uv.java:24-26`), `Grop` (`Grop.java:26-33`), control/geometry holders (`MeshControl.java:25-29`, `AnimControl.java:25-29`, `AnimGeom.java:26-35`). |
| Decompression algorithm | None in `KmfFile`. The envelope/codec lives in the WAD reader (§2). The 010 template likewise operates on extracted `.kmf` bytes (`kmf_struct.bt:109-314`). |
| Transformation | `KmfModelLoader.load(AssetInfo)` `:144-171` builds a jME `Node`: `handleMesh` `:201-258` swaps Y/Z (`:204,224,232`) and divides UVs by 32768 (`:228`); `handleAnim` `:267-511` reconstructs poses from `ITAB`/`VGEO`/`GEOM` (`:329-336`), 30 fps times (`:278-280`) and a dummy `AnimClip` (`:503-507`); `createIndices` `:513-549` uses only LOD0 (`:209-210,237-241,462-466` — explicit `FIXME: LODs are broken`); `createGroup` `:179-192` links other `.j3o` assets; `getMaterials` `:651-767` writes `.j3m` files via `J3MExporter` (`:750-754`); tangent generation `:600-605`. |
| What the engine receives | A jME `com.jme3.scene.Node` with `Geometry`, `Material`, `AnimComposer`/`AnimClip` and user data keys (`MATERIAL_ALTERNATIVE_TEXTURES` `:101`, `ANIM_CONTROL_POINT` `:115`, `ANIM_FRONT_POINT` `:122`). `KmfAssetInfo` is only a cache wrapper: `getKmfFile()` `:48-50`, and `openStream()` deliberately throws `UnsupportedOperationException` (`:43-46`). |
| Dependencies | Parser: `javax.vecmath.Vector3f` only (`KmfFile.java:25`, `AnimGeom.java:20-ish`, `Mesh.java`); loader: heavy jME3 (`KmfModelLoader.java:19-42`: `AnimComposer`, `Mesh`, `VertexBuffer`, `Material`, `J3MExporter`, `MikktspaceTangentGenerator`). A standalone C decoder needs only a `{float x,y,z}` struct and a scene representation of your choice; none of the jME classes are required to parse geometry. |
| Template cross-check | `kmf_struct.bt:21` `FormatType`; `:23-35` `MaterialFlags`; `:69-77` `CTRL`; `:79-86` `SPHD`; `:88-107` `MATL/MAT2`; `:109-314` `Model` with ANIM/MESH/GROP branches; `:316-324` `getAnimGeom` decodes the same 10-bit packed coordinates as `KmfFile.java:443-450`; `:326-331` `checkSignature`. The template calls the field `gamma` (`:97`) while Java calls the same float `shininess` (`KmfFile.java:210`) — a naming divergence, not a layout one. |

## 4. Engine textures (`EngineTextures.dat/.dir`) and loading screens (`.444`)

Sources: `.../tools/convert/textures/Dk2TextureDecoder.java` (455 lines),
`.../textures/enginetextures/EngineTextureDecoder.java` (281),
`EngineTexturesFile.java` (330), `EngineTextureEntry.java` (90),
`.../textures/loadingscreens/LoadingScreenFile.java` (73),
`LoadingScreenTextureDecoder.java` (109), `.../textures/ImageUtil.java`;
template `Formats/Archives/texturecache_dat_struct.bt` (19) and
`texturecache_dir_struct.bt` (45).

| Aspect | Finding with citation |
|---|---|
| Key methods | `Dk2TextureDecoder.decompress_func1` `:148-223`, `decompress_func2` `:225-304`, `bs_read` `:306-319`, `prepare_decompress` `:321-426`, `clamp` `:428-436`, abstract `decompress_block` `:438`, `dd_texture` `:440-454`. `EngineTextureDecoder.decompress_block` `:65-108`, `decompress(boolean)` `:110-280`. `EngineTexturesFile(Path)` `:64-125`, `getFileData` `:221-263`, `decompressTexture` `:305-314`, `generateChessBoard` `:271-296`. `LoadingScreenFile(byte[])` `:36-55`, `decompressTexture` `:57-63`; `LoadingScreenTextureDecoder.decompress_block` `:32-108`. |
| What it reads / input | `.dir` header `TCHC`, size, version, entry count and `name\0+u32 offset` records (`EngineTexturesFile.java:64-114`; template `texturecache_dir_struct.bt:12-22`). `.dat` per-entry 20-byte header `u32 resX, resY, size(-8), u16 sResX, sResY, u32 flags` and payload start (`EngineTexturesFile.java:98-110`). `.444` header `u16 width, u16 height, u32 alphaFlag`, rest as u32 words (`LoadingScreenFile.java:38-51`). |
| Data structures | `EngineTextureEntry` fields `resX/resY/size/sResX/sResY/alphaFlag/dataStartLocation` (`EngineTextureEntry.java:25-33`); 8×8 block buffers `decompress2_chunk[256]`, `decompress3_chunk[288]`, `decompress4_chunk[512]` (`Dk2TextureDecoder.java:130-133`). |
| Decompression algorithm | Bit-plane + per-channel transform codec, **not LZ**: fixed `magic_input_table_6c10c0` (`:34-43`) transformed into `magic_output_table` (`:113-123`); `dc_control_table_7af0e0` (`:44-108`); `prepare_decompress` walks a prefix-code tree (`:321-426`); two 8-point IDCT-like passes `decompress_func1`/`decompress_func2` (`:148-304`); engine textures use a per-channel jump table, signed delta from the previous block and `blanket_fill == 2` short-circuit (`EngineTextureDecoder.java:31-54,110-280`); loading screens read an 8-bit red value per block and skip the `blanket_fill` path (`LoadingScreenTextureDecoder.java:32-78`). `dd_texture` iterates 8×8 blocks (`:447-452`). Credits: "Texture extraction code by George Gensure" (`Dk2TextureDecoder.java:25-27`). |
| Transformation / engine receives | Engine textures: u32 words → RGBA bytes (`EngineTextureDecoder.java:78-107`) → `BufferedImage` → PNG bytes (`EngineTexturesFile.java:230-262,305-314`); used by `ConvertTextures.extractTextureContainer` (`ConvertTextures.java:190-217`). `.444`: `LoadingScreenFile` → `BufferedImage` (`LoadingScreenFile.java:53-63`); `ConvertTextures.java:194-205` converts `.444` entries to PNG. A debug chessboard path exists when `DECOMPRESSION_ENABLED` is false (`EngineTexturesFile.java:54,249-253,271-296`). |
| Dependencies | The codec itself is pure Java arithmetic + `java.nio` (`Dk2TextureDecoder.java:19-22`; `EngineTextureDecoder.java:19-21`) — directly portable to C. PNG output uses `java.awt.image`/`ImageIO` (`EngineTexturesFile.java:19-34`), replaceable by libpng. No jME in these classes. |
| Template divergence | `texturecache_dat_struct.bt:12-19` declares `long dataStartLocation`, whereas Java consumes a 20-byte header (`readChunk(20)`, `EngineTexturesFile.java:100`) and sets `dataStartLocation = getFilePointer()` (`:110`). The template's field width/model differs from the Java reader; treat the Java behaviour as the one exercised by our data. |

## 5. TGQ / TQI movies

Sources: `.../video/tgq/TgqFile.java` (367), `TgqFrame.java` (495),
`EAAudioFrame.java` (146), `EAAudioHeader.java`,
`.../video/TgqPlayer.java` (469). No 010 template exists for TGQ in
`port/research/dk2-tools/Formats/**`.

| Aspect | Finding with citation |
|---|---|
| Key methods | `TgqFile.main` `:66-108`; `readFrame()` `:119-181`; `readAudioHeader(int)` `:204-218`; `readAudioHeaderTagData` `:220-240`; `readAudioHeaderSubStream` `:250-285`; `getPlatform` `:299-326`; `getCompression` `:328-345`; `getValue` `:355-366`. `TgqFrame(ByteBuffer,int)` `:158-175`; `decodeFrame` `:177-191`; `calculateDequantizationTable` `:193-201`; `decodeBlock` `:203-209`; `mpeg1DecodeBlock` `:217-271`; `decodeDc` `:273-293`; `decodeVlc` `:311-329`; `idctPut` `:336-350`; `eaIdctPut` `:352-362`; `idctTransform` `:379-414`; `getImage` `:427-446`. `EAAudioFrame(…)` `:44-50`; `decodeFrame` `:52-109`; `signExtend` `:118-122`. `TgqPlayer.play` `:113-137`; `AudioPlayer.run` `:203-305`; `VideoPlayer.run` `:336-402`; `TgqDecoder.run` `:416-467`. |
| What it reads | EA IFF-style chunks: fourCC + `int size` at `:123-129`, tags `pIQT` (video), `SCHl/SHEN`, `SCCl/SCEN`, `SCDl/SDEN`, `SCEl/SEEN` (`:55-64,132-178`). Audio header: `PT` patch, platform id and length-prefixed big-endian tag values (`:204-218,355-366`). |
| Data structures | `EAAudioHeader` defaults 16-bit, EA XA ADPCM, 1 channel, 22050 Hz (`TgqFile.java:287-297`); `EAAudioFrame` keeps LE PCM `ByteBuffer` (`:42,72-74,133-135`); `TgqFrame` allocates 3 YCbCr planes with 16-pixel-padded `codedWidth/codedHeight` (`TgqFrame.java:166-171`) and exposes them via `getBufferForPlane(int)` / `getLineSize(int)` (`:448-472`). |
| Decompression algorithm | Video: **EA TQI**, an MPEG-1-derived VLC/DCT variant — dequant table from `BASE_TABLE`/`BASE_TABLE2` and a quantizer (`:65-82,193-201`), 9-bit DC Huffman and 28-bit texture VLC with tables `:85-139` and `:311-329`, zig-zag scan `:47-64`, escape handling `:237-257`, custom two-pass integer IDCT `:336-414`. Frame header consumed before the bitstream: `u16 width, u16 height, u8 quantizer, skip 3` (`:161-179`) — i.e. 8 bytes, matching our probe's `TQI_HEADER_SIZE = 8` (`port/tools/tgq_decode.py:39-41`). Audio: EA XA ADPCM, 2 channels, 28-sample blocks with predictor coefficients from `EA_ADPCM_TABLE` (`EAAudioFrame.java:31-37,77-107`). |
| Transformation / engine receives | `TgqFile` is abstract with callbacks `onAudioHeader`, `addVideoFrame`, `addAudioFrame` (`:188-202`); `TgqPlayer` queues decoded frames (`:420-437`), feeds PCM bytes to `javax.sound.sampled.SourceDataLine` (`:312-325,265-275`) and hands `TgqFrame` YUV planes to subclasses via `onNewVideoFrame` (`:191,382`). `getImage()` is explicitly "only used for debugging" (`:421-427`) and uses nearest-neighbour chroma (`:432-440`). Playback is 25 fps (`:52-53`) despite the format spec's 15 fps. |
| Dependencies | Parsers/decoders: JDK NIO, `BitReader`, `javax.imageio`/`java.awt.image` only for the debug `getImage()` (`TgqFrame.java:19-27,427-446`). `TgqPlayer` adds Java Sound and threads (`:19-34,56-74`) — irrelevant to a C decoder. No jME in `video/tgq/**`. |
| Not implemented here | `port/tools/tgq_decode.py:1-24` deliberately stops at the bitstream boundary ("container/header probe, not a pixel decoder"); OpenKeeper **does** contain a full TQI pixel decoder in `TgqFrame`, so our probe is the weaker implementation. `LoadingScreenFile.java:25-27` contains an upstream comment that ".444" files use "some sort of TQI packing (but our movie decoder doesn't unpack them)"; that comment was not verified against the movie decoder and may be stale. |

## 6. dk2-tools binary templates and scripts (inventory read from source)

All paths are under `port/research/dk2-tools/`. Line counts from
`wc -l`; key declarations were read in the files themselves.

| File | Lines | What it parses (cited declaration) | Executed by us? |
|---|---:|---|---|
| `Formats/Archives/wad_struct.bt` | 58 | `CompressType` `:12`, `WadHeader` `:15-31`, `WadEntry` `:33-54`, `getAssetFileName` `:56-58` | No (needs 010 Editor); algorithm cross-checked against our decoder |
| `Scripts/wad_extract.1sc` | 178 | template invocation `:11-14`; extraction loop `:32-72`; full `decompressFileData` `:74-178` | No; algorithm ported and verified on real data (doc A §8) |
| `Formats/Model/kmf_struct.bt` | 331 | `FormatType` `:21`, `MaterialFlags` `:23-35`, `MATL/MAT2` `:88-107`, `Model` `:109-314`, `getAnimGeom` `:316-324` | No; our `kmf_tool.py` parsed 2015 KMF files (§8) |
| `Formats/Archives/texturecache_dir_struct.bt` | 45 | `DirHeader` `:12-17`, `Entry` `:19-22`, mip-level helpers `:24-45` | No |
| `Formats/Archives/texturecache_dat_struct.bt` | 19 | `DatHeader` `:12-19` | No |
| `Formats/Archives/sprite_struct.bt` | 53 | `sprite_struct.bt:11-53`: `Header` (PSFB) `:11-14`, `Entry` `:16-20`, per-frame byte walk `:27-53` | No |
| `Formats/Sound/sdt_struct.bt` | 47 | `SoundType` `:19-25`, `count`/offsets `:27-28`, 40-byte `Entry` `:30-43`, `ReadSoundData` `:45-47`; rev-1.1 comment `:10-16` claims verification against 142 files / 8959 entries | No; we independently reproduced those counts (§8) |
| `Scripts/sdt_extract.1sc` | 117 | template invocation `:12-15`; type switch `:36-55`; clipboard copy `:65`; RIFF/WAVE header synthesis `:75-106`; save `:108-115` | No |
| `Formats/Level/kwd_struct.bt` | 161 | includes `:10-36`, `Header` `:38-89`, `while (!FEof())` + 16-way signature switch `:91-161` | No |
| `Formats/Camera/kcs_struct.bt` | 25 | camera-sweep structure | No |
| `Formats/Sound/{bank,sf2,sfx,sfx_struct_2}.bt` | 84/321/202/146 | sound bank / SoundFont / SFX map variants | No |
| `Formats/Font/bf4_struct.bt` | 34 | BF4 font | No |
| `Formats/Level/*.bt` (24 files, e.g. `things_struct.bt` 318, `creatures_struct.bt` 444, `variables_struct.bt` 416, `level_struct.bt` 220, `player_struct.bt` 212) | 17..444 | included by `kwd_struct.bt:11-36`; chunk-specific record layouts | No |
| `Formats/Text/*.bt` (`str`, `residx`, `addressbook`, `hiscores`, `jcn`, `mb2uni`) | 17..42 | text/resource-index data files | No |
| `Formats/Save/save_struct.bt` | 177 | savegame structure | No |
| `Formats/util_struc.bt` | 169 | shared helper types (`Vec3f`, `string`, flags) | No |
| `decode.txt` | 91 | hand-maintained checklist of yet-undecoded fields (e.g. traps `:4-7`, terrains `:11-15`, players `:29-33`) | No; read as evidence of incompleteness |

## 7. Format → implementation → verified?

"Verified" means we ran a tool in this checkout against shipped game bytes and
recorded the result (§8). "Source-read" means the code was read but not
executed. Neither status implies completeness of the implementation.

| Format | Implementation(s) | Verified? |
|---|---|---|
| DWFB container | `WadFile.java:64-139`; `wad_struct.bt:15-54`; `port/tools/wad_tool.py:84-157` | **Yes** — all 5 WADs, counts/layout, zero warnings (§8.1) |
| KMSH/KMF compression | `WadFile.decompressFileData` `WadFile.java:292-385`; `wad_extract.1sc:74-178`; `kmsh_decompress.py:148-258` | **Yes** — 2015/2015 entries to exact size, all KMSH (§8.2) |
| KMF chunk tree | `KmfFile.java:123-625`; `kmf_struct.bt:109-331`; `kmf_tool.py` | **Yes** — 2015/2015 KMF files parse clean (§8.2) |
| KMF → engine scene | `KmfModelLoader.java:144-608` | Source-read only (jME runtime not executed) |
| EngineTextures `.dat/.dir` + DCT codec | `EngineTexturesFile.java:64-314`, `EngineTextureDecoder.java:65-280`, `Dk2TextureDecoder.java:148-454`; templates `:12-22` / `:12-19` | Source-read only; `.dir` header bytes inspected, decoding not executed |
| `.444` loading screens | `LoadingScreenFile.java:36-63`, `LoadingScreenTextureDecoder.java:32-108` | Source-read only |
| TGQ/TQI container | `TgqFile.java:119-181`; `port/tools/tgq_decode.py`, `tgq_probe.py` | **Yes** — `trailer.tgq`: 1503 chunks, 750 video frames, 320×480, one terminal header pattern (§8.3) |
| TQI video pixels | `TgqFrame.java:158-446` | Source-read only; our Python tool explicitly does not decode pixels |
| EA XA ADPCM audio | `EAAudioFrame.java:44-109` | Source-read only |
| SDT sound archive | `sdt_struct.bt:19-47`, `sdt_extract.1sc:36-115`, OpenKeeper MP2 path `audio/plugins/MP2Loader.java:43-164` | **Partly** — 142 files/8959 entries/40-byte headers and type values 0,2,36,37 confirmed from bytes (§8.4); templates not executed, WAV/MP2 extraction not verified |
| KWD/KLD level chunks | `kwd_struct.bt:38-161` + 24 included templates; our `kwd_tool.py` | Source-read here; see `port/docs/KWD_FORMAT.md` and `port/tests/test_kwd.py` for our independent verification |
| Original game logic / AI | OpenKeeper's own ECS: `game/component/CreatureAi.java:19-22`, `game/controller/creature/CreatureController.java`, `game/navigation/NavigationService.java`, 543 Java files under `src/main/java/toniarts/openkeeper/game/` | Source-read; **not** a byte-exact reimplementation — see §9 |

## 8. Verification commands and results

Run from `/home/kali/Desktop/rea/Dungeon Keeper 2` unless noted.

### 8.1 WAD (also in doc A §8)

```
python3 port/tools/wad_tool.py list Data/Meshes.WAD --json
# entry_count=2015, flags {4:2015}, pool_end == file_size == 32026721, warnings=[]
```

### 8.2 KMSH + KMF

```
$ python3 tools/kmsh_decompress.py batch ../Data/Meshes.WAD tmp/wad_kmf_all --json \
    > tmp/wad_kmf_all.json
extracted 2015 statuses {'ok'} ; files 2015 non-KMSH 0 size-mismatch 0
$ python3 tools/kmf_tool.py validate tmp/wad_kmf_all --json
{'clean': 2015, 'with_issues': 0, 'errors': 0}
$ python3 tools/kmf_tool.py info tmp/wad_kmf_check/g_Maiden-Torture.kmf --json
type=ANIM version=17 chunks=25 head.frame_count=101 itab_entries=100
```

### 8.3 TGQ container (OpenKeeper's frame header cross-check)

```
$ python3 port/tools/tgq_decode.py info Data/Movies/trailer.tgq --json > port/tmp/tgq_trailer_info.json
{"path":"Data/Movies/trailer.tgq","file_size":6785068,"walk_exact":true,
 "chunk_count":1503,"frame_count":750,"widths":{"320":750},
 "heights":{"480":750},"quantizer_flags_histogram":{"100":716,"99":34},
 "extra_field_histogram":{"14 1e 03":750},"mpeg_start_codes_total":3,
 "decoder_status":"NOT_IMPLEMENTED: ... bitstream is exposed verbatim only"}

$ python3 port/tools/tgq_decode.py headers Data/Movies/trailer.tgq --limit 3 --json
index 0 source_offset 40    chunk_size 2124 width 320 height 480 quantizer 100
       extra "14 1e 03" bitstream_offset 8 bitstream_size 2108 prefix "bf 94 52 e8 ..."
index 1 source_offset 2176  chunk_size 2124 (same header)
index 2 source_offset 5280  chunk_size 2124 (same header)
```

The constant 8-byte header and bitstream offset match
`TgqFrame.java:161-183` (two `getShort()` + one `get()` + skip 3, then
`new BitReader(buf)`).

### 8.4 SDT template cross-check

```
$ python3 - <<'PY'   # parse u32 count, u32 offsets, 40-byte entry headers
...
PY
sdt_files= 142 total_entries= 8959 types= {0: 999, 36: 7461, 2: 211, 37: 288} header_sizes= {40: 8959}
```

This matches the statement in `sdt_struct.bt:11-16` (142 files, 8959 entries,
40-byte headers) and the `SoundType` values at `:19-25`; `WAV_OLD=3` is not
present in this data set. No 010 template was run.

## 9. What OpenKeeper does **not** implement (corrected claim)

The often-repeated claim "OpenKeeper has no game logic / no AI" is **false for
this checkout** and is not repeated here. Verified facts:

* `README.md:4-6` describes the goal as a full open-source remake of DK2 v1.7
  using the original assets, and `README.md:6` notes the original game is
  required to play/develop.
* There are **543 Java files** under
  `port/research/OpenKeeper/src/main/java/toniarts/openkeeper/game/`, including
  `game/component/CreatureAi.java:19-22` ("Simple creature AI component"),
  `game/logic/MovementSystem.java`, `game/controller/creature/CreatureController.java`,
  `game/navigation/NavigationService.java` and `game/task/TaskManager.java`.
  So game logic exists — but it is **hand-written**, not the original AI from
  `DKII.EXE` and not a byte-exact behavioural clone. The libGDX `gdx-ai`
  dependency (`build.gradle:96`) supports pathfinding/steering in this rewritten
  logic (27 files under `game/**`), not the asset parsers.
* What is genuinely absent from these implementations:
  - any DWFB **writer**/repacker (doc A §9);
  - use of original executable code, scripts, AI or pathfinding tables —
    OpenKeeper consumes converted **assets** only
    (`ConvertModels.java:119`, `ConvertTextures.java:98-99`, `ConvertPaths.java:83`);
  - 010 templates for TGQ/TQI video, and a Python pixel decoder in *our* tools
    (`port/tools/tgq_decode.py:1-24`) — OpenKeeper's `TgqFrame` does implement
    pixels, so this gap is ours, not OpenKeeper's;
  - execution of the 010 templates in this environment (010 Editor absent);
  - resolution of the unknown KWD/Save fields listed in `dk2-tools/decode.txt:1-91`;
  - support for DWFB versions other than 2 (`WadFile.java:56,79-81`;
    `wad_struct.bt:22-25`).

## 10. Standalone C decoder: what can be reused

* **Directly portable arithmetic**: WAD token codec
  (`WadFile.java:292-385`), KMF chunk parser (`KmfFile.java:123-625`), texture
  block codec (`Dk2TextureDecoder.java:148-454`, `EngineTextureDecoder.java:65-280`,
  `LoadingScreenTextureDecoder.java:32-108`), TQI VLC/IDCT
  (`TgqFrame.java:193-446`) and EA ADPCM (`EAAudioFrame.java:52-109`). These
  use only integer/float arithmetic and byte buffers; jME/libGDX/AWT are not
  involved.
* **Avoidable runtime deps**: replace `javax.vecmath.Vector3f` with
  `struct { float x,y,z; }` (`KmfFile.java:25`); replace `BufferedImage`/`ImageIO`
  with libpng or your engine texture type (`EngineTexturesFile.java:19-34`);
  replace `AnimComposer`/`Material`/`J3MExporter` with your own scene struct
  (`KmfModelLoader.java:19-42,750-754`); skip `TgqPlayer` entirely (Java Sound
  + threads, `TgqPlayer.java:56-74,312-325`).
* **Licensing caution**: OpenKeeper-derived code is GPLv3-or-later
  (`license.txt:3-16`); dk2-tools has no licence file in this checkout. Our
  `port/tools/kmsh_decompress.py:4-10` names both sources explicitly.
  Re-implementing an algorithm from observed bytes is different from copying
  source text; keep that distinction if this work is redistributed.
