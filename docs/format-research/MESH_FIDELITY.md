# MESH / ANIM fidelity fixes (DK2 iOS port)

Scope: `decoder/kmf/kmf.c`, `decoder/kmf/anim.c`, `decoder/kmf/anim.h`,
`ios/DK2Port/DK2Mesh.swift`, `ios/DK2Port/EffectsPipeline.swift`,
`ios/DK2Port/Shaders.metal`, deletion of `ios/DK2Port/DK2MeshRenderer.swift`.
No engine/, CI, status, other Swift files, commits, IPA or workflows touched.
All validation below was local (`clang`/`clang++`, `python3`, corpus in
`port/build/KMF/`).

## 1. LOD0-only OBJ export (critical bug)

`parse_sprs` still walks every LOD ( needed to reach `MeshVertEx[]` ), but only
LOD0 triangles are counted and emitted. This follows OpenKeeper
`KmfModelLoader.java:209-210,237-241,288-291,462-466` — *"LODs are broken, take
L0"*.

`Dk2KmfInfo.triangle_count` and `lod0_triangles` are now both the LOD0 count,
so the acceptance identity is direct:

```
f-lines in OBJ == info.triangle_count == info.lod0_triangles
```

The historical all-LOD total is no longer exposed through this C API
(`tools/kmf_tool.py` / `kmf_catalog.json` still report it separately).

OBJ vertices are now the extended vertices (`MeshVertEx`), not the raw `GEOM`
pool: position = `GEOM[gi]`, `vt` = `u16/32768` (unsigned, as Java does),
`vn` = the three f32 normal fields, faces reference `v/vt/vn` with a global
extended-vertex base. `usemtl mat_<n>` is emitted per sprite group from the
group's material index.

## 2. Material -> texture manifest

`MATL`/`MAT2` are parsed into a per-material table (`name`, flags, up to 16
texture names). The manifest is JSON:

```json
{"format":"dk2-kmf-materials-v1","mesh":"DHeartPlug4","lod":0,
 "triangle_count":32,"usemtl_prefix":"mat_","materials":[
 {"usemtl":"mat_0","name":"...","flags":128,"textures":["D Heart Wall"]}, ...]}
```

Both MESH and ANIM OBJ exports emit `usemtl mat_<n>` per sprite group and one
`# DK2_MATERIAL_MANIFEST <json>` line per OBJ file. Two emission paths:

* embedded, parsed by Swift into `DK2MaterialManifest` (`usemtl` -> texture
  names) from the same OBJ text supplied by the existing bridge;
  `DK2Mesh.textures(forMaterial:)` exposes it to the atlas-binding layer;
* standalone sidecar file via `dk2_kmf_export_manifest(data, size, path)`
  (or `dk2_kmf_write_manifest(data, size, FILE*)`), declared in `anim.h`.

Limits: texture names are cp1252 bytes copied verbatim (JSON-escaped), 16
textures per material and 4096 materials are retained; `MAT2` declares up to
64 textures, so entries beyond 16 are recorded in the flat
`Dk2KmfInfo.textures[]` list but not in the per-material JSON. Flags above bit
8 have no decoded semantics. GROP files have no MATL and are unsupported for
OBJ/manifest export.

## 3. Single projection source

`DK2MeshRenderer.swift` (dead: referenced only in comments) was deleted.
`DK2MeshUniforms` moved into `EffectsPipeline.swift`; `viewProjection(for:tilt:)`
now exists exactly once. `Shaders.metal` no longer mentions the deleted class.

## 4. ANIM morph targets, DKAF v2, 30 fps

`anim.c` uses one common pose builder for both OBJ and binary export. For each
(vertex, frame) it stores OpenKeeper's `FrameInfo` triple:

```
geomBase  = ITAB[frame >> 7][itabIndex]
geomIndex = geomBase + VGEO[itabIndex][frame]
prevCoord = GEOM[geomIndex], nextCoord = GEOM[geomIndex + 1]
weight    = clamp(((frame & 0x7f) - GEOM.frameBase) /
                  (GEOM[+1].frameBase - GEOM.frameBase), 0, 1)
prevPose  = (frame >> 7) * 128 + frameBase
```

Binary layout (`DKAF` v2, fully specified in `anim.h`): 56-byte header
(`fps=30`, frame/vertex/index/pose counts, `HEAD.scale`, `HEAD.pos`,
`HEAD.cubeScale`, frame-factor), 40-byte vertices with **VERT UV (`u16/32768`)
and normal (3×f32) read from the 18-byte VERT record**, u16 LOD0 indices, then
40-byte pose records `prevXYZ, nextXYZ, weight, prevPose, nextPose`. Renderers
evaluate `mix(prev, next, weight)`; all 30 frames/s are distinct poses. The
OBJ transport (used by the existing bridge) carries the same positions plus
`vt`/`vn` and `# DK2_ANIM_HEAD ...` comments; the binary file is canonical.

