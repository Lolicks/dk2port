import Foundation
import Metal
import simd

/// Matched 1:1 with `MeshUniforms` in Shaders.metal (80 bytes).
/// The tile pipeline only has 2D camera scalars; this struct gives the mesh
/// pipeline a real view/projection transform without changing that pipeline.
struct DK2MeshUniforms {
    var viewProjection: simd_float4x4 = matrix_identity_float4x4
    var time: Float = 0
    var pad0: Float = 0
    var pad1: Float = 0
    var pad2: Float = 0
}

/// Draws original KMF geometry (static MESH OBJs and animated ANIM frames).
/// Kept separate from the tile renderer so a failure here cannot break the
/// working 2D fallback. Public entry point: draw(encoder:uniforms:creatures:).
final class DK2MeshRenderer {
    /// Set to true only when the render pass carries a depth attachment
    /// (e.g. metalView.depthStencilPixelFormat = .depth32float). Defaults to
    /// false so the existing MTKView setup keeps working unchanged.
    var depthEnabled = false
    /// Small camera tilt so model height is visible; 0 restores an exact
    /// top-down alignment with the 2D tile renderer.
    var tilt: Float = 0.32
    /// Frames per second used to advance ANIM playback from uniforms.time.
    var animationFPS: Float = 8

    private static let defaultCreatureNames: [String] = [
        "Imp", "Prince Tristran", "Bile Demon", "Mistress", "Warlock",
        "Dark Elf", "Goblin", "Vampire", "Skeleton", "Troll",
        "Salamander", "Firefly", "Knight", "Dwarf", "Giant", "Wizard",
        "Elven Archer", "Thief", "Monk", "Fairy", "King Reginald",
        "Black Knight", "Dark Angel", "Rogue", "Guard", "Prince Felix",
        "Horny", "Stone Knight", "Lord Of The Land", "Royal Guard",
        "Prince Balder", "Unique Firefly", "Unique Goblin", "Unique Warlock",
        "Unique Troll", "Unique Dark Elf", "Unique Skeleton",
        "Unique Mistress", "Unique Salamander", "Unique Rogue",
        "Unique Bile Demon", "Unique Vampire", "Unique Black Knight",
        "Unique Dark Angel", "Maiden Of The Nest", "Jack",
    ]

    private let device: MTLDevice
    private let pipeline: MTLRenderPipelineState
    private let depthPipeline: MTLRenderPipelineState?
    private let depthState: MTLDepthStencilState?
    private let atlas: MTLTexture
    private let sampler: MTLSamplerState
    private let store: DK2MeshStore

    init?(device: MTLDevice, atlas: MTLTexture, assetRoot: URL) {
        guard let library = device.makeDefaultLibrary(),
              let vertex = library.makeFunction(name: "mesh_vertex"),
              let fragment = library.makeFunction(name: "mesh_fragment")
        else { return nil }

        let descriptor = MTLRenderPipelineDescriptor()
        descriptor.vertexFunction = vertex
        descriptor.fragmentFunction = fragment
        descriptor.colorAttachments[0].pixelFormat = .bgra8Unorm
        descriptor.vertexDescriptor = DK2MeshRenderer.vertexDescriptor()

        guard let pipeline = try? device.makeRenderPipelineState(descriptor: descriptor)
        else { return nil }

        // Optional depth variant. Created eagerly but only selected when the
        // host supplies a depth attachment and sets depthEnabled = true.
        let depthDescriptor = MTLRenderPipelineDescriptor()
        depthDescriptor.vertexFunction = vertex
        depthDescriptor.fragmentFunction = fragment
        depthDescriptor.colorAttachments[0].pixelFormat = .bgra8Unorm
        depthDescriptor.vertexDescriptor = DK2MeshRenderer.vertexDescriptor()
        let depthPipeline = try? device.makeRenderPipelineState(descriptor: depthDescriptor)

        let depthDesc = MTLDepthStencilDescriptor()
        depthDesc.depthCompareFunction = .less
        depthDesc.isDepthWriteEnabled = true
        let depthState = device.makeDepthStencilState(descriptor: depthDesc)

        let samplerDescriptor = MTLSamplerDescriptor()
        samplerDescriptor.minFilter = .nearest
        samplerDescriptor.magFilter = .nearest
        samplerDescriptor.sAddressMode = .clampToEdge
        samplerDescriptor.tAddressMode = .clampToEdge
        guard let sampler = device.makeSamplerState(descriptor: samplerDescriptor)
        else { return nil }

        self.device = device
        self.pipeline = pipeline
        self.depthPipeline = depthPipeline
        self.depthState = depthState
        self.atlas = atlas
        self.sampler = sampler

        let names = DK2AssetLoader.gameData?.creatureNames ??
                    DK2MeshRenderer.defaultCreatureNames
        self.store = DK2MeshStore(device: device, assetRoot: assetRoot,
                                  creatureNames: names)
    }

