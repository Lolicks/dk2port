import AVFoundation
import Foundation

/// Gameplay events that map to original samples from the imported audio catalog.
enum DK2GameAudioEvent: String, CaseIterable {
    case spawn
    case death
    case build
    case spell
    case hit
    case pickup
    case victory
    case defeat
}

/// Notification API for one-shot game sound effects.
/// Post with `DK2EventAudio.post(.build, x:y:)`; `DK2EventAudioResponder`
/// resolves the event to up to four original samples and plays one on the sfx
/// bus through DK2AudioController.
enum DK2EventAudio {
    static let played = Notification.Name("dk2.audio.event")

    enum Key {
        static let event = "event"
        static let x = "x"
        static let y = "y"
        static let variant = "variant"
    }

    static func post(_ event: DK2GameAudioEvent, x: Float, y: Float, variant: Int = 0) {
        NotificationCenter.default.post(name: played, object: nil, userInfo: [
            Key.event: event.rawValue,
            Key.x: x,
            Key.y: y,
            Key.variant: variant
        ])
    }
}

/// Covers both on-disk catalog shapes:
///   audio/manifest.json -> { "version", "entries": [{ id, path, ... }] }
///   audio/audio.json     -> { "banks", "files": [{ bank, index, name, wav }] }
/// The second form is what tools/import_assets.py actually writes today.
private struct DK2SoundEntry: Decodable {
    let id: String?
    let bank: String?
    let index: Int?
    let name: String
    let path: String?
    let wav: String?
    let wavSource: String?

    enum CodingKeys: String, CodingKey {
        case id, bank, index, name, path, wav
        case wavSource = "wav_source"
    }

    var soundPath: String? {
        for candidate in [path, wav, wavSource] where !(candidate ?? "").isEmpty {
            return candidate
        }
        return nil
    }

    var key: String {
        if let id = id, !id.isEmpty { return id }
        return "\(bank ?? "bank")#\(index ?? 0):\(name)"
    }
}

private struct DK2SoundCatalog: Decodable {
    let entries: [DK2SoundEntry]?
    let files: [DK2SoundEntry]?

    var all: [DK2SoundEntry] { entries ?? files ?? [] }
}

/// Loads matched manifest samples once and plays them on the audio controller.
final class DK2EventAudioResponder {
    private let audio: DK2AudioController
    private var keys: [DK2GameAudioEvent: [String]] = [:]
    private var cursors: [DK2GameAudioEvent: Int] = [:]
    private var installed = false

    init(audio: DK2AudioController) {
        self.audio = audio
    }

    deinit {
        NotificationCenter.default.removeObserver(self)
    }

    /// Reads the audio catalog shipped in the asset bundle. Safe to call more
    /// than once; preloading in DK2AudioController is idempotent per key.
    func configure() {
        var catalog: [DK2SoundEntry] = []
        for relative in ["audio/manifest.json", "audio/audio.json"] {
            let url = DK2AssetLoader.root.appendingPathComponent(relative)
            guard let data = try? Data(contentsOf: url),
                  let decoded = try? JSONDecoder().decode(DK2SoundCatalog.self, from: data)
            else { continue }
            catalog = decoded.all
            if !catalog.isEmpty { break }
        }

        let usable = catalog.filter {
            $0.soundPath != nil && !$0.name.lowercased().contains("blank")
        }
        for event in DK2GameAudioEvent.allCases {
            var matched: [String] = []
            for entry in usable where matches(event, entry: entry) {
                guard let path = entry.soundPath else { continue }
                let key = entry.key
                guard !matched.contains(key) else { continue }
                audio.preload(relativePath: path, key: key, bus: .sfx, positional: true)
                matched.append(key)
                if matched.count >= 4 { break }
            }
            if matched.isEmpty, let fallback = fallback(for: event, in: usable),
               let path = fallback.soundPath {
                audio.preload(relativePath: path, key: fallback.key,
                              bus: .sfx, positional: true)
                matched.append(fallback.key)
            }
            keys[event] = matched
        }
    }

    func install() {
        guard !installed else { return }
        installed = true
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(onEvent(_:)),
            name: DK2EventAudio.played,
            object: nil)
    }

    /// Sound keys selected for an event, for diagnostics and UI hints.
    func soundKeys(for event: DK2GameAudioEvent) -> [String] {
        keys[event] ?? []
    }

    @objc private func onEvent(_ note: Notification) {
        guard let info = note.userInfo as? [String: Any],
              let raw = info[DK2EventAudio.Key.event] as? String,
              let event = DK2GameAudioEvent(rawValue: raw),
              let candidates = keys[event], !candidates.isEmpty else { return }
        let x = DK2EventAudioResponder.coordinate(info[DK2EventAudio.Key.x])
        let y = DK2EventAudioResponder.coordinate(info[DK2EventAudio.Key.y])
        let requested = info[DK2EventAudio.Key.variant] as? Int ?? 0
        let index = ((requested % candidates.count) + candidates.count) % candidates.count
        cursors[event] = index + 1
        audio.play(key: candidates[index], atX: x, y: y, volume: 0.8)
    }

    private static func coordinate(_ value: Any?) -> Float {
        if let v = value as? Float { return v }
        if let v = value as? Double { return Float(v) }
        if let v = value as? Int { return Float(v) }
        if let v = value as? NSNumber { return v.floatValue }
        return 0
    }

    private func matches(_ event: DK2GameAudioEvent, entry: DK2SoundEntry) -> Bool {
        let name = entry.name.lowercased()
        let bank = (entry.bank ?? "").lowercased()
        switch event {
        case .spawn:
            return name.contains("spawn") || name.contains("appear")
                || name.contains("summon") || name.contains("createchick")
                || (bank.contains("imp") && name.contains("hello"))
        case .death:
            return name.contains("die") || name.contains("death") || name.contains("sacri")
        case .build:
            return name.contains("build") || name.contains("construct")
        case .spell:
            return name.contains("cast") || name.contains("spell")
                || bank.contains("keeperspells")
        case .hit:
            return name.contains("hit") || name.contains("punch")
                || name.contains("melee") || name.contains("slap")
        case .pickup:
            return name.contains("pick") || name.contains("gold")
                || name.contains("collect") || name.contains("coin")
        case .victory:
            return name.contains("victor") || name.contains("fanfare")
                || name.contains("triumph")
        case .defeat:
            return name.contains("defeat") || name.contains("gameover")
                || name.contains("lost the")
        }
    }

    /// The catalog has no samples literally named victory/defeat; pick a
    /// deterministic frontend sting so the wired game events still have audio.
    private func fallback(for event: DK2GameAudioEvent,
                          in entries: [DK2SoundEntry]) -> DK2SoundEntry? {
        let needles: [String]
        switch event {
        case .victory: needles = ["button_big", "button big", "gemfe", "glow"]
        case .defeat: needles = ["spikes", "glow", "button"]
        default: return nil
        }
        for needle in needles {
            if let found = entries.first(where: {
                $0.name.lowercased().contains(needle)
            }) {
                return found
            }
        }
        return nil
    }
}
