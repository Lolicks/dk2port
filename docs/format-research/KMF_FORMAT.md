# KMF format — Dungeon Keeper 2 meshes (v17)

**Status:** format recovered and verified against all 2015 decompressed meshes.
All numbers below come from two primary references, both read in full:

| Ref | File | Lines | Role |
|---|---|---|---|
| BT | `port/research/dk2-tools/Formats/Model/kmf_struct.bt` | 1–331 | 010 Editor v6.0.3 template, author **ufdada**, "Original Code by **Hoenir**" (BT:5–9) |
| JAVA | `port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/kmf/KmfFile.java` | 1–650 | GPLv3 parser, Toni Helenius (JAVA:31–38) |
| VEC | `port/research/dk2-tools/Formats/util_struc.bt` | 34–38 | `Vec3f` = three **float** fields (vs. `Vector3f` = int/4096 fixed point) |
| RDR | `port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/ResourceChunkReader.java` | 236–253 | NUL-terminated string reads |

Reader implementation: `port/tools/kmf_tool.py`. Catalog:
`port/docs/format-research/kmf_catalog.json`.

## 1. Global properties

| Property | Value | Evidence |
|---|---|---|
| Byte order | **Little endian** for every integer and float | JAVA:33 ("The file is LITTLE ENDIAN I might say"), `BufferedResourceReader.java:67`, `ByteArrayResourceReader.java:36` |
| Alignment | **None.** All fields are packed; strings are NUL-terminated and are NOT padded to 4 bytes | JAVA:202–213 followed by unaligned `u32` flags; verified in `3dfe_beams.kmf` at 0x59 |
| Chunk signature | 4 ASCII bytes, one of `KMSH HEAD MATL MAT2 MESH ANIM GROP ELEM CTRL SPRS SPHD POLY VERT GEOM VGEO ITAB` | BT:37–52, JAVA:69–85 |
| Chunk `size` | **Includes the 4-byte signature and the 4-byte size field** (i.e. `end = offset + size`) | explicit comment BT:226 (`MESH`), BT:290 (`GROP`); empirically confirmed for **all 46 660 chunks** in the 2015-file corpus: every `offset + size` landed exactly on another chunk start or EOF |
| Strings | NUL-terminated bytes, cp1252/ASCII; length is NOT stored | BT `string` type (BT:66, 92, 303), JAVA `readVaryingLengthStrings()` via RDR:236–253 |
| `u32` / `uint` | unsigned 32-bit little endian; `short` in BT is signed 16-bit, Java uses both signed and unsigned reads | BT:55–56, JAVA:345–346 |
| Top magic / version | `KMSH`, version **17** in 2015/2015 files | BT:109–113, JAVA:69/129; catalog `version_distribution={"17": 2015}` |
| Outer size | `KMSH.size` equals the physical file length | verified on 2015/2015 files |

### Chunk tree

```
KMSH (file)
├── HEAD                     format = 1/2/3
├── MATL                     (absent when format == GROP)
│   └── MAT2 × numMaterials
└── MESH | ANIM | GROP
    ├── HEAD
    ├── CTRL
    ├── MESH: SPRS
    │   ├── SPHD × sprsCount          (all headers first)
    │   └── SPRS × sprsCount          (all data groups second)
    │       ├── [MESH] triangles[lod] + MeshVertEx[]
    │       └── [ANIM] POLY, VERT
    │   ├── ITAB                      (ANIM only)
    │   ├── GEOM                      (Mesh Vec3f[] / packed AnimGeom[])
    │   └── VGEO                      (ANIM only)
    └── GROP: HEAD, ELEM × elementCount
```

The two header blocks are *not* interleaved: all `SPHD` chunks come first, then
all per-group `SPRS` chunks (BT:156 vs. 158–192, 248 vs. 250–273;
JAVA:295–312 vs. 315–354, 515–532 vs. 535–577).

## 2. Top-level structures

Offsets in the tables are **relative to the start of the structure**, including
its signature at offset 0. `n` denotes a variable-length string's byte count
including its terminating NUL.

### KMSH (BT:109–113, JAVA:123–129)

