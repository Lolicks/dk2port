import Foundation
import SwiftUI
import UIKit

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

struct DK2PanelGameData: Decodable {
    let creatureNames: [String]
    let roomNames: [String]
    let trapNames: [String]
    let keeperSpellNames: [String]
    let creatureSpellNames: [String]
    let doorNames: [String]
    let objectNames: [String]

    enum CodingKeys: String, CodingKey {
        case creatureNames = "creature_names"
        case roomNames = "room_names"
        case trapNames = "trap_names"
        case keeperSpellNames = "keeper_spell_names"
        case creatureSpellNames = "creature_spell_names"
        case doorNames = "door_names"
        case objectNames = "object_names"
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        creatureNames = (try? c.decode([String].self, forKey: .creatureNames)) ?? []
        roomNames = (try? c.decode([String].self, forKey: .roomNames)) ?? []
        trapNames = (try? c.decode([String].self, forKey: .trapNames)) ?? []
        keeperSpellNames = (try? c.decode([String].self, forKey: .keeperSpellNames)) ?? []
        creatureSpellNames = (try? c.decode([String].self, forKey: .creatureSpellNames)) ?? []
        doorNames = (try? c.decode([String].self, forKey: .doorNames)) ?? []
        objectNames = (try? c.decode([String].self, forKey: .objectNames)) ?? []
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

enum DK2GameArt {
    static func frontEnd(_ relative: String) -> UIImage? { DK2Art.image(relative) }

    static func engine(_ relative: String) -> UIImage? {
        let url = DK2AssetLoader.root
            .appendingPathComponent("textures/EngineTextures")
            .appendingPathComponent(relative)
        return UIImage(contentsOfFile: url.path)
    }

    static func fallback() -> UIImage? { DK2UIStyle.image("Bigbutton.PNG") }

    static func gui(_ path: String, fallback: String? = nil) -> UIImage? {
        var candidates: [String] = [path]
        candidates.append(path.replacingOccurrences(of: "Textures/GUI/", with: "textures/GUI/"))
        let base = (path as NSString).lastPathComponent
        if !base.isEmpty {
            candidates.append(base)
            candidates.append("textures/EngineTextures/\(base)")
        }
        let stripped = path.replacingOccurrences(of: "Textures/GUI/", with: "")
                           .replacingOccurrences(of: "textures/GUI/", with: "")
        if stripped != path {
            candidates.append("textures/EngineTextures/\(stripped)")
            candidates.append(stripped)
        }
        for candidate in candidates {
            if let image = DK2UIStyle.image(candidate) { return image }
        }
        if let fallbackName = fallback, let image = DK2UIStyle.image(fallbackName) { return image }
        return fallback()
    }

    static func tabIcon(_ tab: String, active: Bool = false) -> UIImage? {
        let singular: String
        let plural: String
        switch tab {
        case "creature": singular = "creature"; plural = "creatures"
        case "room": singular = "room"; plural = "rooms"
        case "spell": singular = "spell"; plural = "spells"
        case "workshop": singular = "workshop"; plural = "workshop"
        default: singular = tab; plural = tab
        }
        if active {
            return gui("Textures/GUI/Icons/selected-\(singular).png",
                       fallback: "Hilight_Frame.png")
        }
        return gui("Textures/GUI/Tabs/t-\(plural).png", fallback: "Bigbutton.PNG")
    }

