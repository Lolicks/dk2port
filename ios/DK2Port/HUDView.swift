import Foundation
import SwiftUI
import UIKit

enum DK2PanelKind: String, Hashable {
    case creatures, rooms, spells, workshop, objectives
}

struct DK2GameHUD: View {
    let state: NSDictionary
    let mission: DK2Mission?
    let onQuit: () -> Void

    @State private var activeTab: DK2PanelKind? = nil
    @State private var showPauseMenu = false
    @State private var showBriefing = false
    @State private var showConsole = false
    @State private var minimised = false
    @State private var tooltip = ""
    @State private var hint = ""
    @State private var hoveredCorner: String?
    @State private var activeTool: String?
    @State private var mapZoom: CGFloat = 1
    @State private var hpAlarm = false
    @State private var callToArms = false
    @State private var cutsceneName: String?
    @State private var slot = "slot1"
    @State private var saveMessage = ""
    @State private var consoleInput = ""
    @State private var localConsole: [String] = []

    private func number(_ key: String) -> Int {
        if let v = state[key] as? Int { return v }
        if let v = state[key] as? NSNumber { return v.intValue }
        if let v = state[key] as? Float { return Int(v) }
        if let v = state[key] as? Double { return Int(v) }
        return 0
    }

    private func fraction(_ key: String, fallback: CGFloat) -> CGFloat {
        if let v = state[key] as? Float { return CGFloat(Swift.max(0, Swift.min(1, v))) }
        if let v = state[key] as? Double { return CGFloat(Swift.max(0, Swift.min(1, v))) }
        if let v = state[key] as? NSNumber { return CGFloat(Swift.max(0, Swift.min(1, v.floatValue))) }
        if let v = state[key] as? Int { return CGFloat(Swift.max(0, Swift.min(1, Float(v)))) }
        return fallback
    }

    private func flag(_ key: String) -> Bool {
        state[key] as? Bool ?? (state[key] as? NSNumber)?.boolValue ?? false
    }

    private var gold: Int { number("gold") }
    private var goldCapacity: Int { number("goldCapacity") }
    private var mana: Int { number("mana") }
    private var manaMax: Int { number("manaMax") }
    private var manaGain: Int { number("manaGet") }
    private var manaLoss: Int { number("manaLose") }
    private var creatures: Int { number("creatures") }
    private var roomsCount: Int { number("rooms") }
    private var ticks: Int { number("ticks") }
    private var victory: Bool { flag("victory") }
    private var defeat: Bool { flag("defeat") }
    private var possessionActive: Bool { flag("possession") || flag("possessed") }
    private var cinematicActive: Bool { flag("cinematic") }

    private var possessedHealth: CGFloat { fraction("possessedHealth", fallback: 1) }

    private var tabDefinitions: [DK2GameTab] {
        [
            DK2GameTab(key: "creatures", titleID: 1807, hintID: 2153, tipID: 2154),
            DK2GameTab(key: "rooms", titleID: 1809, hintID: 2155, tipID: 2156),
            DK2GameTab(key: "spells", titleID: 320, hintID: 2157, tipID: 2158),
            DK2GameTab(key: "workshop", titleID: 467, hintID: 2159, tipID: 2160)
        ]
    }

    private var tabSelection: Binding<String> {
        Binding<String>(
            get: { activeTab?.rawValue ?? "" },
            set: { value in activeTab = DK2PanelKind(rawValue: value) }
        )
    }

    var body: some View {
        GeometryReader { geo in
            ZStack(alignment: .top) {
                hudLayer(geo)
                if cinematicActive { cinematicLayer(geo) }
                if possessionActive { possessionLayer(geo) }
                if showBriefing { briefingLayer(geo) }
                if victory || defeat { debriefingLayer(geo) }
                if showPauseMenu { pauseMenuLayer(geo) }
                if showConsole { consoleLayer(geo) }
                if let name = cutsceneName {
                    DK2CutsceneView(name: name) { cutsceneName = nil }
                        .allowsHitTesting(true)
                }
            }
            .frame(width: geo.size.width, height: geo.size.height)
        }
        .allowsHitTesting(false)
        .onAppear { DK2GameCommand.postPause(false) }
        .onReceive(NotificationCenter.default.publisher(for: DK2Cutscene.playRequest)) { note in
            if let name = note.userInfo?[DK2Cutscene.Key.name] as? String {
                cutsceneName = name
            }
        }
    }

    @ViewBuilder
    private func hudLayer(_ geo: GeometryProxy) -> some View {
        VStack(spacing: 0) {
            topBar
            Spacer(minLength: 0)
            bottomPanel(geo)
        }
    }