| Field | Offset | Size | Type | Purpose |
|---|---|---|---|---|
| signature | 0 | 4 | char[4] | `KMSH` |
| size | 4 | 4 | u32 | total file size, signature included |
| version | 8 | 4 | u32 | 17 |

### HEAD — top level (BT:114–120, JAVA:131–133, 165–169)

| Field | Offset | Size | Type | Purpose |
|---|---|---|---|---|
| signature | 0 | 4 | char[4] | `HEAD` |
| size | 4 | 4 | u32 | 16 |
| format | 8 | 4 | u32 enum | `1=MESH, 2=ANIM, 3=GROP` (BT:21, JAVA:40–62) |
| unknown | 12 | 4 | u32 | always 1; BT:119, JAVA:168 (`unknown` discarded); 2015/2015 observed = 1 |

### MATL (BT:101–107, JAVA:177–187)

| Field | Offset | Size | Type | Purpose |
|---|---|---|---|---|
| signature | 0 | 4 | char[4] | `MATL` |
| size | 4 | 4 | u32 | 12 + sum(MAT2.size) |
| numMaterials | 8 | 4 | u32 | number of following MAT2 chunks |
| materials[] | 12 | variable | MAT2[numMaterials] | back-to-back |

### MAT2 (BT:88–99, JAVA:195–216)

| Field | Offset | Size | Type | Purpose |
|---|---|---|---|---|
| signature | 0 | 4 | char[4] | `MAT2` |
| size | 4 | 4 | u32 | chunk size including signature |
| name | 8 | n | cstring | material name, e.g. `Material #4#add#Falloff` |
| numTextures | 8+n | 4 | u32 | number of texture names |
| texturenames[] | 12+n | variable | cstring[numTextures] | NUL-terminated texture names |
| flags | after names | 4 | u32 bitfield | `MaterialFlags` (BT:23–35; JAVA:208 reads a 4-byte flag) |
| brightness | +4 | 4 | f32 | brightness (BT:96); Java calls it `brightness` (JAVA:209) |
| gamma / shininess | +8 | 4 | f32 | BT calls it `gamma`, Java `shininess` (JAVA:210) |
| envmap | +12 | n | cstring | environment-map texture, e.g. `DefaultEnvMap` |

`MaterialFlags` bit values (BT:23–35 agree numerically with
`Material.MaterialFlag`, `Material.java:29–41`; names disagree, see §5):

| Bit | Value | BT name | Java name |
|---|---|---|---|
| 0 | 0x0001 | HAS_ALPHA | HAS_ALPHA |
| 1 | 0x0002 | DOUBLE_SIDED | DOUBLE_SIDED |
| 2 | 0x0004 | ALPHA_ADDITIVE | ALPHA_ADDITIVE |
| 3 | 0x0008 | TRANSLUCENT | UNKNOWN8 |
| 4 | 0x0010 | UNKNOWN5 | UNKNOWN10 |
| 5 | 0x0020 | UNKNOWN6 | UNKNOWN20 |
| 6 | 0x0040 | HAS_SPECULAR | IS_SHININESS_SET |
| 7 | 0x0080 | HAS_EMISSIVE | IS_BRIGHTNESS_SET |
| 8 | 0x0100 | invisible | INVISIBLE |
| 9–31 | — | 2 unused bytes in BT; whole u32 read by Java | — |

## 3. MESH (format 1)

### MESH chunk and MESH/HEAD (BT:222–238, JAVA:224–241)

| Field | Offset | Size | Type | Purpose |
|---|---|---|---|---|
| MESH.signature | 0 | 4 | char[4] | `MESH` |
| MESH.size | 4 | 4 | u32 | whole MESH chunk |
| HEAD.signature | 8 | 4 | char[4] | `HEAD` |
| HEAD.size | 12 | 4 | u32 | 8 + 28 + length(meshname) + 1 |
| HEAD.meshname | 16 | n | cstring | mesh name |
| HEAD.sprsCount | 16+n | 4 | u32 | number of SPHD and group SPRS chunks |
| HEAD.vertsCount | 20+n | 4 | u32 | number of Vec3f in GEOM (Java: `geomCount`) |
| HEAD.pos | 24+n | 12 | Vec3f | origin (3 × f32) |
| HEAD.scale | 36+n | 4 | f32 | scale |
| HEAD.lodCount | 40+n | 4 | u32 | number of LOD entries per sprite |

