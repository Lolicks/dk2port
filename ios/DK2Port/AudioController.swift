import AVFoundation
import Foundation

/// Runtime audio for the port. The original game used QMIXER.DLL with a full
/// 3D listener/source model. This adapter reproduces the observable parts
/// (distance attenuation + stereo panning) on top of AVAudioEngine; it is an
/// approximation, not a bit-exact port of QMIXER.
final class DK2AudioController {
    private let engine = AVAudioEngine()
    private var players: [String: AVAudioPlayerNode] = [:]
    private var buffers: [String: AVAudioPCMBuffer] = [:]
    private var mixer: [String: AVAudioMixerNode] = [:]
    private var listenerX: Float = 0
    private var listenerY: Float = 0
    private var worldSize: Float = 64

    init() {
        let main = engine.mainMixerNode
        main.outputVolume = 0.7
    }

    func start() {
        guard !engine.isRunning else { return }
        do { try engine.start() } catch { NSLog("DK2 audio start failed: \(error)") }
    }

    func configure(worldWidth: Int, height: Int) {
        worldSize = Float(max(width: worldWidth, height: height))
    }

    func updateListener(x: Float, y: Float) {
        listenerX = x
        listenerY = y
    }

    /// Loads one decoded sample from the imported bundle.
    func preload(relativePath: String, key: String) {
        if buffers[key] != nil { return }
        let url = DK2AssetLoader.root.appendingPathComponent(relativePath)
        guard FileManager.default.fileExists(atPath: url.path),
              let file = try? AVAudioFile(forReading: url),
              let format = AVAudioFormat(standardFormatWithSampleRate: file.fileFormat.sampleRate,
                                         channels: 2),
              let buffer = AVAudioPCMBuffer(
                  pcmFormat: format,
                  frameCapacity: AVAudioFrameCount(file.length))
        else { return }
        do {
            try file.read(into: buffer)
        } catch { return }

        let player = AVAudioPlayerNode()
        let node = AVAudioMixerNode()
        engine.attach(player)
        engine.attach(node)
        engine.connect(player, to: node, format: format)
        engine.connect(node, to: engine.mainMixerNode, format: format)
        players[key] = player
        mixer[key] = node
        buffers[key] = buffer
        start()
    }

    /// Plays a sound positioned in world tile coordinates.
    func play(key: String, atX x: Float, y: Float, volume: Float = 1.0) {
        guard let player = players[key], let buffer = buffers[key] else { return }
        let dx = x - listenerX, dy = y - listenerY
        let distance = sqrtf(dx * dx + dy * dy)
        let gain = volume / (1.0f + distance / max(4.0f, worldSize * 0.15f))
        let node = mixer[key]
        node?.volume = min(1.0, max(0.0, gain))
        node?.pan = min(1.0, max(-1.0, dx / max(4.0f, worldSize * 0.15f)))
        player.scheduleBuffer(buffer, at: nil, options: [], completionHandler: nil)
        if !player.isPlaying { player.play() }
    }

    func stopAll() {
        for player in players.values { player.stop() }
    }

    private func max(width: Int, height: Int) -> Int { Swift.max(width, height) }
}