    private var topBar: some View {
        ZStack(alignment: .leading) {
            HStack(spacing: 0) {
                if let bar = DK2GameArt.gui("Textures/GUI/Windows/Bar-00.png",
                                            fallback: "BG-Controls-00.png") {
                    Image(uiImage: bar).resizable().scaledToFill().frame(height: 72).clipped()
                } else {
                    Color.black.opacity(0.75).frame(height: 72)
                }
                if let bar = DK2GameArt.gui("Textures/GUI/Windows/Bar-02.png",
                                            fallback: "Bigbutton.PNG") {
                    Image(uiImage: bar).resizable().scaledToFit().frame(width: 64, height: 64)
                }
            }
            .frame(height: 72)
            HStack(spacing: 10) {
                manaGroup
                Spacer(minLength: 4)
                ZStack {
                    if let bar = DK2GameArt.gui("Textures/GUI/Windows/Bar-00.png",
                                                fallback: "BG-Controls-00.png") {
                        Image(uiImage: bar).resizable().scaledToFill().frame(height: 40).clipped()
                    }
                    DK2BitmapText(DK2L10n.shared.resolve(tooltip.isEmpty ? hint : tooltip),
                                  font: "Ingame10", size: 10, color: DK2UIStyle.parchment)
                        .scaledToFit()
                        .frame(maxWidth: 420)
                }
                .frame(height: 44)
                goldGroup
                Color.clear.frame(width: 56, height: 64)
            }
            .padding(.horizontal, 10)
            .frame(height: 72)
        }
        .frame(height: 72)
    }

    private var manaGroup: some View {
        ZStack(alignment: .leading) {
            HStack(spacing: 0) {
                if let bg = DK2GameArt.gui("Textures/GUI/Windows/Mana-Bg-00.png",
                                           fallback: "BG-Map-00.png") {
                    Image(uiImage: bg).resizable().scaledToFit().frame(width: 128, height: 64)
                }
                if let bg = DK2GameArt.gui("Textures/GUI/Windows/Mana-Bg-01.png",
                                           fallback: "BG-Map-00.png") {
                    Image(uiImage: bg).resizable().scaledToFit().frame(width: 128, height: 64)
                }
            }
            .background(DK2HoverArea { active in
                hint = active ? DK2L10n.shared[2133] : ""
                tooltip = active ? DK2L10n.shared[2134] : ""
            })
            HStack(spacing: 4) {
                ZStack {
                    if let image = DK2GameArt.gui("Textures/GUI/Windows/heart-hp.png",
                                                  fallback: "Hilight_Frame.png") {
                        Image(uiImage: image).resizable().scaledToFit().frame(width: 64, height: 64)
                    }
                    if let image = DK2GameArt.gui("Textures/GUI/Windows/heart-bg.png",
                                                  fallback: "Hilight_Frame.png") {
                        Image(uiImage: image).resizable().scaledToFit().frame(width: 54, height: 54)
                    }
                    if hpAlarm,
                       let image = DK2GameArt.gui("Textures/GUI/Windows/heart-alarm.png",
                                                  fallback: "Hilight_Frame.png") {
                        Image(uiImage: image).resizable().scaledToFit().frame(width: 64, height: 64)
                    }
                }
                .frame(width: 64, height: 64)
                .contentShape(Rectangle())
                .onTapGesture {
                    hpAlarm.toggle()
                    DK2UIStyle.postFrontEndSound()
                }
                .allowsHitTesting(true)
                .background(DK2HoverArea { active in
                    hint = active ? DK2L10n.shared[2139] : ""
                    tooltip = active ? DK2L10n.shared[2140] : ""
                })
                VStack(alignment: .trailing, spacing: 0) {
                    DK2BitmapText("\(mana)", font: "Ingame18", size: 18,
                                  color: Color(red: 0.15, green: 0.82, blue: 0.40))
                    HStack(spacing: 8) {
                        DK2BitmapText("+\(manaGain)", font: "Ingame10", size: 10,
                                      color: Color(red: 0.15, green: 0.82, blue: 0.40))
                        DK2BitmapText("-\(manaLoss)", font: "Ingame10", size: 10,
                                      color: Color(red: 0.93, green: 0.10, blue: 0.10))
                    }
                }
                .frame(width: 128, alignment: .trailing)
            }
            .padding(.leading, 4)
        }
        .frame(width: 256, height: 64)
    }