Swift: `DK2Animation` holds the pose track, `DK2AnimationClock` freezes on
`dk2.command.pause`, `DK2FrameCache` (LRU, 96 frames) replaces the old
`limit: 8` cap — every frame remains addressable, only recently used frames
hold GPU buffers. `DK2MeshVertex` gained a float3 normal (attribute 2, offset
24, stride 48) and `lit_mesh_fragment` uses `in.normal` (derivative fallback
only for legacy DKAF v1 data).

## 5. Per-species scale / offset

`ANIM/HEAD.scale`, `pos` and `cubeScale` are exported into DKAF v2 and into
OBJ comments; Swift stores them in `DK2MeshPlacement` and
`EffectsPipeline.drawMeshes` applies `scale`, `offset.x/y` per placement and
`offset.z` through vertex buffer 4. The remaining `0.32` is a *view* tilt, not
species data, and is defined once (`DK2EffectsRenderer.cameraTilt`). Static OBJ
meshes keep the legacy `scale = 2.4`, zero offset.

## Local validation (exact numbers)

* `bash decoder/build.sh`: clean build, `C DECODER TESTS: OK (0 failures)`,
  `C TQI TESTS: OK`, `C AUDIO TESTS: OK` (no warnings).
* All **862/862** MESH files: `f`-lines == `triangle_count` == `lod0_triangles`;
  `usemtl` present 862/862; embedded JSON manifest parses and its
  `triangle_count` matches 862/862; `vt`+`vn` present 862/862; **0 failures**.
* Corpus context: 2015 files = 862 MESH / 1145 ANIM / 8 GROP; LOD0 triangles
  for MESH files = **86,962** (all-LOD total 727,596).
* `Mis-Idle1.kmf` DKAF: `fps=30`, 255 frames, 501 vertices, 813 indices,
  127,755 poses, `scale=0.705900`, `origin=(-0.0612, 0.0349, -0.3513)`,
  `cube=0.369737`, `factor=0`; file 5,131,922 B =
  `56 + 40*501 + 2*813 + 40*127755`. Vertex 0 UV `(25383,10822)/32768 =
  (0.77463, 0.33026)` and normal `(0.2253,-0.8548,-0.4675)` match
  `kmf_tool.py`'s VERT sample bit-for-bit; 140/255 distinct pose samples for
  that vertex, **255/255 unique full-frame pose vectors = 30.0 unique poses/s**.
* `REA3_PickUpGem4.kmf` DKAF: 600 frames, 1056 vertices, 633,600 poses,
  480 distinct morph weights for vertex 0, **600/600 unique full-frame pose
  vectors = 30.0 unique poses/s**; file 25,389,308 B, size formula matches.
* Swift/Metal static checks: 1 `viewProjection` definition, 0 `DK2MeshRenderer`
  files, 0 `limit: 8`, 1 LRU cache class, normal attribute in both descriptor
  and shader, `n = in.normal` present.

## Remaining UNKNOWNs / limitations

1. `ios/DK2Port/DK2DecoderBridge.mm` and its header are outside the permitted
   write set. The bridge still exposes `animFrameBuffers:` as legacy DKAF v1
   and converts OBJ→v1 buffers (positions + faces only). Swift therefore uses
   the existing `animFrames:` OBJ path (which now carries UV, normal and
   HEAD metadata) and can parse DKAF v2 directly when a `.dkaf` file is staged
   under `models/`. Updating the bridge to hand `dk2_anim_export_dkaf` bytes to
   Swift is a follow-up, not silently assumed here.
2. The standalone `.json` sidecar is written only by
   `dk2_kmf_export_manifest`; the existing `dk2_kmf_export_obj(FILE*)` call
   cannot derive an output path portably, so it embeds the same JSON as an OBJ
   comment. Tools wanting a file must call the new function explicitly.
3. `HEAD.pos`/`cubeScale` semantics are named but not explained by the
   references; x/y/z are applied literally as world offsets. Whether OpenKeeper's
   `(x,-z,y)` swap should also be applied in the port's `(x,y,-z)` world remains
   a render-orientation inference, not a format fact.
4. Morph weights are exported per (vertex, frame) rather than deduplicated into
   OpenKeeper's sparse key-frame `Pose` objects; equivalent result, larger file.
   The 16-texture/4096-material manifest cap is documented above.
5. No IPA was built and no iOS simulator/device run was possible in this
   environment (no Swift toolchain/Metal): Swift/Metal changes were verified by
   source-level checks only, not compiled.
