import Foundation
import Metal

/// Draws static MESH geometry exported from the original KMF files.
/// It is deliberately separate from the tile renderer so a failure here
/// cannot break the working 2D fallback.
final class DK2MeshRenderer {
    private let device: MTLDevice
    private let pipeline: MTLRenderPipelineState
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

        let vertexDescriptor = MTLVertexDescriptor()
        descriptor.vertexDescriptor = vertexDescriptor
        let layout = vertexDescriptor.layouts[0]!
        layout.stride = 24
        vertexDescriptor.attributes[0].format = MTLVertexFormat.float4
        vertexDescriptor.attributes[0].offset = 0
        vertexDescriptor.attributes[0].bufferIndex = 0
        vertexDescriptor.attributes[1].format = MTLVertexFormat.float2
        vertexDescriptor.attributes[1].offset = 16
        vertexDescriptor.attributes[1].bufferIndex = 0

        guard let pipeline = try? device.makeRenderPipelineState(descriptor: descriptor) else {
            return nil
        }
        let s = MTLSamplerDescriptor()
        s.minFilter = .nearest
        s.magFilter = .nearest
        s.sAddressMode = .clampToEdge
        s.tAddressMode = .clampToEdge
        guard let sampler = device.makeSamplerState(descriptor: s) else { return nil }

        self.device = device
        self.pipeline = pipeline
        self.atlas = atlas
        self.sampler = sampler
        self.store = DK2MeshStore(device: device, assetRoot: assetRoot,
                                  names: ["Imp", "Skeleton", "Troll", "DarkElf", "Vampire"])
    }

    func draw(encoder: MTLRenderCommandEncoder,
              uniforms: DK2Uniforms,
              creatures: [[AnyHashable: Any]]) {
        guard !creatures.isEmpty else { return }
        var localUniforms = uniforms
        encoder.setRenderPipelineState(pipeline)
        encoder.setVertexBytes(&localUniforms, length: MemoryLayout<DK2Uniforms>.size, index: 0)
        encoder.setFragmentTexture(atlas, index: 0)
        encoder.setFragmentSamplerState(sampler, index: 0)

        var placed = 0
        for creature in creatures {
            guard let species = creature["species"] as? Int32,
                  let mesh = store.mesh(forSpecies: Int(species)),
                  let buffer = mesh.vertexBuffer,
                  let x = creature["x"] as? Float,
                  let y = creature["y"] as? Float else { continue }
            var placement = SIMD4<Float>(x + 0.5, y + 0.5, 0.35, 0)
            encoder.setVertexBytes(&placement, length: 16, index: 1)
            encoder.setVertexBuffer(buffer, offset: 0, index: 0)
            encoder.drawPrimitives(type: .triangle,
                                   vertexStart: 0,
                                   vertexCount: mesh.vertices.count)
            placed += 1
        }
        if placed > 0 { /* meshes rendered */ }
    }
}
