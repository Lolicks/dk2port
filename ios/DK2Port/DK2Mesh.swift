import Foundation
import Metal

/// GPU vertex layout. 48-byte stride, matched by `MeshVertexIn` in
/// Shaders.metal: float4 position @0, float2 uv @16, float3 normal @24.
/// The normal comes from the original VERT/MeshVertEx record; it is never
/// synthesised from screen-space derivatives.
struct DK2MeshVertex {
    var position: SIMD4<Float>   // xyz = local position, w = unused
    var uv: SIMD2<Float>
    var nx: Float
    var ny: Float
    var nz: Float
    var pad0: Float
    var pad1: Float
    var pad2: Float
}

/// Per-species placement recovered from ANIM/HEAD. Static MESH OBJs keep the
/// legacy scale and an identity offset, so existing assets are unchanged.
struct DK2MeshPlacement {
    var scale: Float = 2.4
    var offset: SIMD3<Float> = SIMD3<Float>(0, 0, 0)
    var cubeScale: Float = 1
    var fps: Float = DK2AnimationClock.fps
    var frameFactor: UInt32 = 0
}

/// One morph sample: the Swift counterpart of the C `Dk2Pose` / Java
/// `FrameInfo`. `position` = mix(prev, next, weight).
struct DK2VertexPose {
    var prev: SIMD3<Float>
    var next: SIMD3<Float>
    var weight: Float
    var prevFrame: UInt16
    var nextFrame: UInt16

    var position: SIMD3<Float> {
        prev + (next - prev) * weight
    }
}

/// Explicit pose track exported as DKAF v2. One sample per vertex per frame,
/// indexed as frame * vertexCount + vertex.
struct DK2Animation {
    let fps: Float
    let frameCount: Int
    let vertexCount: Int
    let poses: [DK2VertexPose]

    func pose(vertex: Int, frame: Int) -> DK2VertexPose? {
        guard vertex >= 0, vertex < vertexCount,
              frame >= 0, frame < frameCount else { return nil }
        return poses[frame * vertexCount + vertex]
    }

    func position(vertex: Int, frame: Int) -> SIMD3<Float>? {
        pose(vertex: vertex, frame: frame)?.position
    }

    /// Evaluate a pose track at a (possibly fractional) time in seconds.
    /// Stays at the last frame after the clip ends.
    func position(vertex: Int, time: Float) -> SIMD3<Float>? {
        let raw = time * fps
        let f0 = max(0, min(frameCount - 1, Int(floor(raw))))
        let f1 = max(0, min(frameCount - 1, f0 + 1))
        let frac = max(0, min(1, raw - Float(f0)))
        guard let a = position(vertex: vertex, frame: f0),
              let b = position(vertex: vertex, frame: f1) else { return nil }
        return a + (b - a) * frac
    }
}

/// Material -> texture mapping exported by `dk2_kmf_export_obj`. The OBJ
/// carries the same JSON as a `# DK2_MATERIAL_MANIFEST` comment so Swift can
/// read it through the existing bridge without a new Objective-C method.
struct DK2MaterialManifest {
    let format: String
    let mesh: String
    let lod: Int
    let triangleCount: Int
    let usemtlPrefix: String
    let materials: [String: [String]]   // `mat_N` -> texture names

    static func parse(_ text: String) -> DK2MaterialManifest? {
        guard let line = text.split(separator: "\n").first(
                where: { $0.hasPrefix("# DK2_MATERIAL_MANIFEST ") })
        else { return nil }
        let json = String(line.dropFirst("# DK2_MATERIAL_MANIFEST ".count))
        guard let bytes = json.data(using: .utf8),
              let raw = try? JSONSerialization.jsonObject(with: bytes),
              let obj = raw as? [String: Any] else { return nil }
        var map: [String: [String]] = [:]
        if let mats = obj["materials"] as? [[String: Any]] {
            for m in mats {
                guard let name = m["usemtl"] as? String else { continue }
                map[name] = (m["textures"] as? [String]) ?? []
            }
        }
        return DK2MaterialManifest(
            format: obj["format"] as? String ?? "",
            mesh: obj["mesh"] as? String ?? "",
            lod: (obj["lod"] as? NSNumber)?.intValue ?? 0,
            triangleCount: (obj["triangle_count"] as? NSNumber)?.intValue ?? 0,
            usemtlPrefix: obj["usemtl_prefix"] as? String ?? "mat_",
            materials: map)
    }
}

