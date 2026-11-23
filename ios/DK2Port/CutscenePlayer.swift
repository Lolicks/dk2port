import Foundation
import MetalKit

/// Plays pre-decoded original cutscene frames. The PNG sequence is produced
/// from the shipped .tgq by the verified TQI decoder (C/Java), so every pixel
/// comes from the original game.
final class DK2CutscenePlayer {
    private let device: MTLDevice
    private var frames: [MTLTexture] = []
    private var index = 0

    init?(device: MTLDevice, assetRoot: URL, name: String) {
        self.device = device
        let dir = assetRoot.appendingPathComponent("movies/\(name)")
        let files = (try? FileManager.default.contentsOfDirectory(atPath: dir.path))?
            .filter { $0.hasSuffix(".png") }.sorted() ?? []
        let loader = MTKTextureLoader(device: device)
        for file in files {
            let url = dir.appendingPathComponent(file)
            guard let tex = try? loader.newTexture(URL: url, options: nil) else { continue }
            frames.append(tex)
        }
        if frames.isEmpty { return nil }
    }

    var count: Int { frames.count }
    var current: MTLTexture { frames[index % frames.count] }

    func advance() {
        index = (index + 1) % max(1, frames.count)
    }
}
