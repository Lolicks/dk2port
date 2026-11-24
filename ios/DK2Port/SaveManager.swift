import Foundation

/// Local, self-contained save system. No iCloud, no downloads, no original
/// game files required. Slots live in Documents/saves.
final class DK2SaveManager {
    struct SlotInfo {
        let slot: String
        let exists: Bool
        let mission: String
        let timestamp: UInt64
        let tick: UInt32
        let creatures: UInt32
        let valid: Bool
        let size: Int64
    }

    static let shared = DK2SaveManager()
    static let manualSlots = ["slot1", "slot2", "slot3", "slot4", "slot5"]
    static let quickSlot = "quick"
    static let autoSlot = "auto"
    static let autosaveInterval: Float = 300.0  // game seconds

    private let fm = FileManager.default
    private var lastAutosaveElapsed: Float = 0
    private var lastAutosaveMission = ""

    private var directory: URL {
        let docs = fm.urls(for: .documentDirectory, in: .userDomainMask)[0]
        return docs.appendingPathComponent("saves", isDirectory: true)
    }

    private init() {
        try? fm.createDirectory(at: directory, withIntermediateDirectories: true)
    }

    func url(for slot: String) -> URL {
        directory.appendingPathComponent("\(slot).dk2s")
    }

    func info(for slot: String) -> SlotInfo? {
        let url = self.url(for: slot)
        guard fm.fileExists(atPath: url.path) else {
            return SlotInfo(slot: slot, exists: false, mission: "", timestamp: 0,
                            tick: 0, creatures: 0, valid: false, size: 0)
        }
        let size = (try? fm.attributesOfItem(atPath: url.path)[.size] as? NSNumber)?
            .int64Value ?? 0
        let meta = try? DK2Bridge.saveInfo(atPath: url.path)
        return SlotInfo(
            slot: slot,
            exists: true,
            mission: meta?["mission"] as? String ?? "",
            timestamp: meta?["timestamp"] as? UInt64 ?? 0,
            tick: meta?["tick"] as? UInt32 ?? 0,
            creatures: meta?["creatures"] as? UInt32 ?? 0,
            valid: meta?["valid"] as? Bool ?? false,
            size: size)
    }

    func allSlots() -> [SlotInfo] {
        (DK2SaveManager.manualSlots + [DK2SaveManager.quickSlot,
                                       DK2SaveManager.autoSlot])
            .compactMap { info(for: $0) }
    }

    @discardableResult
    func save(slot: String, bridge: DK2Bridge) -> Bool {
        let target = url(for: slot)
        if fm.fileExists(atPath: target.path) {
            let backup = target.deletingPathExtension()
                .appendingPathExtension("bak")
            try? fm.removeItem(at: backup)
            try? fm.copyItem(at: target, to: backup)
        }
        return bridge.save(toPath: target.path)
    }

    @discardableResult
    func load(slot: String, bridge: DK2Bridge) -> Bool {
        let target = url(for: slot)
        guard fm.fileExists(atPath: target.path) else { return false }
        return bridge.load(fromPath: target.path)
    }

    @discardableResult
    func delete(slot: String) -> Bool {
        guard let info = info(for: slot), info.exists else { return false }
        try? fm.removeItem(at: url(for: slot))
        return true
    }

    /// Called every frame with the current snapshot. Saves to the auto slot
    /// once per AUTOSAVE_INTERVAL of *game* time.
    func updateAutosave(snapshot: NSDictionary, bridge: DK2Bridge) {
        let mission = snapshot["mission"] as? String ?? ""
        let elapsed = snapshot["elapsed"] as? Float ?? 0
        if mission.isEmpty { return }
        if mission != lastAutosaveMission {
            lastAutosaveMission = mission
            lastAutosaveElapsed = elapsed
        }
        if elapsed - lastAutosaveElapsed >= DK2SaveManager.autosaveInterval {
            if save(slot: DK2SaveManager.autoSlot, bridge: bridge) {
                lastAutosaveElapsed = elapsed
            }
        }
    }
}
