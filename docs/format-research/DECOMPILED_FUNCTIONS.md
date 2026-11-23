# `DKII.EXE` decompiled functions ↔ OpenKeeper / dk2-tools format implementations

Scope: map the 8 260 decompiled Ghidra functions in `re/ghidra/` to the format
implementations in `port/research/`. All work is static: decompiled C, assembly,
PE string tables and source files already in this checkout. No claim below rests
on a function name alone, and no claim is upgraded to a behavioural fact without
a quoted byte-, constant-, string- or call-level observation.

## 0. Method and confidence rubric

The Ghidra export gives **no semantic function names**: every entry in
`re/ghidra/functions.json` is `FUN_<address>` with a stub signature
`undefined FUN_…(void)`. The only meaningful names in `re/ghidra/symbols.json`
are import thunks (`QSWaveMix*`, Win32 API), data labels (`DAT_…`) and string
labels (`s_…`). Therefore:

| Label | Meaning used in this document |
|---|---|
| HIGH | A literal magic, fourcc, table address, or a token-by-token algorithmic identity is visible in the quoted decompiled C **and** in the reference implementation. |
| MEDIUM | The call graph, string references and field arithmetic identify the subsystem, but the correspondence to a particular Java method is structural, not byte-for-byte. |
| LOW | Plausible from neighbourhood/arity only; explicitly not used as a finding. |
| none | No function found; the reason is stated in §5. |

The word "confirmed" is deliberately not used in this document. Every mapping
carries its evidence inline so the reader can downgrade it.

The decompiled C signature (first lines of `re/ghidra/decomp/<addr>.c`) is
reported where it exists; the JSON `signature` field is the export stub and is
not evidence.

## 1. Corpus facts (reproduced)

```
$ cat re/ghidra/export_summary.txt
functions=8260
decompiled=8260
failed=0
heavy=3
006211c0 FUN_006211c0 72501
006251e0 FUN_006251e0 91688
00666480 FUN_00666480 62895
```

```
$ python3 -c "import json;d=json.load(open('re/ghidra/functions.json'));print(len(d),sum(f['thunk'] for f in d),sum(f['external'] for f in d))"
8260 0 0
$ ls re/ghidra/decomp/*.c | wc -l ; ls re/ghidra/asm/*.asm | wc -l
8260
8260
```

`re/ghidra/memory.json` identifies the sections that matter here:
`.text 00401000..00652fff`, `.rdata 0066c000..0068dfff`,
`.data 0068e000..007a6fff`, `idct_dat 007ad000..007aefff`,
`tqia_dat 007af000..007affff`, `lbmpeg_d 007b2000..007b2fff`.

Section raw offsets were independently re-derived from the PE header of
`DKII.EXE` (image base `0x400000`; `.data` RVA `0x28e000`, raw `0x28c600`).
This matters for one correction made below: the KMF tag table
`HEAD\0\0\0\0 KMSH\0\0\0\0 TEXTURE_SET\0 MAT2\0 MATL\0 …` is at file offset
`0x2bc730`, which maps to VA `0x6be130` via the **`.data`** delta `0x401a00`,
not via the `.rdata` delta. Check:

```
$ python3 -c "d=open('DKII.EXE','rb').read();print(repr(d[0x2bc730:0x2bc760]))"
b'HEAD\x00\x00\x00\x00KMSH\x00\x00\x00\x00TEXTURE_SET\x00MAT2\x00\x00\x00\x00MATL'
```

So `DAT_006be130 = "HEAD"`, `DAT_006be138 = "KMSH"`,
`s_TEXTURE_SET_006be140 = "TEXTURE_SET"` are genuine labels, and the decompiled
pushes of `0x6be130` / `0x6be138` in `FUN_0057d270` are genuine string references
(verified against `objdump -d --start-address=0x57d270 --stop-address=0x57d380 DKII.EXE`:
`push $0x6be138`, `push $0x6be130`).

## 2. Candidate catalogue

### 2.1 KMF / KMSH loading and parsing

**FUN_0057d270 — KMSH object builder and layout selector. HIGH.**
Address `0x0057d270`; ID `FUN_0057d270`; signature
`int * FUN_0057d270(char *param_1, undefined4 param_2, undefined4 param_3)`;
size 284 B; callers (JSON) 3, direct textual callers 2
(`0057ed30`, `0057ef30`); related: `0057a800`, `0057a1e0`, `0059a500`,
`0057d5b0`, `0057dad0`, `0057e110`.

Evidence (`re/ghidra/decomp/0057d270.c:53-69`):