/// Pause-aware animation clock. The engine posts `dk2.command.pause`; while
/// paused the clock returns the frozen time so every creature holds its pose.
final class DK2AnimationClock {
    static let fps: Float = 30
    static let shared = DK2AnimationClock()

    private let lock = NSLock()
    private var paused = false
    private var frozen: Float = 0

    init() {
        NotificationCenter.default.addObserver(
            forName: Notification.Name("dk2.command.pause"),
            object: nil, queue: nil) { [weak self] note in
                guard let self = self else { return }
                let value = note.userInfo?[DK2GameCommand.Key.paused] as? Bool ?? false
                self.lock.lock()
                self.paused = value
                self.lock.unlock()
            }
    }

    func time(_ engineTime: Float) -> Float {
        lock.lock(); defer { lock.unlock() }
        if !paused { frozen = engineTime }
        return frozen
    }
}

/// One frame of original geometry, independent of how it was obtained.
struct DK2MeshGeometry {
    var positions: [SIMD3<Float>] = []
    var indices: [UInt16] = []
    var uvs: [SIMD2<Float>] = []
    var normals: [SIMD3<Float>] = []

    var vertexCount: Int { positions.count }
    var indexCount: Int { indices.count }

    private struct VertexKey: Hashable {
        let v: Int
        let t: Int
        let n: Int
    }

    /// Parse OBJ text from `dk2_kmf_export_obj` / `dk2_anim_export`. Faces
    /// carry v/vt/vn indices; the exporter emits one vt and one vn per vertex,
    /// so the original UVs and normals survive the round trip.
    static func parseOBJ(_ text: String) -> DK2MeshGeometry? {
        var rawPositions: [SIMD3<Float>] = []
        var rawUVs: [SIMD2<Float>] = []
        var rawNormals: [SIMD3<Float>] = []
        var faces: [[(v: Int, t: Int, n: Int)]] = []

        for rawLine in text.split(separator: "\n") {
            let line = String(rawLine).trimmingCharacters(in: .whitespacesAndNewlines)
            if line.hasPrefix("v ") {
                let values = line.dropFirst(2).split(separator: " ")
                    .compactMap { Float(String($0)) }
                if values.count >= 3 {
                    rawPositions.append(SIMD3<Float>(values[0], values[1], values[2]))
                }
            } else if line.hasPrefix("vt ") {
                let values = line.dropFirst(3).split(separator: " ")
                    .compactMap { Float(String($0)) }
                if values.count >= 2 {
                    rawUVs.append(SIMD2<Float>(values[0], values[1]))
                }
            } else if line.hasPrefix("vn ") {
                let values = line.dropFirst(3).split(separator: " ")
                    .compactMap { Float(String($0)) }
                if values.count >= 3 {
                    rawNormals.append(SIMD3<Float>(values[0], values[1], values[2]))
                }
            } else if line.hasPrefix("f ") {
                var corners: [(v: Int, t: Int, n: Int)] = []
                for token in line.dropFirst(2).split(separator: " ") {
                    let parts = String(token).split(separator: "/",
                                                    omittingEmptySubsequences: false)
                    guard let first = parts.first, let vi = Int(String(first)) else { continue }
                    let ti = parts.count > 1 ? (Int(String(parts[1])) ?? 0) : 0
                    let ni = parts.count > 2 ? (Int(String(parts[2])) ?? 0) : 0
                    corners.append((v: vi - 1, t: ti - 1, n: ni - 1))
                }
                if corners.count >= 3 { faces.append(corners) }
            }
        }

        guard !rawPositions.isEmpty, !faces.isEmpty else { return nil }

        var geometry = DK2MeshGeometry()
        var compactIndex: [VertexKey: UInt16] = [:]
        for face in faces {
            for i in 1..<(face.count - 1) {
                let triangle = [face[0], face[i], face[i + 1]]
                for corner in triangle {
                    guard corner.v >= 0, corner.v < rawPositions.count else { continue }
                    let key = VertexKey(v: corner.v, t: corner.t, n: corner.n)
                    let index: UInt16
                    if let existing = compactIndex[key] {
                        index = existing
                    } else {
                        let next = geometry.positions.count
                        guard next < 65535 else { return nil }
                        let uv = (corner.t >= 0 && corner.t < rawUVs.count)
                            ? rawUVs[corner.t] : SIMD2<Float>(0.5, 0.5)
                        let normal = (corner.n >= 0 && corner.n < rawNormals.count)
                            ? rawNormals[corner.n] : SIMD3<Float>(0, 0, 1)
                        geometry.positions.append(rawPositions[corner.v])
                        geometry.uvs.append(uv)
                        geometry.normals.append(normal)
                        index = UInt16(next)
                        compactIndex[key] = index
                    }
                    geometry.indices.append(index)
                }
            }
        }

        guard geometry.indices.count >= 3 else { return nil }
        return geometry
    }

