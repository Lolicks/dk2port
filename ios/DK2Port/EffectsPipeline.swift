//
//  EffectsPipeline.swift
//  DK2Port
//
//  Enhanced Metal effects layer. The original DirectDraw renderer combined
//  textured 3D geometry with a small set of hand-tuned effects. This file
//  reproduces the *look* with the following deliberate approximations:
//
//  - Bump maps: the shipped BumpWater*/BumpLava* images are height fields, not
//    tangent-space normals. Normals are derived in the shader with a finite
//    difference over a second 8x8 atlas. If no bump PNG exists the atlas is
//    filled with neutral grey, which degrades to a flat (0,0,1) normal.
//  - Water / lava: the original engine scrolled and blended several bump
//    frames. Here a two-frequency sine warp of the atlas UV plus a single
//    bump lookup is used; it is animated and reactive to the bump data but
//    is not frame-identical to DK2.
//  - Lighting: one fixed directional light plus specular is applied to tiles
//    and creature meshes. There are no dynamic or point lights.
//  - Shadows: there is no shadow map. Tiles use the per-tile shadow hint when
//    supplied by the bridge; creature meshes use a cheap contact-shadow term
//    derived from local height. These are stylistic contact shadows only.
//  - Fog: distance from the camera centre is faded into a constant fog colour.
//    The original fog used a range table keyed by tile surface flags.
//  - Fog of war: the overlay reads the tile `flags` byte, bit 0x80 (revealed).
//    The current ObjC bridge does not forward that byte, so when it is absent
//    a camera-centred visibility bubble plus owned tiles is used as a fallback.
//    Exposing `t->flags` in `tileInstances` would make this exact.
//  - Creature geometry: this file is the single projection / mesh pipeline.
//    It reuses DK2MeshStore; the earlier DK2MeshRenderer duplicate was removed.
//    Morph poses, per-species placement and normals come from the DKAF v2 or
//    OBJ export; animation runs at the game tick rate of 30 fps and freezes on
//    dk2.command.pause.
//

import Foundation
import Metal
import MetalKit
import simd

// MARK: - Shader-facing data layouts

/// 80 bytes. Must match `MeshUniforms` in Shaders.metal field for field.
struct DK2MeshUniforms {
    var viewProjection: simd_float4x4 = matrix_identity_float4x4
    var time: Float = 0
    var pad0: Float = 0
    var pad1: Float = 0
    var pad2: Float = 0
}

/// 80 bytes. Must match `EffectsUniforms` in Shaders.metal field for field.
struct DK2EffectsUniforms {
    var lightDirection: SIMD4<Float>   // xyz = toward light, w = intensity
    var lightColor: SIMD4<Float>       // rgb = colour, w = ambient
    var fog: SIMD4<Float>              // rgb = colour, w = density
    var shading: SIMD4<Float>          // x bump, y shadow, z spec power, w spec strength
    var waves: SIMD4<Float>            // x water speed, y lava speed, z UV distortion, w spare

    static let `default` = DK2EffectsUniforms(
        lightDirection: SIMD4<Float>(0.35, 0.42, 0.84, 0.55),
        lightColor: SIMD4<Float>(1.0, 0.94, 0.82, 0.55),
        fog: SIMD4<Float>(0.045, 0.035, 0.06, 0.016),
        shading: SIMD4<Float>(2.6, 0.45, 18.0, 0.35),
        waves: SIMD4<Float>(1.0, 1.6, 1.0, 0)
    )
}

/// 32 bytes. Must match `FogInstance` in Shaders.metal.
struct DK2FogInstance {
    var worldSize: SIMD4<Float>
    var shade: SIMD4<Float>
}

// MARK: - Bump atlas

/// Second 8x8 atlas. Cell 0 holds the water height map, cell 1 the lava one;
/// all other cells are neutral grey. Missing PNGs are not an error.
final class DK2BumpAtlas {
    static let shared = DK2BumpAtlas()

    private(set) var texture: MTLTexture?
    private(set) var hasBumpData = false