```c
uVar4 = FUN_0059a500(local_400);
FUN_0057a800(uVar4,&DAT_006be138,local_404);
FUN_0057a1e0(&DAT_006be130);
iVar2 = *(int *)(DAT_00766648 + DAT_0076622c);
DAT_0076622c = DAT_0076622c + 8;
FUN_0057a310();
piVar8 = (int *)0x0;
if (iVar2 == 1) { FUN_0057d390(param_3); piVar8 = (int *)FUN_0057d5b0(); }
else if (iVar2 == 2) { FUN_0057d390(param_3); piVar8 = (int *)FUN_0057dad0(); }
else if (iVar2 == 3) { piVar8 = (int *)FUN_0057e110(); }
FUN_0057a150();
(**(code **)(*piVar8 + 8))(param_2);
```

Read: open the `KMSH` chunk, seek to `HEAD`, read a u32 from the decompressed
buffer (`DAT_00766648` + cursor `DAT_0076622c`), skip 8 bytes, dispatch on 1/2/3.
This is the same field order as
`KmfFile.parseKmfFile` / `parseHead`
(`research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/kmf/KmfFile.java:123-157`
and `:165-169`): `KMSH` → skip chunk size → version → `HEAD` → skip size →
`Type.toType(u32)` → unknown u32. The selector value is the **type**, not the
version. `KmfFile.Type.toType` (`KmfFile.java:50-61`) maps 1=MESH, 2=ANIM,
3=GROP; the EXE's branch-3 body (§2.5) is consistent with GROP.

**FUN_0059a500 — mesh-name normalisation and resource load. HIGH.**
Address `0x0059a500`; signature `void FUN_0059a500(char *param_1)`; size 99 B;
callers 1 (`0057d270`).

```c
iVar2 = _strncmp(s_uniuquemesh_006be880,param_1,0xb);   /* :13 */
...
(**(code **)(*DAT_00795700 + 0x118))(local_104);        /* :39 */
```

The `uniuquemesh` prefix (typo preserved in the binary string at `0x6be880`) is
stripped, then the load is delegated through vtable slot `+0x118` of
`DAT_00795700`. The slot is the file-system/WAD layer; see §5.

**FUN_0057a800 — IFF-style chunk open. MEDIUM.** Size 343 B; caller
`0057d270`. It resets a chunk stack (`param_1[0x85] = 0x7fff0000`),
calls `FUN_0057a1e0(param_3)` to seek to the requested tag and reads a 4-byte
chunk size via `FUN_0057a3a0(&param_2,4)`
(`re/ghidra/decomp/0057a800.c:38-56`). This is the conceptual counterpart of
`IResourceChunkReader.readChunk(28)` used by `KmfFile.java:125`, but there is no
byte-level identity of a Java method here.

**FUN_0057a1e0 — IFF chunk scanner/seek. MEDIUM.** Size 299 B; callers
(JSON) 26, direct textual callers 7. It compares `param_2 == (int *)iVar3`
where `iVar3 = *param_2` (4-byte tag), skips `size - 8` and records
`param_1[param_1[4]+0x86] = iVar1`. Together with `FUN_0057a800` it implements
the chunk navigation that `KmfFile` gets for free from `IResourceChunkReader`.

**FUN_0057d5b0 — layout parser, selector 1 (MESH). HIGH.** Size 1288 B;
caller `0057d270`. It reads a name up to `\0`, then `uVar5 = iVar7 * 3`
(tri-strip vertex count) and consumes vertex/polygon arrays from the
decompressed buffer:

```c
uVar5 = iVar7 * 3;                                     /* 0057d5b0.c:106 */
local_8 = (int *)FUN_0057ceb0();                        /* :98 */
iVar6 = FUN_0057e860();                                /* :104 */
```

**FUN_0057dad0 — layout parser, selector 2 (ANIM). HIGH.** Size 1586 B;
caller `0057d270`. Reads a name, five u32 header fields at offsets
`+0,+5,+9,+0xd,+0x11` and 0x12-stride records
(`re/ghidra/decomp/0057dad0.c:39-49`). Structurally matches the ANIM layout in
`kmf_struct.bt:127-217` / `KmfFile.parseAnim` (`KmfFile.java:386-478`), with the
caveat that the exact per-field mapping was not re-derived here.

**FUN_0057e110 — layout parser, selector 3 (GROP). HIGH.** Size 395 B;
caller `0057d270`; calls `FUN_0057ef30` per element.

```c
FUN_0057a1e0(&DAT_006be1b4);
FUN_0057a1e0(&DAT_006be130);
local_410 = *(int *)(DAT_00766648 + DAT_0076622c);
...
while (*(char *)(DAT_00766648 + -1 + DAT_0076622c) != '\0');   /* element name */
uVar4 = FUN_0057ef30(local_40c,0,1,1);                          /* :47 */
*(undefined4 *)(iVar5 + iVar2) = uVar4;                         /* :48 */
iVar5 = iVar5 + 0x10;                                           /* :64 */
```

