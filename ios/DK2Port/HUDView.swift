import Foundation
import SwiftUI
import UIKit

enum DK2PanelKind: String, Hashable {
    case rooms, traps, doors, spells, creatures, objectives
}

struct DK2GameHUD: View {
    let state: NSDictionary
    let mission: DK2Mission?
    let onQuit: () -> Void

    @State private var activePanel: DK2PanelKind? = nil
    @State private var paused = false
    @State private var speed: Int = 1
    @State private var showSaveLoad = false
    @State private var slot = "slot1"
    @State private var saveMessage = ""
    @State private var cutsceneName: String?

    private func number(_ key: String) -> Int {
        if let v = state[key] as? Int { return v }
        if let v = state[key] as? NSNumber { return v.intValue }
        if let v = state[key] as? Float { return Int(v) }
        if let v = state[key] as? Double { return Int(v) }
        return 0
    }

    private func flag(_ key: String) -> Bool {
        state[key] as? Bool ?? false
    }

    private var gold: Int { number("gold") }
    private var goldCapacity: Int { number("goldCapacity") }
    private var creatures: Int { number("creatures") }
    private var roomsCount: Int { number("rooms") }
    private var ticks: Int { number("ticks") }
    private var victory: Bool { flag("victory") }
    private var defeat: Bool { flag("defeat") }

    var body: some View {
        ZStack {
            VStack(spacing: 0) {
                topBar
                resourceStrip
                Spacer()
                if victory || defeat {
                    resultBanner
                }
                Spacer()
                commandBar
            }

            if let panel = activePanel {
                panelOverlay(panel)
            }
            if showSaveLoad {
                saveLoadOverlay
            }
        }
        .overlay {
            if let name = cutsceneName {
                DK2CutsceneView(name: name) { cutsceneName = nil }
                    .allowsHitTesting(true)
            }
        }
        .allowsHitTesting(false)
        .onAppear {
            DK2GameCommand.postPause(false)
        }
        .onReceive(NotificationCenter.default.publisher(for: DK2Cutscene.playRequest)) { note in
            if let name = note.userInfo?[DK2Cutscene.Key.name] as? String {
                cutsceneName = name
            }
        }
    }

    // MARK: Top bar

    @ViewBuilder
    private var topBar: some View {
        HStack(spacing: 8) {
            DK2HUDButton(title: "Menu", system: "line.horizontal.3") {
                DK2GameCommand.postQuit()
                onQuit()
            }
            DK2HUDButton(title: paused ? "Resume" : "Pause", system: "pause.circle") {
                paused.toggle()
                DK2GameCommand.postPause(paused)
            }
            DK2HUDButton(title: "Objectives", system: "list.bullet") {
                activePanel = (activePanel == .objectives) ? nil : .objectives
            }
            DK2HUDButton(title: "Save", system: "square.and.arrow.down") {
                slot = slot.isEmpty ? "slot1" : slot
                DK2GameCommand.postSave(slot: slot)
                saveMessage = "Saved \"\(slot)\""
            }
            DK2HUDButton(title: "Load", system: "square.and.arrow.up") {
                slot = slot.isEmpty ? "slot1" : slot
                DK2GameCommand.postLoad(slot: slot)
                saveMessage = "Loaded \"\(slot)\""
            }
            DK2HUDButton(title: "Slot: \(slot)", system: "folder") {
                showSaveLoad.toggle()
            }
            Spacer()
            HStack(spacing: 2) {
                ForEach([1, 2, 3], id: \.self) { value in
                    Button {
                        speed = value
                        DK2GameCommand.postSpeed(value)
                    } label: {
                        Text("\(value)×")
                            .font(.system(size: 12, weight: .bold, design: .monospaced))
                            .foregroundColor(speed == value ? .yellow : .white)
                            .padding(.horizontal, 6)
                            .padding(.vertical, 4)
                            .background(Color.black.opacity(speed == value ? 0.8 : 0.45))
                            .cornerRadius(4)
                    }
                    .buttonStyle(.plain)
                    .allowsHitTesting(true)
                }
            }
        }
        .padding(.horizontal, 10)
        .padding(.top, 30)
        .padding(.bottom, 6)
        .background(LinearGradient(gradient: Gradient(colors: [.black.opacity(0.85), .clear]),
                                   startPoint: .top, endPoint: .bottom))
        .allowsHitTesting(true)
    }

