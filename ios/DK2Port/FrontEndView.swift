import SwiftUI
import UIKit

/// Loads original FrontEnd artwork from the imported asset bundle.
enum DK2Art {
    static func image(_ relative: String) -> UIImage? {
        let url = DK2AssetLoader.root
            .appendingPathComponent("textures/FrontEnd")
            .appendingPathComponent(relative)
        return UIImage(contentsOfFile: url.path)
    }
}

struct DK2OriginalButton: View {
    let title: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            ZStack {
                if let img = DK2Art.image("Bigbutton.PNG") {
                    Image(uiImage: img)
                        .resizable()
                        .scaledToFill()
                } else {
                    Rectangle().fill(Color(red: 0.18, green: 0.12, blue: 0.10))
                }
                Text(title)
                    .font(.system(size: 18, weight: .bold, design: .serif))
                    .foregroundColor(Color(red: 0.95, green: 0.85, blue: 0.62))
                    .shadow(color: .black, radius: 2)
            }
            .frame(width: 260, height: 52)
            .clipShape(RoundedRectangle(cornerRadius: 6))
        }
        .buttonStyle(.plain)
    }
}

struct DK2FrontEndView: View {
    let missions: [DK2Mission]
    let onStart: (DK2MapEntry) -> Void

    @State private var page: Page = .main
    @State private var playingCutscene: String?
    @State private var showSaves = false

    enum Page { case main, campaign, skirmish, options }

    private var campaign: [DK2Mission] {
        missions.filter { $0.category == "campaign" }
            .sorted { ($0.campaignLevel ?? 999) < ($1.campaignLevel ?? 999) }
    }
    private var skirmish: [DK2Mission] {
        missions.filter { $0.category != "campaign" }
            .sorted { $0.name.localizedCompare($1.name) == .orderedAscending }
    }

    var body: some View {
        ZStack {
            if showSaves {
                DK2SaveSlotsView { showSaves = false }
            } else if let movie = playingCutscene {
                DK2CutsceneView(name: movie) { playingCutscene = nil }
            } else if let bg = DK2Art.image("BG_Tiles/BG-Skirmish-00.png") {
                Image(uiImage: bg).resizable().scaledToFill().ignoresSafeArea()
            } else {
                Color.black.ignoresSafeArea()
            }

            switch page {
            case .main:
                VStack(spacing: 16) {
                    if let logo = DK2Art.image("Logo.png") {
                        Image(uiImage: logo).resizable().scaledToFit()
                            .frame(height: 130)
                    } else if let title = DK2Art.image("Titlescreen.png") {
                        Image(uiImage: title).resizable().scaledToFit()
                            .frame(height: 130)
                    }
                    DK2OriginalButton(title: "Campaign") { page = .campaign }
                    DK2OriginalButton(title: "Skirmish") { page = .skirmish }
                    DK2OriginalButton(title: "Intro") { playingCutscene = "BullfrogIntro" }
                    DK2OriginalButton(title: "Save / Load") { showSaves = true }
                    DK2OriginalButton(title: "Options") { page = .options }
                }
            case .campaign, .skirmish:
                VStack {
                    HStack {
                        Button("‹ Back") { page = .main }
                            .foregroundColor(Color(red: 0.95, green: 0.85, blue: 0.62))
                        Spacer()
                        Text(page == .campaign ? "Campaign" : "Skirmish & Secret")
                            .font(.system(size: 22, weight: .bold, design: .serif))
                            .foregroundColor(Color(red: 0.95, green: 0.85, blue: 0.62))
                        Spacer()
                    }
                    .padding()
                    ScrollView {
                        LazyVStack(spacing: 8) {
                            ForEach(page == .campaign ? campaign : skirmish) { m in
                                DK2MissionRow(mission: m) { onStart(m.mapEntry()) }
                            }
                        }
                        .padding(.horizontal)
                    }
                }
            case .options:
                VStack(spacing: 18) {
                    HStack { Button("‹ Back") { page = .main }.foregroundColor(.white); Spacer() }.padding()
                    Text("Options").font(.title).foregroundColor(.white)
                    Text("Original DK2 frontend artwork is used for all buttons and backgrounds.")
                        .foregroundColor(.gray).multilineTextAlignment(.center)
                    Spacer()
                }
            }
        }
    }
}

struct DK2MissionRow: View {
    let mission: DK2Mission
    let action: () -> Void

    private var artName: String? {
        if let level = mission.campaignLevel {
            let variant = mission.campaignVariant ?? ""
            let candidates = [
                "2dLevel\(level)\(variant)-1.png",
                "Level\(level)\(variant)-0.png",
                "2dLevel\(level)-1.png",
                "Level\(level)-0.png"
            ]
            for name in candidates where DK2Art.image(name) != nil { return name }
        }
        return nil
    }

    var body: some View {
        Button(action: action) {
            HStack(spacing: 10) {
                Group {
                    if let name = artName, let img = DK2Art.image(name) {
                        Image(uiImage: img).resizable().scaledToFill()
                    } else if let img = DK2Art.image("Box-00.png") {
                        Image(uiImage: img).resizable().scaledToFit()
                    } else {
                        Rectangle().fill(Color(red: 0.18, green: 0.12, blue: 0.10))
                    }
                }
                .frame(width: 72, height: 48)
                .clipped()
                .cornerRadius(4)

                VStack(alignment: .leading, spacing: 2) {
                    Text(mission.name)
                        .font(.system(size: 16, weight: .bold, design: .serif))
                        .foregroundColor(Color(red: 0.95, green: 0.85, blue: 0.62))
                    Text("\(mission.width) × \(mission.height) tiles"
                        + (mission.campaignLevel.map { "  •  Level \($0)" } ?? ""))
                        .font(.caption)
                        .foregroundColor(.gray)
                }
                Spacer()
                Image(systemName: "chevron.right").foregroundColor(.gray)
            }
            .padding(8)
            .background(Color.black.opacity(0.55))
            .cornerRadius(6)
            .overlay(RoundedRectangle(cornerRadius: 6)
                .stroke(Color(red: 0.45, green: 0.32, blue: 0.18), lineWidth: 1))
        }
        .buttonStyle(.plain)
    }
}