    /// Placement and morph metadata carried as OBJ comments by the C exporter.
    static func placementFromOBJ(_ text: String) -> DK2MeshPlacement {
        var p = DK2MeshPlacement()
        for rawLine in text.split(separator: "\n") {
            let line = String(rawLine)
            guard line.hasPrefix("# DK2_ANIM_HEAD ") else { continue }
            for field in line.dropFirst("# DK2_ANIM_HEAD ".count).split(separator: " ") {
                let parts = field.split(separator: "=", maxSplits: 1,
                                        omittingEmptySubsequences: false)
                guard parts.count == 2 else { continue }
                let key = String(parts[0]); let value = String(parts[1])
                if key == "scale", let v = Float(value) { p.scale = v }
                else if key == "origin" {
                    let xyz = value.split(separator: ",").compactMap { Float(String($0)) }
                    if xyz.count == 3 { p.offset = SIMD3<Float>(xyz[0], xyz[1], xyz[2]) }
                } else if key == "cube", let v = Float(value) { p.cubeScale = v }
                else if key == "factor", let v = UInt32(value) { p.frameFactor = v }
            }
        }
        return p
    }

    /// Parse the canonical binary DKAF v2 animation (see anim.h).
    static func parseDKAF(_ data: Data) -> (DK2MeshGeometry, DK2Animation, DK2MeshPlacement)? {
        let headerBytes = 56
        guard data.count >= headerBytes else { return nil }
        var magic: UInt32 = 0, version: UInt32 = 0, fps: UInt32 = 0
        var frames: UInt32 = 0, vertexCount: UInt32 = 0, indexCount: UInt32 = 0
        var poseCount: UInt32 = 0, factor: UInt32 = 0
        var scale: Float = 0, origin = SIMD3<Float>(0, 0, 0), cube: Float = 0
        data.withUnsafeBytes { raw in
            guard let b = raw.baseAddress else { return }
            func u32(_ o: Int) -> UInt32 { var v: UInt32 = 0; memcpy(&v, b.advanced(by: o), 4); return v }
            func f32(_ o: Int) -> Float { var v: Float = 0; memcpy(&v, b.advanced(by: o), 4); return v }
            magic = u32(0); version = u32(4); fps = u32(8)
            frames = u32(12); vertexCount = u32(16); indexCount = u32(20)
            poseCount = u32(24); scale = f32(28)
            origin = SIMD3<Float>(f32(32), f32(36), f32(40))
            cube = f32(44); factor = u32(48)
        }
        guard magic == 0x46414B44, version == 2,
              frames > 0, vertexCount > 0, poseCount == frames * vertexCount
        else { return nil }
        let vcount = Int(vertexCount), icount = Int(indexCount)
        let expected = headerBytes + vcount * 40 + icount * 2 + Int(poseCount) * 40
        guard data.count >= expected else { return nil }

        var positions: [SIMD3<Float>] = []
        var uvs: [SIMD2<Float>] = []
        var normals: [SIMD3<Float>] = []
        positions.reserveCapacity(vcount)
        data.withUnsafeBytes { raw in
            guard let b = raw.baseAddress else { return }
            func f32(_ o: Int) -> Float { var v: Float = 0; memcpy(&v, b.advanced(by: o), 4); return v }
            for i in 0..<vcount {
                let o = headerBytes + i * 40
                positions.append(SIMD3<Float>(f32(o), f32(o + 4), f32(o + 8)))
                normals.append(SIMD3<Float>(f32(o + 12), f32(o + 16), f32(o + 20)))
                uvs.append(SIMD2<Float>(f32(o + 24), f32(o + 28)))
            }
        }
        var indices = [UInt16](repeating: 0, count: icount)
        let indexBase = headerBytes + vcount * 40
        data.withUnsafeBytes { raw in
            guard let b = raw.baseAddress else { return }
            for i in 0..<icount {
                var v: UInt16 = 0
                memcpy(&v, b.advanced(by: indexBase + i * 2), 2)
                indices[i] = v
            }
        }
        let validIndices = indices.filter { Int($0) < vcount }
        guard validIndices.count >= 3, validIndices.count % 3 == 0 else { return nil }

        var poses: [DK2VertexPose] = []
        poses.reserveCapacity(Int(poseCount))
        let poseBase = indexBase + icount * 2
        data.withUnsafeBytes { raw in
            guard let b = raw.baseAddress else { return }
            func f32(_ o: Int) -> Float { var v: Float = 0; memcpy(&v, b.advanced(by: o), 4); return v }
            func u16(_ o: Int) -> UInt16 { var v: UInt16 = 0; memcpy(&v, b.advanced(by: o), 2); return v }
            for i in 0..<Int(poseCount) {
                let o = poseBase + i * 40
                poses.append(DK2VertexPose(
                    prev: SIMD3<Float>(f32(o), f32(o + 4), f32(o + 8)),
                    next: SIMD3<Float>(f32(o + 12), f32(o + 16), f32(o + 20)),
                    weight: f32(o + 24),
                    prevFrame: u16(o + 28), nextFrame: u16(o + 30)))
            }
        }

        let geometry = DK2MeshGeometry(positions: positions, indices: validIndices,
                                       uvs: uvs, normals: normals)
        var placement = DK2MeshPlacement()
        placement.scale = scale
        placement.offset = origin
        placement.cubeScale = cube
        placement.fps = fps > 0 ? Float(fps) : DK2AnimationClock.fps
        placement.frameFactor = factor
        let animation = DK2Animation(fps: placement.fps, frameCount: Int(frames),
                                     vertexCount: vcount, poses: poses)
        return (geometry, animation, placement)
    }
}