A counted list of names, each resolved to another mesh via `FUN_0057ef30`,
with 16-byte element records. This is the `GROP/ELEM` shape
(`kmf_struct.bt:292-305`, `KmfFile.parseGrop` at `KmfFile.java:588-612`).

**FUN_0057ef30 — mesh lookup/create by name. MEDIUM.** Size 223 B; callers 3;
calls `FUN_0058df40` (lookup by hashed name) and `FUN_0058de00` (register), then
`FUN_0057d270` when the name is non-empty. This is a resource-manager wrapper,
not a parser.

**Version 17: no dedicated check found.** A scan of every decompiled file for
`== 0x11`/`!= 0x11` in the mesh/WAD neighbourhood (`0057*`, `0059*`, `005d*`,
`0060*`) found 0 version comparisons; the `0x11` hits there are array indices
(`param_1[0x11]`) or unrelated case labels. The decompressed KMF body begins
`11 00 00 00 48 45 41 44` (version 17 + `"HEAD"`) per `docs/KMSH_CODEC.md:§1`,
but in `OpenKeeper` the version is read and stored
(`KmfFile.java:127-130`) without an equality check either. Statement: **no
function in the export implements a `version == 17` gate**; the selector in
`FUN_0057d270` is `HEAD.type`.

### 2.2 WAD / DWFB archive reading

**FUN_005d8560 — WAD open / header validation. HIGH.**
Address `0x005d8560`; signature
`undefined4 * __thiscall FUN_005d8560(int *param_1, uint param_2)`;
size 750 B; callers 1 (`005baad0`); related `005fe5d0`, `005fe9b0`.

```c
iVar7 = (**(code **)(*piVar3 + 8))(param_1 + 0x18,0x58);          /* :92 */
if (((iVar7 == 0x58) && (param_1[0x18] == 0x42465744)) &&
    ((uint)param_1[0x19] < 3)) {                                  /* :93 */
  FUN_005fe5d0(auStack_262 + -0x12,piVar3,param_1[0x19] == 2);    /* :94 */
```

`0x42465744` is little-endian `"DWFB"`; `0x58` is the 88-byte WAD header;
`< 3` is the accepted version set {0,1,2}, and `== 2` is passed as a boolean to
the next stage. All three facts match `Formats/Archives/wad_struct.bt:15-31`
and `WadFile.java:64-139` (v2 only in Java: `WadFile.java:56,79-81`).

**FUN_005fe5d0 — WAD entry table + name pool reader. HIGH.**
Signature `void __thiscall FUN_005fe5d0(int *param_1, undefined4 param_2, int *param_3)`;
size 887 B; callers 2 (`005d8560`, `005d8b10`).

```c
iVar2 = (**(code **)(*param_3 + 8))(auStack_6c,0x58);  /* :81 88-byte header */
if (iVar2 != 0x58) { ... return; }                      /* :82 */
iVar5 = uStack_24 * 0x28;                               /* :87 count * 40 bytes */
iVar2 = FUN_00634e80(iVar5);
iVar3 = (**(code **)(*param_3 + 8))(iVar2,iVar5);
...
FUN_005fe000(pcVar7,~uVar4,0);                          /* :126 name fixup */
...
piVar6 = piVar6 + 10;                                   /* :145 next 40-byte entry */
```

`0x28 = 40` bytes per entry and a 10-`int` stride are exactly
`WadHeader.entry count` + `WadEntry` in `wad_struct.bt:33-54` and
`WadFile.java:93-133`. The name is read from the pool using the entry's
length/offset pair (`piVar6[-1]`, `*piVar6`), matching `getAssetFileName`
(`wad_struct.bt:56-58`) and `WadFile.java:112-130`.

**FUN_005fe9b0 — WAD reader object setup. MEDIUM.** Size 176 B; callers 2
(`005d8560`, `005d8850`). Copies a name into a 0x104-byte buffer and zeroes
eight header words (`param_3[0..7]`, `param_3[1] = 0x10`). No format constant;
the role is inferred from its two callers.

**FUN_005d8b10 / FUN_005d8850 — alternate WAD open paths. MEDIUM.**
Sizes 1456 B / 364 B. `005d8b10` calls `005fe5d0` (`decomp/005d8b10.c:46`);
`005d8850` calls `005fe9b0` (`decomp/005d8850.c:84`). They are the archive-layer
callers, not separate parsers.

### 2.3 KMSH / WAD LZ decompressor — `FUN_00603710`

**FUN_00603710 — the byte decompressor. HIGH.** Address `0x00603710`;
signature `uint FUN_00603710(byte *param_1, byte *param_2)` (destination,
source); size 489 B; JSON callers 1, but **no direct call site exists in the
8260-file export** — it is reached through an indirect pointer. This is the
single most important new mapping in this document: it is a token-for-token
implementation of `WadFile.decompressFileData`
(`research/OpenKeeper/src/main/java/.../wad/WadFile.java:292-385`) and
`wad_extract.1sc:74-178`, which in turn decode the KMSH payloads.