    static func image(for name: String, category: DK2GameCommand.Category? = nil,
                      kind: String? = nil) -> UIImage? {
        let key = normalized(name)
        let candidates = artNames(for: key, category: category, kind: kind)
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

    private static func artNames(for key: String, category: DK2GameCommand.Category?,
                                 kind: String?) -> [String] {
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
            case "training", "training_room": add("training")
            case "wooden_bridge": add("Bridge_Wooden")
            case "guard", "guard_room": add("guard_room")
            case "workshop", "work_shop": add("GUI/Rooms/workshop")
            case "prison": add("prison")
            case "torture", "torture_chamber": add("torture")
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
            switch kind ?? "" {
            case "spell":
                switch key {
                case "possession": add("goblin")
                case "create_imp": add("imp")
                case "oneshotreaper", "reaper": add("reaper")
                case "create_gold": add("Treasury")
                case "call_to_arms": add("tba")
                default: add("tba")
                }
            case "workshop":
                switch key {
                case "call_to_arms": add("tba")
                case "spell_book": add("tba")
                case "portal_gem": add("portal")
                case "anvil": add("tba")
                default: add("tba")
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
        }
        return names
    }
}

struct DK2PanelItem: Identifiable {
    let id: Int
    let name: String
    let cost: Int?
    let detail: String?
    let artName: String?
    let kind: String?
}

struct DK2GameTab: Identifiable {
    let key: String
    let titleID: Int
    let hintID: Int
    let tipID: Int
    var id: String { key }
}

struct DK2BuildSubTab: Identifiable {
    let category: DK2GameCommand.Category
    let titleID: Int
    var id: DK2GameCommand.Category { category }
}

struct DK2ArtTileButton: View {
    let item: DK2PanelItem
    let category: DK2GameCommand.Category?
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            ZStack(alignment: .bottom) {
                if let image = DK2GameArt.image(for: item.artName ?? item.name,
                                                category: category, kind: item.kind) {
                    Image(uiImage: image)
                        .resizable()
                        .scaledToFill()
                } else {
                    Rectangle().fill(DK2UIStyle.panelDark)
                }
                LinearGradient(gradient: Gradient(colors: [.clear, .black.opacity(0.85)]),
                               startPoint: .center, endPoint: .bottom)
                VStack(spacing: 1) {
                    DK2BitmapText(item.name, font: "Ingame10", size: 10,
                                  color: .white)
                    if let cost = item.cost {
                        DK2BitmapText("\(cost)g", font: "Ingame10", size: 10,
                                      color: DK2UIStyle.gold)
                    }
                }
                .padding(.horizontal, 2)
                .padding(.bottom, 3)
            }
            .frame(height: 84)
            .cornerRadius(5)
            .overlay(RoundedRectangle(cornerRadius: 5)
                .stroke(DK2UIStyle.border, lineWidth: 1))
        }
        .buttonStyle(.plain)
        .allowsHitTesting(true)
    }
}

struct DK2TileFlow: View {
    let items: [DK2PanelItem]
    let category: DK2GameCommand.Category?
    let maxRows: Int
    let tileWidth: CGFloat
    let action: (DK2PanelItem) -> Void

    @State private var page = 0

    var body: some View {
        GeometryReader { geo in
            let perRow = max(1, Int(geo.size.width / max(1, tileWidth)))
            let capacity = max(1, perRow * max(1, maxRows))
            let pages = max(1, (items.count + capacity - 1) / capacity)
            let current = min(Swift.max(0, page), pages - 1)
            let visible = Array(items.dropFirst(current * capacity).prefix(capacity))
            let rows = max(0, (visible.count + perRow - 1) / perRow)
            VStack(alignment: .leading, spacing: 6) {
                ForEach(0..<rows, id: \.self) { row in
                    HStack(spacing: 6) {
                        ForEach(visible.dropFirst(row * perRow).prefix(perRow)) { item in
                            DK2ArtTileButton(item: item, category: category) { action(item) }
                        }
                        Spacer(minLength: 0)
                    }
                }
                Spacer(minLength: 0)
                HStack(spacing: 8) {
                    Button {
                        page = Swift.max(0, current - 1)
                    } label: {
                        Text("<").foregroundColor(DK2UIStyle.gold)
                    }
                    Spacer()
                    Text("\(current + 1) / \(pages)")
                        .foregroundColor(DK2UIStyle.parchment)
                    Spacer()
                    Button {
                        page = min(pages - 1, current + 1)
                    } label: {
                        Text(">").foregroundColor(DK2UIStyle.gold)
                    }
                }
                .font(.system(size: 11))
                .buttonStyle(.plain)
                .allowsHitTesting(true)
            }
            .frame(width: geo.size.width, alignment: .leading)
        }
    }
}

struct DK2GameTabStrip: View {
    let tabs: [DK2GameTab]
    @Binding var selection: String
    let hint: Binding<String>
    let tooltip: Binding<String>

    var body: some View {
        HStack(spacing: 4) {
            ForEach(tabs) { tab in
                Button {
                    selection = tab.key
                    DK2UIStyle.postFrontEndSound()
                } label: {
                    ZStack {
                        if let icon = DK2GameArt.tabIcon(tab.key, active: false) {
                            Image(uiImage: icon).resizable().scaledToFit()
                        }
                        if selection == tab.key,
                           let active = DK2GameArt.tabIcon(tab.key, active: true) {
                            Image(uiImage: active).resizable().scaledToFit()
                        }
                    }
                    .frame(width: 92, height: 44)
                    .padding(3)
                    .background(selection == tab.key ? DK2UIStyle.gold.opacity(0.28)
                                                     : Color.black.opacity(0.55))
                    .overlay(RoundedRectangle(cornerRadius: 5)
                        .stroke(selection == tab.key ? DK2UIStyle.gold : DK2UIStyle.border,
                                lineWidth: selection == tab.key ? 2 : 1))
                }
                .buttonStyle(.plain)
                .allowsHitTesting(true)
                .background(DK2HoverArea { active in
                    hint.wrappedValue = active ? DK2L10n.shared[tab.hintID] : ""
                    tooltip.wrappedValue = active ? DK2L10n.shared[tab.tipID] : ""
                })
            }
        }
    }
}

struct DK2BuildPanel: View {
    @State private var tab: DK2GameCommand.Category

