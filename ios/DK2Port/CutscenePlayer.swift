import AVFoundation
import Combine
import Foundation
import UIKit

/// Kinds of original cutscenes that can be requested from UI or game logic.
enum DK2CutsceneKind: String {
    case intro, outro, trailer, level
}

/// Cutscene requests are posted on NotificationCenter.default with object: nil.
/// Payload keys are `name` (folder / TGQ base name) and `kind` (raw value).
enum DK2Cutscene {
    static let playRequest = Notification.Name("dk2.cutscene.play")

    enum Key {
        static let name = "name"
        static let kind = "kind"
    }

    static func request(_ kind: DK2CutsceneKind, name: String? = nil) {
        let resolved = name ?? defaultName(for: kind)
        NotificationCenter.default.post(name: playRequest, object: nil, userInfo: [
            Key.name: resolved,
            Key.kind: kind.rawValue
        ])
    }

    static func defaultName(for kind: DK2CutsceneKind) -> String {
        switch kind {
        case .intro: return "BullfrogIntro"
        case .outro: return "Outro"
        case .trailer: return "trailer"
        case .level: return "CutSceneLevel1"
        }
    }

    /// The decoded frame folders use the TGQ base name; the original movie is
    /// looked up under Data/Movies in the user's original installation.
    static func tgqNames(for name: String) -> [String] {
        let base = (name as NSString).lastPathComponent
        if base.lowercased().hasSuffix(".tgq") {
            return [base]
        }
        return [base + ".tgq", (base as NSString).appendingPathExtension("TGQ") ?? base]
    }

    static func audioURL(for name: String) -> URL? {
        for candidate in tgqNames(for: name) {
            if let url = DK2OriginalAssets.movieAudioWAV(candidate) {
                return url
            }
        }
        return nil
    }
}

/// Drives a pre-decoded PNG sequence from the original TGQ. Video frame timing
/// follows the decoded TGQ audio clock (OpenKeeper TgqPlayer: 25 fps, 40 ms
/// frame interval); frames skipped by a late clock are dropped instead of
/// playing back at a fixed Timer rate.
final class DK2CutscenePlayer: NSObject, ObservableObject, AVAudioPlayerDelegate {
    static let framesPerSecond: Double = 25.0
    static let frameInterval: TimeInterval = 1.0 / 25.0

    @Published private(set) var frame: Int = 0
    @Published private(set) var hasAudio = false
    private(set) var droppedFrames = 0
    private(set) var playbackDuration: TimeInterval = 0

    private let name: String
    private var images: [UIImage] = []
    private var audioPlayer: AVAudioPlayer?
    private var poll: Timer?
    private var wallStart: TimeInterval = 0
    private var completion: (() -> Void)?
    private var finished = false

    init(name: String) {
        self.name = name
        super.init()
    }

    var frameCount: Int { images.count }

    var currentImage: UIImage? {
        guard images.indices.contains(frame) else { return nil }
        return images[frame]
    }

    func start(onComplete: @escaping () -> Void) {
        guard poll == nil else { return }
        completion = onComplete
        finished = false
        loadFrames()
        startAudio()
        let videoDuration = Double(max(0, images.count - 1)) * DK2CutscenePlayer.frameInterval
        playbackDuration = Swift.max(videoDuration, audioPlayer?.duration ?? 0)
        wallStart = Date().timeIntervalSinceReferenceDate
        let timer = Timer(timeInterval: 1.0 / 60.0, target: self,
                          selector: #selector(tick), userInfo: nil, repeats: true)
        RunLoop.main.add(timer, forMode: .common)
        poll = timer
        tick()
    }

    private func loadFrames() {
        let dir = DK2AssetLoader.root
            .appendingPathComponent("movies")
            .appendingPathComponent(name)
        let files = (try? FileManager.default.contentsOfDirectory(atPath: dir.path))?
            .filter { $0.lowercased().hasSuffix(".png") }
            .sorted { DK2CutscenePlayer.frameNumber($0) < DK2CutscenePlayer.frameNumber($1) }
            ?? []
        images = files.compactMap {
            UIImage(contentsOfFile: dir.appendingPathComponent($0).path)
        }
    }

    static func frameNumber(_ file: String) -> Int {
        let digits = String(file.filter { $0.isNumber })
        return Int(digits) ?? 0
    }

    private func startAudio() {
        guard let wav = DK2Cutscene.audioURL(for: name),
              let player = try? AVAudioPlayer(contentsOf: wav) else { return }
        audioPlayer = player
        player.delegate = self
        player.prepareToPlay()
        player.play()
        hasAudio = true
    }

    private var elapsed: TimeInterval {
        if let player = audioPlayer, player.isPlaying {
            return player.currentTime
        }
        return Date().timeIntervalSinceReferenceDate - wallStart
    }

    @objc private func tick() {
        guard !finished else { return }
        let target = Int(floor(elapsed / DK2CutscenePlayer.frameInterval))
        if target > frame {
            droppedFrames += target - frame - 1
            frame = min(target, max(0, images.count - 1))
        }
        if playbackDuration > 0 {
            if elapsed >= playbackDuration { finish() }
        } else {
            finish()
        }
    }

    func audioPlayerDidFinishPlaying(_ player: AVAudioPlayer, successfully flag: Bool) {
        if images.isEmpty { finish() }
    }

    func stop() {
        poll?.invalidate()
        poll = nil
        audioPlayer?.stop()
        audioPlayer = nil
        completion = nil
    }

    private func finish() {
        guard !finished else { return }
        finished = true
        poll?.invalidate()
        poll = nil
        audioPlayer?.stop()
        audioPlayer = nil
        let done = completion
        completion = nil
        done?()
    }

    deinit {
        poll?.invalidate()
    }
}