Evidence, quoted from `re/ghidra/decomp/00603710.c`:

```c
uint FUN_00603710(byte *param_1,byte *param_2)                       /* :2 */
pbVar4 = param_2 + 2;
if ((*param_2 & 1) != 0) { pbVar4 = param_2 + 5; }                   /* :19-20 */
uVar3 = (uint)CONCAT21(CONCAT11(*pbVar4,pbVar4[1]),pbVar4[2]);       /* :21 3-byte BE size */
pbVar4 = pbVar4 + 3;
...
if ((bVar1 & 0x80) != 0) break;                                     /* :33 */
pbVar5 = pbVar7 + ((local_c & 0x60) * -8 - (int)param_1) + -1;
iVar6 = (bVar1 >> 2 & 7) + 2;                                       /* :38-39 */
...
if ((bVar1 & 0x40) != 0) break;                                     /* :55 */
pbVar5 = pbVar7 + (((uint)param_1 & 0x3f) * -0x100 - (int)param_2) + -1;
iVar6 = (bVar1 & 0x3f) + 3;                                         /* :60-61 */
...
if ((bVar1 & 0x20) != 0) break;                                     /* :78 */
pbVar5 = pbVar7 + (((local_c & 0x10) * -0x1000 + (uint)bVar2 * -0x100) - (int)param_2) + -1;
iVar6 = (local_c & 0xc) * 0x40 + 4 + local_4;                       /* :86-87 */
...
uVar8 = (bVar1 & 0x1f) * 4 + 4;                                     /* :103 */
if (0x70 < uVar8) break;                                            /* :104 */
```

Token-by-token comparison:

| Stream feature | `FUN_00603710` | `WadFile.java:294-380` / `wad_extract.1sc:82-168` | match |
|---|---|---|---|
| header: bit0 ⇒ +3, skip 1, 3-byte BE size, then start | `+2`/`+5`, `*pbVar4<<16\|pbVar4[1]<<8\|pbVar4[2]`, `+3` | `i++`, `i+=3`, `i++`, `(src[i]<<16)+(src[i+1]<<8)+src[i+2]`, `i+=3` | exact |
| token 0 (`flag&0x80 == 0`) back-distance | `((flag&0x60)*-8) - tmp - 1` | `k -= (flag&0x60)<<3; k -= tmp; k--` | exact |
| token 0 run length | `(flag>>2 & 7)+2` | `((flag>>2)&7)+2` | exact |
| token 1 (`flag&0x40 == 0`) back-distance | `((flag&0x3f)*-0x100) - tmp2 - 1` | `k -= (tmp&0x3F)<<8; k -= tmp2; k--` | exact |
| token 1 run length | `(flag&0x3f)+3` | `(flag&0x3F)+3` | exact |
| token 2 (`flag&0x20 == 0`) back-distance | `(flag&0x10)*-0x1000 + localtemp*-0x100 - tmp2 - 1` | `k -= (flag&0x10)<<12; k -= localtemp<<8; k -= tmp2; k--` | exact |
| token 2 run length | `(flag&0xc)*0x40 + 4 + tmp3` | `tmp3 + ((flag&0x0C)<<6) + 4` | exact |
| token 3 terminator | `(flag&0x1f)*4+4 > 0x70` ⇒ final `flag&3` literals | identical (`WadFile.java:370-378`) | exact |
| overlapping back-copy | `do { *dst++ = *src++; } while(--n)` | same `do/while` | exact |

The assembly independently confirms the unusual terminator branch:
`asm/00603710.asm:174-190` has `AND EDX,0x1f; LEA EDX,[EDX*4+0x4];
CMP EDX,0x70; JA 0x006038d9`, where `0x006038d9` is `AND EBX,0x3` (final
literal run). This is not a decompiler artefact.

**Correction to prior documentation.** `docs/KMSH_CODEC.md:§3.2-3.3` states that
no byte decompressor was found in the export and that the work "must live" in
the `TbWadFileStorage +0x118` virtual method. That search used high-byte
(`0xe0`) and RNC-signature probes, which do not match this token codec. The
data-side algorithm was already recovered in
`docs/format-research/WAD_FORMAT.md:§6`; this section adds the executable-side
function. The `+0x118` call in `FUN_0059a500` remains real — it selects/loads
the archive entry — but it is **not** the only place where decompression code
exists.

### 2.4 Material handling

**FUN_0057c420 — build a render material from KMF material fields. HIGH.**
Signature `undefined4 * __thiscall FUN_0057c420(undefined4 *param_1, undefined4 *param_2, undefined4 param_3)`;
size 733 B; caller `0057c850`.

