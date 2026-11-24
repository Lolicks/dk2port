import SwiftUI

/// Displays a pre-decoded original TGQ cutscene. Frame pacing and the end of
/// playback are driven by DK2CutscenePlayer (audio clock, 25 fps, drop-frame).
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
        ZStack {
            Color.black.ignoresSafeArea()

            if let image = player.currentImage {
                Image(uiImage: image)
                    .resizable()
                    .scaledToFit()
                    .ignoresSafeArea()
            } else if !player.hasAudio {
                Text("Cutscene \(name) has no decoded frames in this bundle")
                    .font(.caption)
                    .foregroundColor(.gray)
                    .multilineTextAlignment(.center)
                    .padding()
            }

            VStack {
                Spacer()
                HStack {
                    if player.droppedFrames > 0 {
                        Text("\(player.droppedFrames) dropped")
                            .font(.caption2)
                            .foregroundColor(.gray)
                            .padding(.leading)
                    }
                    Spacer()
                    Button("Skip") {
                        player.stop()
                        onDone()
                    }
                    .foregroundColor(.white)
                    .padding()
                }
            }
        }
        .onAppear { player.start { onDone() } }
        .onDisappear { player.stop() }
    }
}
