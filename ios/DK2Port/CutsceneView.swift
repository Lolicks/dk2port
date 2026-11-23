import SwiftUI

/// Plays a pre-decoded original TQI cutscene from the app bundle.
/// Frames are produced by our C decoder + OpenKeeper reference pipeline.
struct DK2CutsceneView: View {
    let name: String
    let onDone: () -> Void

    @State private var frame = 0
    @State private var images: [UIImage] = []

    private let timer = Timer.publish(every: 1.0 / 24.0, on: .main, in: .common).autoconnect()

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()
            if let img = images.indices.contains(frame) ? images[frame] : nil {
                Image(uiImage: img)
                    .resizable()
                    .scaledToFit()
                    .ignoresSafeArea()
            }
            VStack {
                Spacer()
                Button("Skip") { onDone() }
                    .foregroundColor(.white)
                    .padding()
            }
        }
        .onAppear {
            let dir = DK2AssetLoader.root
                .appendingPathComponent("movies")
                .appendingPathComponent(name)
            let files = (try? FileManager.default.contentsOfDirectory(atPath: dir.path))?
                .filter { $0.hasSuffix(".png") }.sorted() ?? []
            images = files.compactMap {
                UIImage(contentsOfFile: dir.appendingPathComponent($0).path)
            }
        }
        .onReceive(timer) { _ in
            guard !images.isEmpty else { return }
            frame += 1
            if frame >= images.count { onDone() }
        }
    }
}