/// Least-recently-used cache of decoded frame geometry + GPU buffers. The old
/// hard cap of 8 ANIM frames is gone: all frames remain addressable, only the
/// most recently used ones hold GPU buffers.
final class DK2FrameCache {
    struct Entry {
        let geometry: DK2MeshGeometry
        let vertex: MTLBuffer
        let index: MTLBuffer
    }

    private var entries: [Int: Entry] = [:]
    private var recency: [Int] = []
    let capacity: Int

    init(capacity: Int = 96) { self.capacity = max(1, capacity) }

    func entry(for frame: Int, make: () -> Entry?) -> Entry? {
        if let hit = entries[frame] {
            if let at = recency.firstIndex(of: frame) {
                recency.remove(at: at)
                recency.append(frame)
            }
            return hit
        }
        guard let made = make() else { return nil }
        entries[frame] = made
        recency.append(frame)
        while recency.count > capacity {
            let old = recency.removeFirst()
            entries.removeValue(forKey: old)
        }
        return made
    }
}

/// A mesh is one or more animation frames. Every frame stays addressable;
/// decoding and GPU buffers are bounded by the LRU cache.
final class DK2Mesh {
    let name: String
    let device: MTLDevice
    let frameCount: Int
    let isAnimated: Bool
    let placement: DK2MeshPlacement
    let animation: DK2Animation?
    let materialManifest: DK2MaterialManifest?

