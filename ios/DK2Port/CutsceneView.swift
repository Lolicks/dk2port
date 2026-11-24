import SwiftUI
import UIKit

struct DK2CutsceneView: View {
    let name: String
    let onDone: () -> Void

    @StateObject private var player: DK2CutscenePlayer

    init(name: String, onDone: @escaping () -> Void) {
        self.name = name
        self.onDone = onDone
        _player = StateObject(wrappedValue: DK2CutscenePlayer(name: name))
    }

    var body: some View {
        GeometryReader { geo in
            ZStack {
                Color.black

                if let image = player.currentImage {
                    Image(uiImage: image)
                        .resizable()
                        .scaledToFit()
                        .frame(width: geo.size.width, height: geo.size.height)
                } else if let poster = player.posterImage {
                    Image(uiImage: poster)
                        .resizable()
                        .scaledToFit()
                        .frame(width: geo.size.width, height: geo.size.height)
                        .opacity(0.5)
                }

                VStack(spacing: 0) {
                    Color.black.frame(height: geo.size.height * 0.12)
                    Spacer(minLength: 0)
                    Color.black.frame(height: geo.size.height * 0.12)
                }
                .frame(width: geo.size.width, height: geo.size.height)
                .allowsHitTesting(false)

                VStack(spacing: 12) {
                    Spacer(minLength: 0)
                    if !player.subtitle.isEmpty {
                        DK2BitmapText(DK2CutsceneView.wrapped(player.subtitle),
                                      font: "Frontend20", size: 20,
                                      color: DK2UIStyle.gold)
                            .shadow(color: .black, radius: 2, y: 2)
                            .frame(maxWidth: .infinity)
                    }
                    HStack {
                        if player.droppedFrames > 0 {
                            Text("\(player.droppedFrames) dropped")
                                .font(.caption2)
                                .foregroundColor(.gray)
                        }
                        Spacer()
                        Button {
                            DK2UIStyle.postFrontEndSound()
                            player.stop()
                            onDone()
                        } label: {
                            DK2BitmapText("Skip", font: "Frontend20", size: 20,
                                          color: DK2UIStyle.gold)
                                .shadow(color: .black, radius: 1, y: 1)
                        }
                        .buttonStyle(.plain)
                    }
                }
                .padding(.horizontal, 20)
                .padding(.bottom, geo.size.height * 0.04)
                .frame(width: geo.size.width, height: geo.size.height)
            }
        }
        .background(Color.black)
        .ignoresSafeArea()
        .onAppear { player.start { onDone() } }
        .onDisappear { player.stop() }
    }

    private static func wrapped(_ text: String) -> String {
        let words = text.split(separator: " ", omittingEmptySubsequences: true).map(String.init)
        var lines: [String] = []
        var current = ""
        for word in words {
            if current.isEmpty {
                current = word
            } else if current.count + word.count + 1 <= 48 {
                current += " " + word
            } else {
                lines.append(current)
                current = word
            }
        }
        if !current.isEmpty { lines.append(current) }
        guard !lines.isEmpty else { return text }
        return lines.prefix(3).joined(separator: "\n")
    }
}