```c
uVar2 = param_2[0x11];                 /* :30  flags word from parsed material */
if ((uVar2 & 0x100) == 0) { param_1[3] = 0x5c0; }
if ((uVar2 & 0x10) != 0)  { ... *pfVar1 = 255.0; ... }
if ((uVar2 & 0x200) != 0) { FUN_00590b40(1,param_2[4],0); ... }
...
FUN_00590b40(bVar4,*(undefined4 *)(param_2[5] + iVar5 * 4),iVar8);  /* :93 */
```

Bit tests on a flags word, RGB floats and per-texture lookups correspond to
`Material` flags and `KmfModelLoader.getMaterials`
(`research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/kmf/Material.java:30-60`,
`KmfModelLoader.java:651-767`). The bit-to-flag table is not decoded here, so
the correspondence is at the "same data model" level, not literal.

**FUN_0057c780 — material handle lookup with bounds check. MEDIUM.**
Size 45 B; 31 callers. `if ((param_1 < 0) || (DAT_00765b04 <= param_1))
FUN_005595c0(&DAT_00756ee8,s_Invalid_Material_006be0bc); return ...`.
**FUN_0057c970** (507 B) repeats the same check and uses
`s_%sPRESCALED_TO%dBY%d_006be0d0` when rescaling. Both are indexing helpers in
the global material table `DAT_00765b00`.

### 2.5 Texture loading

**FUN_00592720 — open `EngineTextures.dir` and `EngineTextures.dat`. HIGH.**
Size 366 B; caller `00574310`.

```c
FUN_00634db0(auStack_104,s__s_EngineTextures_dir_006be4d4,param_1);   /* :68 */
DAT_007920d8 = FUN_006383f0(auStack_104);
FUN_00634db0(auStack_104,s__s_EngineTextures_dat_006be4bc,param_1);   /* :70 */
DAT_007920dc = FUN_006383f0(auStack_104);
```

This is the container-level counterpart of `EngineTexturesFile(Path)`
(`.../textures/enginetextures/EngineTexturesFile.java:64-125`). The bit-plane /
IDCT texture codec itself (`Dk2TextureDecoder.java:148-304`, `:321-426`) has
**no matching function found** in this export — see §5.

**FUN_0055c020 — asset path table registration. MEDIUM.** Size 913 B. Builds
the config strings `"%sdata\\Meshes.Wad"` (`006bcb34`),
`"%sdata\\EngineTextures.wad"` (`006bcafc`), `"%sdata\\sound\\SFX\\"`
(`006bcb5c`) and passes each to `FUN_0055be80`. It is path/config plumbing,
not a reader.

### 2.6 TGQ / TQI movie decode

Anchors from `docs/TGQ_CODEC_ANALYSIS.md:§2.2` were re-located in the current
export:

| Address | Role | Evidence (file:line) | Confidence |
|---|---|---|---|
| `0053c020` (67 B) | intro movie setup | `FUN_00634db0(local_104,s__s_BullfrogIntro_tgq_006b147c,…)` (`decomp/0053c020.c:14`) | HIGH |
| `0053bfd0` (73 B), `0053b980` | cutscene/intro/outro movie setup | `s__s__s_tgq_006b13dc`, `s__s_BullfrogOutro_tgq_006b13e8` (`0053bfd0.c:14`, `0053b980.c`) | HIGH |
| `0053bcd0` (761 B) | movie player/loader setup | called by all three above; builds IFF/player objects then `FUN_00559690(&LAB_0053C2E0,param_1)` | MEDIUM |
| `005b2450` (658 B) | VLC mask setup | `void FUN_005b2450(uint,uint,uint,uint)` (`:4`); popcount loops `if ((param_4 & 1 << …) != 0) …` (`:26-38`); calls `FUN_005b21e0()` (`:116`) | HIGH |
| `005b21e0` (621 B) | VLC table build | fills the four 1024-entry tables at `0x00798F90/0x00799F90/0x0079AF90/0x0079BF90` | HIGH |
| `005b1d7e` (403 B) | IDCT table setup | `DAT_007ad000 = param_2 << 2;` (`:5`) writes into `idct_dat` | HIGH |
| `005b0890` (619 B) | codec initialisation | six `FUN_005b1d7e(&DAT_00797d90,0x10)`-style calls (`:33,46,59,…`) | HIGH |
| `005b26f0` (767 B) | TQI frame entry | `DAT_007af0d0 = (*(byte *)(param_1+4) >> 7); DAT_007af0cc = ~…&1; DAT_007af0d4 = (… & 0x10)>>4; FUN_00659580(param_1+8);` (`:28-31`) | HIGH |
| `00659580` (63 B) | bitstream init | `DAT_007af058 = param_1;` (`:5`) | HIGH |
| `00659b90` (4877 B), `0065e280` (6214 B) | block decode + IDCT, path A | called from `005b26f0.c:53`, `:60` | MEDIUM |
| `0065aeb0` (4690 B), `0065ca50` (6182 B) | block decode + IDCT, path B | called from `005b26f0.c:119`, `:126` | MEDIUM |
| `0062b760` (2444 B) | MMX IDCT kernel | `pmaddwd(DAT_007b2200,…)` etc., outputs `short` at `param_1[0x100]`, `param_2[0xf0]`; all constants in `lbmpeg_d` | HIGH for "MMX IDCT"; relation to the TQI path is MEDIUM |

