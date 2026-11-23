import Foundation
import MetalKit
import SwiftUI
import UIKit

final class DK2GameViewController: UIViewController {
    private let bridge = DK2Bridge()
    private let audio = DK2AudioController()
    private var renderer: DK2MetalRenderer?
    private var displayLink: CADisplayLink?
    private var accumulator: TimeInterval = 0
    private let tick: TimeInterval = 1.0 / 30.0
    let mission: DK2MapEntry

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
        } catch {
            let label = UILabel(frame: view.bounds)
            label.text = "Failed to load \(mission.name): \(error.localizedDescription)"
            label.textColor = .white
            label.textAlignment = .center
            label.numberOfLines = 0
            view.addSubview(label)
        }

        installGestures(on: metalView)

        displayLink = CADisplayLink(target: self, selector: #selector(frame(_:)))
        displayLink?.add(to: .main, forMode: .common)
    }

    private func load(_ entry: DK2MapEntry) throws {
        try bridge.loadMapAtPath(DK2AssetLoader.mapURL(entry).path)
        let world = bridge.snapshot
        let w = world["width"] as? Int ?? 0
        let h = world["height"] as? Int ?? 0
        audio.configure(worldWidth: w, height: h)
        audio.start()
        if let manifest = DK2AssetLoader.load(DK2AudioIndex.self, "audio/audio.json") {
            for entry in manifest.files.prefix(6) {
                guard let path = entry.wav, !path.isEmpty else { continue }
                audio.preload(relativePath: path, key: entry.name)
            }
        }
        if w > 0 && h > 0 {
            let cx = min(w - 1, max(0, w / 2))
            let cy = min(h - 1, max(0, h / 2))
            _ = bridge.spawnCreature(0, x: cx, y: cy)
        }
    }

    @objc private func frame(_ link: CADisplayLink) {
        accumulator += link.duration * TimeInterval(link.frameInterval)
        var steps = 0
        while accumulator >= tick && steps < 4 {
            bridge.step()
            accumulator -= tick
            steps += 1
        }
        let snapshot = bridge.snapshot
        audio.updateListener(x: snapshot["centerX"] as? Float ?? 0,
                             y: snapshot["centerY"] as? Float ?? 0)
        onState?(snapshot)
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
    }

    func saveGame(name: String) {
        _ = bridge.saveToPath(DK2AssetLoader.saveURL(name: name).path)
    }

    func loadGame(name: String) {
        _ = bridge.loadFromPath(DK2AssetLoader.saveURL(name: name).path)
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
