import Foundation
import CryptoKit

/// Automatic startup validation of the embedded original resources.
/// No user interaction, no Files Picker, no downloads.
final class DK2ResourceManager: ObservableObject {
    struct Integrity: Decodable {
        let format: String
        let version: Int
        let files: [Entry]

        struct Entry: Decodable {
            let path: String
            let present: Bool
            let size: Int64?
            let sha256: String?
        }
    }

    @Published private(set) var ready = false
    @Published private(set) var status = "Checking embedded resources…"
    @Published private(set) var checkedFiles = 0

    static let shared = DK2ResourceManager()

    private init() {}

    func initialize() {
        let bundle = Bundle.main.bundleURL
        let original = bundle.appendingPathComponent("Original")
        let assets = bundle.appendingPathComponent("Assets")
        guard FileManager.default.fileExists(atPath: original.path),
              FileManager.default.fileExists(atPath: assets.path) else {
            status = "Embedded resources are missing from the app bundle"
            return
        }

        let manifestURL = assets.appendingPathComponent("integrity.json")
        guard let data = try? Data(contentsOf: manifestURL),
              let integrity = try? JSONDecoder().decode(Integrity.self, from: data) else {
            status = "Resource integrity manifest is missing or invalid"
            return
        }

        for entry in integrity.files {
            let url = bundle.appendingPathComponent(entry.path)
            guard let fileSize = try? FileManager.default.attributesOfItem(
                    atPath: url.path)[.size] as? NSNumber,
                  entry.present else {
                status = "Missing resource: \(entry.path)"
                return
            }
            if let expected = entry.size, fileSize.int64Value != expected {
                status = "Size mismatch: \(entry.path)"
                return
            }
            if let expectedHash = entry.sha256,
               let bytes = try? Data(contentsOf: url),
               SHA256.hash(data: bytes).map({ String(format: "%02x", $0) }).joined()
                != expectedHash {
                status = "Checksum mismatch: \(entry.path)"
                return
            }
            checkedFiles += 1
        }
        ready = true
        status = "Ready — \(checkedFiles) original resources verified"
    }
}