Frame-header size: `FUN_005b26f0` reads only `param_1[4]` before calling
`FUN_00659580(param_1 + 8)`; bytes 0..7 are the header and byte 8 starts the
bitstream. This agrees with `TgqFrame.java:158-179` and with the 8-byte
`TQI_HEADER_SIZE` in `port/tools/tgq_decode.py:39-41`.

`libpng` is a separate, statically linked concern: `FUN_005d15a0` (301 B)
checks `s_Incompatible_libpng_version_in_a_006c42ac` and installs error/longjmp
callbacks (`decomp/005d15a0.c:23-33`). It is not a game codec.

### 2.7 Sound / QMIXER imports

The import table exposes 28 `QSWaveMix*` entries
(`symbols.json`, IAT slots `0x0066c268..0x0066c2d0`). Twelve decompiled
functions call them by name; all are thin wrappers around a channel/session
object (slot `+0xc` or `param_1[3]`):

| Address | Size | Called import | Evidence |
|---|---:|---|---|
| `0061a780` | 45 | `QSWaveMixGetDirectSoundBuffer` | `decomp/0061a780.c:9` |
| `0061a7b0` | 40 | `QSWaveMixGetDirectSound` | `decomp/0061a7b0.c:9` |
| `0061a7e0` | ~40 | `QSWaveMixCloseSession` | `decomp/0061a7e0.c:6` |
| `0061a850` | 91 | `QSWaveMixSetPanRate`, `QSWaveMixEnableChannel` | `decomp/0061a850.c:17-19` |
| `0061aa30` | ~40 | `QSWaveMixStopChannel` | `decomp/0061aa30.c:12` |
| `0061aa70` | ~40 | `QSWaveMixPauseChannel` | `decomp/0061aa70.c:12` |
| `0061aab0` | ~40 | `QSWaveMixRestartChannel` | `decomp/0061aab0.c:12` |
| `0061ad10` | 126 | `QSWaveMixSetFrequency` | `decomp/0061ad10.c:15` |
| `0061aec0` | ~50 | `QSWaveMixSetVolume` | `decomp/0061aec0.c:12` |
| `0061b170` | ~40 | `QSWaveMixSetPanRate` | `decomp/0061b170.c:12` |
| `0061b1b0` | ~40 | `QSWaveMixSetSpeedOfSound` | `decomp/0061b1b0.c:12` |
| `0061b1f0` | ~40 | `QSWaveMixSetSpeakerPlacement` | `decomp/0061b1f0.c:12` |

**FUN_0060e550** (302 B) is the SDT path selector: it passes
`s_W_sdt_006c6fe0` / `s_D_sdt_006c6fe8` through a vtable call at `+0x20` and
then calls `FUN_005b3bf0` (`decomp/0060e550.c:24-35`). That is the resource-side
counterpart of `Formats/Sound/sdt_struct.bt`. There is **no** function in the
export that decodes EA XA ADPCM (`EAAudioFrame.java:52-109`); see §5.

### 2.8 Non-format anchors (sanity checks)

| Address | Role | Evidence | Confidence |
|---|---|---|---|
| `00448d80` | grid cellular automaton step | `if ((param_1[7] != 0) && (DVar5 = timeGetTime(), 0x31 < DVar5 - DAT_006da460))` and neighbour sums at `decomp/00448d80.c:19-55` | HIGH |
| `00575700` | FPS counter | `docs/EXE_ANALYSIS.md:§5`; value derived from frame delta | MEDIUM |
| `005b5e00` | timer setup | `docs/EXE_ANALYSIS.md:§5` | MEDIUM |
| `00555b10` | rdtsc clock calibration | `uVar1 = rdtsc(); … return (iVar4 - local_8) / (DVar3 - DVar2);` (`decomp/00555b10.c:19-38`); also `timeGetTime()` at `:13` | HIGH |
| `005d15a0` | libpng init | `s_Incompatible_libpng_version_in_a_006c42ac` (`decomp/005d15a0.c:23`) | HIGH |

These are listed to show the search covered the supplied anchors; they have no
counterpart in the format implementations.

## 3. Cross-implementation correspondence table

"Algorithmic" means a quoted constant/branch sequence is reproduced on both
sides. "Conceptual" means the same subsystem and data model but no shared
byte-level sequence. "None" means no counterpart found (reason in §5).

