import Foundation

/// Access to the ORIGINAL game archives at runtime. The encrypted/compressed
/// resources are decoded by our own C decoder (no Java, no OpenKeeper).
///
/// Place your legally owned original installation here:
///   Documents/Original/Data/Meshes.WAD
///   Documents/Original/Data/Movies/*.tgq
/// Nothing is copied into the app bundle.
enum DK2OriginalAssets {
    static var root: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Original")
    }

    static func data(_ relative: String) -> Data? {
        let url = root.appendingPathComponent(relative)
        return try? Data(contentsOf: url)
    }

    static func wadInfo(_ name: String) -> NSDictionary? {
        guard let data = data("Data/\(name)") else { return nil }
        return try? DK2DecoderBridge.wadInfo(data)
    }

    /// Decode a compressed WAD entry into real KMF bytes.
    static func kmf(entry: String, in wad: String) -> Data? {
        guard let archive = data("Data/\(wad)"),
              let stored = try? DK2DecoderBridge.wadEntry(entry, from: archive),
              let kmf = try? DK2DecoderBridge.decompressKmsh(stored) else {
            return nil
        }
        return kmf
    }

    static func meshOBJ(entry: String, in wad: String = "Meshes.WAD") -> Data? {
        guard let kmf = kmf(entry: entry, in: wad) else { return nil }
        return try? DK2DecoderBridge.kmfObj(kmf)
    }

    static func animFrames(entry: String, limit: Int = 8) -> [Data]? {
        guard let kmf = kmf(entry: entry) else { return nil }
        return try? DK2DecoderBridge.animFrames(kmf, limit: UInt(limit))
    }

    static func movieInfo(_ name: String) -> NSDictionary? {
        guard let data = data("Data/Movies/\(name)") else { return nil }
        return try? DK2DecoderBridge.tgqInfo(data)
    }

    static func movieAudioWAV(_ name: String) -> URL? {
        guard let data = data("Data/Movies/\(name)") else { return nil }
        let target = FileManager.default.urls(for: .cachesDirectory,
                                              in: .userDomainMask)[0]
            .appendingPathComponent("\(name).wav")
        guard (try? DK2DecoderBridge.extractTgqAudio(data, toWAV: target.path)) != nil
        else { return nil }
        return target
    }
}
