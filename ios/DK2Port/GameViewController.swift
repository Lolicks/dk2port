import Foundation
import MetalKit
import SwiftUI
import UIKit


private struct DK2AudioFileEntry: Decodable {
    let bank: String?
    let wav: String?
    let bytes: Int?
}

private struct DK2AudioCatalog: Decodable {
    let files: [DK2AudioFileEntry]
}

final class DK2GameViewController: UIViewController {
    private let bridge = DK2Bridge.shared()
    private let audio = DK2SimpleAudio()
    private var renderer: DK2MetalRenderer?
    private var displayLink: CADisplayLink?
    private var accumulator: TimeInterval = 0
    private let tick: TimeInterval = 1.0 / 30.0
    let mission: DK2MapEntry

    private var selectedX: Int32 = 0
    private var selectedY: Int32 = 0
    private var eventAudio: DK2EventAudioResponder?
    private var lastListenerX: Float = 0
    private var lastListenerY: Float = 0
    private var lastCreatureCount = 0
    private var lastGold = 0
    private var lastVictory = false
    private var lastDefeat = false
    private var lastCreatureHP: [String: Float] = [:]
    private var lastHitSound: TimeInterval = 0

    var onState: ((NSDictionary) -> Void)?

    init(mission: DK2MapEntry) {
        self.mission = mission
        super.init(nibName: nil, bundle: nil)
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func viewDidLoad() {
        super.viewDidLoad()
        let metalView = MTKView(frame: view.bounds)
        metalView.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        metalView.preferredFramesPerSecond = 60
        metalView.isMultipleTouchEnabled = true
        view.addSubview(metalView)

        renderer = DK2MetalRenderer(metalView: metalView, bridge: bridge,
                                    assetRoot: DK2AssetLoader.root)
        if let data = DK2AssetLoader.gameData {
            bridge.loadCreatures(data.creatureNames, rooms: data.roomNames,
                                 spells: data.keeperSpellNames)
        }

        do {
            try load(mission)
            bridge.spawnInitialKeeper(3)
            debugLog("LOAD imps ok")
        } catch {
            debugLog("LOAD FAILED \(error.localizedDescription)")
            let label = UILabel(frame: view.bounds)
            label.text = "Failed to load \(mission.name): \(error.localizedDescription)"
            label.textColor = .white
            label.textAlignment = .center
            label.numberOfLines = 0
            view.addSubview(label)
        }

        installGestures(on: metalView)

        installCommandObservers()

        let responder = DK2EventAudioResponder(audio: audio)
        responder.configure()
        responder.install()
        eventAudio = responder

        displayLink = CADisplayLink(target: self, selector: #selector(frame(_:)))
        displayLink?.add(to: .main, forMode: .common)
    }

    private func debugLog(_ message: String) {
        let url = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("dk2_level.log")
        let line = message + "\n"
        if let handle = try? FileHandle(forWritingTo: url) {
            handle.seekToEndOfFile()
            handle.write(Data(line.utf8))
            try? handle.close()
        } else {
            try? line.write(to: url, atomically: true, encoding: .utf8)
        }
    }

    private func load(_ entry: DK2MapEntry) throws {
        debugLog("LOAD start \(entry.name) \(entry.file)")
        let mapURL = DK2AssetLoader.mapURL(entry)
        debugLog("MAP \(mapURL.path) exists=\(FileManager.default.fileExists(atPath: mapURL.path))")
        try bridge.loadMap(atPath: mapURL.path)
        debugLog("LOAD map ok")
        let world = bridge.snapshot
        let w = world["width"] as? Int ?? 0
        let h = world["height"] as? Int ?? 0
        audio.configure(worldWidth: w, height: h)
        audio.start()
        let catalogURL = DK2AssetLoader.root.appendingPathComponent("audio/audio.json")
        if let data = try? Data(contentsOf: catalogURL),
           let catalog = try? JSONDecoder().decode(DK2AudioCatalog.self, from: data),
           let tracks = catalog.files.filter {
               ($0.bank ?? "").lowercased().contains("track") &&
               !($0.wav ?? "").isEmpty
           },
           let track = tracks.max(by: { ($0.bytes ?? 0) < ($1.bytes ?? 0) }),
           let path = track.wav {
            audio.playMusic(relativePath: path, key: "music")
        }
        if w > 0 && h > 0 {
            let cx = min(w - 1, max(0, w / 2))
            let cy = min(h - 1, max(0, h / 2))
            selectedX = Int32(cx)
            selectedY = Int32(cy)
            _ = bridge.spawnCreature(0, x: selectedX, y: selectedY)
        }
        lastCreatureCount = DK2GameViewController.integer(world["creatures"])
        lastGold = DK2GameViewController.integer(world["gold"])
        lastVictory = DK2GameViewController.boolean(world["victory"])
        lastDefeat = DK2GameViewController.boolean(world["defeat"])
        lastCreatureHP.removeAll()
    }

    @objc private func frame(_ link: CADisplayLink) {
        accumulator += link.duration * TimeInterval(link.frameInterval)
        var steps = 0
        while accumulator >= tick && steps < 4 {
            bridge.step()
            accumulator -= tick
            steps += 1
        }
        let snapshot = bridge.snapshot as NSDictionary
        let state = DK2GameViewController.stateSnapshot(snapshot)
        let cx = DK2GameViewController.number(snapshot["centerX"])
        let cy = DK2GameViewController.number(snapshot["centerY"])
        let dt = Swift.max(0.001, link.duration * TimeInterval(link.frameInterval))
        let vx = (cx - lastListenerX) / Float(dt)
        let vy = (cy - lastListenerY) / Float(dt)
        lastListenerX = cx
        lastListenerY = cy
        audio.updateListener(x: cx, y: cy, vx: vx, vy: vy)
        emitStateAudioEvents(snapshot)
        DK2SaveManager.shared.updateAutosave(snapshot: snapshot, bridge: bridge)
        onState?(state)
    }

    /// Emits NotificationCenter game events consumed by DK2EventAudioResponder.
    private func emitStateAudioEvents(_ snapshot: NSDictionary) {
        let cx = DK2GameViewController.number(snapshot["centerX"])
        let cy = DK2GameViewController.number(snapshot["centerY"])

        let creatures = DK2GameViewController.integer(snapshot["creatures"])
        if creatures < lastCreatureCount {
            DK2EventAudio.post(.death, x: cx, y: cy)
        }
        lastCreatureCount = creatures

        let gold = DK2GameViewController.integer(snapshot["gold"])
        if gold > lastGold {
            DK2EventAudio.post(.pickup, x: Float(selectedX), y: Float(selectedY))
        }
        lastGold = gold

        let now = Date().timeIntervalSinceReferenceDate
        var hit: (x: Float, y: Float)?
        for (index, creature) in bridge.creatureInstances.enumerated() {
            let hp = DK2GameViewController.number(creature["hp"])
            let species = DK2GameViewController.integer(creature["species"])
            let key = "\(index):\(species)"
            if let previous = lastCreatureHP[key], hp < previous - 0.04 {
                hit = (DK2GameViewController.number(creature["x"]),
                       DK2GameViewController.number(creature["y"]))
            }
            lastCreatureHP[key] = hp
        }
        if let hit = hit, now - lastHitSound > 0.25 {
            lastHitSound = now
            DK2EventAudio.post(.hit, x: hit.x, y: hit.y)
        }

        let victory = DK2GameViewController.boolean(snapshot["victory"])
        if victory && !lastVictory {
            DK2EventAudio.post(.victory, x: cx, y: cy)
            DK2Cutscene.request(.outro)
        }
        let defeat = DK2GameViewController.boolean(snapshot["defeat"])
        if defeat && !lastDefeat {
            DK2EventAudio.post(.defeat, x: cx, y: cy)
        }
        lastVictory = victory
        lastDefeat = defeat
    }

    private static func number(_ value: Any?) -> Float {
        if let v = value as? Float { return v }
        if let v = value as? Double { return Float(v) }
        if let v = value as? Int { return Float(v) }
        if let v = value as? NSNumber { return v.floatValue }
        return 0
    }

    private static func integer(_ value: Any?) -> Int {
        if let v = value as? Int { return v }
        if let v = value as? NSNumber { return v.intValue }
        if let v = value as? Float { return Int(v) }
        if let v = value as? Double { return Int(v) }
        return 0
    }

    private static func boolean(_ value: Any?) -> Bool {
        if let v = value as? Bool { return v }
        if let v = value as? NSNumber { return v.boolValue }
        return false
    }

    private static func stateSnapshot(_ snapshot: NSDictionary) -> NSDictionary {
        let state = NSMutableDictionary(dictionary: snapshot)
        state["gold"] = snapshot["gold"] ?? NSNumber(value: 0)
        state["mana"] = snapshot["mana"] ?? NSNumber(value: 0)
        state["manaMax"] = snapshot["manaMax"] ?? NSNumber(value: 0)
        state["goldCapacity"] = snapshot["goldCapacity"] ?? NSNumber(value: 0)
        state["creatures"] = snapshot["creatures"] ?? NSNumber(value: 0)
        state["rooms"] = snapshot["rooms"] ?? NSNumber(value: 0)
        state["ticks"] = snapshot["ticks"] ?? NSNumber(value: 0)
        return state
    }

    private func installGestures(on target: UIView) {
        let pan = UIPanGestureRecognizer(target: self, action: #selector(didPan(_:)))
        pan.maximumNumberOfTouches = 1
        target.addGestureRecognizer(pan)

        let pinch = UIPinchGestureRecognizer(target: self, action: #selector(didPinch(_:)))
        target.addGestureRecognizer(pinch)

        let rotation = UIRotationGestureRecognizer(target: self, action: #selector(didRotate(_:)))
        target.addGestureRecognizer(rotation)

        let tap = UITapGestureRecognizer(target: self, action: #selector(didTap(_:)))
        target.addGestureRecognizer(tap)
        for recognizer in [pan, pinch, rotation, tap] {
            recognizer.delegate = self
        }
    }

    @objc private func didPan(_ gesture: UIPanGestureRecognizer) {
        let delta = gesture.translation(in: view)
        let zoom = bridge.snapshot["zoom"] as? Float ?? 1
        bridge.panDX(Float(delta.x) / (zoom * 28), dy: Float(delta.y) / (zoom * 28))
        gesture.setTranslation(.zero, in: view)
    }

    @objc private func didPinch(_ gesture: UIPinchGestureRecognizer) {
        bridge.zoom(Float(gesture.scale))
        gesture.scale = 1
    }

    @objc private func didRotate(_ gesture: UIRotationGestureRecognizer) {
        bridge.rotate(Float(gesture.rotation))
        gesture.rotation = 0
    }

    @objc private func didTap(_ gesture: UITapGestureRecognizer) {
        let point = gesture.location(in: view)
        let snap = bridge.snapshot
        let cx = snap["centerX"] as? Float ?? 0
        let cy = snap["centerY"] as? Float ?? 0
        let zoom = snap["zoom"] as? Float ?? 1
        let wx = cx + Float(point.x - view.bounds.midX) / (zoom * 28)
        let wy = cy + Float(point.y - view.bounds.midY) / (zoom * 28)
        bridge.selectWorldX(wx, y: wy)
        let maxX = Int32(max(0, DK2GameViewController.integer(snap["width"]) - 1))
        let maxY = Int32(max(0, DK2GameViewController.integer(snap["height"]) - 1))
        selectedX = min(maxX, max(0, Int32(floor(wx))))
        selectedY = min(maxY, max(0, Int32(floor(wy))))
    }

    private func installCommandObservers() {
        let center = NotificationCenter.default
        for name in DK2GameCommand.allNames {
            center.addObserver(self, selector: #selector(handleCommand(_:)),
                               name: name, object: nil)
        }
    }

    @objc private func handleCommand(_ note: Notification) {
        guard let info = note.userInfo as? [String: Any] else { return }
        switch note.name {
        case DK2GameCommand.build:
            let category = info[DK2GameCommand.Key.category] as? String ?? "room"
            let index = info[DK2GameCommand.Key.index] as? Int ?? 0
            let placed: Bool
            switch category {
            case "trap":
                placed = bridge.placeTrap(Int32(index), x: selectedX, y: selectedY, owner: 0)
            case "door":
                placed = bridge.placeDoor(Int32(index), x: selectedX, y: selectedY, owner: 0)
            default:
                placed = bridge.buildRoom(Int32(index), x: selectedX, y: selectedY, owner: 0)
            }
            if placed {
                DK2EventAudio.post(.build, x: Float(selectedX), y: Float(selectedY))
            }
        case DK2GameCommand.spell:
            let index = info[DK2GameCommand.Key.index] as? Int ?? 0
            if bridge.castSpell(Int32(index), x: selectedX, y: selectedY) {
                DK2EventAudio.post(.spell, x: Float(selectedX), y: Float(selectedY))
            }
        case DK2GameCommand.creature:
            let index = info[DK2GameCommand.Key.index] as? Int ?? 0
            if bridge.spawnCreature(Int32(index), x: selectedX, y: selectedY) >= 0 {
                DK2EventAudio.post(.spawn, x: Float(selectedX), y: Float(selectedY))
            }
        case DK2GameCommand.save:
            if let slot = info[DK2GameCommand.Key.slot] as? String {
                _ = DK2SaveManager.shared.save(slot: slot, bridge: bridge)
            }
        case DK2GameCommand.load:
            if let slot = info[DK2GameCommand.Key.slot] as? String {
                _ = DK2SaveManager.shared.load(slot: slot, bridge: bridge)
            }
        case DK2GameCommand.pause:
            let paused = info[DK2GameCommand.Key.paused] as? Bool ?? false
            displayLink?.isPaused = paused
        case DK2GameCommand.speed:
            let mult = info[DK2GameCommand.Key.multiplier] as? Int ?? 1
            accumulator = 0
            displayLink?.preferredFramesPerSecond = 60 * mult
        case DK2GameCommand.quit:
            displayLink?.invalidate()
            displayLink = nil
        default: break
        }
    }

    func saveGame(name: String) {
        _ = bridge.save(toPath: DK2AssetLoader.saveURL(name: name).path)
    }

    func loadGame(name: String) {
        _ = bridge.load(fromPath: DK2AssetLoader.saveURL(name: name).path)
    }
}

extension DK2GameViewController: UIGestureRecognizerDelegate {
    func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer,
                           shouldRecognizeSimultaneouslyWith other: UIGestureRecognizer) -> Bool {
        true
    }
}

struct DK2GameView: UIViewControllerRepresentable {
    let mission: DK2MapEntry
    let onState: (NSDictionary) -> Void

    func makeUIViewController(context: Context) -> DK2GameViewController {
        let vc = DK2GameViewController(mission: mission)
        vc.onState = onState
        return vc
    }
    func updateUIViewController(_ vc: DK2GameViewController, context: Context) {}
}
