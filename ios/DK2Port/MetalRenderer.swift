import Foundation
import Metal
import MetalKit
import simd
import UIKit

struct DK2Uniforms {
    var center: SIMD2<Float> = .zero
    var zoom: Float = 1.0
    var yaw: Float = 0.0
    var aspect: Float = 1.0
    var time: Float = 0.0
    var pad0: Float = 0.0
    var pad1: Float = 0.0
    var pad2: Float = 0.0
}

struct DK2Instance {
    var worldSize: SIMD4<Float>
    var tintIndex: SIMD4<Float>
    var effect: SIMD4<Float>
}

/// Builds an 8x8 atlas from the *original* PNG textures shipped in
/// EngineTextures.WAD. No procedural replacement art is used.
final class DK2Atlas {
    static let shared = DK2Atlas()
    private(set) var texture: MTLTexture?

    // Indices must match atlasIndexForTerrain() in DK2Bridge.mm.
    private let terrainFiles: [Int: String] = [
        0: "MapRock.png", 1: "MapRock.png", 2: "MapBack.PNG",
        3: "Map-BG.PNG", 4: "Map-BG.PNG", 5: "tba.png",
        6: "tba.png", 7: "MapRock.png", 8: "barricade.png",
        9: "Treasury.png", 10: "lair.png", 11: "portal.png",
        12: "hatchery.png", 13: "heart.png", 14: "dark_library.png",
        15: "training.png", 16: "Bridge_Wooden.png", 17: "guard_room.png",
        18: "GUI/Rooms/workshop.png", 19: "prison.png", 20: "torture.png",
        21: "graveyard.png", 22: "temple.png", 23: "pit.png",
        24: "pit.png",
    ]

    private let creatureFiles: [String] = [
        "imp.png", "goblin.png", "skeleton.png", "troll.png",
        "bile_demon.png", "black_knight.png", "dark_angel.png",
        "warlock.png", "wizard.png", "knight.png", "giant.png",
        "fairy.png", "vampire.png", "rogue.png", "dwarf.png", "elf.png",
    ]

    func build(device: MTLDevice, assetRoot: URL) {
        let size = 512
        let cell = 64
        let space = CGColorSpaceCreateDeviceRGB()
        guard let context = CGContext(data: nil, width: size, height: size,
                                      bitsPerComponent: 8, bytesPerRow: size * 4,
                                      space: space,
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
        else { return }
        context.setFillColor(CGColor(red: 0.05, green: 0.04, blue: 0.06, alpha: 1))
        context.fill(CGRect(x: 0, y: 0, width: size, height: size))

        func draw(_ name: String, at index: Int) {
            let path = assetRoot
                .appendingPathComponent("textures/EngineTextures")
                .appendingPathComponent(name)
            guard let image = UIImage(contentsOfFile: path.path)?.cgImage else { return }
            let x = (index % 8) * cell
            let y = (index / 8) * cell
            context.draw(image, in: CGRect(x: x, y: y, width: cell, height: cell))
        }

        for (index, file) in terrainFiles { draw(file, at: index) }
        for (i, file) in creatureFiles.enumerated() { draw(file, at: 32 + i) }

        guard let cg = context.makeImage(),
              let provider = cg.dataProvider,
              let cfData = provider.data,
              let bytes = CFDataGetBytePtr(cfData)
        else { return }
        let desc = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .rgba8Unorm, width: size, height: size, mipmapped: false)
        desc.usage = [.shaderRead]
        texture = device.makeTexture(descriptor: desc)
        texture?.replace(region: MTLRegionMake2D(0, 0, size, size), mipmapLevel: 0,
                         withBytes: bytes, bytesPerRow: size * 4)
    }
}

final class DK2MetalRenderer: NSObject, MTKViewDelegate {
    private let device: MTLDevice
    private let queue: MTLCommandQueue
    private let bridge: DK2Bridge
    private var uniforms = DK2Uniforms()
    private var instances: [DK2Instance] = []
    private var fogInstances: [DK2FogInstance] = []
    private var creatureData: [[AnyHashable: Any]] = []
    private let effectsRenderer: DK2EffectsRenderer
    private let particleBuffer: MTLBuffer?
    private let particleCount = 64
    private var instanceBuffer: MTLBuffer?
    private var fogBuffer: MTLBuffer?
    private var instanceCapacity = 0
    private var fogCapacity = 0

    init?(metalView: MTKView, bridge: DK2Bridge, assetRoot: URL) {
        guard let device = MTLCreateSystemDefaultDevice(),
              let queue = device.makeCommandQueue(),
              let library = device.makeDefaultLibrary()
        else { return nil }

        // Depth must be configured before the view creates its first texture.
        DK2DepthSupport.configureView(metalView)

        DK2Atlas.shared.build(device: device, assetRoot: assetRoot)
        DK2BumpAtlas.shared.build(device: device, assetRoot: assetRoot)

        guard let atlas = DK2Atlas.shared.texture,
              let bumpAtlas = DK2BumpAtlas.shared.texture,
              let effects = DK2EffectsRenderer(device: device, library: library,
                                               colorFormat: metalView.colorPixelFormat,
                                               atlas: atlas, bumpAtlas: bumpAtlas,
                                               assetRoot: assetRoot)
        else { return nil }

        self.device = device
        self.queue = queue
        self.bridge = bridge
        self.effectsRenderer = effects
        self.particleBuffer = Self.makeParticles(device: device)

        super.init()

        metalView.delegate = self
        metalView.clearColor = MTLClearColor(red: 0.04, green: 0.03, blue: 0.05, alpha: 1)
    }