    func build(device: MTLDevice, assetRoot: URL) {
        let size = 512
        let cell = 64
        guard let context = CGContext(data: nil, width: size, height: size,
                                      bitsPerComponent: 8, bytesPerRow: size * 4,
                                      space: CGColorSpaceCreateDeviceRGB(),
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
        else { return }

        // Neutral height: the shader's finite difference is zero => flat normal.
        context.setFillColor(CGColor(red: 0.5, green: 0.5, blue: 0.5, alpha: 1))
        context.fill(CGRect(x: 0, y: 0, width: size, height: size))

        let directories = [
            assetRoot.appendingPathComponent("textures/EngineTextures"),
            assetRoot.appendingPathComponent("Data/EngineTextures"),
            assetRoot.appendingPathComponent("EngineTextures"),
            assetRoot.deletingLastPathComponent().appendingPathComponent("Data/EngineTextures")
        ]

        func findFile(_ prefixes: [String]) -> String? {
            for directory in directories {
                let files = (try? FileManager.default.contentsOfDirectory(atPath: directory.path)) ?? []
                for prefix in prefixes {
                    if let match = files.first(where: {
                        $0.hasPrefix(prefix) && $0.lowercased().hasSuffix(".png")
                    }) {
                        return directory.appendingPathComponent(match).path
                    }
                }
            }
            return nil
        }

        func draw(_ path: String, at index: Int) -> Bool {
            guard let image = UIImage(contentsOfFile: path)?.cgImage else { return false }
            let x = (index % 8) * cell
            let y = (index / 8) * cell
            context.draw(image, in: CGRect(x: x, y: y, width: cell, height: cell))
            return true
        }

        let water = findFile(["BumpWaterBumpmap", "BumpWater0", "BumpWater"])
        let lava = findFile(["BumpLavaBumpmap", "BumpLava0", "BumpLava"])
        if let path = water { hasBumpData = draw(path, at: 0) || hasBumpData }
        if let path = lava { hasBumpData = draw(path, at: 1) || hasBumpData }

        guard let cg = context.makeImage(),
              let provider = cg.dataProvider,
              let cfData = provider.data,
              let bytes = CFDataGetBytePtr(cfData)
        else { return }

        let descriptor = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .rgba8Unorm, width: size, height: size, mipmapped: false)
        descriptor.usage = .shaderRead
        texture = device.makeTexture(descriptor: descriptor)
        texture?.replace(region: MTLRegionMake2D(0, 0, size, size), mipmapLevel: 0,
                         withBytes: bytes, bytesPerRow: size * 4)
    }
}

// MARK: - Effects renderer

private enum DK2BlendMode {
    case none
    case additive
    case alpha
}

/// Owns every depth-aware pipeline: tiles, lit creature meshes, particles and
/// the fog-of-war overlay. Public entry points are the four `draw...` methods.
final class DK2EffectsRenderer {
    var effects = DK2EffectsUniforms.default
    /// View-only camera tilt; per-species Z scale/offset come from ANIM/HEAD
    /// via DK2MeshPlacement and are applied per mesh below.
    private static let cameraTilt: Float = 0.32

    private let device: MTLDevice
    private let atlas: MTLTexture
    private let bumpAtlas: MTLTexture
    private let sampler: MTLSamplerState

    private let tilePipeline: MTLRenderPipelineState
    private let meshPipeline: MTLRenderPipelineState
    private let particlePipeline: MTLRenderPipelineState
    private let fogPipeline: MTLRenderPipelineState

    private let depthWriteState: MTLDepthStencilState?
    private let depthReadState: MTLDepthStencilState?
    private let alwaysState: MTLDepthStencilState?

    private let meshStore: DK2MeshStore
    private var settingsToken: NSObjectProtocol?

    private static let fallbackCreatureNames: [String] = [
        "Imp", "Goblin", "Skeleton", "Troll", "Bile Demon", "Black Knight",
        "Dark Angel", "Warlock", "Wizard", "Knight", "Giant", "Fairy",
        "Vampire", "Rogue", "Dwarf", "Elf"
    ]

