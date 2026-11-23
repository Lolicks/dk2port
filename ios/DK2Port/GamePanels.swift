import Foundation
import SwiftUI
import UIKit

// MARK: - Game command notifications (wire these in DK2GameViewController)
//
// All commands are posted on NotificationCenter.default with object: nil.
// Payload keys live in DK2GameCommand.Key. Every name is unique and stable.
//
//  dk2.command.build    -> category:String("room"|"trap"|"door"), index:Int, name:String
//  dk2.command.spell    -> index:Int, name:String
//  dk2.command.creature -> index:Int, name:String
//  dk2.command.pause    -> paused:Bool
//  dk2.command.speed    -> multiplier:Int
//  dk2.command.save     -> slot:String
//  dk2.command.load     -> slot:String
//  dk2.command.quit     -> (no payload)
//
// Example observer in GameViewController.swift:
//   let nc = NotificationCenter.default
//   names.forEach { nc.addObserver(self, selector: #selector(onCommand(_:)), name: $0, object: nil) }
//   @objc func onCommand(_ note: Notification) {
//     guard let info = note.userInfo else { return }
//     switch note.name {
//     case DK2GameCommand.build:
//       let cat = info[DK2GameCommand.Key.category] as? String ?? ""
//       let idx = info[DK2GameCommand.Key.index] as? Int ?? 0
//       ...
//     case DK2GameCommand.save:
//       let slot = info[DK2GameCommand.Key.slot] as? String ?? "slot1"
//       saveGame(name: slot)
//     ...
//     }
//   }
enum DK2GameCommand {
    static let center = NotificationCenter.default

    enum Key {
        static let category = "category"
        static let index = "index"
        static let name = "name"
        static let paused = "paused"
        static let multiplier = "multiplier"
        static let slot = "slot"
    }

    enum Category: String, Hashable {
        case room, trap, door
    }

    static let build = Notification.Name("dk2.command.build")
    static let spell = Notification.Name("dk2.command.spell")
    static let creature = Notification.Name("dk2.command.creature")
    static let pause = Notification.Name("dk2.command.pause")
    static let speed = Notification.Name("dk2.command.speed")
    static let save = Notification.Name("dk2.command.save")
    static let load = Notification.Name("dk2.command.load")
    static let quit = Notification.Name("dk2.command.quit")

    static let allNames: [Notification.Name] = [
        build, spell, creature, pause, speed, save, load, quit
    ]

    static func postBuild(category: Category, index: Int, name: String) {
        center.post(name: build, object: nil, userInfo: [
            Key.category: category.rawValue, Key.index: index, Key.name: name
        ])
    }

    static func postSpell(index: Int, name: String) {
        center.post(name: spell, object: nil, userInfo: [Key.index: index, Key.name: name])
    }

    static func postCreature(index: Int, name: String) {
        center.post(name: creature, object: nil, userInfo: [Key.index: index, Key.name: name])
    }

    static func postPause(_ paused: Bool) {
        center.post(name: pause, object: nil, userInfo: [Key.paused: paused])
    }

    static func postSpeed(_ multiplier: Int) {
        center.post(name: speed, object: nil, userInfo: [Key.multiplier: multiplier])
    }

    static func postSave(slot: String) {
        center.post(name: save, object: nil, userInfo: [Key.slot: slot])
    }

    static func postLoad(slot: String) {
        center.post(name: load, object: nil, userInfo: [Key.slot: slot])
    }

    static func postQuit() {
        center.post(name: quit, object: nil)
    }
}

// MARK: - Optional, defensive data loaders (local to the UI layer)

struct DK2PanelGameData: Decodable {
    let creatureNames: [String]
    let roomNames: [String]
    let trapNames: [String]
    let keeperSpellNames: [String]
    let creatureSpellNames: [String]
    let doorNames: [String]

    enum CodingKeys: String, CodingKey {
        case creatureNames = "creature_names"
        case roomNames = "room_names"
        case trapNames = "trap_names"
        case keeperSpellNames = "keeper_spell_names"
        case creatureSpellNames = "creature_spell_names"
        case doorNames = "door_names"
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        creatureNames = (try? c.decode([String].self, forKey: .creatureNames)) ?? []
        roomNames = (try? c.decode([String].self, forKey: .roomNames)) ?? []
        trapNames = (try? c.decode([String].self, forKey: .trapNames)) ?? []
        keeperSpellNames = (try? c.decode([String].self, forKey: .keeperSpellNames)) ?? []
        creatureSpellNames = (try? c.decode([String].self, forKey: .creatureSpellNames)) ?? []
        doorNames = (try? c.decode([String].self, forKey: .doorNames)) ?? []
    }
}