    private var goldGroup: some View {
        ZStack(alignment: .leading) {
            HStack(spacing: 0) {
                if let bg = DK2GameArt.gui("Textures/GUI/Windows/gold-bg-00.png",
                                           fallback: "BG-Map-00.png") {
                    Image(uiImage: bg).resizable().scaledToFit().frame(width: 128, height: 64)
                }
                if let bg = DK2GameArt.gui("Textures/GUI/Windows/gold-bg-01.png",
                                           fallback: "BG-Map-00.png") {
                    Image(uiImage: bg).resizable().scaledToFit().frame(width: 128, height: 64)
                }
            }
            .background(DK2HoverArea { active in
                hint = active ? DK2L10n.shared[2141] : ""
                tooltip = active ? DK2L10n.shared[2142] : ""
            })
            HStack(spacing: 4) {
                ZStack {
                    if let image = DK2GameArt.gui("Textures/GUI/Windows/payday-Hp.png",
                                                  fallback: "Hilight_Frame.png") {
                        Image(uiImage: image).resizable().scaledToFit().frame(width: 64, height: 64)
                    }
                    if let image = DK2GameArt.gui("Textures/GUI/Windows/payday-BG.png",
                                                  fallback: "Hilight_Frame.png") {
                        Image(uiImage: image).resizable().scaledToFit().frame(width: 54, height: 54)
                    }
                }
                .frame(width: 64, height: 64)
                .contentShape(Rectangle())
                .onTapGesture { DK2UIStyle.postFrontEndSound() }
                .allowsHitTesting(true)
                .background(DK2HoverArea { active in
                    hint = active ? DK2L10n.shared[2143] : ""
                    tooltip = active ? DK2L10n.shared[2144] : ""
                })
                DK2BitmapText("\(gold)", font: "Ingame18", size: 18,
                              color: Color(red: 0.83, green: 0.69, blue: 0.0))
                    .frame(width: 128, alignment: .trailing)
            }
            .padding(.leading, 4)
        }
        .frame(width: 256, height: 64)
    }

