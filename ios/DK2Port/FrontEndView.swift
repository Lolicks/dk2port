import SwiftUI
import UIKit

enum DK2Art {
    static func image(_ relative: String) -> UIImage? {
        let url = DK2AssetLoader.root
            .appendingPathComponent("textures/FrontEnd")
            .appendingPathComponent(relative)
        return UIImage(contentsOfFile: url.path)
    }
}

struct DK2FrontEndView: View {
    let missions: [DK2Mission]
    let onStart: (DK2MapEntry) -> Void

    @StateObject private var model: DK2FrontEndModel

    init(missions: [DK2Mission], onStart: @escaping (DK2MapEntry) -> Void) {
        self.missions = missions
        self.onStart = onStart
        let model = DK2FrontEndModel()
        model.missions = missions
        model.onStart = onStart
        _model = StateObject(wrappedValue: model)
    }

    var body: some View {
        ZStack {
            DK2ScreenContent(screen: model.base)

            if let popup = model.popup {
                DK2PopupLayer(popup: popup)
            }

            if model.showSaveSlots {
                DK2SaveSlotsView { model.showSaveSlots = false }
            }

            if let movie = model.playingCutscene {
                DK2CutsceneView(name: movie) { model.finishCutscene() }
            }
        }
        .environmentObject(model)
        .onReceive(NotificationCenter.default.publisher(for: DK2Cutscene.playRequest)) { note in
            if let name = note.userInfo?[DK2Cutscene.Key.name] as? String {
                model.externalCutscene(name)
            }
        }
    }
}