struct DK2PanelMission: Identifiable {
    let name: String
    let mapFile: String
    let category: String
    let campaignLevel: Int?
    let width: Int?
    let height: Int?
    let playerCount: Int?
    let enemyCount: Int?
    let thingCount: Int?

    var id: String { mapFile.isEmpty ? name : mapFile }
}

enum DK2PanelData {
    static let gameData: DK2PanelGameData? = DK2AssetLoader.load(
        DK2PanelGameData.self, "data/game_data.json")

    static let stats: [String: Any]? = {
        let url = DK2AssetLoader.root.appendingPathComponent("data/stats.json")
        guard let data = try? Data(contentsOf: url) else { return nil }
        return (try? JSONSerialization.jsonObject(with: data)) as? [String: Any]
    }()

    static let missions: [DK2PanelMission] = {
        let url = DK2AssetLoader.root.appendingPathComponent("data/missions.json")
        guard let data = try? Data(contentsOf: url),
              let rows = (try? JSONSerialization.jsonObject(with: data)) as? [[String: Any]] else {
            return []
        }
        return rows.compactMap { row in
            let name = row["name"] as? String ?? ""
            let map = row["map_file"] as? String ?? ""
            let nums = row["numeric_parameters"] as? [String: Any]
            func number(_ key: String) -> Int? {
                guard let entry = nums?[key] as? [String: Any] else { return nil }
                if let v = entry["value"] as? Int { return v }
                if let v = entry["value"] as? NSNumber { return v.intValue }
                return nil
            }
            return DK2PanelMission(
                name: name,
                mapFile: map,
                category: row["category"] as? String ?? "skirmish",
                campaignLevel: row["campaign_level"] as? Int,
                width: row["width"] as? Int,
                height: row["height"] as? Int,
                playerCount: number("player_count"),
                enemyCount: number("enemy_player_count"),
                thingCount: number("thing_count")
            )
        }
    }()

    static let balanceRoomCosts: [String: Int] = {
        let url = DK2AssetLoader.root.appendingPathComponent("data/balance.json")
        guard let data = try? Data(contentsOf: url),
              let root = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any],
              let costs = root["room_costs"] as? [String: Any] else { return [:] }
        var result: [String: Int] = [:]
        for (key, value) in costs {
            if let v = value as? Int { result[key] = v }
            else if let v = value as? NSNumber { result[key] = v.intValue }
        }
        return result
    }()

    private static func statEntry(_ section: String, _ name: String) -> [String: Any]? {
        guard let rows = stats?[section] as? [[String: Any]] else { return nil }
        return rows.first { ($0["name"] as? String) == name }
    }

    private static func statNumber(_ section: String, name: String, keys: [String]) -> Int? {
        guard let fields = statEntry(section, name)?["stats"] as? [String: Any] else { return nil }
        for key in keys {
            if let entry = fields[key] as? [String: Any] {
                if let v = entry["value"] as? Int { return v }
                if let v = entry["value"] as? NSNumber { return v.intValue }
                if let v = entry["value"] as? Double { return Int(v) }
            }
        }
        return nil
    }

    static func cost(for name: String, section: String, keys: [String]) -> Int? {
        if let v = statNumber(section, name: name, keys: keys) { return v }
        if section == "rooms", let v = balanceRoomCosts[name] { return v }
        return nil
    }

    static func hp(for name: String, section: String) -> Int? {
        statNumber(section, name: name, keys: ["hp", "health"])
    }

    static func pay(for name: String) -> Int? {
        statNumber("creatures", name: name, keys: ["pay"])
    }

    static func mission(named mapFile: String) -> DK2PanelMission? {
        missions.first { $0.mapFile == mapFile }
    }
}

// MARK: - Original artwork lookup with graceful fallback

enum DK2GameArt {
    static func frontEnd(_ relative: String) -> UIImage? { DK2Art.image(relative) }

    static func engine(_ relative: String) -> UIImage? {
        let url = DK2AssetLoader.root
            .appendingPathComponent("textures/EngineTextures")
            .appendingPathComponent(relative)
        return UIImage(contentsOfFile: url.path)
    }

    static func fallback() -> UIImage? { DK2Art.image("Bigbutton.PNG") }

    /// Maps a game-data name to original artwork, trying engine then FrontEnd art.
    static func image(for name: String, category: DK2GameCommand.Category? = nil) -> UIImage? {
        let key = normalized(name)
        let candidates = artNames(for: key, category: category)
        for path in candidates {
            if let image = engine(path) { return image }
            if let image = frontEnd(path) { return image }
        }
        return fallback()
    }