    init?(device: MTLDevice, library: MTLLibrary, colorFormat: MTLPixelFormat,
          atlas: MTLTexture, bumpAtlas: MTLTexture, assetRoot: URL) {
        guard let tilePipeline = Self.makePipeline(device: device, library: library,
                                                   colorFormat: colorFormat,
                                                   vertex: "instance_vertex",
                                                   fragment: "instance_fragment",
                                                   blend: .none),
              let meshPipeline = Self.makePipeline(
                  device: device, library: library, colorFormat: colorFormat,
                  vertex: "lit_mesh_vertex", fragment: "lit_mesh_fragment",
                  vertexDescriptor: Self.meshVertexDescriptor(), blend: .none),
              let particlePipeline = Self.makePipeline(device: device, library: library,
                                                       colorFormat: colorFormat,
                                                       vertex: "particle_vertex",
                                                       fragment: "particle_fragment",
                                                       blend: .additive),
              let fogPipeline = Self.makePipeline(device: device, library: library,
                                                  colorFormat: colorFormat,
                                                  vertex: "fog_vertex",
                                                  fragment: "fog_fragment",
                                                  blend: .alpha)
        else { return nil }

        let samplerDescriptor = MTLSamplerDescriptor()
        samplerDescriptor.minFilter = .nearest
        samplerDescriptor.magFilter = .nearest
        samplerDescriptor.sAddressMode = .clampToEdge
        samplerDescriptor.tAddressMode = .clampToEdge
        guard let sampler = device.makeSamplerState(descriptor: samplerDescriptor)
        else { return nil }

        self.device = device
        self.atlas = atlas
        self.bumpAtlas = bumpAtlas
        self.sampler = sampler
        self.tilePipeline = tilePipeline
        self.meshPipeline = meshPipeline
        self.particlePipeline = particlePipeline
        self.fogPipeline = fogPipeline
        self.depthWriteState = DK2DepthSupport.depthState(device: device, write: true)
        self.depthReadState = DK2DepthSupport.depthState(device: device, write: false)
        self.alwaysState = DK2DepthSupport.depthState(device: device, write: false,
                                                      compare: .always)

        let names = DK2AssetLoader.gameData?.creatureNames ?? Self.fallbackCreatureNames
        self.meshStore = DK2MeshStore(device: device, assetRoot: assetRoot,
                                      creatureNames: names)

        settingsToken = NotificationCenter.default.addObserver(
            forName: DK2Settings.renderingDidChange, object: nil, queue: .main)
            { [weak self] _ in self?.applyRenderSettings() }
        applyRenderSettings()
    }

    // MARK: Pipeline construction

    private func applyRenderSettings() {
        effects.fog.w = 0.016 / max(0.2, DK2Settings.viewDistance)
        effects.shading.y = DK2Settings.shadows ? 0.45 : 0.0
        effects.lightColor.w = 0.55 * DK2Settings.gamma
    }

    private static func makePipeline(
        device: MTLDevice,
        library: MTLLibrary,
        colorFormat: MTLPixelFormat,
        vertex: String,
        fragment: String,
        vertexDescriptor: MTLVertexDescriptor? = nil,
        blend: DK2BlendMode
    ) -> MTLRenderPipelineState? {
        guard let vertexFunction = library.makeFunction(name: vertex),
              let fragmentFunction = library.makeFunction(name: fragment)
        else { return nil }

        let descriptor = MTLRenderPipelineDescriptor()
        descriptor.vertexFunction = vertexFunction
        descriptor.fragmentFunction = fragmentFunction
        _ = DK2DepthSupport.configurePipeline(descriptor, colorFormat: colorFormat)
        if let vertexDescriptor = vertexDescriptor {
            descriptor.vertexDescriptor = vertexDescriptor
        }

        let attachment = descriptor.colorAttachments[0]!
        switch blend {
        case .none:
            attachment.isBlendingEnabled = false
        case .additive:
            attachment.isBlendingEnabled = true
            attachment.sourceRGBBlendFactor = .sourceAlpha
            attachment.destinationRGBBlendFactor = .one
            attachment.rgbBlendOperation = .add
            attachment.sourceAlphaBlendFactor = .one
            attachment.destinationAlphaBlendFactor = .one
            attachment.alphaBlendOperation = .add
        case .alpha:
            attachment.isBlendingEnabled = true
            attachment.sourceRGBBlendFactor = .sourceAlpha
            attachment.destinationRGBBlendFactor = .oneMinusSourceAlpha
            attachment.rgbBlendOperation = .add
            attachment.sourceAlphaBlendFactor = .one
            attachment.destinationAlphaBlendFactor = .oneMinusSourceAlpha
            attachment.alphaBlendOperation = .add
        }
        return try? device.makeRenderPipelineState(descriptor: descriptor)
    }