    func reloadInstances() {
        var result: [DK2Instance] = []
        var fog: [DK2FogInstance] = []
        let tiles = bridge.tileInstances
        for tile in tiles {
            guard let x = tile["x"] as? Float, let y = tile["y"] as? Float,
                  let index = tile["index"] as? UInt32 else { continue }
            let owner = tile["owner"] as? UInt8 ?? 0
            let tint: SIMD4<Float> = owner == 0
                ? SIMD4<Float>(1, 1, 1, Float(index))
                : SIMD4<Float>(0.85, 0.75, 0.95, Float(index))
            let terrain = Int(tile["terrain"] as? Int32 ?? 0)

            var rawFlags = tile["flags"]
            if rawFlags == nil { rawFlags = tile["tileFlags"] }
            let flags = (rawFlags as? NSNumber)?.uint8Value
                ?? (rawFlags as? UInt8).map { $0 }
                ?? (rawFlags as? Int).map { UInt8(truncatingIfNeeded: $0) }
                ?? 0
            let shadow = (tile["shadow"] as? NSNumber)?.floatValue
                ?? ((flags & 0x40) != 0 ? 1.0 : 0.0)

            let fx = SIMD4<Float>(terrain == 3 ? 1 : (terrain == 4 ? 2 : 0),
                                  Float(index % 16) * 0.4, shadow, 0)
            result.append(DK2Instance(worldSize: SIMD4<Float>(x + 0.5, y + 0.5, 1, 1),
                                      tintIndex: tint, effect: fx))
        }
        creatureData = bridge.creatureInstances
        for creature in creatureData {
            guard let x = creature["x"] as? Float, let y = creature["y"] as? Float,
                  let index = creature["index"] as? UInt32 else { continue }
            result.append(DK2Instance(worldSize: SIMD4<Float>(x + 0.5, y + 0.5, 0.9, 0.9),
                                      tintIndex: SIMD4<Float>(1, 1, 1, Float(index)),
                                      effect: SIMD4<Float>(0, 0, 0, 0)))
        }
        instances = result
        instanceBuffer = upload(result, to: &instanceBuffer, capacity: &instanceCapacity)

        let snap = bridge.snapshot
        uniforms.center = SIMD2<Float>(snap["centerX"] as? Float ?? 0,
                                       snap["centerY"] as? Float ?? 0)
        uniforms.zoom = snap["zoom"] as? Float ?? 1
        uniforms.yaw = snap["yaw"] as? Float ?? 0
        uniforms.time = Float(Date().timeIntervalSince1970.truncatingRemainder(dividingBy: 1024.0))

        fog = DK2EffectsRenderer.fogInstances(from: tiles, center: uniforms.center)
        fogInstances = fog
        fogBuffer = upload(fog, to: &fogBuffer, capacity: &fogCapacity)
    }

    /// Grows a shared buffer on demand; instance arrays are far larger than
    /// the ~4 KB limit of setVertexBytes, so they always go through MTLBuffer.
    private func upload<T>(_ values: [T], to buffer: inout MTLBuffer?,
                           capacity: inout Int) -> MTLBuffer? {
        guard !values.isEmpty else { return nil }
        let length = values.count * MemoryLayout<T>.stride
        if capacity < length {
            capacity = max(length, capacity * 2)
            buffer = device.makeBuffer(length: capacity, options: .storageModeShared)
        }
        guard let target = buffer else { return nil }
        values.withUnsafeBytes { raw in
            if let base = raw.baseAddress {
                memcpy(target.contents(), base, length)
            }
        }
        return target
    }

    private static func makeParticles(device: MTLDevice) -> MTLBuffer? {
        struct P { var pos: SIMD4<Float>; var color: SIMD4<Float> }
        var data: [P] = []
        for i in 0..<64 {
            let a = Float(i) * 0.31
            data.append(P(pos: SIMD4<Float>(cos(a) * 3, sin(a) * 3,
                                            sin(a) * 0.4, cos(a) * 0.4 + 0.2),
                          color: SIMD4<Float>(1.0, 0.5 + 0.4 * sin(a), 0.1, 0.6)))
        }
        return device.makeBuffer(bytes: data,
                                 length: data.count * MemoryLayout<P>.stride,
                                 options: .storageModeShared)
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        uniforms.aspect = Float(size.width / max(1, size.height))
    }

    func draw(in view: MTKView) {
        reloadInstances()
        guard let drawable = view.currentDrawable,
              let pass = view.currentRenderPassDescriptor,
              let command = queue.makeCommandBuffer()
        else { return }

        DK2DepthSupport.configureRenderPass(pass)
        guard let encoder = command.makeRenderCommandEncoder(descriptor: pass)
        else { return }

        if let buffer = instanceBuffer {
            effectsRenderer.drawTiles(encoder: encoder, uniforms: uniforms,
                                      instanceBuffer: buffer,
                                      instanceCount: instances.count)
        }
        effectsRenderer.drawMeshes(encoder: encoder, uniforms: uniforms,
                                   creatures: creatureData)
        if let particles = particleBuffer {
            effectsRenderer.drawParticles(encoder: encoder, uniforms: uniforms,
                                          buffer: particles, count: particleCount)
        }
        if let buffer = fogBuffer {
            effectsRenderer.drawFog(encoder: encoder, uniforms: uniforms,
                                    instanceBuffer: buffer,
                                    instanceCount: fogInstances.count)
        }

        encoder.endEncoding()
        command.present(drawable)
        command.commit()
    }
}