    private static func normalized(_ name: String) -> String {
        var s = name.lowercased()
        s = s.replacingOccurrences(of: "unique ", with: "")
        s = s.replacingOccurrences(of: " trap", with: "")
        s = s.replacingOccurrences(of: " room", with: "")
        s = s.replacingOccurrences(of: " chamber", with: "")
        s = s.replacingOccurrences(of: "work shop", with: "workshop")
        s = s.replacingOccurrences(of: " ", with: "_")
        s = s.replacingOccurrences(of: "-", with: "_")
        while s.contains("__") { s = s.replacingOccurrences(of: "__", with: "_") }
        return s.trimmingCharacters(in: CharacterSet(charactersIn: "_"))
    }

    private static func artNames(for key: String, category: DK2GameCommand.Category?) -> [String] {
        var names = [String]()
        func add(_ base: String) {
            names.append(base + ".png")
            names.append(base + ".PNG")
        }

        switch category {
        case .room?:
            switch key {
            case "treasury": add("Treasury")
            case "lair": add("lair")
            case "portal", "hero_portal": add("portal")
            case "hatchery": add("hatchery")
            case "dungeon_heart": add("heart")
            case "library": add("dark_library")
            case "training": add("training")
            case "wooden_bridge": add("Bridge_Wooden")
            case "guard": add("guard_room")
            case "workshop": add("GUI/Rooms/workshop")
            case "prison": add("prison")
            case "torture": add("torture")
            case "temple": add("temple")
            case "graveyard", "crypt": add("graveyard")
            case "casino": add("casino")
            case "combat_pit": add("pit")
            case "stone_bridge": add("Bridge_Stone")
            default: break
            }
        case .trap?:
            switch key {
            case "gas": add("gas")
            case "boulder": add("boulder")
            case "sentry", "hero_sentry": add("sentry")
            case "alarm", "hero_alarm": add("alarm")
            case "fireburst": add("fireburst")
            case "lightning": add("lightning")
            case "freeze": add("freeze")
            case "fear": add("fear")
            case "trigger": add("trigger")
            case "spike": add("spike")
            case "guard_post": add("Guard_Post")
            case "magic_door": add("magic_door")
            case "jack_in_the_box": add("tba")
            default: break
            }
        case .door?:
            switch key {
            case "wooden_door": add("GUI/Doors/wood_door"); add("barricade")
            case "steel_door": add("steel_door")
            case "braced_door": add("braced")
            case "magic_door": add("magic_door")
            case "secret_door": add("secret_door")
            case "barricade", "hero_barricade": add("barricade")
            case "hero_portcullis": add("prison")
            default: break
            }
        default:
            switch key {
            case "imp": add("imp")
            case "bile_demon": add("bile_demon")
            case "mistress": add("mistress")
            case "warlock": add("warlock")
            case "dark_elf": add("dark_elf")
            case "goblin": add("goblin")
            case "vampire": add("vampire")
            case "skeleton": add("skeleton")
            case "troll": add("troll")
            case "salamander": add("salamander")
            case "firefly", "unique_firefly": add("firefly")
            case "knight": add("knight")
            case "dwarf": add("dwarf")
            case "giant": add("giant")
            case "wizard": add("wizard")
            case "elven_archer": add("elf")
            case "thief": add("thief")
            case "monk": add("monk")
            case "fairy": add("fairy")
            case "king_reginald": add("king")
            case "black_knight": add("black_knight")
            case "dark_angel": add("dark_angel")
            case "rogue": add("rogue")
            case "guard": add("guard")
            case "horny": add("tba")
            case "stone_knight": add("stone_knight")
            case "lord_of_the_land": add("lord")
            case "royal_guard": add("royal_guard")
            case "prince_tristran", "prince_felix", "prince_balder": add("prince")
            case "maiden_of_the_nest", "jack": add("tba")
            default: break
            }
        }
        return names
    }
}

// MARK: - Panel item model

struct DK2PanelItem: Identifiable {
    let id: Int
    let name: String
    let cost: Int?
    let detail: String?
    let artName: String?
}

// MARK: - Reusable original-art button

