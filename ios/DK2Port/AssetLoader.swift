import Foundation

struct DK2Manifest: Decodable {
    let format: String
    let version: Int
    let maps: Int
    let missions: Int?
    let gameData: GameDataSection
    let audio: AudioSection

    struct GameDataSection: Decodable {
        let counts: [String: Int]
    }
    struct AudioSection: Decodable {
        let banks: Int
        let samples: Int
        let decoded: Int
    }
}

struct DK2MapEntry: Decodable, Identifiable {
    let name: String
    let file: String
    let width: Int
    let height: Int
    let sha256: String

    var id: String { file }
}

struct DK2MapIndex: Decodable {
    let maps: [DK2MapEntry]
}

struct DK2GameData: Decodable {
    let creatureNames: [String]
    let roomNames: [String]
    let trapNames: [String]
    let keeperSpellNames: [String]

    enum CodingKeys: String, CodingKey {
        case creatureNames = "creature_names"
        case roomNames = "room_names"
        case trapNames = "trap_names"
        case keeperSpellNames = "keeper_spell_names"
    }
}

struct DK2Mission: Decodable, Identifiable {
    let name: String
    let mapFile: String
    let width: Int
    let height: Int
    let category: String?
    let campaignLevel: Int?
    let campaignVariant: String?

    enum CodingKeys: String, CodingKey {
        case name, width, height, category
        case mapFile = "map_file"
        case campaignLevel = "campaign_level"
        case campaignVariant = "campaign_variant"
    }

    var id: String { mapFile }

    func mapEntry() -> DK2MapEntry {
        DK2MapEntry(name: name, file: mapFile, width: width,
                    height: height, sha256: "")
    }
}

struct DK2AudioIndex: Decodable {
    struct AudioFile: Decodable {
        let wav: String?
        let name: String
    }
    let banks: Int
    let samples: Int
    let decoded: Int
    let files: [AudioFile]
}

enum DK2AssetLoader {
    static let root: URL = {
        if let explicit = ProcessInfo.processInfo.environment["DK2_ASSET_ROOT"],
           !explicit.isEmpty {
            return URL(fileURLWithPath: explicit)
        }
        if let bundled = Bundle.main.url(forResource: "Assets", withExtension: nil) {
            return bundled
        }
        let documents = FileManager.default.urls(for: .documentDirectory,
                                                 in: .userDomainMask)[0]
            .appendingPathComponent("Assets")
        if FileManager.default.fileExists(atPath: documents.path) {
            return documents
        }
        return Bundle.main.bundleURL.appendingPathComponent("Assets")
    }()

    static func load<T: Decodable>(_ type: T.Type, _ relative: String) -> T? {
        let url = root.appendingPathComponent(relative)
        guard let data = try? Data(contentsOf: url) else { return nil }
        return try? JSONDecoder().decode(type, from: data)
    }

    static var manifest: DK2Manifest? { load(DK2Manifest.self, "manifest.json") }
    static var maps: DK2MapIndex? { load(DK2MapIndex.self, "maps/maps.json") }
    static var missions: [DK2Mission]? { load([DK2Mission].self, "data/missions.json") }
    static var gameData: DK2GameData? { load(DK2GameData.self, "data/game_data.json") }

    static func mapURL(_ entry: DK2MapEntry) -> URL {
        root.appendingPathComponent(entry.file)
    }

    static func saveURL(name: String) -> URL {
        let dir = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        return dir.appendingPathComponent("\(name).dk2sav")
    }
}
