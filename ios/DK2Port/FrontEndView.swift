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
            if let bg = DK2Art.image("BG_Tiles/BG-Skirmish-00.png") {
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
                    }
                    DK2OriginalButton(title: "Campaign") { page = .campaign }
                    DK2OriginalButton(title: "Skirmish") { page = .skirmish }
                    DK2OriginalButton(title: "Options") { page = .options }
                }
            case .campaign, .skirmish:
                VStack {
                    HStack {
                        Button("‹ Back") { page = .main }
                            .foregroundColor(.white)
                        Spacer()
                    }.padding()
                    ScrollView {
                        LazyVStack(spacing: 8) {
                            ForEach(page == .campaign ? campaign : skirmish) { m in
                                DK2OriginalButton(title: m.name) {
                                    onStart(m.mapEntry())
                                }
                            }
                        }
                    }
                }
            case .options:
                VStack(spacing: 18) {
                    HStack { Button("‹ Back") { page = .main }.foregroundColor(.white); Spacer() }.padding()
                    Text("Options").font(.title).foregroundColor(.white)
                    Text("Original DK2 frontend artwork is used for all buttons.")
                        .foregroundColor(.gray).multilineTextAlignment(.center)
                    Spacer()
                }
            }
        }
    }
}