### CTRL (BT:69–77, JAVA:267–282 for MESH, 486–502 for ANIM)

| Field | Offset | Size | Type | Purpose |
|---|---|---|---|---|
| signature | 0 | 4 | char[4] | `CTRL` |
| size | 4 | 4 | u32 | 12 + 8·numCTRL (MESH) / 12 + 8·numCTRL (ANIM) |
| numCTRL | 8 | 4 | u32 | control count; 0 in most files |
| entries[] | 12 | 8 × numCTRL | 2×u32 (MESH) or u16,u16,u32 (ANIM) | unknown control data |

BT declares `uint uk[numCTRL*2]` for both forms; Java reads the ANIM form as
`u16, u16, u32` (JAVA:495–497). Both consume exactly 8 bytes per entry.

### SPRS container and SPHD (BT:242–248, JAVA:290–312)

| Field | Offset | Size | Type | Purpose |
|---|---|---|---|---|
| SPRS.signature | 0 | 4 | char[4] | `SPRS` (container, not a group) |
| SPRS.size | 4 | 4 | u32 | all SPHDs + all group SPRS chunks + 8 |
| SPHD.signature | 8 | 4 | char[4] | `SPHD`, repeated sprsCount times |
| SPHD.size | +4 | 4 | u32 | 16 + 4·lodCount |
| SPHD.lodEntries[lodCount] | +8 | 4·lodCount | u32[] | triangle count for each LOD; 0 = empty |
| SPHD.numVertsEx | 8+4·lodCount | 4 | u32 | number of MeshVertEx/AnimVertEx in the group |
| SPHD.mmFactor | 12+4·lodCount | 4 | f32 | distance/mip-map factor |

### MESH group SPRS (BT:251–272, JAVA:315–354)

| Field | Offset | Size | Type | Purpose |
|---|---|---|---|---|
| signature | 0 | 4 | char[4] | `SPRS` |
| size | 4 | 4 | u32 | 12 + 3·Σ lodEntries + 18·numVertsEx |
| index | 8 | 4 | u32 | material index into MATL |
| triangles[lod][k] | 12 | 3·Σ lodEntries | Triangle[] | per-LOD index triples, LOD 0 first; Triangle = 3 × u8 (BT:59–63) |
| verts[] | after triangles | 18·numVertsEx | MeshVertEx[] | extended vertices |

### MeshVertEx — 18 bytes (BT:266–270, JAVA:342–352)

| Field | Offset | Size | Type | Purpose |
|---|---|---|---|---|
| index | 0 | 2 | i16 (BT) / u16 (JAVA:345) | index into MESH/GEOM |
| uv.u | 2 | 2 | i16 (BT:55) / u16 (JAVA:346) | texture U |
| uv.v | 4 | 2 | i16 (BT:56) / u16 (JAVA:346) | texture V |
| normal.x/y/z | 6, 10, 14 | 12 | 3 × f32 | vertex normal |

### MESH/GEOM (BT:277–282, JAVA:365–378)

| Field | Offset | Size | Type | Purpose |
|---|---|---|---|---|
| signature | 0 | 4 | char[4] | `GEOM` |
| size | 4 | 4 | u32 | 8 + 12·vertsCount |
| vertices[] | 8 | 12·vertsCount | Vec3f[] | x,y,z f32; `Vec3f` is float, **not** the fixed-point `Vector3f` (VEC:34–38) |

## 4. ANIM (format 2)

### ANIM chunk and ANIM/HEAD (BT:127–147, JAVA:386–407)

