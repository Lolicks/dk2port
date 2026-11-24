import AVFoundation
import Combine
import Foundation
import UIKit

enum DK2CutsceneKind: String {
    case intro, outro, trailer, level
}

struct DK2MovieEntry: Decodable, Identifiable {
    let name: String
    let directory: String?
    let source: String?
    let sourceSha256: String?
    let width: Int?
    let height: Int?
    let totalFrames: Int?
    let audioBlocks: Int?
    let fps: Double?
    let frameCount: Int?
    let frames: [String]?
    let audio: String?
    let poster: String?
    let label: String?
    let speechIDs: [Int]?

    var id: String { name }

    var folder: String { directory ?? name }
}

struct DK2MoviesManifest: Decodable {
    let format: String?
    let version: Int?
    let fps: Double?
    let movies: [DK2MovieEntry]
}

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

    static func movieStem(_ name: String) -> String {
        let base = (name as NSString).lastPathComponent
        if base.lowercased().hasSuffix(".tgq") {
            return String(base.dropLast(4))
        }
        return base
    }

    static func tgqNames(for name: String) -> [String] {
        let base = (name as NSString).lastPathComponent
        if base.lowercased().hasSuffix(".tgq") {
            return [base]
        }
        return [base + ".tgq", base + ".TGQ"]
    }

    static var moviesRoot: URL {
        DK2AssetLoader.root.appendingPathComponent("movies", isDirectory: true)
    }

    static var manifest: DK2MoviesManifest? {
        let url = moviesRoot.appendingPathComponent("manifest.json")
        guard let data = try? Data(contentsOf: url) else { return nil }
        return try? JSONDecoder().decode(DK2MoviesManifest.self, from: data)
    }

    static func movieNames() -> [String] {
        if let entries = manifest?.movies, !entries.isEmpty {
            return entries.map { $0.name }.sorted {
                $0.localizedCaseInsensitiveCompare($1) == .orderedAscending
            }
        }
        let root = moviesRoot
        let folders = (try? FileManager.default.contentsOfDirectory(atPath: root.path)) ?? []
        let names = folders.filter { entry in
            var isDirectory: ObjCBool = false
            let url = root.appendingPathComponent(entry)
            return FileManager.default.fileExists(atPath: url.path, isDirectory: &isDirectory)
                && isDirectory.boolValue
        }
        return names.sorted { $0.localizedCaseInsensitiveCompare($1) == .orderedAscending }
    }

    static func hasMovie(name: String) -> Bool {
        let stem = movieStem(name)
        if movieNames().contains(where: {
            $0.caseInsensitiveCompare(stem) == .orderedSame
        }) {
            return true
        }
        let folder = moviesRoot.appendingPathComponent(stem)
        var isDirectory: ObjCBool = false
        guard FileManager.default.fileExists(atPath: folder.path, isDirectory: &isDirectory),
              isDirectory.boolValue else { return false }
        let files = (try? FileManager.default.contentsOfDirectory(atPath: folder.path)) ?? []
        return files.contains { $0.lowercased().hasSuffix(".png") }
    }

    static func movie(for name: String) -> DK2MovieEntry? {
        let stem = movieStem(name)
        return manifest?.movies.first {
            $0.name.caseInsensitiveCompare(stem) == .orderedSame
        }
    }

    static func label(for name: String) -> String {
        movie(for: name)?.label ?? movieStem(name)
    }

    static func posterImage(for name: String) -> UIImage? {
        guard let relative = movie(for: name)?.poster else { return nil }
        let direct = DK2AssetLoader.root.appendingPathComponent(relative)
        if let image = UIImage(contentsOfFile: direct.path) { return image }
        let directory = direct.deletingLastPathComponent()
        let wanted = direct.lastPathComponent.lowercased()
        guard let entries = try? FileManager.default.contentsOfDirectory(atPath: directory.path),
              let match = entries.first(where: { $0.lowercased() == wanted }) else { return nil }
        return UIImage(contentsOfFile: directory.appendingPathComponent(match).path)
    }

    static func speechIDs(for name: String) -> [Int] {
        movie(for: name)?.speechIDs ?? []
    }

    static func subtitle(for name: String, progress: Double) -> String {
        let ids = speechIDs(for: name)
        guard !ids.isEmpty else { return label(for: name) }
        let bounded = min(max(progress, 0), 0.999999)
        let index = min(ids.count - 1, Int(bounded * Double(ids.count)))
        let text = DK2L10n.shared.speech(ids[index])
        if text.hasPrefix("{") && text.hasSuffix("}") {
            return label(for: name)
        }
        return text
    }

    static func audioURL(for name: String) -> URL? {
        if let relative = movie(for: name)?.audio {
            let bundled = moviesRoot.appendingPathComponent(relative)
            if FileManager.default.fileExists(atPath: bundled.path) {
                return bundled
            }
        }
        for candidate in tgqNames(for: name) {
            if let url = DK2OriginalAssets.movieAudioWAV(candidate) {
                return url
            }
        }
        return nil
    }
}

final class DK2CutscenePlayer: NSObject, ObservableObject, AVAudioPlayerDelegate {
    static let framesPerSecond: Double = 25.0
    static let frameInterval: TimeInterval = 1.0 / 25.0

    @Published private(set) var frame: Int = 0
    @Published private(set) var hasAudio = false
    @Published private(set) var subtitle: String = ""
    private(set) var droppedFrames = 0
    private(set) var playbackDuration: TimeInterval = 0

    private let name: String
    private let movie: DK2MovieEntry?
    private let poster: UIImage?
    private var images: [UIImage] = []
    private var audioPlayer: AVAudioPlayer?
    private var poll: Timer?
    private var wallStart: TimeInterval = 0
    private var completion: (() -> Void)?
    private var finished = false

    init(name: String) {
        self.name = name
        self.movie = DK2Cutscene.movie(for: name)
        self.poster = DK2Cutscene.posterImage(for: name)
        super.init()
        self.subtitle = DK2Cutscene.subtitle(for: name, progress: 0)
    }

    var frameCount: Int { images.count }

    var posterImage: UIImage? { poster }

    var currentImage: UIImage? {
        guard images.indices.contains(frame) else { return nil }
        return images[frame]
    }

    private var folder: String { movie?.folder ?? DK2Cutscene.movieStem(name) }

    private var interval: TimeInterval {
        let rate = movie?.fps ?? DK2CutscenePlayer.framesPerSecond
        return rate > 0 ? 1.0 / rate : DK2CutscenePlayer.frameInterval
    }

    func start(onComplete: @escaping () -> Void) {
        guard poll == nil else { return }
        completion = onComplete
        finished = false
        loadFrames()
        startAudio()
        let videoDuration = Double(max(0, images.count - 1)) * interval
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
            .appendingPathComponent(folder)
        var files = movie?.frames ?? []
        if files.isEmpty {
            files = (try? FileManager.default.contentsOfDirectory(atPath: dir.path))?
                .filter { $0.lowercased().hasSuffix(".png") }
                .sorted { DK2CutscenePlayer.frameNumber($0) < DK2CutscenePlayer.frameNumber($1) }
                ?? []
        }
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
        let target = Int(floor(elapsed / interval))
        if target > frame {
            droppedFrames += target - frame - 1
            frame = min(target, max(0, images.count - 1))
        }
        let progress = images.count > 1
            ? Double(frame) / Double(images.count - 1)
            : 0
        let nextSubtitle = DK2Cutscene.subtitle(for: name, progress: progress)
        if nextSubtitle != subtitle { subtitle = nextSubtitle }
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