| Original function (address) | OpenKeeper / dk2-tools counterpart | Level |
|---|---|---|
| `FUN_00603710` | `WadFile.decompressFileData` (`WadFile.java:292-385`) + `wad_extract.1sc:74-178` | algorithmic (all 4 tokens) |
| `FUN_005d8560` | `WadFile(Path)` (`WadFile.java:64-139`), `WadHeader` (`wad_struct.bt:15-31`) | algorithmic (DWFB, 88 B, version < 3) |
| `FUN_005fe5d0` | `WadFile` entry/name-pool loop (`WadFile.java:93-133`), `WadEntry` (`wad_struct.bt:33-54`) | algorithmic (40-byte stride) |
| `FUN_005fe9b0`, `005d8b10`, `005d8850` | `WadFile.getFileData` / archive plumbing (`WadFile.java:234-283`) | conceptual |
| `FUN_0059a500` | `ConvertModels` consuming WAD bytes (`ConvertModels.java:119`) | conceptual (vtable boundary) |
| `FUN_0057d270` | `KmfFile.parseKmfFile` (`KmfFile.java:123-157`) | algorithmic (KMSH→HEAD→type selector) |
| `FUN_0057a800`, `0057a1e0` | `IResourceChunkReader.readChunk` usage (`KmfFile.java:125,134,136`) | conceptual |
| `FUN_0057d5b0` | `KmfFile.parseMesh` (`KmfFile.java:224-259`) | conceptual |
| `FUN_0057dad0` | `KmfFile.parseAnim` (`KmfFile.java:386-478`) | conceptual |
| `FUN_0057e110` | `KmfFile.parseGrop` (`KmfFile.java:588-612`) | algorithmic (counted named elements) |
| `FUN_0057ef30` | `KmfModelLoader.load` / `createGroup` (`KmfModelLoader.java:144-192`) | conceptual |
| `FUN_0057c420`, `0057c780`, `0057c970` | `Material` + `KmfModelLoader.getMaterials` (`Material.java:30-60`, `KmfModelLoader.java:651-767`) | conceptual |
| `FUN_00592720` | `EngineTexturesFile(Path)` (`EngineTexturesFile.java:64-125`) | algorithmic (both `.dir` and `.dat`) |
| `FUN_0055c020` | `ConvertTextures` path setup (`ConvertTextures.java:98-99,190-217`) | conceptual |
| texture bit-plane/IDCT decoder | `Dk2TextureDecoder` (`Dk2TextureDecoder.java:148-304,321-426`) | none in this export (§5) |
| `FUN_0053c020`, `0053bfd0`, `0053b980`, `0053bcd0` | `TgqFile.readFrame` / `TgqPlayer.play` (`TgqFile.java:119-181`, `TgqPlayer.java:113-137`) | conceptual |
| `FUN_005b2450`, `005b21e0` | `TgqFrame` VLC tables (`TgqFrame.java:85-139,311-329`) | conceptual (same role, different constants) |
| `FUN_005b1d7e`, `005b0890`, `0062b760` | `TgqFrame.idctTransform` / `eaIdctPut` (`TgqFrame.java:336-414`) | conceptual |
| `FUN_005b26f0`, `00659580` | `TgqFrame(ByteBuffer,int)` (`TgqFrame.java:158-179`) | algorithmic (8-byte header, bitstream at +8) |
| `00659b90`, `0065aeb0`, `0065ca50`, `0065e280` | `TgqFrame.mpeg1DecodeBlock` / `idctPut` (`TgqFrame.java:217-414`) | conceptual |
| `0060e550` | `sdt_struct.bt:19-47` / `sdt_extract.1sc:36-106` | conceptual |
| `0061a780..0061b1f0` (12 wrappers) | `TgqPlayer.AudioPlayer` (`TgqPlayer.java:203-305`) | conceptual (QMixer channel API) |
| EA XA ADPCM decoder | `EAAudioFrame.decodeFrame` (`EAAudioFrame.java:52-109`) | none in this export (§5) |
| `FUN_005d15a0` | libpng (vendored C, not in OpenKeeper) | none (third-party library) |

## 4. Where no matching function was found — and why

1. **A second KMSH "high-byte" codec.** `docs/KMSH_CODEC.md` probed bytes
   `>= 0xe0` and the `10 fb` envelope as RNC. No function in the export
   compares a byte to `0xe0` (`grep -rnE "0xe0\s*<=|<=?\s*0xe0|0xe0\s*<|>\s*0xe0" re/ghidra/decomp/`
   → 0 hits). The correct interpretation is now known: those bytes are
   **tokens** of the LZ codec at `FUN_00603710`, which compares against
   `0x80/0x40/0x20`, not `0xe0`. The prior negative result stands as a
   statement about the failed RNC hypothesis, not about the existence of a
   decompressor.