| Field | Offset | Size | Type | Purpose |
|---|---|---|---|---|
| ANIM.signature | 0 | 4 | char[4] | `ANIM` |
| ANIM.size | 4 | 4 | u32 | whole ANIM chunk |
| HEAD.signature | 8 | 4 | char[4] | `HEAD` |
| HEAD.size | 12 | 4 | u32 | 8 + 44 + len(meshname) + 1 |
| HEAD.meshname | 16 | n | cstring | animation name |
| HEAD.sprsCount | 16+n | 4 | u32 | sprite groups |
| HEAD.frameCount | 20+n | 4 | u32 | frames |
| HEAD.indexCount | 24+n | 4 | u32 | animated indices |
| HEAD.geomCount | 28+n | 4 | u32 | packed GEOM samples |
| HEAD.frameFactorFunction | 32+n | 4 | u32 | 0=CLAMP, 1=WRAP (JAVA `Anim.java:29–47`; observed 936/209) |
| HEAD.pos | 36+n | 12 | Vec3f | origin |
| HEAD.cubeScale | 48+n | 4 | f32 | cube scale |
| HEAD.scale | 52+n | 4 | f32 | multiplier for decoded GEOM coordinates (JAVA:452) |
| HEAD.lodCount | 56+n | 4 | u32 | LOD levels |

### ANIM SPRS / SPHD / group SPRS (BT:150–191, JAVA:510–577)

The outer `SPRS` container, `SPHD` headers and group `SPRS.index` are byte-for-byte
the same layout as MESH (§3). Difference: instead of inline triangles + vertices,
each group contains a `POLY` chunk and a `VERT` chunk (BT:165–190, JAVA:546–576).

#### POLY (BT:165–178, JAVA:547–559)

| Field | Offset | Size | Type | Purpose |
|---|---|---|---|---|
| signature | 0 | 4 | char[4] | `POLY` |
| size | 4 | 4 | u32 | 8 + 3·Σ lodEntries |
| triangles[lod] | 8 | 3·lodEntries[lod] | Triangle[] | 3 × u8 per triangle, in LOD order |

#### VERT / AnimVertEx — 18 bytes (BT:180–190, JAVA:566–575)

| Field | Offset | Size | Type | Purpose |
|---|---|---|---|---|
| VERT.signature | 0 | 4 | char[4] | `VERT` |
| VERT.size | 4 | 4 | u32 | 8 + 18·numVertsEx |
| uv.u / uv.v | 8, 10 | 4 | 2 × u16 | texture coordinates |
| normal | 12 | 12 | 3 × f32 | normal |
| itabIndex | 24 | 2 | u16 | index into ITAB (JAVA:573); BT:188 calls it "index of the vertex in GEOM" |

Per entry (AnimVertEx): `uv.u`@0 u16, `uv.v`@2 u16, `normal`@4/8/12 f32,
`index`@16 u16 → **18 bytes**, same size as MeshVertEx but different field order.

### ITAB (BT:194–200, JAVA:421–433)

| Field | Offset | Size | Type | Purpose |
|---|---|---|---|---|
| signature | 0 | 4 | char[4] | `ITAB` |
| size | 4 | 4 | u32 | 8 + 4·chunks·indexCount |
| entries | 8 | 4·chunks·indexCount | u32[] | frame-block → GEOM index table |

```
chunks = floor((frameCount - 1) / 128.0 + 1)          # BT:198, JAVA:426
       = (frameCount - 1) // 128 + 1                  # for frameCount >= 1
```

### ANIM GEOM — packed 10-bit coordinates (BT:202–210, JAVA:435–458)

| Field | Offset | Size | Type | Purpose |
|---|---|---|---|---|
| signature | 0 | 4 | char[4] | `GEOM` |
| size | 4 | 4 | u32 | 8 + 5·geomCount |
| entries[] | 8 | 5 each | u32 + u8 | packed coordinates + frame base; **no padding** |

Each `AnimGeom` entry is 5 bytes (BT:206–209, JAVA:443–456):

| Field | Offset in entry | Size | Type | Purpose |
|---|---|---|---|---|
| coordinates | 0 | 4 | u32 | three 10-bit fields, high bits unused |
| frameBase | 4 | 1 | u8 | base frame for interpolation |

**Decode formula** (BT `getAnimGeom()`, BT:316–324; JAVA:446–452):

```
x10 = (coordinates >> 20) & 0x3ff
y10 = (coordinates >> 10) & 0x3ff
z10 =  coordinates        & 0x3ff
x   = (x10 - 0x200) / 511.0
y   = (y10 - 0x200) / 511.0
z   = (z10 - 0x200) / 511.0
# Java (equivalent): ((coordinates >> shift) % 1024 - 512) / 511.0
# Java additionally scales the vector by ANIM/HEAD.scale (JAVA:452).
```