    init(defaultTab: DK2GameCommand.Category = .room) {
        _tab = State(initialValue: defaultTab)
    }

    private var subTabs: [DK2BuildSubTab] {
        [
            DK2BuildSubTab(category: .room, titleID: 1809),
            DK2BuildSubTab(category: .trap, titleID: 324),
            DK2BuildSubTab(category: .door, titleID: 330)
        ]
    }

    private var items: [DK2PanelItem] {
        let data = DK2PanelData.gameData
        switch tab {
        case .room:
            let names = data?.roomNames ?? []
            return names.enumerated().map { index, name in
                DK2PanelItem(id: index, name: name,
                             cost: DK2PanelData.cost(for: name, section: "rooms", keys: ["cost"]),
                             detail: nil, artName: name, kind: nil)
            }
        case .trap:
            let names = data?.trapNames ?? []
            return names.enumerated().map { index, name in
                DK2PanelItem(id: index, name: name,
                             cost: DK2PanelData.cost(for: name, section: "traps", keys: ["gold_cost"]),
                             detail: nil, artName: name, kind: nil)
            }
        case .door:
            let names = data?.doorNames ?? []
            return names.enumerated().map { index, name in
                DK2PanelItem(id: index, name: name,
                             cost: DK2PanelData.cost(for: name, section: "doors", keys: ["gold_cost"]),
                             detail: nil, artName: name, kind: nil)
            }
        }
    }

    var body: some View {
        VStack(spacing: 6) {
            HStack(spacing: 4) {
                ForEach(subTabs) { sub in
                    Button {
                        tab = sub.category
                        DK2UIStyle.postFrontEndSound()
                    } label: {
                        DK2BitmapText(DK2L10n.shared[sub.titleID], font: "Ingame14", size: 14,
                                      color: tab == sub.category ? DK2UIStyle.gold
                                                                 : DK2UIStyle.parchment)
                            .padding(.horizontal, 8)
                            .padding(.vertical, 4)
                            .background(tab == sub.category ? Color.black.opacity(0.7)
                                                            : Color.black.opacity(0.35))
                            .overlay(RoundedRectangle(cornerRadius: 4)
                                .stroke(tab == sub.category ? DK2UIStyle.gold : DK2UIStyle.border,
                                        lineWidth: 1))
                    }
                    .buttonStyle(.plain)
                    .allowsHitTesting(true)
                }
                Spacer()
            }
            DK2TileFlow(items: items, category: tab, maxRows: 2, tileWidth: 98) { item in
                DK2GameCommand.postBuild(category: tab, index: item.id, name: item.name)
            }
        }
    }
}

struct DK2SpellPanel: View {
    @State private var showCreatureSpells = false

    private var keeperItems: [DK2PanelItem] {
        (DK2PanelData.gameData?.keeperSpellNames ?? []).enumerated().map { index, name in
            DK2PanelItem(id: index, name: name,
                         cost: DK2PanelData.cost(for: name, section: "keeper_spells",
                                                 keys: ["mana_cost"]),
                         detail: nil, artName: name, kind: "spell")
        }
    }

    private var creatureItems: [DK2PanelItem] {
        (DK2PanelData.gameData?.creatureSpellNames ?? []).enumerated().map { index, name in
            DK2PanelItem(id: index, name: name, cost: nil, detail: nil,
                         artName: name, kind: "spell")
        }
    }

