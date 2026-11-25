import AVFoundation
import Foundation

/// Stable audio backend using AVAudioPlayer only. The AVAudioEngine graph was
/// removed after it caused launch/level-start crashes on device.
final class DK2SimpleAudio {
    enum Bus {
        case master, music, sfx, speech
    }

    private var players: [String: AVAudioPlayer] = [:]
    private var listenerX: Float = 0
    private var listenerY: Float = 0
    private var worldSize: Float = 64

    func configure(worldWidth: Int, height: Int) {
        worldSize = Float(max(worldWidth, height))
    }

    func start() {}

    func updateListener(x: Float, y: Float, vx: Float = 0, vy: Float = 0) {
        listenerX = x
        listenerY = y
    }

    func setVolume(_ value: Float, for bus: Bus) {}

    func preload(relativePath: String, key: String, bus: Bus = .sfx, positional: Bool = false) {
        guard players[key] == nil else { return }
        let url = DK2AssetLoader.root.appendingPathComponent(relativePath)
        guard FileManager.default.fileExists(atPath: url.path),
              let player = try? AVAudioPlayer(contentsOf: url) else { return }
        player.prepareToPlay()
        players[key] = player
    }

    func play(key: String, atX x: Float, y: Float, volume: Float = 1.0,
              positional: Bool = true) {
        guard let player = players[key] else { return }
        let dx = x - listenerX
        let dy = y - listenerY
        let distance = sqrtf(dx * dx + dy * dy)
        let reference = max(4.0, worldSize * 0.15)
        let attenuation = 1.0 / (1.0 + distance / reference)
        let pan = max(-1.0, min(1.0, dx / reference))
        player.pan = pan
        player.volume = max(0.0, min(1.0,
            volume * attenuation * DK2Settings.masterVolume * DK2Settings.sfxVolume))
        player.currentTime = 0
        player.play()
    }

    func playMusic(relativePath: String, key: String) {
        let url = DK2AssetLoader.root.appendingPathComponent(relativePath)
        guard FileManager.default.fileExists(atPath: url.path),
              let player = try? AVAudioPlayer(contentsOf: url) else { return }
        player.numberOfLoops = -1
        player.volume = DK2Settings.masterVolume * DK2Settings.musicVolume
        player.prepareToPlay()
        players[key] = player
        player.play()
    }

    func stopAll() {
        for player in players.values { player.stop() }
    }
}