    /// Draws every creature whose species has original geometry. Animation
    /// frames advance with uniforms.time; instance index offsets the phase so
    /// same-species creatures do not move in lockstep.
    func draw(encoder: MTLRenderCommandEncoder,
              uniforms: DK2Uniforms,
              creatures: [[AnyHashable: Any]]) {
        guard !creatures.isEmpty else { return }

        struct Placement {
            let mesh: DK2Mesh
            let frame: Int
            let x: Float
            let y: Float
            let cell: Float
        }

        var placements: [Placement] = []
        placements.reserveCapacity(creatures.count)

        for creature in creatures {
            let speciesValue = creature["species"]
            let species = (speciesValue as? Int32).map { Int($0) }
                ?? (speciesValue as? Int)
                ?? (speciesValue as? NSNumber).map { $0.intValue }
            let indexValue = creature["index"]
            let index = (indexValue as? UInt32).map { Int($0) }
                ?? (indexValue as? Int)
                ?? (indexValue as? NSNumber).map { Int($0.uintValue) }
                ?? 0
            guard let species = species,
                  let mesh = store.mesh(forSpecies: species) ?? store.meshByIndex(index),
                  let x = creature["x"] as? Float,
                  let y = creature["y"] as? Float
            else { continue }

            let frameCount = mesh.frameCount
            let phase = Float(index) * 0.37
            let frame = Int(max(0, uniforms.time * animationFPS + phase)) % frameCount
            let cell = (indexValue as? UInt32).map { Float($0) }
                ?? (indexValue as? NSNumber).map { Float($0.floatValue) }
                ?? Float(32 + (species % 16))
            placements.append(Placement(mesh: mesh, frame: frame,
                                        x: x + 0.5, y: y + 0.5, cell: cell))
        }
        guard !placements.isEmpty else { return }

        // Painter's order keeps overlapping meshes plausible when no depth
        // buffer is attached.
        placements.sort { $0.y < $1.y }

        var localUniforms = uniforms
        var meshUniforms = DK2MeshUniforms(
            viewProjection: DK2MeshRenderer.viewProjection(for: uniforms, tilt: tilt),
            time: uniforms.time)

        let activePipeline = depthEnabled ? (depthPipeline ?? pipeline) : pipeline
        encoder.setRenderPipelineState(activePipeline)
        if depthEnabled, let state = depthState {
            encoder.setDepthStencilState(state)
        }
        encoder.setVertexBytes(&localUniforms, length: MemoryLayout<DK2Uniforms>.size,
                               index: 0)
        encoder.setVertexBytes(&meshUniforms, length: MemoryLayout<DK2MeshUniforms>.size,
                               index: 2)
        encoder.setFragmentTexture(atlas, index: 0)
        encoder.setFragmentSamplerState(sampler, index: 0)

        let scale: Float = 2.4
        for placement in placements {
            var transform = SIMD4<Float>(placement.x, placement.y, scale, placement.cell)
            encoder.setVertexBytes(&transform, length: MemoryLayout<SIMD4<Float>>.stride,
                                   index: 1)
            encoder.setVertexBuffer(placement.mesh.vertexBuffer(frame: placement.frame),
                                    offset: 0, index: 0)
            encoder.drawIndexedPrimitives(
                type: .triangle,
                indexCount: placement.mesh.indexCount(frame: placement.frame),
                indexType: .uint16,
                indexBuffer: placement.mesh.indexBuffer(frame: placement.frame),
                indexBufferOffset: 0)
        }
    }

    private static func vertexDescriptor() -> MTLVertexDescriptor {
        let descriptor = MTLVertexDescriptor()
        if let layout = descriptor.layouts[0] {
            layout.stride = MemoryLayout<DK2MeshVertex>.stride
        }
        descriptor.attributes[0].format = .float4
        descriptor.attributes[0].offset = 0
        descriptor.attributes[0].bufferIndex = 0
        descriptor.attributes[1].format = .float2
        descriptor.attributes[1].offset = 16
        descriptor.attributes[1].bufferIndex = 0
        return descriptor
    }

    /// Builds a perspective projection whose ground-plane scale at z == 0
    /// matches the tile renderer (zoom / 13), plus a camera tilted slightly
    /// over the ground plane so model height becomes visible.
    private static func viewProjection(for uniforms: DK2Uniforms,
                                       tilt: Float) -> simd_float4x4 {
        let aspect = max(uniforms.aspect, 0.001)
        let zoom = max(uniforms.zoom, 0.001)
        let cameraHeight: Float = 12.0
        let focal = cameraHeight * zoom / 13.0
        let near: Float = 0.1
        let far: Float = 100.0
        let depthScale = 1.0 / (near - far)
        let depthBias = near * depthScale

        var projection = matrix_identity_float4x4
        projection.columns.0 = SIMD4<Float>(focal / aspect, 0, 0, 0)
        projection.columns.1 = SIMD4<Float>(0, focal, 0, 0)
        projection.columns.2 = SIMD4<Float>(0, 0, depthScale, -1)
        projection.columns.3 = SIMD4<Float>(0, 0, depthBias, 0)

        let c = cos(uniforms.yaw), s = sin(uniforms.yaw)
        let ct = cos(tilt), st = sin(tilt)
        let cx = uniforms.center.x, cy = uniforms.center.y

        var view = matrix_identity_float4x4
        view.columns.0 = SIMD4<Float>(c, -s * ct, s * st, 0)
        view.columns.1 = SIMD4<Float>(s, c * ct, -c * st, 0)
        view.columns.2 = SIMD4<Float>(0, st, ct, 0)
        view.columns.3 = SIMD4<Float>(-(c * cx + s * cy),
                                     ct * (s * cx - c * cy),
                                     -st * (s * cx - c * cy) - cameraHeight,
                                     1)

        return projection * view
    }
}