2. **WAD read + decompression behind `+0x118`.** `FUN_0059a500.c:39` calls
   `(** (code **)(*DAT_00795700 + 0x118))(local_104)`. The RTTI names in the
   binary (`.?AVTbWadFileStorage@FileSystem@@`, `.?AVCFileManager@@`) place this
   in the file-storage vtable; the export contains the *caller* but not the
   overridden method body for this slot. `FUN_00603710` is the standalone codec
   reached from that layer (no direct call site in the text export).
3. **Texture bit-plane/IDCT decoder.** String references locate only the
   container open (`FUN_00592720`). No decompiled function references the
   texture tables or matches the 8×8 prefix-tree/decompress structure of
   `Dk2TextureDecoder.java:148-426` / `EngineTextureDecoder.java:65-280`.
   Reason: the table constants and 8×8 loops are either inlined in a function
   not distinguished by the current export or accessed as raw data without a
   named `DAT_` label; no honest address can be assigned.
4. **EA XA ADPCM audio decode.** 28 `QSWaveMix*` imports exist, but IAT slots
   for `QSWaveMixInitEx` (`0x0066c274`), `QSWaveMixOpenWaveEx`
   (`0x0066c288`), `QSWaveMixPlayEx` (`0x0066c28c`) and
   `QSWaveMixFreeWave` (`0x0066c290`) have **zero call sites** in
   `re/ghidra/asm/` (counted over `[0x0066c2…]` operands). The 12 decompiled
   wrappers are channel/session control only. ADPCM itself is presumably inside
   `QMIXER.DLL`, which is not part of this analysis target.
5. **Explicit `version == 17` gate.** Covered in §2.1; neither side has one.
6. **`.kmf` filename construction.** `s_%s.kmf_00697870` and
   `s_%s% s.kmf_00697878` exist in `symbols.json` but have **zero** references
   in `re/ghidra/decomp/`; no caller can be named.
7. **Legitimate negative candidates.** `FUN_005ac880` was flagged by an
   `006c10` text search because of `s_Uknown_Error_Code_006c10a8`; reading the
   body shows an error-code→string loop, **not** a texture routine.
   `FUN_004ad7a0` matched a 3-byte shift pattern but its body is creature/AI
   logic (`FUN_004b78d0`, `FUN_004b5560`, `FUN_004ba7d0`), not a codec. They are
   listed so the search record is auditable.

## 5. Reproduction — exact commands and results

Run from `port/`:

```
$ cat re/ghidra/export_summary.txt
functions=8260
decompiled=8260
failed=0
heavy=3

$ python3 - <<'PY'
import os,re
D='re/ghidra/decomp'
texts={f[:-2]:open(D+'/'+f,errors='replace').read() for f in os.listdir(D)}
for name,pat in [('DWFB','0x42465744'),('KMSH tag','DAT_006be138'),
                 ('QMIXER','QSWaveMix'),('0xe0 compare',r'0xe0\s*[<>]'),
                 ('LZ masks',r'bVar1 & 0x80')]:
    print(name, len([a for a,t in texts.items() if re.search(pat,t)]))
PY
DWFB 1
KMSH tag 1
QMIXER 12
0xe0 compare 0
LZ masks 1

$ grep -rl '0x42465744' re/ghidra/decomp/
re/ghidra/decomp/005d8560.c
$ grep -rl '006be138' re/ghidra/decomp/
re/ghidra/decomp/0057d270.c
$ grep -rl 'bVar1 & 0x80' re/ghidra/decomp/
re/ghidra/decomp/00603710.c
$ for s in 0066c274 0066c288 0066c28c 0066c290; do grep -rl "0x$s" re/ghidra/asm/ | wc -l; done
0
0
0
0

$ python3 -m pytest tests/test_decompiled_mapping.py -q
... (see the test file; all assertions are structural)
```

The PE string-table check in §1 and the KMF tag-table offsets were derived with:

```
$ python3 -c "d=open('DKII.EXE','rb').read();print(repr(d[0x2bc730:0x2bc760]))"
```

## 6. Limitations

* Every `FUN_` address refers to the Ghidra export in this checkout. The export
  has no parameter names, no local types and no RTTI-derived class membership;
  `__thiscall`/`__fastcall` in the snippets is Ghidra's convention inference.
* The `callers` numbers in `functions.json` and the number of textual call sites
  in `decomp/` disagree for a few functions (`00603710`, `005b2450`,
  `005b26f0`, all QMixer wrappers). Indirect calls and vtable slots are not
  rendered as callee names in the decompiled text; both numbers are reported
  rather than reconciled by guessing.
* The comparison table expresses correspondence of *implementations*, not of
  runtime behaviour. None of the EXE functions was executed.
* `docs/TGQ_CODEC.md` and `docs/KMSH_CODEC.md` predate this export analysis;
  where they conflict with §2.3/§2.6, the quoted decompiled lines here are the
  authority.