struct DK2ArtTileButton: View {
    let item: DK2PanelItem
    let category: DK2GameCommand.Category?
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            ZStack(alignment: .bottom) {
                if let image = DK2GameArt.image(for: item.artName ?? item.name, category: category) {
                    Image(uiImage: image)
                        .resizable()
                        .scaledToFill()
                } else {
                    Rectangle().fill(Color(red: 0.16, green: 0.12, blue: 0.10))
                }
                LinearGradient(gradient: Gradient(colors: [.clear, .black.opacity(0.85)]),
                               startPoint: .center, endPoint: .bottom)
                VStack(spacing: 1) {
                    Text(item.name)
                        .font(.system(size: 10, weight: .semibold))
                        .foregroundColor(.white)
                        .lineLimit(2)
                        .multilineTextAlignment(.center)
                    if let cost = item.cost {
                        Text("\(cost)g")
                            .font(.system(size: 10, weight: .bold, design: .monospaced))
                            .foregroundColor(Color(red: 0.95, green: 0.82, blue: 0.42))
                    }
                }
                .padding(.horizontal, 2)
                .padding(.bottom, 3)
            }
            .frame(height: 88)
            .cornerRadius(5)
            .overlay(RoundedRectangle(cornerRadius: 5)
                .stroke(Color(red: 0.45, green: 0.32, blue: 0.18), lineWidth: 1))
        }
        .buttonStyle(.plain)
    }
}

// MARK: - Build panels (rooms / traps / doors)

struct DK2BuildPanel: View {
    @State private var tab: DK2GameCommand.Category

    init(defaultTab: DK2GameCommand.Category = .room) {
        _tab = State(initialValue: defaultTab)
    }

    private var items: [DK2PanelItem] {
        let data = DK2PanelData.gameData
        switch tab {
        case .room:
            let names = data?.roomNames ?? []
            return names.enumerated().map { index, name in
                DK2PanelItem(id: index, name: name,
                             cost: DK2PanelData.cost(for: name, section: "rooms", keys: ["cost"]),
                             detail: nil, artName: name)
            }
        case .trap:
            let names = data?.trapNames ?? []
            return names.enumerated().map { index, name in
                DK2PanelItem(id: index, name: name,
                             cost: DK2PanelData.cost(for: name, section: "traps", keys: ["gold_cost"]),
                             detail: nil, artName: name)
            }
        case .door:
            let names = data?.doorNames ?? []
            return names.enumerated().map { index, name in
                DK2PanelItem(id: index, name: name,
                             cost: DK2PanelData.cost(for: name, section: "doors", keys: ["gold_cost"]),
                             detail: nil, artName: name)
            }
        }
    }

    var body: some View {
        VStack(spacing: 8) {
            Picker("Build", selection: $tab) {
                Text("Rooms").tag(DK2GameCommand.Category.room)
                Text("Traps").tag(DK2GameCommand.Category.trap)
                Text("Doors").tag(DK2GameCommand.Category.door)
            }
            .pickerStyle(.segmented)
            .colorMultiply(Color(red: 0.95, green: 0.82, blue: 0.55))

            ScrollView {
                LazyVGrid(columns: [GridItem(.adaptive(minimum: 92), spacing: 8)], spacing: 8) {
                    ForEach(items) { item in
                        DK2ArtTileButton(item: item, category: tab) {
                            DK2GameCommand.postBuild(category: tab, index: item.id, name: item.name)
                        }
                    }
                }
                .padding(4)
            }
            if items.isEmpty {
                Spacer()
                Text("No build data bundled.").foregroundColor(.gray)
                Spacer()
            }
        }
    }
}

// MARK: - Spell panel (keeper + creature spell reference)

struct DK2SpellPanel: View {
    @State private var showCreatureSpells = false

    private var keeperItems: [DK2PanelItem] {
        (DK2PanelData.gameData?.keeperSpellNames ?? []).enumerated().map { index, name in
            DK2PanelItem(id: index, name: name,
                         cost: DK2PanelData.cost(for: name, section: "keeper_spells", keys: ["mana_cost"]),
                         detail: nil, artName: name)
        }
    }

    private var creatureItems: [DK2PanelItem] {
        (DK2PanelData.gameData?.creatureSpellNames ?? []).enumerated().map { index, name in
            DK2PanelItem(id: index, name: name, cost: nil, detail: "Creature spell", artName: name)
        }
    }

    var body: some View {
        VStack(spacing: 8) {
            Text("Keeper Spells").font(.headline).foregroundColor(Color(red: 0.95, green: 0.82, blue: 0.55))
            ScrollView {
                LazyVGrid(columns: [GridItem(.adaptive(minimum: 92), spacing: 8)], spacing: 8) {
                    ForEach(keeperItems) { item in
                        DK2ArtTileButton(item: item, category: nil) {
                            DK2GameCommand.postSpell(index: item.id, name: item.name)
                        }
                    }
                }
                .padding(4)
            }
            Button { showCreatureSpells.toggle() } label: {
                Text(showCreatureSpells ? "Hide creature spells (\(creatureItems.count))"
                                        : "Creature spells (\(creatureItems.count))")
                    .foregroundColor(Color(red: 0.95, green: 0.82, blue: 0.55))
            }
            if showCreatureSpells {
                ScrollView {
                    LazyVGrid(columns: [GridItem(.adaptive(minimum: 92), spacing: 8)], spacing: 8) {
                        ForEach(creatureItems) { item in
                            DK2ArtTileButton(item: item, category: nil) {
                                DK2GameCommand.postSpell(index: item.id, name: item.name)
                            }
                        }
                    }
                    .padding(4)
                }
            }
        }
    }
}