Java appends one extra copy of the last `AnimGeom` for interpolation
(JAVA:439, 460–461); **that entry is not in the file** (geomCount counts only
the stored entries).

### VGEO (BT:212–217, JAVA:465–475)

| Field | Offset | Size | Type | Purpose |
|---|---|---|---|---|
| signature | 0 | 4 | char[4] | `VGEO` |
| size | 4 | 4 | u32 | 8 + indexCount·frameCount |
| offsets | 8 | indexCount·frameCount | u8[] | per-index per-frame GEOM offsets |

## 5. GROP (format 3)

No `MATL` chunk at all (BT:122–124, JAVA:138).

### GROP / HEAD (BT:292–297, JAVA:591–594)

| Field | Offset | Size | Type | Purpose |
|---|---|---|---|---|
| GROP.signature | 0 | 4 | char[4] | `GROP` |
| GROP.size | 4 | 4 | u32 | whole GROP chunk |
| HEAD.signature | 8 | 4 | char[4] | `HEAD` |
| HEAD.size | 12 | 4 | u32 | 12 |
| HEAD.elementCount | 16 | 4 | u32 | number of ELEM chunks |

### ELEM (BT:299–305, JAVA:600–608)

| Field | Offset | Size | Type | Purpose |
|---|---|---|---|---|
| signature | 0 | 4 | char[4] | `ELEM` |
| size | 4 | 4 | u32 | 8 + len(name) + 1 + 12 |
| name | 8 | n | cstring | element name |
| pos | 8+n | 12 | Vec3f | element position (3 × f32) |

## 6. Confirmed / assumed / unknown

| Class | Items | Basis |
|---|---|---|
| **Confirmed** | Magic `KMSH`; version 17; type enum 1/2/3; all chunk signatures and nesting; little endian; NUL strings; packed layout; `size` includes signature; SPHD-before-group ordering; MESH/ANIM/GROP field order; 18-byte MeshVertEx/AnimVertEx; 5-byte packed AnimGeom; ITAB chunk formula; 10-bit decode; `HEAD.unknown == 1`; material flag bit values | BT + JAVA agreement **and** all 2015 files parsed with 0 trailing bytes, 0 invalid chunk sizes, 46 660/46 660 chunk ends on a chunk boundary |
| **Assumed** | Texture-name semantics, `mmFactor` meaning (probably mip-map/LOD distance), `cubeScale`, material brightness/gamma influence, control-entry purpose, `frameBase` interpolation role, meaning of material flags beyond bit positions | named but not explained in either reference; names taken as given |
| **Unknown** | `HEAD.unknown` purpose (value always 1); ITAB/VGEO index semantics; whether multiple MESH/ANIM chunks can occur (JAVA:143 comments "there are n amount of these" but parses only one; BT also declares one — no corpus example); `frameFactorFunction` values beyond 0/1; high 2 bits of packed GEOM coordinates; flags 9–31; ANIM VERT index ("GEOM index" in BT:188 vs "itabIndex" in JAVA:573) | no evidence in references or corpus |

## 7. Where BT and JAVA disagree

