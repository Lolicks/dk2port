import SwiftUI
import Foundation

struct DK2RootView: View {
    @State private var selected: DK2MapEntry?
    @State private var selectedMission: DK2Mission?
    @State private var state: NSDictionary = [:]
    @StateObject private var resources = DK2ResourceManager.shared
    @State private var menuAudio: DK2MenuAudio?

    private func start(_ mission: DK2Mission) {
        selectedMission = mission
        selected = mission.mapEntry()
    }

    var body: some View {
        ZStack(alignment: .topLeading) {
            if let mission = selected {
                DK2GameView(mission: mission, onState: { state = $0 })
                    .ignoresSafeArea()
                DK2GameHUD(state: state, mission: selectedMission) {
                    selected = nil
                    selectedMission = nil
                }
                .ignoresSafeArea()
            } else {
                VStack {
                    if !resources.ready {
                        ProgressView(resources.status)
                            .foregroundColor(.white)
                    } else {
                        DK2FrontEndView(missions: DK2AssetLoader.missions ?? []) { map in
                            if let mission = DK2AssetLoader.missions?.first(where: {
                                $0.mapFile == map.file
                            }) {
                                start(mission)
                            } else {
                                selectedMission = DK2Mission(
                                    name: map.name, mapFile: map.file,
                                    width: map.width, height: map.height,
                                    category: "skirmish", campaignLevel: nil,
                                    campaignVariant: nil)
                                selected = map
                            }
                        }
                    }
                }
                .onAppear { resources.initialize() }
                .onChange(of: resources.ready) { ready in
                    guard ready, menuAudio == nil else { return }
                    let audio = DK2MenuAudio()
                    audio.startMusic()
                    menuAudio = audio
                }
            }
        }
    }
}

@main
struct DK2App: App {
    init() {
        NSSetUncaughtExceptionHandler { exception in
            let text = "CRASH \(exception.name.rawValue): \(exception.reason ?? "")"
            let url = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
                .appendingPathComponent("dk2_crash.log")
            try? text.write(to: url, atomically: true, encoding: .utf8)
        }
    }

    var body: some Scene {
        WindowGroup {
            DK2RootView()
        }
    }
}
