import SwiftUI

struct DK2HUD: View {
    let state: NSDictionary

    private func number(_ key: String) -> String {
        if let value = state[key] as? Float {
            return String(format: "%.0f", value)
        }
        return "0"
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("Gold \(number("gold")) / \(number("goldCapacity"))")
            Text("Creatures \(number("creatures"))   Rooms \(number("rooms"))")
            Text("Tick \(state["ticks"] ?? 0)")
            if (state["victory"] as? Bool) == true {
                Text("VICTORY").foregroundColor(.green)
            }
        }
        .font(.system(size: 12, weight: .semibold, design: .monospaced))
        .foregroundColor(.white)
        .padding(8)
        .background(Color.black.opacity(0.55))
        .cornerRadius(6)
        .padding(.leading, 12)
        .padding(.top, 32)
    }
}

struct DK2MenuView: View {
    let missions: [DK2Mission]
    let onStart: (DK2MapEntry) -> Void

    private var campaign: [DK2Mission] {
        missions.filter { $0.category == "campaign" }
            .sorted { (a, b) in
                let la = a.campaignLevel ?? 999, lb = b.campaignLevel ?? 999
                if la != lb { return la < lb }
                return (a.campaignVariant ?? "") < (b.campaignVariant ?? "")
            }
    }
    private var others: [DK2Mission] {
        missions.filter { $0.category != "campaign" }
            .sorted { $0.name.localizedCompare($1.name) == .orderedAscending }
    }

    private func row(_ m: DK2Mission) -> some View {
        Button { onStart(m.mapEntry()) } label: {
            VStack(alignment: .leading) {
                Text(m.name).font(.headline)
                Text("\(m.width) × \(m.height) tiles")
                    .font(.caption).foregroundColor(.secondary)
            }
        }
    }

    var body: some View {
        NavigationView {
            List {
                if !campaign.isEmpty {
                    Section("Campaign") { ForEach(campaign) { row($0) } }
                }
                if !others.isEmpty {
                    Section("Skirmish & Secret") { ForEach(others) { row($0) } }
                }
            }
            .navigationTitle("Dungeon Keeper 2")
        }
    }
}

struct DK2RootView: View {
    @State private var selected: DK2MapEntry?
    @State private var state: NSDictionary = [:]
    @StateObject private var resources = DK2ResourceManager.shared

    var body: some View {
        ZStack(alignment: .topLeading) {
            if let mission = selected {
                DK2GameView(mission: mission, onState: { state = $0 })
                    .ignoresSafeArea()
                DK2HUD(state: state)
                HStack {
                    Button("Menu") { selected = nil }
                    Spacer()
                }
                .padding()
            } else {
                VStack {
                    if !resources.ready {
                        ProgressView(resources.status)
                            .foregroundColor(.white)
                    } else {
                        DK2FrontEndView(missions: DK2AssetLoader.missions ?? []) { map in
                            selected = map
                        }
                    }
                }
                .onAppear { resources.initialize() }
            }
        }
    }
}

@main
struct DK2App: App {
    var body: some Scene {
        WindowGroup {
            DK2RootView()
        }
    }
}