    /// Legacy transport: OBJ text per frame returned by DK2DecoderBridge.
    private let objFrames: [Data]
    /// Canonical DKAF v2 pose track, when the binary animation was supplied.
    private let dkafAnimation: DK2Animation?
    private let fallback: DK2MeshGeometry
    private let cache = DK2FrameCache()

    init?(name: String, objFrames: [Data], device: MTLDevice) {
        guard let first = objFrames.first,
              let base = DK2MeshGeometry.parseOBJ(String(decoding: first, as: UTF8.self))
        else { return nil }
        self.name = name
        self.device = device
        self.objFrames = objFrames
        self.dkafAnimation = nil
        self.animation = nil
        let text = String(decoding: first, as: UTF8.self)
        self.materialManifest = DK2MaterialManifest.parse(text)
        self.frameCount = objFrames.count
        self.isAnimated = objFrames.count > 1
        self.placement = DK2MeshGeometry.placementFromOBJ(text)
        self.fallback = base
    }

    init?(name: String, dkafData: Data, device: MTLDevice) {
        guard let parsed = DK2MeshGeometry.parseDKAF(dkafData) else { return nil }
        self.name = name
        self.device = device
        self.objFrames = []
        self.dkafAnimation = parsed.1
        self.animation = parsed.1
        self.materialManifest = nil
        self.frameCount = parsed.1.frameCount
        self.isAnimated = parsed.1.frameCount > 1
        self.placement = parsed.2
        self.fallback = parsed.0
    }

    convenience init?(name: String, url: URL, device: MTLDevice) {
        guard let text = try? String(contentsOf: url, encoding: .utf8),
              DK2MeshGeometry.parseOBJ(text) != nil,
              let data = text.data(using: .utf8) else { return nil }
        self.init(name: name, objFrames: [data], device: device)
    }

    private func entry(for frame: Int) -> DK2FrameCache.Entry? {
        let f = ((frame % frameCount) + frameCount) % frameCount
        return cache.entry(for: f) { [weak self] in
            guard let self = self else { return nil }
            guard let geometry = self.geometry(for: f) else { return nil }
            return Self.makeBuffers(geometry, device: self.device)
        }
    }

    func geometry(frame: Int) -> DK2MeshGeometry {
        entry(for: frame)?.geometry ?? fallback
    }

    func vertexBuffer(frame: Int) -> MTLBuffer {
        if let hit = entry(for: frame) { return hit.vertex }
        // The fallback buffer is always present: init decoded the first frame.
        return Self.makeBuffers(fallback, device: device)!.vertex
    }

    func indexBuffer(frame: Int) -> MTLBuffer {
        if let hit = entry(for: frame) { return hit.index }
        return Self.makeBuffers(fallback, device: device)!.index
    }

    func indexCount(frame: Int) -> Int {
        entry(for: frame)?.geometry.indices.count ?? fallback.indices.count
    }

    /// Texture names declared by the `usemtl mat_N` of a submesh. Used by the
    /// asset binding layer to map KMF materials onto the shipped texture atlas.
    func textures(forMaterial usemtl: String) -> [String] {
        materialManifest?.materials[usemtl] ?? []
    }

