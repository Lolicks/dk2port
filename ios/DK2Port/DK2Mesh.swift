import Foundation
import Metal

struct DK2MeshVertex {
    var position: SIMD4<Float>   // xyz = local position, w = unused
    var uv: SIMD2<Float>
}

/// Loads Wavefront OBJ files exported from the original KMF meshes by
/// `tools/export_meshes.py` (which uses the C decoder). No geometry is
/// generated here: every vertex/triangle comes from the shipped game.
final class DK2Mesh {
    let name: String
    let vertices: [DK2MeshVertex]
    let device: MTLDevice
    private(set) var vertexBuffer: MTLBuffer?

    init?(name: String, url: URL, device: MTLDevice) {
        guard let text = try? String(contentsOf: url, encoding: .utf8) else {
            return nil
        }
        var positions: [SIMD3<Float>] = []
        var uvs: [SIMD2<Float>] = []
        var indices: [Int] = []
        for raw in text.split(separator: "\n") {
            let line = raw.trimmingCharacters(in: .whitespaces)
            if line.hasPrefix("v ") {
                let p = line.dropFirst(2).split(separator: " ").compactMap { Float($0) }
                if p.count >= 3 { positions.append(SIMD3<Float>(p[0], p[1], p[2])) }
            } else if line.hasPrefix("vt ") {
                let t = line.dropFirst(3).split(separator: " ").compactMap { Float($0) }
                if t.count >= 2 { uvs.append(SIMD2<Float>(t[0], t[1])) }
            } else if line.hasPrefix("f ") {
                for token in line.dropFirst(2).split(separator: " ") {
                    let parts = token.split(separator: "/", omittingEmptySubsequences: false)
                    if let first = parts.first, let vi = Int(first) {
                        indices.append(vi - 1)
                    }
                }
            }
        }
        guard !positions.isEmpty, !indices.isEmpty else { return nil }

        var out: [DK2MeshVertex] = []
        out.reserveCapacity(indices.count)
        for idx in indices {
            guard idx >= 0, idx < positions.count else { continue }
            let p = positions[idx]
            let uv = idx < uvs.count ? uvs[idx] : SIMD2<Float>(0.5, 0.5)
            out.append(DK2MeshVertex(position: SIMD4<Float>(p.x, p.y, p.z, 1),
                                     uv: uv))
        }
        self.name = name
        self.vertices = out
        self.device = device
        self.vertexBuffer = device.makeBuffer(
            bytes: out, length: out.count * MemoryLayout<DK2MeshVertex>.stride,
            options: .storageModeShared)
    }
}

/// Loads a small, explicit set of original creature meshes by name.
final class DK2MeshStore {
    private(set) var meshes: [String: DK2Mesh] = [:]

    init(device: MTLDevice, assetRoot: URL, names: [String]) {
        for name in names {
            let url = assetRoot.appendingPathComponent("models/\(name).obj")
            if let mesh = DK2Mesh(name: name, url: url, device: device) {
                meshes[name] = mesh
            }
        }
    }

    func mesh(forSpecies species: Int) -> DK2Mesh? {
        // Only a few shipped creature models are MESH-type; the rest are ANIM
        // and require the skeletal path (not implemented yet).
        let map = ["Imp", "Skeleton", "Troll", "DarkElf", "Vampire"]
        guard species >= 0, species < map.count else { return nil }
        return meshes[map[species]]
    }
}