    var body: some View {
        VStack(spacing: 6) {
            HStack {
                DK2BitmapText(DK2L10n.shared[320], font: "Ingame14", size: 14,
                              color: DK2UIStyle.gold)
                Spacer()
                Button {
                    showCreatureSpells.toggle()
                    DK2UIStyle.postFrontEndSound()
                } label: {
                    DK2BitmapText(showCreatureSpells ? "Creature Spells" : "Keeper Spells",
                                  font: "Ingame10", size: 10, color: DK2UIStyle.parchment)
                }
                .buttonStyle(.plain)
                .allowsHitTesting(true)
            }
            DK2TileFlow(items: showCreatureSpells ? creatureItems : keeperItems,
                        category: nil, maxRows: 2, tileWidth: 98) { item in
                DK2GameCommand.postSpell(index: item.id, name: item.name)
            }
        }
    }
}

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
            return DK2PanelItem(id: index, name: name, cost: nil, detail: detail,
                                artName: name, kind: "creature")
        }
    }

    private var filtered: [DK2PanelItem] {
        let q = query.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        if q.isEmpty { return creatures }
        return creatures.filter { $0.name.lowercased().contains(q) }
    }

    var body: some View {
        VStack(spacing: 6) {
            HStack {
                TextField(DK2L10n.shared[1807], text: $query)
                    .textFieldStyle(.roundedBorder)
                if let index = selectedIndex, index < creatures.count {
                    DK2BitmapText(creatures[index].detail ?? creatures[index].name,
                                  font: "Ingame10", size: 10, color: DK2UIStyle.parchment)
                }
            }
            DK2TileFlow(items: filtered, category: nil, maxRows: 2, tileWidth: 98) { item in
                selectedIndex = item.id
                DK2GameCommand.postCreature(index: item.id, name: item.name)
            }
        }
    }
}

struct DK2WorkshopPanel: View {
    private let workshopNames = [
        "Crate Alarm", "Training Dummy", "Call To Arms", "Anvil", "Steel Bucket",
        "Portal Gem", "Torture Wheel", "Roulette Wheel", "Spell Book", "Training Target"
    ]

    private var items: [DK2PanelItem] {
        let available = Set(DK2PanelData.gameData?.objectNames ?? [])
        return workshopNames.enumerated().compactMap { index, name in
            guard available.contains(name) else { return nil }
            return DK2PanelItem(id: index, name: name,
                                cost: DK2PanelData.cost(for: name, section: "objects",
                                                       keys: ["mana_value"]),
                                detail: nil, artName: name, kind: "workshop")
        }
    }

    var body: some View {
        VStack(spacing: 6) {
            HStack {
                DK2BitmapText(DK2L10n.shared[467], font: "Ingame14", size: 14,
                              color: DK2UIStyle.gold)
                Spacer()
            }
            DK2TileFlow(items: items, category: nil, maxRows: 2, tileWidth: 98) { _ in
                DK2UIStyle.postFrontEndSound()
            }
        }
    }
}

struct DK2ObjectivesPanel: View {
    let mission: DK2Mission?

    private var detail: DK2PanelMission? {
        guard let map = mission?.mapFile else { return nil }
        return DK2PanelData.mission(named: map)
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                DK2BitmapText(mission?.name ?? "Mission",
                              font: "Ingame18", size: 18, color: DK2UIStyle.gold)
                DK2BitmapText(DK2L10n.shared[1408], font: "Ingame14", size: 14,
                              color: DK2UIStyle.gold)
                DK2BitmapText(objectiveText, font: "Ingame14", size: 14,
                              color: DK2UIStyle.parchment)
                DK2BitmapText(DK2L10n.shared[1409], font: "Ingame14", size: 14,
                              color: DK2UIStyle.gold)
                infoRow("Category", detail?.category ?? mission?.category ?? "skirmish")
                if let level = detail?.campaignLevel ?? mission?.campaignLevel {
                    infoRow("Campaign level", "\(level)")
                }
                if let w = detail?.width ?? mission?.width,
                   let h = detail?.height ?? mission?.height {
                    infoRow("Map size", "\(w) × \(h)")
                }
                if let players = detail?.playerCount {
                    infoRow("Players", "\(players)")
                }
                if let things = detail?.thingCount {
                    infoRow("Map objects", "\(things)")
                }
                if detail == nil {
                    DK2BitmapText("missions.json unavailable", font: "Ingame10", size: 10,
                                  color: .gray)
                }
            }
            .padding()
        }
    }

    private var objectiveText: String {
        "Destroy the rival keepers and claim their dungeons. Protect your Dungeon Heart, "
            + "dig out your lair, and raise an army of creatures."
    }

    @ViewBuilder
    private func infoRow(_ label: String, _ value: String) -> some View {
        HStack {
            DK2BitmapText(label, font: "Ingame10", size: 10, color: DK2UIStyle.parchment)
            Spacer()
            DK2BitmapText(value, font: "Ingame10", size: 10, color: .white)
        }
    }
}