    private func geometry(for frame: Int) -> DK2MeshGeometry? {
        if let anim = dkafAnimation {
            let vertexCount = anim.vertexCount
            var g = fallback
            var positions: [SIMD3<Float>] = []
            positions.reserveCapacity(vertexCount)
            for v in 0..<vertexCount {
                positions.append(anim.position(vertex: v, frame: frame)
                                 ?? fallback.positions[min(v, fallback.positions.count - 1)])
            }
            g.positions = positions
            return g
        }
        guard frame < objFrames.count else { return nil }
        return DK2MeshGeometry.parseOBJ(String(decoding: objFrames[frame], as: UTF8.self))
    }

    private static func makeBuffers(_ geometry: DK2MeshGeometry,
                                    device: MTLDevice) -> DK2FrameCache.Entry? {
        var vertices: [DK2MeshVertex] = []
        vertices.reserveCapacity(geometry.positions.count)
        for (i, position) in geometry.positions.enumerated() {
            let uv = i < geometry.uvs.count ? geometry.uvs[i] : SIMD2<Float>(0.5, 0.5)
            let n = i < geometry.normals.count
                ? geometry.normals[i] : SIMD3<Float>(0, 0, 1)
            vertices.append(DK2MeshVertex(
                position: SIMD4<Float>(position.x, position.y, position.z, 1),
                uv: uv, nx: n.x, ny: n.y, nz: n.z,
                pad0: 0, pad1: 0, pad2: 0))
        }
        guard !vertices.isEmpty,
              let vertexBuffer = device.makeBuffer(
                  bytes: vertices,
                  length: vertices.count * MemoryLayout<DK2MeshVertex>.stride,
                  options: .storageModeShared)
        else { return nil }
        var indices = geometry.indices
        guard let indexBuffer = device.makeBuffer(
                  bytes: &indices,
                  length: indices.count * MemoryLayout<UInt16>.stride,
                  options: .storageModeShared)
        else { return nil }
        return DK2FrameCache.Entry(geometry: geometry,
                                   vertex: vertexBuffer, index: indexBuffer)
    }
}

/// Resolves the 46 creature species of game_data.json to real original
/// meshes. Static OBJ meshes come from assetRoot/models/; animated meshes are
/// decoded on demand from Meshes.WAD through the C bridge.
final class DK2MeshStore {
    private static let aliases: [String] = [
        "Imp", "PR1", "BD", "Mis", "War", "DElf", "Gob", "Vam",
        "Ske", "Tro", "Sal", "Fai", "Kni", "Dwa", "Gia", "Wiz",
        "Arc", "Thf", "Mon", "Fai", "Kin", "DKni", "DAN", "Rog",
        "Gua", "PR2", "Mis", "StKni", "LOL", "RGua", "PR3", "Fai",
        "Gob", "War", "Tro", "DElf", "Ske", "Mis", "Sal", "Rog",
        "BD", "Vam", "DKni", "DAN", "Maiden", "Jack",
    ]

    private static let poseFrames: [String] = [
        "Imp", "Gob", "Ske", "Tro", "Vam", "War", "Wiz", "Kni",
        "Gia", "Fai", "Rog", "Dwa", "Arc", "Thf", "Mon", "Mis",
        "BD", "DAN", "DKni", "Gua", "LOL", "StKni", "RGua",
        "PR1", "PR2", "PR3",
    ]

    private let device: MTLDevice
    private let assetRoot: URL
    private(set) var creatureNames: [String]
    private var staticMeshes: [Int: DK2Mesh] = [:]
    private var animatedMeshes: [Int: DK2Mesh] = [:]
    private var animationAttempted: Set<Int> = []
    private var wadCache: Data?