    @ViewBuilder
    private func bottomPanel(_ geo: GeometryProxy) -> some View {
        VStack(spacing: 0) {
            ZStack(alignment: .leading) {
                HStack(spacing: 0) {
                    if let bar = DK2GameArt.gui("Textures/GUI/Windows/Bar-00.png",
                                                fallback: "BG-Controls-00.png") {
                        Image(uiImage: bar).resizable().scaledToFill().frame(height: 64).clipped()
                    }
                    if let bar = DK2GameArt.gui("Textures/GUI/Windows/Bar-02.png",
                                                fallback: "Bigbutton.PNG") {
                        Image(uiImage: bar).resizable().scaledToFill().frame(width: 128, height: 64)
                    }
                }
            }
            .frame(height: 64)
            HStack(alignment: .top, spacing: 0) {
                minimap
                VStack(spacing: 0) {
                    messageRow
                    HStack(spacing: 4) {
                        DK2GameTabStrip(tabs: tabDefinitions, selection: tabSelection,
                                        hint: $hint, tooltip: $tooltip)
                        Spacer(minLength: 0)
                    }
                    .frame(height: 52)
                    if !minimised, let tab = activeTab {
                        ZStack {
                            if let bg = DK2GameArt.gui("Textures/GUI/Windows/Panel-BG.png",
                                                       fallback: "BG-Map-00.png") {
                                Image(uiImage: bg).resizable().scaledToFill().opacity(0.35)
                            }
                            tabContent(tab)
                        }
                        .frame(maxHeight: .infinity)
                        .allowsHitTesting(true)
                    } else {
                        Spacer(minLength: 0)
                    }
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                rightControls
            }
            .frame(height: 156)
        }
        .frame(height: 220)
    }

    private var minimap: some View {
        VStack(spacing: 0) {
            ZStack {
                if let bg = DK2GameArt.gui("Textures/GUI/Map/Map-BG.png",
                                           fallback: "Map-BG.PNG") {
                    Image(uiImage: bg).resizable().scaledToFit().frame(width: 180, height: 180)
                }
                if let map = DK2GameArt.gui("Textures/GUI/Map/map.png",
                                            fallback: "BackdropMap.png") {
                    Image(uiImage: map).resizable().scaledToFit()
                        .frame(width: 180, height: 180)
                        .scaleEffect(mapZoom)
                }
                cornerControl("query", x: 32, y: 32,
                              normal: "Textures/GUI/Map/mapbf-00.png",
                              selected: "Textures/GUI/Map/selected-mapbf-00.png",
                              hintID: 1803, tipID: 1804) {
                    activeTool = "query"
                }
                cornerControl("sell", x: 148, y: 32,
                              normal: "Textures/GUI/Map/mapbf-01.png",
                              selected: "Textures/GUI/Map/selected-mapbf-01.png",
                              hintID: 1805, tipID: 1806) {
                    activeTool = "sell"
                }
                cornerControl("menu", x: 32, y: 148,
                              normal: "Textures/GUI/Map/mapbf-02.png",
                              selected: "Textures/GUI/Map/selected-mapbf-02.png",
                              hintID: 1801, tipID: 1802) {
                    showPauseMenu = true
                }
                cornerControl("resize", x: 148, y: 148,
                              normal: "Textures/GUI/Map/mapbf-03.png",
                              selected: "Textures/GUI/Map/selected-mapbf-03.png",
                              hintID: 1799, tipID: 1800) {
                    mapZoom = mapZoom >= 2 ? 1 : mapZoom + 0.25
                }
            }
            .frame(width: 180, height: 180)
            .clipped()
        }
        .frame(width: 230, height: 180)
        .padding(6)
    }

    @ViewBuilder
    private func cornerControl(_ tag: String, x: CGFloat, y: CGFloat,
                               normal: String, selected: String,
                               hintID: Int, tipID: Int,
                               action: @escaping () -> Void) -> some View {
        ZStack {
            if let image = DK2GameArt.gui(normal, fallback: "Hilight_Frame.png") {
                Image(uiImage: image).resizable().scaledToFit()
                    .frame(width: 64, height: 64)
                    .opacity(hoveredCorner == tag || activeTool == tag ? 0.95 : 0.6)
            }
            if hoveredCorner == tag || activeTool == tag,
               let image = DK2GameArt.gui(selected, fallback: "Hilight_Frame.png") {
                Image(uiImage: image).resizable().scaledToFit().frame(width: 64, height: 64)
            }
        }
        .frame(width: 64, height: 64)
        .position(x: x, y: y)
        .contentShape(Rectangle())
        .onTapGesture {
            DK2UIStyle.postFrontEndSound()
            action()
        }
        .allowsHitTesting(true)
        .background(DK2HoverArea { active in
            hoveredCorner = active ? tag : nil
            hint = active ? DK2L10n.shared[hintID] : ""
            tooltip = active ? DK2L10n.shared[tipID] : ""
        })
    }

    private var messageRow: some View {
        HStack(spacing: 4) {
            Button {
                minimised.toggle()
                DK2UIStyle.postFrontEndSound()
            } label: {
                if let image = DK2GameArt.gui("Textures/GUI/Icons/i-mise.png",
                                              fallback: "Bigbutton.PNG") {
                    Image(uiImage: image).resizable().scaledToFit().frame(width: 64, height: 64)
                }
            }
            .buttonStyle(.plain)
            .allowsHitTesting(true)
            .background(DK2HoverArea { active in
                hint = active ? DK2L10n.shared[2161] : ""
                tooltip = active ? DK2L10n.shared[2162] : ""
            })
            Button {
                showBriefing = true
                activeTab = .objectives
            } label: {
                if let image = DK2GameArt.gui("Textures/GUI/Tabs/Messages/mt-objective-00.png",
                                              fallback: "BG-Objective-00.png") {
                    Image(uiImage: image).resizable().scaledToFit().frame(width: 32, height: 64)
                }
            }
            .buttonStyle(.plain)
            .allowsHitTesting(true)
            .background(DK2HoverArea { active in
                hint = active ? DK2L10n.shared[2163] : ""
                tooltip = active ? DK2L10n.shared[2164] : ""
            })
            Spacer(minLength: 0)
            if !saveMessage.isEmpty {
                DK2BitmapText(saveMessage, font: "Ingame10", size: 10,
                              color: DK2UIStyle.gold)
                    .scaledToFit()
                    .frame(maxWidth: 300)
            }
        }
        .frame(height: 64)
    }

    private var rightControls: some View {
        VStack(spacing: 0) {
            HStack {
                Spacer(minLength: 0)
                if callToArms {
                    if let image = DK2GameArt.gui("Textures/GUI/Spells/call_to_arms-kill.png",
                                                  fallback: "tba.png") {
                        Image(uiImage: image).resizable().scaledToFit()
                            .frame(width: 64, height: 64)
                            .contentShape(Rectangle())
                            .onTapGesture {
                                callToArms = false
                                DK2UIStyle.postFrontEndSound()
                            }
                            .allowsHitTesting(true)
                    }
                } else {
                    if let image = DK2GameArt.gui("Textures/GUI/Icons/frame.png",
                                                  fallback: "Hilight_Frame.png") {
                        Image(uiImage: image).resizable().scaledToFit()
                            .frame(width: 64, height: 64)
                            .contentShape(Rectangle())
                            .onTapGesture {
                                callToArms = true
                                DK2GameCommand.postSpell(index: 2, name: "Call To Arms")
                            }
                            .allowsHitTesting(true)
                    }
                }
            }
            .frame(height: 64)
            .background(DK2HoverArea { active in
                hint = active ? DK2L10n.shared[2589] : ""
                tooltip = active ? DK2L10n.shared[2590] : ""
            })
            Spacer(minLength: 0)
            if let reaper = DK2GameArt.gui("Textures/GUI/Icons/reaper.png",
                                           fallback: "reaper.png") {
                Image(uiImage: reaper).resizable().scaledToFit().frame(width: 96, height: 96)
            }
            Spacer(minLength: 0)
        }
        .frame(width: 128)
    }

    @ViewBuilder
    private func tabContent(_ tab: DK2PanelKind) -> some View {
        switch tab {
        case .creatures: DK2CreaturePanel()
        case .rooms: DK2BuildPanel(defaultTab: .room)
        case .spells: DK2SpellPanel()
        case .workshop: DK2WorkshopPanel()
        case .objectives: DK2ObjectivesPanel(mission: mission)
        }
    }

    @ViewBuilder
    private func cinematicLayer(_ geo: GeometryProxy) -> some View {
        VStack(spacing: 0) {
            ZStack(alignment: .trailing) {
                Color.black.opacity(0.12)
                if let image = DK2GameArt.gui("Textures/GUI/Icons/i-camera-no_go.png",
                                              fallback: "0-H-Left_Arrow.png") {
                    Image(uiImage: image).resizable().scaledToFit()
                        .frame(width: 64, height: 64)
                        .padding(.trailing, 10)
                }
            }
            .frame(height: geo.size.height * 0.12)
            Spacer(minLength: 0)
            ZStack {
                Color.black.opacity(0.12)
                VStack(spacing: 2) {
                    ForEach(speechLines, id: \.self) { line in
                        DK2BitmapText(line, font: "Ingame18", size: 18,
                                      color: DK2UIStyle.parchment)
                    }
                }
                .frame(width: geo.size.width * 0.9)
            }
            .frame(height: geo.size.height * 0.12)
        }
        .frame(width: geo.size.width, height: geo.size.height)
        .allowsHitTesting(true)
    }

    private var speechLines: [String] {
        let text: String
        if let value = state["speechText"] as? String, !value.isEmpty {
            text = DK2L10n.shared.resolve(value)
        } else {
            let id = number("speech")
            text = id == 0 ? "" : DK2L10n.shared.speech(id)
        }
        guard !text.isEmpty else { return [] }
        var lines: [String] = []
        var current = ""
        for word in text.split(separator: " ") {
            let candidate = current.isEmpty ? String(word) : current + " " + word
            if candidate.count > 52 && !current.isEmpty {
                lines.append(current)
                current = String(word)
            } else {
                current = candidate
            }
        }
        if !current.isEmpty { lines.append(current) }
        return lines
    }

    @ViewBuilder
    private func possessionLayer(_ geo: GeometryProxy) -> some View {
        VStack(spacing: 0) {
            ZStack(alignment: .leading) {
                if let bar = DK2GameArt.gui("Textures/GUI/Windows/Bar-00.png",
                                            fallback: "BG-Controls-00.png") {
                    Image(uiImage: bar).resizable().scaledToFill().frame(height: 72).clipped()
                }
                HStack(spacing: 6) {
                    manaGroup
                    if let goblin = DK2GameArt.gui("Textures/GUI/Creatures/Evil/goblin.png",
                                                   fallback: "goblin.png") {
                        Image(uiImage: goblin).resizable().scaledToFit()
                            .frame(width: 64, height: 64)
                    }
                    ZStack {
                        if let bg = DK2GameArt.gui("Textures/GUI/Map/m-h-bg.png",
                                                   fallback: "Hilight_Frame.png") {
                            Image(uiImage: bg).resizable().scaledToFit()
                                .frame(width: 128, height: 32)
                        }
                        if let hp = DK2GameArt.gui("Textures/GUI/Map/m-hp.png",
                                                   fallback: "Hilight_Frame.png") {
                            Image(uiImage: hp).resizable().scaledToFit()
                                .frame(width: 128 * possessedHealth, height: 28)
                        }
                        if let tone = DK2GameArt.gui("Textures/GUI/Map/M-H-Tone-BG.png",
                                                     fallback: "Hilight_Frame.png") {
                            Image(uiImage: tone).resizable().scaledToFit()
                                .frame(width: 128, height: 32).opacity(0.35)
                        }
                    }
                    .frame(width: 128, height: 32)
                    ZStack {
                        if let treasury = DK2GameArt.gui("Textures/GUI/Rooms/Treasury.png",
                                                         fallback: "Treasury.png") {
                            Image(uiImage: treasury).resizable().scaledToFit()
                                .frame(width: 64, height: 64)
                        }
                        if let bg = DK2GameArt.gui("Textures/GUI/Map/m-h-bg.png",
                                                   fallback: "Hilight_Frame.png") {
                            Image(uiImage: bg).resizable().scaledToFit()
                                .frame(width: 128, height: 32)
                        }
                        DK2BitmapText("\(gold)", font: "Ingame10", size: 10,
                                      color: DK2UIStyle.gold)
                    }
                    .frame(width: 128, height: 32)
                    Spacer(minLength: 0)
                }
                .padding(.horizontal, 8)
            }
            .frame(height: 72)
            Spacer(minLength: 0)
            VStack(spacing: 0) {
                Rectangle().fill(Color.clear).frame(height: 96)
                HStack(spacing: 0) {
                    if let bar = DK2GameArt.gui("Textures/GUI/Windows/Bar-00.png",
                                                fallback: "BG-Controls-00.png") {
                        Image(uiImage: bar).resizable().scaledToFill().frame(height: 64).clipped()
                    }
                }
                .frame(height: 64)
                HStack(alignment: .top, spacing: 0) {
                    VStack {
                        ZStack {
                            if let bg = DK2GameArt.gui("Textures/GUI/Map/Map-BG.png",
                                                       fallback: "Map-BG.PNG") {
                                Image(uiImage: bg).resizable().scaledToFit()
                                    .frame(width: 180, height: 180)
                            }
                            if let map = DK2GameArt.gui("Textures/GUI/Map/map-2.png",
                                                        fallback: "Map-BG.PNG") {
                                Image(uiImage: map).resizable().scaledToFit()
                                    .frame(width: 180, height: 180)
                            }
                        }
                        .frame(width: 180, height: 180)
                    }
                    .frame(width: 230)
                    VStack(alignment: .leading, spacing: 0) {
                        HStack(spacing: 4) {
                            ForEach(0..<4, id: \.self) { index in
                                if let ability = DK2GameArt.tabIcon("spell", active: false) {
                                    Image(uiImage: ability).resizable().scaledToFit()
                                        .frame(width: 48, height: 48)
                                        .opacity(index == 0 ? 1 : 0.7)
                                }
                            }
                            Spacer(minLength: 0)
                        }
                        .frame(height: 56)
                        Spacer(minLength: 0)
                        ZStack {
                            if let group = DK2GameArt.gui("Textures/GUI/Icons/1st-person/group-mode.png",
                                                          fallback: "Hilight_Frame.png") {
                                Image(uiImage: group).resizable().scaledToFit()
                                    .frame(width: 64, height: 128)
                            }
                            if hoveredCorner == "group",
                               let selected = DK2GameArt.gui("Textures/GUI/Icons/selected_group.png",
                                                             fallback: "Hilight_Frame.png") {
                                Image(uiImage: selected).resizable().scaledToFit()
                                    .frame(width: 64, height: 128)
                            }
                        }
                        .frame(width: 64, height: 128)
                        .contentShape(Rectangle())
                        .onTapGesture { DK2UIStyle.postFrontEndSound() }
                        .allowsHitTesting(true)
                        .background(DK2HoverArea { active in
                            hoveredCorner = active ? "group" : nil
                        })
                    }
                    Spacer(minLength: 0)
                }
                .frame(height: 156)
            }
            .frame(height: 220)
        }
        .frame(width: geo.size.width, height: geo.size.height)
        .allowsHitTesting(true)
    }

    @ViewBuilder
    private func briefingLayer(_ geo: GeometryProxy) -> some View {
        ZStack {
            if let bg = DK2UIStyle.image("BG-Objective-00.png") {
                Image(uiImage: bg).resizable().scaledToFill()
            } else {
                Color.black
            }
            Color.black.opacity(0.6)
            VStack(spacing: 12) {
                DK2BitmapText(DK2L10n.shared[1476], font: "Ingame28", size: 28,
                              color: DK2UIStyle.gold)
                DK2BitmapText(mission?.name ?? "", font: "Ingame18", size: 18,
                              color: DK2UIStyle.parchment)
                objectiveRow(image: "BG-Objective-01.png", titleID: 1408, body: missionObjective)
                objectiveRow(image: "BG-Objective-02.png", titleID: 1409, body: missionSubObjective)
                DK2UIStyle.okButton {
                    showBriefing = false
                    DK2GameCommand.postPause(false)
                }
            }
            .padding(24)
        }
        .frame(width: geo.size.width, height: geo.size.height)
        .allowsHitTesting(true)
    }

    @ViewBuilder
    private func objectiveRow(image: String, titleID: Int, body: String) -> some View {
        HStack(spacing: 12) {
            if let art = DK2UIStyle.image(image) {
                Image(uiImage: art).resizable().scaledToFit().frame(width: 110, height: 90)
            }
            VStack(alignment: .leading, spacing: 4) {
                DK2BitmapText(DK2L10n.shared[titleID], font: "Ingame18", size: 18,
                              color: DK2UIStyle.gold)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(6)
                    .background(DK2UIStyle.gold.opacity(0.35))
                DK2BitmapText(body, font: "Ingame14", size: 14,
                              color: DK2UIStyle.parchment)
                    .scaledToFit()
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(6)
                    .background(Color.black.opacity(0.55))
            }
        }
    }

    private var missionObjective: String {
        if let name = mission?.name {
            return "Claim \(name) for your dungeon. Destroy the rival keepers and protect your Dungeon Heart."
        }
        return "Destroy the rival keepers and protect your Dungeon Heart."
    }

    private var missionSubObjective: String {
        "Dig out your lair, raise creatures and mine gold. Creatures: \(creatures), rooms: \(roomsCount), gold: \(gold)."
    }

    @ViewBuilder
    private func debriefingLayer(_ geo: GeometryProxy) -> some View {
        ZStack {
            if let bg = DK2UIStyle.image("BG-Objective-03.png") {
                Image(uiImage: bg).resizable().scaledToFill()
            } else {
                Color.black
            }
            Color.black.opacity(0.65)
            VStack(spacing: 10) {
                DK2BitmapText(DK2L10n.shared[1478], font: "Ingame28", size: 28,
                              color: DK2UIStyle.gold)
                DK2BitmapText(mission?.name ?? "", font: "Ingame18", size: 18,
                              color: DK2UIStyle.parchment)
                HStack(alignment: .top, spacing: 12) {
                    if let art = DK2UIStyle.image("BG-Objective-02.png") {
                        Image(uiImage: art).resizable().scaledToFit().frame(width: 110, height: 120)
                    }
                    VStack(alignment: .leading, spacing: 4) {
                        DK2BitmapText(DK2L10n.shared[2898], font: "Ingame18", size: 18,
                                      color: DK2UIStyle.gold)
                        ForEach([1641, 1637, 1638], id: \.self) { id in
                            DK2BitmapText("\(DK2L10n.shared[id]): \(debriefValue(for: id))",
                                          font: "Ingame10", size: 10,
                                          color: DK2UIStyle.parchment)
                        }
                    }
                }
                ScrollView {
                    HStack(alignment: .top) {
                        VStack(alignment: .leading, spacing: 2) {
                            ForEach(debriefLabels, id: \.self) { id in
                                DK2BitmapText(DK2L10n.shared[id], font: "Ingame10", size: 10,
                                              color: DK2UIStyle.parchment)
                            }
                        }
                        VStack(alignment: .trailing, spacing: 2) {
                            ForEach(debriefLabels.indices, id: \.self) { index in
                                DK2BitmapText(debriefValue(for: debriefLabels[index]),
                                              font: "Ingame10", size: 10, color: .white)
                            }
                        }
                    }
                }
                .background(DK2UIStyle.gold.opacity(0.3))
                HStack {
                    DK2UIStyle.okButton {
                        DK2GameCommand.postQuit()
                        onQuit()
                    }
                }
            }
            .padding(24)
        }
        .frame(width: geo.size.width, height: geo.size.height)
        .allowsHitTesting(true)
    }

    private let debriefLabels = [1516, 1410, 1413, 1509, 1510, 1511, 1411, 1512,
                                 1513, 1514, 1429, 1515, 1636]

    private func debriefValue(for id: Int) -> String {
        switch id {
        case 1516: return victory ? "Yes" : "No"
        case 1410: return "\(ticks / 30)s"
        case 1511: return "\(gold) / \(goldCapacity)"
        case 1512: return "\(mana) / \(manaMax)"
        case 1514: return "\(creatures)"
        case 1513: return "\(roomsCount)"
        case 1641: return victory ? "100" : "0"
        case 1637: return "0"
        case 1638: return "0/?"
        default:
            let key = "debrief.\(id)"
            if let value = state[key] as? Int { return "\(value)" }
            if let value = state[key] as? NSNumber { return "\(value.intValue)" }
            return "?"
        }
    }

    @ViewBuilder
    private func pauseMenuLayer(_ geo: GeometryProxy) -> some View {
        ZStack {
            Color.black.opacity(0.78)
            VStack(spacing: 10) {
                DK2BitmapText(DK2L10n.shared[94], font: "Ingame28", size: 28,
                              color: DK2UIStyle.gold)
                HStack(alignment: .top, spacing: 48) {
                    VStack(alignment: .leading, spacing: 10) {
                        DK2UIStyle.menuText(96) { DK2UIStyle.postFrontEndSound() }
                        DK2UIStyle.menuText(95) { DK2UIStyle.postFrontEndSound() }
                        DK2UIStyle.menuText(98) { DK2UIStyle.postFrontEndSound() }
                    }
                    VStack(alignment: .leading, spacing: 10) {
                        DK2UIStyle.menuText(142) {
                            showPauseMenu = false
                            DK2GameCommand.postPause(false)
                        }
                        DK2UIStyle.menuText(145) {
                            DK2GameCommand.postLoad(slot: slot)
                            saveMessage = "Loaded \(slot)"
                            showPauseMenu = false
                        }
                        Button {
                            DK2GameCommand.postSave(slot: slot)
                            saveMessage = "Saved \(slot)"
                            showPauseMenu = false
                        } label: {
                            DK2BitmapText("Save Game", font: "Frontend20", size: 20,
                                          color: DK2UIStyle.gold)
                        }
                        .buttonStyle(.plain)
                        DK2UIStyle.menuText(11) {
                            DK2GameCommand.postQuit()
                            onQuit()
                        }
                        Button {
                            showConsole = true
                            showPauseMenu = false
                        } label: {
                            DK2BitmapText("Console", font: "Frontend20", size: 20,
                                          color: DK2UIStyle.gold)
                        }
                        .buttonStyle(.plain)
                    }
                }
                TextField("Slot", text: $slot)
                    .textFieldStyle(.roundedBorder)
                    .frame(width: 200)
                HStack(spacing: 16) {
                    DK2UIStyle.okButton { showPauseMenu = false }
                    DK2UIStyle.cancelButton { showPauseMenu = false }
                }
            }
            .padding(24)
        }
        .frame(width: geo.size.width, height: geo.size.height)
        .allowsHitTesting(true)
    }

    @ViewBuilder
    private func consoleLayer(_ geo: GeometryProxy) -> some View {
        VStack(spacing: 0) {
            ZStack {
                Color.black.opacity(0.9)
                ScrollView {
                    VStack(alignment: .leading, spacing: 2) {
                        ForEach(allConsoleLines, id: \.self) { line in
                            DK2BitmapText(line, font: "Ingame10", size: 10,
                                          color: DK2UIStyle.parchment)
                                .frame(maxWidth: .infinity, alignment: .leading)
                        }
                    }
                    .padding(8)
                }
            }
            .frame(height: geo.size.height * 0.35)
            HStack {
                TextField("Console", text: $consoleInput, onCommit: {
                    if !consoleInput.isEmpty {
                        localConsole.append(consoleInput)
                        if localConsole.count > 25 { localConsole.removeFirst() }
                        consoleInput = ""
                    }
                })
                .textFieldStyle(.roundedBorder)
                Button {
                    if !consoleInput.isEmpty {
                        localConsole.append(consoleInput)
                        if localConsole.count > 25 { localConsole.removeFirst() }
                        consoleInput = ""
                    }
                } label: {
                    DK2BitmapText("Enter", font: "Ingame10", size: 10,
                                  color: DK2UIStyle.gold)
                }
                .buttonStyle(.plain)
                Button {
                    showConsole = false
                } label: {
                    DK2BitmapText("Close", font: "Ingame10", size: 10,
                                  color: DK2UIStyle.parchment)
                }
                .buttonStyle(.plain)
            }
            .padding(8)
            .background(DK2UIStyle.panelDark)
            Spacer(minLength: 0)
        }
        .frame(width: geo.size.width, height: geo.size.height)
        .allowsHitTesting(true)
    }

    private var allConsoleLines: [String] {
        var lines: [String] = []
        if let values = state["consoleLines"] as? [String] { lines.append(contentsOf: values) }
        else if let text = state["console"] as? String {
            lines.append(contentsOf: text.split(separator: "\n").map(String.init))
        }
        lines.append(contentsOf: localConsole)
        return Array(lines.suffix(25))
    }
}

struct DK2HoverArea: UIViewRepresentable {
    let changed: (Bool) -> Void

    func makeUIView(context: Context) -> UIView {
        let view = UIView()
        view.backgroundColor = .clear
        let recognizer = UIHoverGestureRecognizer(target: context.coordinator,
                                                  action: #selector(Coordinator.hover(_:)))
        view.addGestureRecognizer(recognizer)
        return view
    }

    func updateUIView(_ uiView: UIView, context: Context) {
        context.coordinator.changed = changed
    }

    func makeCoordinator() -> Coordinator { Coordinator(self) }

    final class Coordinator: NSObject {
        var changed: (Bool) -> Void

        init(_ parent: DK2HoverArea) { changed = parent.changed }

        @objc func hover(_ recognizer: UIHoverGestureRecognizer) {
            switch recognizer.state {
            case .began, .changed: changed(true)
            default: changed(false)
            }
        }
    }
}