// MARK: - Creature panel (46 creatures, selectable)

struct DK2CreaturePanel: View {
    @State private var selectedIndex: Int? = nil
    @State private var query = ""

    private var creatures: [DK2PanelItem] {
        (DK2PanelData.gameData?.creatureNames ?? []).enumerated().map { index, name in
            let hp = DK2PanelData.hp(for: name, section: "creatures")
            let pay = DK2PanelData.pay(for: name)
            var detail: String? = nil
            if let hp = hp { detail = "HP \(hp)" }
            if let pay = pay { detail = (detail ?? "") + "  Pay \(pay)" }
            return DK2PanelItem(id: index, name: name, cost: nil, detail: detail, artName: name)
        }
    }

    private var filtered: [DK2PanelItem] {
        let q = query.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        if q.isEmpty { return creatures }
        return creatures.filter { $0.name.lowercased().contains(q) }
    }

    var body: some View {
        VStack(spacing: 6) {
            TextField("Search creatures", text: $query)
                .textFieldStyle(.roundedBorder)
                .padding(.horizontal, 4)
            ScrollView {
                LazyVGrid(columns: [GridItem(.adaptive(minimum: 92), spacing: 8)], spacing: 8) {
                    ForEach(filtered) { item in
                        DK2ArtTileButton(item: item, category: nil) {
                            selectedIndex = item.id
                            DK2GameCommand.postCreature(index: item.id, name: item.name)
                        }
                        .overlay(RoundedRectangle(cornerRadius: 5)
                            .stroke(selectedIndex == item.id ? Color.yellow : Color.clear, lineWidth: 2))
                    }
                }
                .padding(4)
            }
            if let index = selectedIndex, index < creatures.count {
                Text("Selected: \(creatures[index].name)  \(creatures[index].detail ?? "")")
                    .font(.caption).foregroundColor(.white)
            }
            if filtered.isEmpty {
                Spacer()
                Text("No creature data bundled.").foregroundColor(.gray)
                Spacer()
            }
        }
    }
}

// MARK: - Objectives panel (mission brief from missions.json)

struct DK2ObjectivesPanel: View {
    let mission: DK2Mission?

    private var detail: DK2PanelMission? {
        guard let map = mission?.mapFile else { return nil }
        return DK2PanelData.mission(named: map)
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                Text(mission?.name ?? "Mission")
                    .font(.title2.bold())
                    .foregroundColor(Color(red: 0.95, green: 0.82, blue: 0.55))
                Text(objectiveText)
                    .font(.body)
                    .foregroundColor(.white)
                Divider().background(Color.gray)
                infoRow("Category", detail?.category ?? mission?.category ?? "skirmish")
                if let level = detail?.campaignLevel ?? mission?.campaignLevel {
                    infoRow("Campaign level", "\(level)")
                }
                if let w = detail?.width ?? mission?.width,
                   let h = detail?.height ?? mission?.height {
                    infoRow("Map size", "\(w) × \(h) tiles")
                }
                if let players = detail?.playerCount {
                    infoRow("Players", "\(players)")
                }
                if let enemies = detail?.enemyCount {
                    infoRow("Enemy keepers (inferred)", "\(enemies)")
                }
                if let things = detail?.thingCount {
                    infoRow("Map objects", "\(things)")
                }
                if detail == nil {
                    Text("Detailed brief unavailable — missions.json not bundled.")
                        .font(.caption).foregroundColor(.gray)
                }
            }
            .padding()
        }
    }

    private var objectiveText: String {
        "Destroy the rival keepers and claim their dungeons. Protect your Dungeon Heart, "
            + "dig out your lair, and raise an army of creatures. Enemy counts and map facts "
            + "below are read from the recovered mission data where available."
    }

    @ViewBuilder
    private func infoRow(_ label: String, _ value: String) -> some View {
        HStack {
            Text(label).foregroundColor(.gray)
            Spacer()
            Text(value).foregroundColor(.white)
        }
        .font(.system(size: 14))
    }
}
