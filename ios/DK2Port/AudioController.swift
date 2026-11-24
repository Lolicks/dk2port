import AVFoundation
import Foundation

/// Runtime audio for the port. The original game used QMIXER.DLL with a full
/// 3D listener/source model. This adapter keeps four mixing buses and uses
/// AVAudioEnvironmentNode plus equal-power panning to approximate it.
final class DK2AudioController {
    enum Bus {
        case master, music, sfx, speech
    }

    private struct Voice {
        let player: AVAudioPlayerNode
        let mixer: AVAudioMixerNode
        let bus: Bus
    }

    private let engine = AVAudioEngine()
    private let masterBus = AVAudioMixerNode()
    private let musicBus = AVAudioMixerNode()
    private let sfxBus = AVAudioMixerNode()
    private let speechBus = AVAudioMixerNode()
    private let environment = AVAudioEnvironmentNode()
    private var voices: [String: Voice] = [:]
    private var buffers: [String: AVAudioPCMBuffer] = [:]
    private var listenerX: Float = 0
    private var listenerY: Float = 0
    private var listenerVX: Float = 0
    private var listenerVY: Float = 0
    private var worldSize: Float = 64

    init() {
        engine.attach(masterBus)
        engine.attach(musicBus)
        engine.attach(sfxBus)
        engine.attach(speechBus)
        engine.attach(environment)

        engine.connect(musicBus, to: masterBus, format: nil)
        engine.connect(speechBus, to: masterBus, format: nil)
        engine.connect(sfxBus, to: masterBus, format: nil)
        engine.connect(environment, to: sfxBus, format: environment.outputFormat(forBus: 0))
        engine.connect(masterBus, to: engine.mainMixerNode, format: nil)

        environment.distanceAttenuationParameters.rolloffFactor = 1
        applyVolumes()

        NotificationCenter.default.addObserver(
            self,
            selector: #selector(settingsDidChange),
            name: DK2Settings.audioDidChange,
            object: nil)
    }

    deinit {
        NotificationCenter.default.removeObserver(self)
    }

    @objc private func settingsDidChange() {
        applyVolumes()
    }

    private func applyVolumes() {
        masterBus.outputVolume = DK2Settings.masterVolume
        musicBus.outputVolume = DK2Settings.musicVolume
        sfxBus.outputVolume = DK2Settings.sfxVolume
        speechBus.outputVolume = DK2Settings.speechVolume
    }

    /// Explicit bus knob; callers normally set DK2Settings which posts
    /// `dk2.settings.audioDidChange` and reaches applyVolumes().
    func setVolume(_ value: Float, for bus: Bus) {
        let clamped = min(1.0, Swift.max(0.0, value))
        switch bus {
        case .master: masterBus.outputVolume = clamped
        case .music: musicBus.outputVolume = clamped
        case .sfx: sfxBus.outputVolume = clamped
        case .speech: speechBus.outputVolume = clamped
        }
    }

    func start() {
        guard !engine.isRunning else { return }
        do { try engine.start() } catch { NSLog("DK2 audio start failed: \(error)") }
    }

    func configure(worldWidth: Int, height: Int) {
        worldSize = Float(Swift.max(worldWidth, height))
    }

    /// Listener position and velocity in world-tile coordinates. Velocity feeds
    /// the environment node so Doppler/pan follow camera motion.
    func updateListener(x: Float, y: Float, vx: Float = 0, vy: Float = 0) {
        listenerX = x
        listenerY = y
        listenerVX = vx
        listenerVY = vy
        environment.listenerPosition = AVAudio3DPoint(x: x, y: 0, z: -y)
    }

    /// Loads one decoded sample from the imported bundle onto a bus.
    func preload(relativePath: String, key: String, bus: Bus = .sfx, positional: Bool = false) {
        guard voices[key] == nil, buffers[key] == nil else { return }
        let url = DK2AssetLoader.root.appendingPathComponent(relativePath)
        guard FileManager.default.fileExists(atPath: url.path),
              let file = try? AVAudioFile(forReading: url)
        else { return }
        let format = file.fileFormat
        guard let buffer = AVAudioPCMBuffer(
            pcmFormat: format,
            frameCapacity: AVAudioFrameCount(file.length))
        else { return }
        do {
            try file.read(into: buffer)
        } catch { return }

        let player = AVAudioPlayerNode()
        let mixer = AVAudioMixerNode()
        engine.attach(player)
        engine.attach(mixer)
        engine.connect(player, to: mixer, format: format)
        if positional {
            engine.connect(mixer, to: environment, format: nil)
        } else {
            let target: AVAudioMixerNode
            switch bus {
            case .music: target = musicBus
            case .speech: target = speechBus
            case .master, .sfx: target = sfxBus
            }
            engine.connect(mixer, to: target, format: nil)
        }
        voices[key] = Voice(player: player, mixer: mixer,
                            bus: positional ? .sfx : bus)
        buffers[key] = buffer
        start()
    }

    /// Loads and loops an original music track on the music bus.
    func playMusic(relativePath: String, key: String) {
        if let voice = voices[key] {
            start()
            if !voice.player.isPlaying { voice.player.play() }
            return
        }
        let url = DK2AssetLoader.root.appendingPathComponent(relativePath)
        guard FileManager.default.fileExists(atPath: url.path),
              let file = try? AVAudioFile(forReading: url),
              let buffer = AVAudioPCMBuffer(pcmFormat: file.fileFormat,
                                            frameCapacity: AVAudioFrameCount(file.length))
        else { return }
        do { try file.read(into: buffer) } catch { return }
        let player = AVAudioPlayerNode()
        let mixer = AVAudioMixerNode()
        engine.attach(player)
        engine.attach(mixer)
        engine.connect(player, to: mixer, format: file.fileFormat)
        engine.connect(mixer, to: musicBus, format: nil)
        voices[key] = Voice(player: player, mixer: mixer, bus: .music)
        buffers[key] = buffer
        start()
        player.scheduleBuffer(buffer, at: nil, options: .loops, completionHandler: nil)
        player.play()
    }

    /// Plays a sound positioned in world tile coordinates.
    func play(key: String, atX x: Float, y: Float, volume: Float = 1.0,
              positional: Bool = true) {
        guard let voice = voices[key], let buffer = buffers[key] else { return }
        // Look ahead along the listener's motion so panning does not lag the
        // camera. Positional voices use AVAudioEnvironmentNode Doppler instead.
        let lead: Float = 0.08
        let dx = x - (listenerX + listenerVX * lead)
        let dy = y - (listenerY + listenerVY * lead)
        let distance = sqrtf(dx * dx + dy * dy)
        let reference = Swift.max(4.0, worldSize * 0.15)
        let attenuation = 1.0 / (1.0 + distance / reference)

        if positional {
            voice.player.position = AVAudio3DPoint(x: x, y: 0, z: -y)
            voice.mixer.volume = min(1.0, Swift.max(0.0, volume * attenuation))
        } else {
            let pan = min(1.0, Swift.max(-1.0, dx / reference))
            voice.mixer.pan = pan
            let theta = (pan + 1.0) * Float.pi / 4.0
            let equalPower = (cosf(theta) + sinf(theta)) / sqrtf(2.0)
            voice.mixer.volume = min(1.0, Swift.max(0.0, volume * attenuation * equalPower))
        }
        voice.player.scheduleBuffer(buffer, at: nil, options: [], completionHandler: nil)
        if !voice.player.isPlaying { voice.player.play() }
    }

    func stopAll() {
        for voice in voices.values { voice.player.stop() }
    }
}
