import Foundation
import Metal

/// GPU vertex layout used by the mesh pipeline. Matches theMetal vertex
/// descriptor: float4 position @ 0, float2 uv @ 16, stride 24.
struct DK2MeshVertex {
    var position: SIMD4<Float>   // xyz = local position, w = unused
    var uv: SIMD2<Float>
}

/// One frame of original geometry, independent of how it was obtained.
/// Static MESH files and animated ANIM frames both end up in this shape.
struct DK2MeshGeometry {
    var positions: [SIMD3<Float>] = []
    var indices: [UInt16] = []
    var uvs: [SIMD2<Float>] = []

    var vertexCount: Int { positions.count }
    var indexCount: Int { indices.count }

    private struct VertexKey: Hashable {
        let v: Int
        let t: Int
    }

    /// Parse the OBJ text produced by `dk2_kmf_export_obj`.
    /// Faces may be quads/triangles and may carry v/vt/vn indices; both are
    /// honoured so the texture mapping from the original file survives.
    static func parseOBJ(_ text: String) -> DK2MeshGeometry? {
        var rawPositions: [SIMD3<Float>] = []
        var rawUVs: [SIMD2<Float>] = []
        var faces: [[(v: Int, t: Int)]] = []

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
            } else if line.hasPrefix("f ") {
                var corners: [(v: Int, t: Int)] = []
                for token in line.dropFirst(2).split(separator: " ") {
                    let parts = String(token).split(separator: "/",
                                                    omittingEmptySubsequences: false)
                    guard let first = parts.first, let vi = Int(String(first)) else { continue }
                    let ti = parts.count > 1 ? (Int(String(parts[1])) ?? 0) : 0
                    corners.append((v: vi - 1, t: ti - 1))
                }
                if corners.count >= 3 { faces.append(corners) }
            }
        }

        guard !rawPositions.isEmpty, !faces.isEmpty else { return nil }

        var geometry = DK2MeshGeometry()
        var compactIndex: [VertexKey: UInt16] = [:]
        for face in faces {
            // Fan triangulate; every original face is convex.
            for i in 1..<(face.count - 1) {
                let triangle = [face[0], face[i], face[i + 1]]
                for corner in triangle {
                    guard corner.v >= 0, corner.v < rawPositions.count else { continue }
                    let key = VertexKey(v: corner.v, t: corner.t)
                    let index: UInt16
                    if let existing = compactIndex[key] {
                        index = existing
                    } else {
                        let next = geometry.positions.count
                        guard next < 65535 else { return nil }
                        let uv = (corner.t >= 0 && corner.t < rawUVs.count)
                            ? rawUVs[corner.t] : SIMD2<Float>(0.5, 0.5)
                        geometry.positions.append(rawPositions[corner.v])
                        geometry.uvs.append(uv)
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

    /// Parse one binary frame returned by DK2DecoderBridge.animFrameBuffers.
    /// ANIM frames carry no UVs in the OBJ export, so planar UVs are derived
    /// from the XY footprint; the shader still selects the atlas cell.
    static func parseBinaryFrame(_ data: Data) -> DK2MeshGeometry? {
        guard data.count >= 16 else { return nil }
        var magic: UInt32 = 0
        var version: UInt32 = 0
        var rawVertexCount: UInt32 = 0
        var rawIndexCount: UInt32 = 0
        data.withUnsafeBytes { raw in
            guard let base = raw.baseAddress else { return }
            memcpy(&magic, base, 4)
            memcpy(&version, base.advanced(by: 4), 4)
            memcpy(&rawVertexCount, base.advanced(by: 8), 4)
            memcpy(&rawIndexCount, base.advanced(by: 12), 4)
        }
        guard magic == 0x46414B44, version == 1,
              rawVertexCount > 0, rawIndexCount >= 3, rawIndexCount % 3 == 0
        else { return nil }

        let vertexCount = Int(rawVertexCount)
        let indexCount = Int(rawIndexCount)
        guard data.count >= 16 + vertexCount * 12 + indexCount * 2 else { return nil }

        var flattened = [Float](repeating: 0, count: vertexCount * 3)
        var rawIndices = [UInt16](repeating: 0, count: indexCount)
        data.withUnsafeBytes { raw in
            guard let base = raw.baseAddress else { return }
            memcpy(&flattened, base.advanced(by: 16), vertexCount * 12)
            memcpy(&rawIndices, base.advanced(by: 16 + vertexCount * 12), indexCount * 2)
        }

        var positions: [SIMD3<Float>] = []
        positions.reserveCapacity(vertexCount)
        for i in 0..<vertexCount {
            positions.append(SIMD3<Float>(flattened[i * 3],
                                          flattened[i * 3 + 1],
                                          flattened[i * 3 + 2]))
        }
        let indices = rawIndices.filter { Int($0) < vertexCount }
        guard indices.count >= 3, indices.count % 3 == 0 else { return nil }

        var minX = Float.infinity, maxX = -Float.infinity
        var minY = Float.infinity, maxY = -Float.infinity
        for p in positions {
            minX = min(minX, p.x); maxX = max(maxX, p.x)
            minY = min(minY, p.y); maxY = max(maxY, p.y)
        }
        let spanX = max(maxX - minX, 0.00001)
        let spanY = max(maxY - minY, 0.00001)
        let uvs = positions.map {
            SIMD2<Float>(($0.x - minX) / spanX, ($0.y - minY) / spanY)
        }

        return DK2MeshGeometry(positions: positions, indices: indices, uvs: uvs)
    }
}

/// A mesh is one or more animation frames; both cases expose the same buffers
/// so the renderer never branches on how the geometry was produced.
final class DK2Mesh {
    let name: String
    let device: MTLDevice
    private(set) var frames: [DK2MeshGeometry]
    private(set) var vertexBuffers: [MTLBuffer] = []
    private(set) var indexBuffers: [MTLBuffer] = []
    private(set) var isAnimated: Bool

    init?(name: String, geometries: [DK2MeshGeometry], device: MTLDevice) {
        guard !geometries.isEmpty else { return nil }
        var vertexBuffers: [MTLBuffer] = []
        var indexBuffers: [MTLBuffer] = []
        for geometry in geometries {
            guard let buffers = DK2Mesh.makeBuffers(geometry, device: device) else { return nil }
            vertexBuffers.append(buffers.0)
            indexBuffers.append(buffers.1)
        }
        self.name = name
        self.device = device
        self.frames = geometries
        self.vertexBuffers = vertexBuffers
        self.indexBuffers = indexBuffers
        self.isAnimated = geometries.count > 1
    }

    convenience init?(name: String, url: URL, device: MTLDevice) {
        guard let text = try? String(contentsOf: url, encoding: .utf8),
              let geometry = DK2MeshGeometry.parseOBJ(text) else { return nil }
        self.init(name: name, geometries: [geometry], device: device)
    }

    convenience init?(name: String, frameData: [Data], device: MTLDevice) {
        let geometries = frameData.compactMap { DK2MeshGeometry.parseBinaryFrame($0) }
        guard !geometries.isEmpty else { return nil }
        self.init(name: name, geometries: geometries, device: device)
    }

    var frameCount: Int { frames.count }

    func geometry(frame: Int) -> DK2MeshGeometry {
        frames[frame % frames.count]
    }

    func vertexBuffer(frame: Int) -> MTLBuffer {
        vertexBuffers[frame % vertexBuffers.count]
    }

    func indexBuffer(frame: Int) -> MTLBuffer {
        indexBuffers[frame % indexBuffers.count]
    }

    func indexCount(frame: Int) -> Int {
        geometry(frame: frame).indices.count
    }

    private static func makeBuffers(_ geometry: DK2MeshGeometry,
                                    device: MTLDevice) -> (MTLBuffer, MTLBuffer)? {
        var vertices: [DK2MeshVertex] = []
        vertices.reserveCapacity(geometry.positions.count)
        for (i, position) in geometry.positions.enumerated() {
            let uv = i < geometry.uvs.count ? geometry.uvs[i] : SIMD2<Float>(0.5, 0.5)
            vertices.append(DK2MeshVertex(
                position: SIMD4<Float>(position.x, position.y, position.z, 1),
                uv: uv))
        }
        guard !vertices.isEmpty else { return nil }
        guard let vertexBuffer = device.makeBuffer(
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
        return (vertexBuffer, indexBuffer)
    }
}

/// Resolves the 46 creature species of game_data.json to real original
/// meshes. Static OBJ meshes come from assetRoot/models/; animated meshes are
/// decoded on demand from Meshes.WAD through the C bridge.
final class DK2MeshStore {
    /// Order matches `creature_names` in docs/game_data.json.
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

    /// Animated geometry is preferred when the original KMF is available.
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

    /// Last-resort mapping by creature instance index.
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

        // Prefer already-decompressed KMFs staged next to the OBJ models;
        // that avoids re-parsing the whole Meshes.WAD for every species.
        for entry in entries {
            let url = assetRoot.appendingPathComponent("textures/Meshes/\(entry).kmf")
            guard let kmf = try? Data(contentsOf: url),
                  let frames = try? DK2DecoderBridge.animFrameBuffers(kmf, limit: 8),
                  let mesh = DK2Mesh(name: entry, frameData: frames, device: device)
            else { continue }
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
                  let frames = try? DK2DecoderBridge.animFrameBuffers(kmf, limit: 8),
                  let mesh = DK2Mesh(name: entry, frameData: frames, device: device)
            else { continue }
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