    private static func meshVertexDescriptor() -> MTLVertexDescriptor {
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
        descriptor.attributes[2].format = .float3
        descriptor.attributes[2].offset = 24
        descriptor.attributes[2].bufferIndex = 0
        return descriptor
    }

    // MARK: Draw passes

    func drawTiles(encoder: MTLRenderCommandEncoder, uniforms: DK2Uniforms,
                   instanceBuffer: MTLBuffer, instanceCount: Int) {
        guard instanceCount > 0 else { return }
        var localUniforms = uniforms
        var localEffects = effects

        encoder.setRenderPipelineState(tilePipeline)
        if let state = depthWriteState { encoder.setDepthStencilState(state) }
        encoder.setVertexBytes(&localUniforms, length: MemoryLayout<DK2Uniforms>.size,
                               index: 0)
        encoder.setVertexBuffer(instanceBuffer, offset: 0, index: 1)
        encoder.setVertexBytes(&localEffects, length: MemoryLayout<DK2EffectsUniforms>.size,
                               index: 2)
        encoder.setFragmentBytes(&localUniforms, length: MemoryLayout<DK2Uniforms>.size,
                                 index: 0)
        encoder.setFragmentBytes(&localEffects, length: MemoryLayout<DK2EffectsUniforms>.size,
                                 index: 2)
        encoder.setFragmentTexture(atlas, index: 0)
        encoder.setFragmentTexture(bumpAtlas, index: 1)
        encoder.setFragmentSamplerState(sampler, index: 0)
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 6,
                               instanceCount: instanceCount)
    }

    func drawMeshes(encoder: MTLRenderCommandEncoder, uniforms: DK2Uniforms,
                    creatures: [[AnyHashable: Any]]) {
        guard !creatures.isEmpty else { return }

        struct Placement {
            let mesh: DK2Mesh
            let frame: Int
            let x: Float
            let y: Float
            let cell: Float
            let scale: Float
            let offset: SIMD3<Float>
        }

        let clock = DK2AnimationClock.shared
        let animationTime = clock.time(uniforms.time)
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
                  let mesh = meshStore.mesh(forSpecies: species) ?? meshStore.meshByIndex(index),
                  let x = creature["x"] as? Float,
                  let y = creature["y"] as? Float,
                  mesh.frameCount > 0
            else { continue }

            let phase = Float(index) * 0.37
            let fps = mesh.placement.fps
            let frame = Int(max(0, animationTime * fps + phase)) % mesh.frameCount
            let cell = (indexValue as? UInt32).map { Float($0) }
                 ?? (indexValue as? NSNumber).map { Float($0.floatValue) }
                 ?? Float(32 + (species % 16))
            placements.append(Placement(mesh: mesh, frame: frame,
                                        x: x + 0.5, y: y + 0.5, cell: cell,
                                        scale: mesh.placement.scale,
                                        offset: mesh.placement.offset))
        }
        guard !placements.isEmpty else { return }
        placements.sort { $0.y < $1.y }

        var localUniforms = uniforms
        var localEffects = effects
        var meshUniforms = DK2MeshUniforms(
            viewProjection: Self.viewProjection(for: uniforms,
                                                tilt: Self.cameraTilt),
            time: animationTime)

        encoder.setRenderPipelineState(meshPipeline)
        if let state = depthWriteState { encoder.setDepthStencilState(state) }
        encoder.setVertexBytes(&localUniforms, length: MemoryLayout<DK2Uniforms>.size,
                               index: 0)
        encoder.setVertexBytes(&meshUniforms, length: MemoryLayout<DK2MeshUniforms>.size,
                               index: 2)
        encoder.setVertexBytes(&localEffects, length: MemoryLayout<DK2EffectsUniforms>.size,
                               index: 3)
        encoder.setFragmentBytes(&localEffects, length: MemoryLayout<DK2EffectsUniforms>.size,
                                 index: 3)
        encoder.setFragmentTexture(atlas, index: 0)
        encoder.setFragmentSamplerState(sampler, index: 0)

        for placement in placements {
            var transform = SIMD4<Float>(placement.x + placement.offset.x,
                                         placement.y + placement.offset.y,
                                         placement.scale, placement.cell)
            var originZ = SIMD4<Float>(placement.offset.z, 0, 0, 0)
            encoder.setVertexBytes(&transform, length: MemoryLayout<SIMD4<Float>>.stride,
                                   index: 1)
            encoder.setVertexBytes(&originZ, length: MemoryLayout<SIMD4<Float>>.stride,
                                   index: 4)
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

    func drawParticles(encoder: MTLRenderCommandEncoder, uniforms: DK2Uniforms,
                       buffer: MTLBuffer, count: Int) {
        guard count > 0 else { return }
        var localUniforms = uniforms
        var localEffects = effects

        encoder.setRenderPipelineState(particlePipeline)
        if let state = depthReadState { encoder.setDepthStencilState(state) }
        encoder.setVertexBytes(&localUniforms, length: MemoryLayout<DK2Uniforms>.size,
                               index: 0)
        encoder.setVertexBuffer(buffer, offset: 0, index: 0)
        encoder.setFragmentBytes(&localEffects, length: MemoryLayout<DK2EffectsUniforms>.size,
                                 index: 0)
        encoder.drawPrimitives(type: .point, vertexStart: 0, vertexCount: count)
    }

    func drawFog(encoder: MTLRenderCommandEncoder, uniforms: DK2Uniforms,
                 instanceBuffer: MTLBuffer, instanceCount: Int) {
        guard instanceCount > 0 else { return }
        var localUniforms = uniforms

        encoder.setRenderPipelineState(fogPipeline)
        if let state = alwaysState { encoder.setDepthStencilState(state) }
        encoder.setVertexBytes(&localUniforms, length: MemoryLayout<DK2Uniforms>.size,
                               index: 0)
        encoder.setVertexBuffer(instanceBuffer, offset: 0, index: 1)
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 6,
                               instanceCount: instanceCount)
    }

    // MARK: Fog of war

    /// Reads the tile flags byte (bit 0x80 = revealed). The bridge currently
    /// omits that byte, so a camera-centred bubble + owned tiles is used.
    static func fogInstances(from tiles: [[AnyHashable: Any]],
                             center: SIMD2<Float>) -> [DK2FogInstance] {
        var result: [DK2FogInstance] = []
        result.reserveCapacity(tiles.count)
        for tile in tiles {
            guard let x = tile["x"] as? Float, let y = tile["y"] as? Float else { continue }

            var raw = tile["flags"]
            if raw == nil { raw = tile["tileFlags"] }

            var revealed: Bool
            if let raw = raw {
                let value = (raw as? NSNumber)?.uint8Value
                    ?? (raw as? UInt8).map { $0 }
                    ?? (raw as? Int).map { UInt8(truncatingIfNeeded: $0) }
                    ?? 0
                revealed = (value & 0x80) != 0
            } else if let explicit = tile["revealed"] as? Bool {
                revealed = explicit
            } else {
                let owner = (tile["owner"] as? UInt8).map { Int($0) }
                    ?? (tile["owner"] as? NSNumber)?.intValue ?? 0
                let dx = x - center.x
                let dy = y - center.y
                revealed = owner != 0 || (dx * dx + dy * dy) <= 36.0
            }
            if revealed { continue }

            result.append(DK2FogInstance(
                worldSize: SIMD4<Float>(x + 0.5, y + 0.5, 1, 1),
                shade: SIMD4<Float>(0.02, 0.015, 0.03, 0.9)))
        }
        return result
    }

    // MARK: Camera

    /// Same ground-plane scale as the 2D tile shader, plus a small tilt so
    /// creature height is visible. The only viewProjection implementation.
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