| # | Topic | BT | JAVA | Resolution |
|---|---|---|---|---|
| 1 | UV signedness | `short uv1, uv2` (BT:54–57), i.e. signed | `readUnsignedShort()` (JAVA:346, 571) | same 4 bytes; `kmf_tool.py` records both signed and unsigned values |
| 2 | MeshVertEx.index | `short index` (BT:267), signed | `readUnsignedShort()` (JAVA:345) | same 2 bytes |
| 3 | Flags 0x0008/0x0040/0x0080 | TRANSLUCENT / HAS_SPECULAR / HAS_EMISSIVE (BT:26, 30–31) | UNKNOWN8 / IS_SHININESS_SET / IS_BRIGHTNESS_SET (`Material.java:32, 38–39`) | bit values agree; names differ; semantics unknown |
| 4 | MAT2 field name at +8 after flags | `gamma` (BT:97) | `shininess` (JAVA:210) | same f32 |
| 5 | ANIM CTRL entry layout | `uint uk[numCTRL*2]` (BT:75) | `u16, u16, u32` (JAVA:495–497) | both 8 bytes; no file in the corpus has controls to disambiguate |
| 6 | ANIM VERT index meaning | "index of the vertex in GEOM" (BT:188) | `itabIndex` (JAVA:573) | unresolved |
| 7 | Extra interpolation GEOM | not present | appended synthetically (JAVA:439, 460–461) | file contains exactly `geomCount` entries |
| 8 | Flags read width | bitfield `ubyte`×9 + 2 `ubyte` = 4 bytes (BT:23–35) | `readIntegerAsFlag` = u32 (JAVA:208, `ResourceChunkReader.java:110`) | equivalent width |
| 9 | Chunk size validation | `size` documented and checked by 010 template | skipped without validation (`JAVA:128, 166, 178, 196, …`) | sizes verified empirically; all consistent |
| 10 | frameFactorFunction | plain `uint` (BT:142) | enum restricted to 0/1 (`Anim.java:29–47`) | Java is stricter; corpus uses only 0/1 |

## 8. Empirical corpus results

Source: 2015 files in `port/build/KMF/`, decompressed from `../Data/Meshes.WAD`
by `port/tools/kmsh_decompress.py` (53.6 MB total).

| Metric | Value |
|---|---|
| Files parsed cleanly | **2015 / 2015** (0 failures, 0 warnings, 0 trailing bytes) |
| Version | 17 (2015/2015) |
| MESH / ANIM / GROP | **862 / 1145 / 8** |
| Total materials | 6492 |
| Texture references / distinct names | 7263 / **818** |
| Sprite groups | 6551 |
| MESH GEOM vertices | 46 462 |
| ANIM packed GEOM samples | 4 606 094 |
| ANIM extended vertices | 631 995 |
| Total vertices (all three sums) | 5 112 121 |
| Triangles across all LODs | 2 853 857 |
| Triangles at LOD 0 only | 339 469 |
| GROP ELEM entries | 86 |
| Chunk headers verified | 46 660 (all ends on a boundary) |

Ten largest files (by bytes):

| File | Bytes | Type | Vertices | Triangles (all LODs) | Materials |
|---|---:|---|---:|---:|---:|
| Jack_Special-Appear.kmf | 334672 | ANIM | 30991 | 5856 | 4 |
| Mis-Idle1.kmf | 226966 | ANIM | 35317 | 2288 | 5 |
| REA3_PickUpGem4.kmf | 216722 | ANIM | 8949 | 4248 | 7 |
| Knight-Idle2.kmf | 192853 | ANIM | 29583 | 2445 | 5 |
| Rogue-Idle2.kmf | 184617 | ANIM | 28792 | 1856 | 4 |
| Fai-Idle1.kmf | 173948 | ANIM | 26633 | 2368 | 3 |
| Warlock-Idle2.kmf | 172708 | ANIM | 26787 | 1821 | 3 |
| Vampire-IntoBat.kmf | 169393 | ANIM | 22189 | 1999 | 3 |
| BileDemon-Idle1.kmf | 157469 | ANIM | 24399 | 1786 | 3 |
| VAM-Entrance.kmf | 155595 | ANIM | 23755 | 1990 | 3 |

Ten largest by counted vertices: Mis-Idle1 (35 317), Jack_Special-Appear
(30 991), Knight-Idle2 (29 583), Rogue-Idle2 (28 792), Warlock-Idle2 (26 787),
Fai-Idle1 (26 633), BileDemon-Idle1 (24 399), VAM-Entrance (23 755),
King-Idle1 (23 525), Dwarf-Idle1 (23 160). Full per-file data is in
`kmf_catalog.json`.

## 9. Reproduction

```bash
cd port
python3 tools/kmf_tool.py info build/KMF/3dfe_beams.kmf
python3 tools/kmf_tool.py chunks build/KMF/3dfe_beams.kmf
python3 tools/kmf_tool.py validate build/KMF
python3 tools/kmf_tool.py catalog build/KMF --out docs/format-research/kmf_catalog.json
python3 -m unittest tests.test_kmf -v
```