    init(device: MTLDevice, assetRoot: URL, creatureNames: [String]) {
        self.device = device
        self.assetRoot = assetRoot
        self.creatureNames = creatureNames
        for species in 0..<min(46, creatureNames.count) {
            let names = DK2MeshStore.candidateModelNames(
                for: creatureNames[species], index: species)
            for name in names {
                let url = assetRoot.appendingPathComponent("models/\(name).obj")
                if let mesh = DK2Mesh(name: name, url: url, device: device) {
                    staticMeshes[species] = mesh
                    break
                }
            }
        }
    }

    func mesh(forSpecies species: Int) -> DK2Mesh? {
        guard species >= 0 else { return nil }
        if let animated = animatedMeshes[species] { return animated }
        if !animationAttempted.contains(species) {
            animationAttempted.insert(species)
            if let animated = loadAnimated(species: species) {
                animatedMeshes[species] = animated
                return animated
            }
        }
        if let exact = staticMeshes[species] { return exact }
        return staticMeshes[species % 46]
    }

    func meshByIndex(_ index: Int) -> DK2Mesh? {
        let keys = staticMeshes.keys.sorted()
        guard !keys.isEmpty else { return nil }
        return staticMeshes[keys[index % keys.count]]
    }

    var availableSpecies: [Int] {
        staticMeshes.keys.sorted()
    }

    private func loadAnimated(species: Int) -> DK2Mesh? {
        guard species < creatureNames.count else { return nil }
        var entries: [String] = []
        for base in DK2MeshStore.candidateModelNames(
                for: creatureNames[species], index: species) {
            entries.append(contentsOf: ["\(base)-Idle1", "\(base)-Walk",
                                        "\(base)-PoseFrame", base])
        }

        // Prefer the canonical binary DKAF v2 animation when staged on disk.
        for entry in entries {
            let url = assetRoot.appendingPathComponent("models/\(entry).dkaf")
            guard let dkaf = try? Data(contentsOf: url),
                  let mesh = DK2Mesh(name: entry, dkafData: dkaf, device: device)
            else { continue }
            return mesh
        }

        // KMF -> OBJ frames. limit 0 exports every frame; the LRU cache in
        // DK2Mesh bounds the resident GPU buffers instead of the frame count.
        func mesh(from kmf: Data, entry: String) -> DK2Mesh? {
            guard let frames = try? DK2DecoderBridge.animFrames(kmf, limit: 0),
                  !frames.isEmpty else { return nil }
            return DK2Mesh(name: entry, objFrames: frames, device: device)
        }

        for entry in entries {
            let url = assetRoot.appendingPathComponent("textures/Meshes/\(entry).kmf")
            guard let kmf = try? Data(contentsOf: url),
                  let mesh = mesh(from: kmf, entry: entry) else { continue }
            return mesh
        }

        let wad: Data
        if let cached = wadCache {
            wad = cached
        } else {
            let url = DK2OriginalAssets.root.appendingPathComponent("Data/Meshes.WAD")
            guard let loaded = try? Data(contentsOf: url) else { return nil }
            wadCache = loaded
            wad = loaded
        }

        for entry in entries {
            guard let stored = try? DK2DecoderBridge.wadEntry(entry, from: wad),
                  let kmf = try? DK2DecoderBridge.decompressKmsh(stored),
                  let mesh = mesh(from: kmf, entry: entry) else { continue }
            return mesh
        }
        return nil
    }

    private static func candidateModelNames(for creatureName: String,
                                            index: Int) -> [String] {
        var result: [String] = []
        let compact = creatureName.filter { !$0.isWhitespace }
        let cleaned = compact.filter { $0.isLetter || $0.isNumber }
        if !cleaned.isEmpty { result.append(cleaned) }
        if index < aliases.count, !aliases[index].isEmpty {
            result.append(aliases[index])
            result.append(aliases[index] + "-PoseFrame")
        }
        if !compact.isEmpty { result.append(compact + "-PoseFrame") }
        if !poseFrames.isEmpty {
            result.append(poseFrames[index % poseFrames.count] + "-PoseFrame")
        }
        var seen: Set<String> = []
        return result.filter { seen.insert($0).inserted }
    }
}