    // MARK: Resource strip

    private var resourceStrip: some View {
        HStack(spacing: 14) {
            DK2StatView(label: "GOLD", value: "\(gold) / \(goldCapacity)",
                        color: Color(red: 0.97, green: 0.82, blue: 0.35))
            DK2StatView(label: "MANA", value: "—",
                        color: Color(red: 0.55, green: 0.72, blue: 0.98))
            DK2StatView(label: "CREATURES", value: "\(creatures)",
                        color: Color(red: 0.72, green: 0.9, blue: 0.6))
            DK2StatView(label: "ROOMS", value: "\(roomsCount)",
                        color: Color(red: 0.9, green: 0.72, blue: 0.55))
            DK2StatView(label: "TICK", value: "\(ticks)",
                        color: Color.white)
        }
        .font(.system(size: 11, design: .monospaced))
        .padding(.horizontal, 12)
        .padding(.vertical, 6)
        .background(Color.black.opacity(0.55))
        .cornerRadius(6)
        .padding(.horizontal, 10)
        .allowsHitTesting(false)
    }

    // MARK: Result banner

    @ViewBuilder
    private var resultBanner: some View {
        VStack(spacing: 8) {
            Text(victory ? "VICTORY" : "DEFEAT")
                .font(.system(size: 44, weight: .black, design: .serif))
                .foregroundColor(victory ? Color(red: 0.95, green: 0.82, blue: 0.4) : .red)
                .shadow(color: .black, radius: 6)
            HStack(spacing: 12) {
                DK2HUDButton(title: "Save Game", system: "square.and.arrow.down") {
                    DK2GameCommand.postSave(slot: slot)
                    saveMessage = "Saved \"\(slot)\""
                }
                DK2HUDButton(title: "Main Menu", system: "house") {
                    DK2GameCommand.postQuit()
                    onQuit()
                }
            }
        }
        .padding()
        .background(Color.black.opacity(0.55))
        .cornerRadius(10)
        .allowsHitTesting(true)
    }

    // MARK: Bottom command bar

    private var commandBar: some View {
        HStack(spacing: 6) {
            DK2HUDButton(title: "Rooms", system: "square.grid.3x3") { toggle(.rooms) }
            DK2HUDButton(title: "Traps", system: "triangle") { toggle(.traps) }
            DK2HUDButton(title: "Doors", system: "door.left.hand.closed") { toggle(.doors) }
            DK2HUDButton(title: "Spells", system: "wand.and.stars") { toggle(.spells) }
            DK2HUDButton(title: "Creatures", system: "person.3") { toggle(.creatures) }
            Spacer()
            if !saveMessage.isEmpty {
                Text(saveMessage)
                    .font(.caption)
                    .foregroundColor(Color(red: 0.95, green: 0.82, blue: 0.55))
                    .lineLimit(1)
            }
        }
        .padding(.horizontal, 10)
        .padding(.bottom, 18)
        .padding(.top, 8)
        .background(LinearGradient(gradient: Gradient(colors: [.clear, .black.opacity(0.85)]),
                                   startPoint: .top, endPoint: .bottom))
        .allowsHitTesting(true)
    }

    private func toggle(_ kind: DK2PanelKind) {
        activePanel = (activePanel == kind) ? nil : kind
        saveMessage = ""
    }

    // MARK: Panel overlay

    @ViewBuilder
    private func panelOverlay(_ panel: DK2PanelKind) -> some View {
        VStack(spacing: 0) {
            HStack {
                Text(title(for: panel))
                    .font(.system(size: 20, weight: .bold, design: .serif))
                    .foregroundColor(Color(red: 0.95, green: 0.82, blue: 0.55))
                Spacer()
                Button { activePanel = nil } label: {
                    Image(systemName: "xmark.circle.fill")
                        .font(.title2)
                        .foregroundColor(.white)
                }
                .buttonStyle(.plain)
                .allowsHitTesting(true)
            }
            .padding()
            .background(Color(red: 0.12, green: 0.08, blue: 0.07))

            ZStack {
                if let bg = DK2Art.image("BG-Map-00.png") {
                    Image(uiImage: bg).resizable().scaledToFill().opacity(0.35)
                } else {
                    Color.black
                }
                panelContent(panel)
            }
        }
        .cornerRadius(10)
        .padding(.horizontal, 16)
        .padding(.vertical, 40)
        .background(Color.black.opacity(0.55))
        .allowsHitTesting(true)
    }

    @ViewBuilder
    private func panelContent(_ panel: DK2PanelKind) -> some View {
        switch panel {
        case .rooms: DK2BuildPanel(defaultTab: .room)
        case .traps: DK2BuildPanel(defaultTab: .trap)
        case .doors: DK2BuildPanel(defaultTab: .door)
        case .spells: DK2SpellPanel()
        case .creatures: DK2CreaturePanel()
        case .objectives: DK2ObjectivesPanel(mission: mission)
        }
    }

    private func title(for panel: DK2PanelKind) -> String {
        switch panel {
        case .rooms: return "Build — Rooms"
        case .traps: return "Build — Traps"
        case .doors: return "Build — Doors"
        case .spells: return "Spells"
        case .creatures: return "Creatures"
        case .objectives: return "Objectives"
        }
    }

    // MARK: Save / Load slot overlay

    private var saveLoadOverlay: some View {
        VStack(spacing: 12) {
            Text("Save / Load Slot")
                .font(.headline)
                .foregroundColor(Color(red: 0.95, green: 0.82, blue: 0.55))
            TextField("Slot name", text: $slot)
                .textFieldStyle(.roundedBorder)
                .padding(.horizontal)
            HStack(spacing: 16) {
                Button("Save") {
                    DK2GameCommand.postSave(slot: slot.isEmpty ? "slot1" : slot)
                    showSaveLoad = false
                }
                Button("Load") {
                    DK2GameCommand.postLoad(slot: slot.isEmpty ? "slot1" : slot)
                    showSaveLoad = false
                }
                Button("Cancel") { showSaveLoad = false }
            }
            .foregroundColor(Color(red: 0.95, green: 0.82, blue: 0.55))
        }
        .padding()
        .background(Color(red: 0.12, green: 0.08, blue: 0.07))
        .cornerRadius(10)
        .overlay(RoundedRectangle(cornerRadius: 10)
            .stroke(Color(red: 0.45, green: 0.32, blue: 0.18), lineWidth: 2))
        .allowsHitTesting(true)
    }
}

// MARK: - Small HUD building blocks

struct DK2HUDButton: View {
    let title: String
    let system: String?
    let action: () -> Void

    init(title: String, system: String? = nil, action: @escaping () -> Void) {
        self.title = title
        self.system = system
        self.action = action
    }

    var body: some View {
        Button(action: action) {
            HStack(spacing: 4) {
                if let system = system {
                    Image(systemName: system)
                }
                Text(title)
                    .font(.system(size: 12, weight: .semibold))
            }
            .foregroundColor(Color(red: 0.95, green: 0.85, blue: 0.62))
            .padding(.horizontal, 8)
            .padding(.vertical, 6)
            .background(Color.black.opacity(0.55))
            .cornerRadius(5)
            .overlay(RoundedRectangle(cornerRadius: 5)
                .stroke(Color(red: 0.45, green: 0.32, blue: 0.18), lineWidth: 1))
        }
        .buttonStyle(.plain)
        .allowsHitTesting(true)
    }
}

struct DK2StatView: View {
    let label: String
    let value: String
    let color: Color

    var body: some View {
        VStack(spacing: 1) {
            Text(label).font(.system(size: 8, weight: .bold)).foregroundColor(.gray)
            Text(value).font(.system(size: 12, weight: .bold)).foregroundColor(color)
        }
    }
}
