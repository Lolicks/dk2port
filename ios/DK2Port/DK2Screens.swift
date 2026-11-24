import Foundation
import SwiftUI
import UIKit

enum DK2Screen: String, CaseIterable, Identifiable {
    case empty
    case start
    case singlePlayer
    case quitGame
    case selectCampaignLevel
    case briefing
    case debriefing
    case cutscene
    case optionsMain
    case optionsGraphics
    case optionsSound
    case optionsControl
    case extras
    case hiscores
    case movies
    case cutsceneLocked
    case credits
    case multiplayer
    case multiplayerLocal
    case multiplayerWatch
    case skirmishLobby
    case myPetDungeon
    case myPetDungeonMapSelect
    case skirmishMapSelect
    case connectingLayer
    case errorMessage
    case confirmNewCampaign

    var id: String { rawValue }

    var isPopup: Bool {
        switch self {
        case .connectingLayer, .errorMessage, .confirmNewCampaign: return true
        default: return false
        }
    }

    var backdrop: String? {
        switch self {
        case .empty, .cutscene, .connectingLayer, .errorMessage, .confirmNewCampaign: return nil
        case .start, .credits, .cutsceneLocked: return "BackdropMain.png"
        case .singlePlayer, .quitGame: return "BG-Load-00.png"
        case .selectCampaignLevel, .skirmishMapSelect: return "BG-Map-00.png"
        case .briefing, .debriefing: return "BG-Objective-00.png"
        case .optionsMain, .optionsGraphics: return "BG-Game_Settings-00.png"
        case .optionsSound: return "BG-Game_Settings-02.png"
        case .optionsControl: return "BG-Controls-00.png"
        case .extras, .hiscores, .movies: return "BG-Service-00.png"
        case .multiplayer, .multiplayerLocal, .multiplayerWatch: return "BG-Multiplayer-00.png"
        case .skirmishLobby: return "BG-Sessions-00.png"
        case .myPetDungeon, .myPetDungeonMapSelect: return "BackdropMPD1.png"
        }
    }
}

final class DK2FrontEndModel: ObservableObject {
    @Published var base: DK2Screen = .start
    @Published var popup: DK2Screen?
    @Published var playingCutscene: String?
    @Published var showSaveSlots = false
    @Published var errorTitle = ""
    @Published var errorMessage = ""
    @Published var lobbyIsMultiplayer = false
    @Published var creditsRevision = 0
    @Published var selectedMapName = ""
    @Published var selectedMapSize = ""

    var missions: [DK2Mission] = []
    var onStart: ((DK2MapEntry) -> Void)?
    var selectedMission: DK2Mission?
    var selectedSkirmishMission: DK2Mission?
    private var cutsceneReturn: DK2Screen = .extras

    var campaign: [DK2Mission] {
        missions.filter { ($0.category ?? "").lowercased() == "campaign" }
            .sorted { ($0.campaignLevel ?? 999) < ($1.campaignLevel ?? 999) }
    }

    var skirmish: [DK2Mission] {
        missions.filter { ($0.category ?? "").lowercased() != "campaign" }
            .sorted { $0.name.localizedCompare($1.name) == .orderedAscending }
    }

    func goToScreen(_ screen: DK2Screen) {
        if screen.isPopup {
            popup = screen
        } else {
            base = screen
            popup = nil
        }
    }

    func doTransition(_ inEffect: Int, _ screen: DK2Screen, _ outEffect: Int?) {
        goToScreen(screen)
    }

    func openPopup(_ screen: DK2Screen) { popup = screen }
    func closePopup() { popup = nil }

    func showError(title: String, message: String) {
        errorTitle = title
        errorMessage = message
        popup = .errorMessage
    }

    func newCampaign() { popup = .confirmNewCampaign }
    func confirmNewCampaign() { popup = nil; base = .selectCampaignLevel }
    func cancelNewCampaign() { popup = nil }

    func startSkirmish() {
        lobbyIsMultiplayer = false
        goToScreen(.skirmishLobby)
    }

    func startLevel() {
        guard let mission = selectedMission else {
            goToScreen(.selectCampaignLevel)
            return
        }
        onStart?(mission.mapEntry())
    }

    func cancelLevelSelect() {
        goToScreen(base == .debriefing ? .start : .singlePlayer)
    }

    func quitToOS() { goToScreen(.empty) }

    func applyGraphicsSettings() { goToScreen(.optionsMain) }
    func applySoundSettings() { goToScreen(.optionsMain) }

    func playMovie(_ kind: DK2CutsceneKind) {
        if base != .cutscene { cutsceneReturn = base }
        base = .cutscene
        playingCutscene = DK2Cutscene.defaultName(for: kind)
        DK2Cutscene.request(kind)
    }

    func externalCutscene(_ name: String) {
        if base != .cutscene { cutsceneReturn = base }
        base = .cutscene
        playingCutscene = name
    }

    func finishCutscene() {
        playingCutscene = nil
        base = cutsceneReturn
    }

    func multiplayerRefresh() {}
    func connectToServer() { openPopup(.connectingLayer) }
    func multiplayerConnect() { openPopup(.connectingLayer) }

    func multiplayerCreate() {
        lobbyIsMultiplayer = true
        goToScreen(.skirmishLobby)
    }

    func kickPlayer() {}
    func changeAI() {}
    func addComputerPlayer() {}
    func setPlayerReady() { openPopup(.connectingLayer) }
    func cancelMultiplayer() { goToScreen(.multiplayer) }
    func selectRandomMap() {}

    func selectMPDLevel(_ level: Int) {
        showError(title: DK2L10n.shared[1675],
                  message: dk2MPDName(level) + " [not available in this port]")
    }

    func chooseSkirmishMission(_ mission: DK2Mission) {
        selectedSkirmishMission = mission
        selectedMapName = mission.name
        selectedMapSize = "\(mission.width) x \(mission.height)"
        onStart?(mission.mapEntry())
    }

    func mapSelected() {
        guard let mission = selectedSkirmishMission else {
            goToScreen(.skirmishLobby)
            return
        }
        selectedSkirmishMission = nil
        onStart?(mission.mapEntry())
    }

    func cancelMapSelection() { goToScreen(.skirmishLobby) }
    func restartCredits() { creditsRevision += 1 }
}

func dk2MPDName(_ level: Int) -> String { "${mpd\(level).0}" }

enum DK2FontStyle {
    case body, small, menu, menuDisabled, subTitle, header, hiscoreHead, hiscoreRow

    var font: String {
        switch self {
        case .body, .hiscoreHead, .hiscoreRow: return "Frontend14"
        case .small: return "Frontend20"
        case .menu, .menuDisabled, .subTitle, .header: return "Frontend28"
        }
    }

    var size: CGFloat {
        switch self {
        case .body, .hiscoreHead, .hiscoreRow: return 14
        case .small: return 20
        case .menu, .menuDisabled, .subTitle, .header: return 28
        }
    }

    var color: Color {
        switch self {
        case .hiscoreHead: return Color(red: 1.0, green: 0.37, blue: 0.0)
        case .subTitle: return Color(red: 0.93, green: 0.93, blue: 0.93)
        case .header: return DK2UIStyle.gold
        default: return DK2UIStyle.parchment
        }
    }
}

struct DK2Text: View {
    let text: String
    var style: DK2FontStyle = .body

    var body: some View {
        DK2BitmapText(text, font: style.font, size: style.size, color: style.color)
    }
}

struct DK2L10nText: View {
    let id: Int
    var style: DK2FontStyle = .body

    var body: some View {
        DK2Text(text: DK2L10n.shared[id], style: style)
            .accessibilityIdentifier("menu.\(id)")
    }
}

struct DK2MenuAction: View {
    let id: Int
    let action: () -> Void

    var body: some View {
        DK2UIStyle.menuText(id, action: action)
            .accessibilityIdentifier("menu.\(id)")
    }
}

struct DK2DisabledMenuText: View {
    let id: Int

    var body: some View {
        DK2L10nText(id: id, style: .menuDisabled)
    }
}

struct DK2TextAction: View {
    let text: String
    var style: DK2FontStyle = .menu
    let action: () -> Void

    var body: some View {
        Button(action: {
            DK2UIStyle.postFrontEndSound()
            action()
        }) {
            DK2Text(text: text, style: style)
        }
        .buttonStyle(.plain)
    }
}

struct DK2Unavailable: View {
    let id: Int

    var body: some View {
        DK2Text(text: DK2L10n.shared[id] + " [not available in this port]", style: .body)
            .accessibilityIdentifier("unavailable.\(id)")
    }
}

struct DK2VSpace: View {
    let height: CGFloat
    var id: String = "spacer"

    var body: some View {
        Spacer().frame(height: height).accessibilityIdentifier(id)
    }
}

struct DK2EmptyStringText: View {
    var body: some View {
        Spacer().frame(height: 18).accessibilityIdentifier("empty.string")
    }
}

struct DK2Panel<Content: View>: View {
    let id: String
    var background: Color?
    var border: Bool = false
    let content: Content

    init(_ id: String, background: Color? = nil, border: Bool = false,
         @ViewBuilder content: () -> Content) {
        self.id = id
        self.background = background
        self.border = border
        self.content = content()
    }

    var body: some View {
        content
            .padding(6)
            .background(Rectangle().fill(background ?? Color.clear))
            .overlay(
                Group {
                    if border { Rectangle().stroke(DK2UIStyle.border, lineWidth: 1) }
                }
            )
            .accessibilityIdentifier(id)
    }
}

struct DK2ScreenHeader: View {
    let id: Int

    var body: some View {
        DK2UIStyle.menuTitle(id)
            .frame(maxWidth: .infinity)
            .frame(height: 84)
            .accessibilityIdentifier("title.menu.\(id)")
    }
}

struct DK2ScreenScaffold<Content: View>: View {
    let screen: DK2Screen
    let content: Content

    init(_ screen: DK2Screen, @ViewBuilder content: () -> Content) {
        self.screen = screen
        self.content = content()
    }

    var body: some View {
        ZStack {
            Group {
                if let name = screen.backdrop, let image = DK2UIStyle.image(name) {
                    Image(uiImage: image).resizable().scaledToFill()
                } else {
                    Color.black
                }
            }
            .ignoresSafeArea()
            content
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }
}

struct DK2CheckBox: View {
    let id: String
    @Binding var checked: Bool

    var body: some View {
        Button(action: {
            checked.toggle()
            DK2UIStyle.postFrontEndSound()
        }) {
            ZStack {
                Rectangle()
                    .fill(DK2UIStyle.panelDark)
                    .frame(width: 24, height: 24)
                if let image = DK2UIStyle.image(checked ? "Tick-1.png" : "Tick-0.png") {
                    Image(uiImage: image).resizable().scaledToFit().frame(width: 24, height: 24)
                } else {
                    Image(systemName: checked ? "checkmark.square" : "square")
                        .foregroundColor(DK2UIStyle.gold)
                }
            }
        }
        .buttonStyle(.plain)
        .accessibilityIdentifier(id)
    }
}

struct DK2DropDown: View {
    let id: String
    let options: [String]
    @Binding var selection: String
    var width: CGFloat = 200

    var body: some View {
        Picker(selection: $selection, label: DK2Text(text: selection, style: .small)) {
            ForEach(options, id: \.self) { option in
                DK2Text(text: option, style: .small).tag(option)
            }
        }
        .pickerStyle(.menu)
        .labelsHidden()
        .frame(width: width, height: 34)
        .background(DK2UIStyle.panelDark.opacity(0.9))
        .overlay(Rectangle().stroke(DK2UIStyle.border, lineWidth: 1))
        .accessibilityIdentifier(id)
    }
}

struct DK2SliderControl: View {
    let id: String
    @Binding var value: Double
    let range: ClosedRange<Double>
    let step: Double

    var body: some View {
        Slider(value: $value, in: range, step: step)
            .accentColor(DK2UIStyle.gold)
            .accessibilityIdentifier(id)
    }
}

struct DK2TextFieldControl: View {
    let id: String
    let initial: String
    @Binding var text: String

    var body: some View {
        TextField("", text: $text)
            .textFieldStyle(.roundedBorder)
            .frame(width: 220)
            .accessibilityIdentifier(id)
            .onAppear { if text.isEmpty { text = initial } }
    }
}

struct DK2TableSpec {
    let id: String
    let columns: [String]
    let rows: [[String]]
    var rowAction: ((Int) -> Void)? = nil
}

struct DK2TableView: View {
    let spec: DK2TableSpec

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 2) {
                ForEach(spec.columns.indices, id: \.self) { column in
                    DK2Text(text: spec.columns[column], style: .small)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
            }
            .padding(.vertical, 4)
            .background(DK2UIStyle.panelDark.opacity(0.95))
            ScrollView {
                LazyVStack(spacing: 0) {
                    ForEach(spec.rows.indices, id: \.self) { row in
                        HStack(spacing: 2) {
                            ForEach(spec.rows[row].indices, id: \.self) { column in
                                DK2Text(text: spec.rows[row][column], style: .small)
                                    .frame(maxWidth: .infinity, alignment: .leading)
                            }
                        }
                        .padding(.vertical, 3)
                        .contentShape(Rectangle())
                        .onTapGesture { spec.rowAction?(row) }
                    }
                }
            }
        }
        .background(Color.black.opacity(0.5))
        .overlay(Rectangle().stroke(DK2UIStyle.border, lineWidth: 1))
        .accessibilityIdentifier(spec.id)
    }
}

struct DK2ChatView: View {
    let id: String
    @State private var draft = ""

    var body: some View {
        VStack {
            ScrollView {
                Text("").frame(maxWidth: .infinity)
            }
            HStack {
                TextField("", text: $draft).textFieldStyle(.roundedBorder)
                Button(action: { draft = "" }) {
                    DK2Text(text: ">", style: .small)
                }
                .buttonStyle(.plain)
            }
        }
        .padding(6)
        .background(Color.black.opacity(0.5))
        .overlay(Rectangle().stroke(DK2UIStyle.border, lineWidth: 1))
        .accessibilityIdentifier(id)
    }
}

struct DK2MissionRow: View {
    let mission: DK2Mission
    let action: () -> Void

    private var artName: String? {
        if let level = mission.campaignLevel {
            let variant = mission.campaignVariant ?? ""
            let candidates = [
                "2dLevel\(level)\(variant)-1.png",
                "Level\(level)\(variant)-0.png",
                "2dLevel\(level)-1.png",
                "Level\(level)-0.png"
            ]
            for name in candidates where DK2UIStyle.image(name) != nil { return name }
        }
        return nil
    }

    var body: some View {
        Button(action: action) {
            HStack(spacing: 10) {
                Group {
                    if let name = artName, let image = DK2UIStyle.image(name) {
                        Image(uiImage: image).resizable().scaledToFill()
                    } else if let image = DK2UIStyle.image("Box-00.png") {
                        Image(uiImage: image).resizable().scaledToFit()
                    } else {
                        Rectangle().fill(DK2UIStyle.panelDark)
                    }
                }
                .frame(width: 72, height: 48)
                .clipped()
                .cornerRadius(4)
                VStack(alignment: .leading, spacing: 2) {
                    DK2Text(text: mission.name, style: .small)
                    DK2Text(text: "\(mission.width) x \(mission.height)", style: .body)
                }
                Spacer()
            }
            .padding(8)
            .background(DK2UIStyle.panelDark.opacity(0.85))
            .overlay(Rectangle().stroke(DK2UIStyle.border, lineWidth: 1))
        }
        .buttonStyle(.plain)
        .accessibilityIdentifier("mission.\(mission.mapFile)")
    }
}

struct DK2ScreenContent: View {
    let screen: DK2Screen

    var body: some View {
        switch screen {
        case .empty: DK2EmptyScreen()
        case .start: DK2StartScreen()
        case .singlePlayer: DK2SinglePlayerScreen()
        case .quitGame: DK2QuitGameScreen()
        case .selectCampaignLevel: DK2SelectCampaignLevelScreen()
        case .briefing: DK2BriefingScreen()
        case .debriefing: DK2DebriefingScreen()
        case .cutscene: DK2CutsceneScreen()
        case .optionsMain: DK2OptionsMainScreen()
        case .optionsGraphics: DK2GraphicsOptionsScreen()
        case .optionsSound: DK2SoundOptionsScreen()
        case .optionsControl: DK2ControlOptionsScreen()
        case .extras: DK2ExtrasScreen()
        case .hiscores: DK2HiscoresScreen()
        case .movies: DK2MoviesScreen()
        case .cutsceneLocked: DK2CutsceneLockedScreen()
        case .credits: DK2CreditsScreen()
        case .multiplayer: DK2MultiplayerScreen()
        case .multiplayerLocal: DK2MultiplayerLocalScreen()
        case .multiplayerWatch: DK2MultiplayerWatchScreen()
        case .skirmishLobby: DK2SkirmishLobbyScreen()
        case .myPetDungeon: DK2MyPetDungeonScreen()
        case .myPetDungeonMapSelect: DK2MyPetDungeonMapSelectScreen()
        case .skirmishMapSelect: DK2SkirmishMapSelectScreen()
        case .connectingLayer, .errorMessage, .confirmNewCampaign: DK2EmptyScreen()
        }
    }
}

struct DK2EmptyScreen: View {
    var body: some View {
        Color.clear.accessibilityIdentifier("empty")
    }
}

struct DK2StartScreen: View {
    @EnvironmentObject private var nav: DK2FrontEndModel

    var body: some View {
        DK2ScreenScaffold(.start) {
            VStack(spacing: 0) {
                VStack {
                    if let logo = DK2UIStyle.image("Logo.png") {
                        Image(uiImage: logo).resizable().scaledToFit().frame(height: 110)
                    }
                }
                .frame(maxWidth: .infinity, minHeight: 110)
                .accessibilityIdentifier("start.logo")

                Spacer()
                VStack(spacing: 10) {
                    DK2MenuAction(id: 71) { nav.goToScreen(.singlePlayer) }
                    DK2MenuAction(id: 72) { nav.doTransition(271, .multiplayer, 273) }
                    DK2MenuAction(id: 1675) { nav.doTransition(262, .myPetDungeon, 264) }
                    DK2EmptyStringText()
                    DK2MenuAction(id: 94) { nav.doTransition(256, .optionsMain, 258) }
                    DK2MenuAction(id: 73) { nav.doTransition(259, .extras, 261) }
                    DK2EmptyStringText()
                    DK2MenuAction(id: 28) { nav.goToScreen(.quitGame) }
                }
                .accessibilityIdentifier("start.menu")
                Spacer()

                VStack(alignment: .trailing, spacing: 2) {
                    DK2Text(text: versionText, style: .body)
                        .accessibilityIdentifier("PROP.VERSION")
                    DK2Text(text: "Open Source Edition", style: .body)
                }
                .padding(.trailing, 10)
            }
        }
    }

    private var versionText: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "OpenKeeper"
    }
}

struct DK2SinglePlayerScreen: View {
    @EnvironmentObject private var nav: DK2FrontEndModel

    var body: some View {
        DK2ScreenScaffold(.singlePlayer) {
            VStack(spacing: 0) {
                DK2ScreenHeader(id: 71)
                Spacer()
                VStack(spacing: 10) {
                    DK2MenuAction(id: 140) { nav.newCampaign() }
                        .accessibilityIdentifier("newCampaign")
                    DK2MenuAction(id: 2059) { nav.doTransition(251, .selectCampaignLevel, nil) }
                    DK2EmptyStringText()
                    DK2MenuAction(id: 141) { nav.startSkirmish() }
                    DK2MenuAction(id: 145) { nav.showSaveSlots = true }
                        .accessibilityIdentifier("loadGame")
                    DK2VSpace(height: 20, id: "cancel.margin")
                    DK2UIStyle.cancelButton { nav.goToScreen(.start) }
                }
                Spacer()
            }
        }
    }
}

struct DK2QuitGameScreen: View {
    @EnvironmentObject private var nav: DK2FrontEndModel

    var body: some View {
        DK2ScreenScaffold(.quitGame) {
            VStack(spacing: 0) {
                DK2ScreenHeader(id: 28)
                Spacer()
                VStack(spacing: 10) {
                    DK2L10nText(id: 15, style: .menuDisabled).accessibilityIdentifier("quitText")
                    DK2VSpace(height: 20, id: "buttons.margin")
                    HStack(spacing: 16) {
                        DK2UIStyle.okButton { nav.quitToOS() }
                        DK2UIStyle.cancelButton { nav.goToScreen(.start) }
                    }
                }
                Spacer()
            }
        }
    }
}

struct DK2SelectCampaignLevelScreen: View {
    @EnvironmentObject private var nav: DK2FrontEndModel

    var body: some View {
        DK2ScreenScaffold(.selectCampaignLevel) {
            VStack(spacing: 0) {
                ScrollView {
                    LazyVStack(spacing: 6) {
                        ForEach(nav.campaign) { mission in
                            DK2MissionRow(mission: mission) {
                                nav.selectedMission = mission
                                nav.onStart?(mission.mapEntry())
                            }
                        }
                    }
                    .padding()
                }
                .accessibilityIdentifier("campaign.map")
                HStack {
                    DK2UIStyle.cancelButton { nav.doTransition(252, .singlePlayer, 274) }
                }
                .padding(.bottom, 12)
            }
        }
    }
}

struct DK2BriefingScreen: View {
    @EnvironmentObject private var nav: DK2FrontEndModel

    private let subObjectiveIDs = [1410, 1413, 1509]

    var body: some View {
        DK2ScreenScaffold(.briefing) {
            VStack(spacing: 0) {
                DK2ScreenHeader(id: 1476)
                DK2Text(text: nav.selectedMission?.name ?? "", style: .subTitle)
                    .accessibilityIdentifier("levelTitle")
                DK2VSpace(height: 30, id: "briefing.gap1")
                DK2Panel("mainObjectivePanel") {
                    HStack(spacing: 8) {
                        ZStack {
                            if let image = DK2UIStyle.image("BG-Objective-00.png") {
                                Image(uiImage: image).resizable().scaledToFit()
                            }
                        }
                        .frame(width: 110, height: 90)
                        .accessibilityIdentifier("mainObjectiveImage")
                        VStack(spacing: 4) {
                            DK2Panel("mainObjectivesTitle.header", background: DK2UIStyle.gold.opacity(0.35)) {
                                DK2L10nText(id: 1408, style: .header)
                            }
                            DK2Panel("mainObjective.body", background: Color.black.opacity(0.5)) {
                                DK2Text(text: nav.selectedMission?.name ?? "", style: .small)
                            }
                        }
                    }
                }
                DK2VSpace(height: 30, id: "briefing.gap2")
                DK2Panel("subObjectivePanel") {
                    HStack(spacing: 8) {
                        ZStack {
                            if let image = DK2UIStyle.image("BG-Objective-01.png") {
                                Image(uiImage: image).resizable().scaledToFit()
                            }
                        }
                        .frame(width: 110, height: 90)
                        .accessibilityIdentifier("subObjectiveImage")
                        VStack(spacing: 4) {
                            DK2Panel("subObjectivesTitle.header", background: DK2UIStyle.gold.opacity(0.35)) {
                                DK2L10nText(id: 1409, style: .header)
                            }
                            DK2Panel("subObjective.body", background: Color.black.opacity(0.5)) {
                                ForEach(Array(subObjectiveIDs.indices), id: \.self) { index in
                                    DK2Text(text: "", style: .small)
                                        .accessibilityIdentifier("subObjective\(index + 1)")
                                }
                            }
                        }
                    }
                }
                Spacer()
                HStack(spacing: 16) {
                    DK2UIStyle.okButton { nav.startLevel() }
                    DK2UIStyle.cancelButton { nav.cancelLevelSelect() }
                }
                .padding(.bottom, 12)
            }
            .padding(.horizontal, 12)
        }
    }
}

struct DK2DebriefingScreen: View {
    @EnvironmentObject private var nav: DK2FrontEndModel

    private let statLabels = [1516, 1410, 1413, 1509, 1510, 1511, 1411, 1512, 1513, 1514, 1429, 1515, 1636]
    private let statValues = ["levelWon", "timeTaken", "enemyCreaturesKilled", "heroesDestroyed",
                              "roomsCaptured", "landOwned", "goldMined", "manaStored",
                              "itemsManufactured", "creaturesCommanded", "creaturesConverted",
                              "creatureTraining", "attemptsAtLevel"]

    var body: some View {
        DK2ScreenScaffold(.debriefing) {
            VStack(spacing: 0) {
                DK2ScreenHeader(id: 1478)
                DK2L10nText(id: 1478, style: .subTitle)
                    .accessibilityIdentifier("dLevelTitle")
                DK2VSpace(height: 30, id: "debriefing.gap1")
                DK2Panel("main_objective") {
                    HStack(spacing: 8) {
                        VStack {
                            ZStack {
                                if let image = DK2UIStyle.image("BG-Objective-02.png") {
                                    Image(uiImage: image).resizable().scaledToFit()
                                }
                            }
                            .accessibilityIdentifier("dMainObjectiveImage")
                            Spacer()
                        }
                        .frame(width: 110)
                        VStack(spacing: 4) {
                            DK2Panel("debriefing.rating.header", background: DK2UIStyle.gold.opacity(0.35)) {
                                DK2L10nText(id: 2898, style: .header)
                            }
                            HStack {
                                VStack(alignment: .leading) {
                                    ForEach([1641, 1637, 1638], id: \.self) { id in
                                        DK2L10nText(id: id, style: .small)
                                    }
                                }
                                VStack(alignment: .leading) {
                                    DK2Text(text: "?", style: .small).accessibilityIdentifier("totalEvilRating")
                                    DK2Text(text: "?", style: .small).accessibilityIdentifier("overallTotalEvilRating")
                                    DK2Text(text: "?/?", style: .small).accessibilityIdentifier("specialsFound")
                                }
                            }
                            .padding(.horizontal, 10)
                            .background(Color.black.opacity(0.5))
                        }
                    }
                }
                DK2VSpace(height: 30, id: "debriefing.gap2")
                DK2Panel("sub_objective") {
                    HStack(spacing: 8) {
                        VStack {
                            ZStack {
                                if let image = DK2UIStyle.image("BG-Objective-03.png") {
                                    Image(uiImage: image).resizable().scaledToFit()
                                }
                            }
                            .accessibilityIdentifier("dSubObjectiveImage")
                            Spacer()
                        }
                        .frame(width: 110)
                        ScrollView {
                            HStack(alignment: .top) {
                                VStack(alignment: .leading, spacing: 2) {
                                    ForEach(statLabels, id: \.self) { id in
                                        DK2L10nText(id: id, style: .small)
                                    }
                                }
                                VStack(alignment: .trailing, spacing: 2) {
                                    ForEach(statValues, id: \.self) { name in
                                        DK2Text(text: "?", style: .small).accessibilityIdentifier(name)
                                    }
                                }
                            }
                        }
                        .background(DK2UIStyle.gold.opacity(0.35))
                    }
                }
                Spacer()
                HStack {
                    DK2UIStyle.okButton { nav.cancelLevelSelect() }
                }
                .padding(.bottom, 12)
            }
            .padding(.horizontal, 12)
        }
    }
}

struct DK2CutsceneScreen: View {
    @EnvironmentObject private var nav: DK2FrontEndModel

    var body: some View {
        ZStack {
            Color.black
            DK2Text(text: nav.playingCutscene ?? "", style: .menuDisabled)
                .accessibilityIdentifier("cutsceneMovieName")
        }
        .ignoresSafeArea()
    }
}

struct DK2OptionsMainScreen: View {
    @EnvironmentObject private var nav: DK2FrontEndModel

    var body: some View {
        DK2ScreenScaffold(.optionsMain) {
            VStack(spacing: 0) {
                DK2ScreenHeader(id: 94)
                Spacer()
                VStack(spacing: 10) {
                    DK2MenuAction(id: 96) { nav.goToScreen(.optionsGraphics) }
                    DK2MenuAction(id: 95) { nav.goToScreen(.optionsSound) }
                    DK2MenuAction(id: 98) { nav.goToScreen(.optionsControl) }
                    DK2VSpace(height: 20, id: "options.ok.margin")
                    DK2UIStyle.okButton { nav.doTransition(257, .start, 274) }
                }
                Spacer()
            }
        }
    }
}

struct DK2GraphicsOptionsScreen: View {
    @EnvironmentObject private var nav: DK2FrontEndModel

    @State private var resolution = DK2Settings.text(DK2Settings.Key.resolution, "1024 x 768")
    @State private var fullscreen = DK2Settings.flag(DK2Settings.Key.fullscreen, false)
    @State private var antialiasing = DK2Settings.text(DK2Settings.Key.antialiasing, "Off")
    @State private var openGl = DK2Settings.text(DK2Settings.Key.openGl, "OpenGL 2.0")
    @State private var ssao = DK2Settings.flag(DK2Settings.Key.ssao, false)
    @State private var gammaCorrection = DK2Settings.flag(DK2Settings.Key.gammaCorrection, true)
    @State private var hdTextures = DK2Settings.flag(DK2Settings.Key.hdTextures, false)
    @State private var bitDepth = DK2Settings.text(DK2Settings.Key.bitDepth, "32")
    @State private var refreshRate = DK2Settings.text(DK2Settings.Key.refreshRate, "60")
    @State private var verticalSync = DK2Settings.flag(DK2Settings.Key.verticalSync, false)
    @State private var anisotropic = DK2Settings.text(DK2Settings.Key.anisotropicFiltering, "Off")

    private let resolutions = ["640 x 480", "800 x 600", "1024 x 768", "1280 x 1024", "1600 x 1200"]
    private let aaOptions = ["Off", "2x", "4x", "8x"]
    private let glOptions = ["OpenGL 1.5", "OpenGL 2.0", "OpenGL 3.0", "OpenGL 4.0"]
    private let depthOptions = ["16", "24", "32"]
    private let refreshOptions = ["30", "60", "75", "120", "144"]
    private let anisotropyOptions = ["Off", "2x", "4x", "8x", "16x"]

    var body: some View {
        DK2ScreenScaffold(.optionsGraphics) {
            VStack(spacing: 0) {
                DK2ScreenHeader(id: 96)
                HStack(alignment: .top) {
                    VStack(alignment: .leading, spacing: 8) {
                        HStack {
                            DK2L10nText(id: 134, style: .small)
                            DK2DropDown(id: "resolution", options: resolutions, selection: $resolution)
                        }
                        HStack {
                            DK2CheckBox(id: "fullscreen", checked: $fullscreen)
                            DK2Text(text: " Fullscreen", style: .small)
                        }
                        VStack(alignment: .leading) {
                            DK2Text(text: "Antialiasing: ", style: .small)
                            DK2DropDown(id: "antialiasing", options: aaOptions, selection: $antialiasing, width: 100)
                        }
                        VStack(alignment: .leading) {
                            DK2Text(text: "OpenGL: ", style: .small)
                            DK2DropDown(id: "openGl", options: glOptions, selection: $openGl)
                        }
                        HStack {
                            DK2CheckBox(id: "ssao", checked: $ssao)
                            DK2Text(text: " SSAO", style: .small)
                        }
                        HStack {
                            DK2CheckBox(id: "gammaCorrection", checked: $gammaCorrection)
                            DK2Text(text: " Gamma correction", style: .small)
                        }
                        HStack {
                            DK2CheckBox(id: "hdTextures", checked: $hdTextures)
                            DK2Text(text: " Make HD textures at the next start (needs a Vulkan GPU, about 6 minutes)", style: .body)
                        }
                    }
                    Spacer()
                    VStack(alignment: .leading, spacing: 8) {
                        VStack(alignment: .leading) {
                            DK2Text(text: "Bit depth: ", style: .small)
                            DK2DropDown(id: "bitDepth", options: depthOptions, selection: $bitDepth, width: 100)
                        }
                        VStack(alignment: .leading) {
                            DK2Text(text: "Refresh rate: ", style: .small)
                            DK2DropDown(id: "refreshRate", options: refreshOptions, selection: $refreshRate, width: 100)
                        }
                        HStack {
                            DK2CheckBox(id: "verticalSync", checked: $verticalSync)
                            DK2Text(text: " Vertical sync", style: .small)
                        }
                        VStack(alignment: .leading) {
                            DK2Text(text: "Anisotropic filtering: ", style: .small)
                            DK2DropDown(id: "anisotropicFiltering", options: anisotropyOptions, selection: $anisotropic, width: 100)
                        }
                    }
                }
                .padding(10)
                .background(DK2UIStyle.panelDark.opacity(0.6))
                .accessibilityIdentifier("graphics.options.panels")
                Spacer()
                HStack(spacing: 16) {
                    DK2UIStyle.okButton { save(); nav.applyGraphicsSettings() }
                    DK2UIStyle.cancelButton { nav.goToScreen(.optionsMain) }
                }
                .padding(.bottom, 12)
            }
            .padding(.horizontal, 12)
        }
    }

    private func save() {
        DK2Settings.setText(DK2Settings.Key.resolution, resolution)
        DK2Settings.setFlag(DK2Settings.Key.fullscreen, fullscreen)
        DK2Settings.setText(DK2Settings.Key.antialiasing, antialiasing)
        DK2Settings.setText(DK2Settings.Key.openGl, openGl)
        DK2Settings.setFlag(DK2Settings.Key.ssao, ssao)
        DK2Settings.setFlag(DK2Settings.Key.gammaCorrection, gammaCorrection)
        DK2Settings.setFlag(DK2Settings.Key.hdTextures, hdTextures)
        DK2Settings.setText(DK2Settings.Key.bitDepth, bitDepth)
        DK2Settings.setText(DK2Settings.Key.refreshRate, refreshRate)
        DK2Settings.setFlag(DK2Settings.Key.verticalSync, verticalSync)
        DK2Settings.setText(DK2Settings.Key.anisotropicFiltering, anisotropic)
    }
}

struct DK2SoundOptionsScreen: View {
    @EnvironmentObject private var nav: DK2FrontEndModel

    @State private var master = Double(DK2Settings.masterVolume)
    @State private var voice = Double(DK2Settings.value(DK2Settings.Key.voiceVolumeValue, 1.0))
    @State private var music = Double(DK2Settings.musicVolume)
    @State private var sfx = Double(DK2Settings.sfxVolume)
    @State private var voiceEnabled = DK2Settings.flag(DK2Settings.Key.voiceEnabled, true)
    @State private var musicEnabled = DK2Settings.flag(DK2Settings.Key.musicEnabled, true)
    @State private var sfxEnabled = DK2Settings.flag(DK2Settings.Key.sfxEnabled, true)

    var body: some View {
        DK2ScreenScaffold(.optionsSound) {
            VStack(spacing: 0) {
                DK2ScreenHeader(id: 95)
                VStack(spacing: 12) {
                    soundRow(id: 1460, value: $master) { DK2Settings.masterVolume = Float($0) }
                    soundRow(id: 1457, value: $voice, toggle: $voiceEnabled) {
                        DK2Settings.setValue(DK2Settings.Key.voiceVolumeValue, Float($0))
                        DK2Settings.speechVolume = Float($0)
                    }
                    soundRow(id: 1459, value: $music, toggle: $musicEnabled) {
                        DK2Settings.musicVolume = Float($0)
                    }
                    soundRow(id: 1458, value: $sfx, toggle: $sfxEnabled) {
                        DK2Settings.sfxVolume = Float($0)
                    }
                }
                .padding(12)
                .background(DK2UIStyle.panelDark.opacity(0.6))
                .accessibilityIdentifier("sound.options.rows")
                Spacer()
                HStack(spacing: 16) {
                    DK2UIStyle.okButton { save(); nav.applySoundSettings() }
                    DK2UIStyle.cancelButton { nav.goToScreen(.optionsMain) }
                }
                .padding(.bottom, 12)
            }
            .padding(.horizontal, 12)
        }
    }

    @ViewBuilder
    private func soundRow(id: Int, value: Binding<Double>,
                          toggle: Binding<Bool>? = nil,
                          onChange: @escaping (Double) -> Void) -> some View {
        HStack {
            DK2L10nText(id: id, style: .small)
                .frame(width: 180, alignment: .leading)
            DK2SliderControl(id: "\(id).slider", value: value, range: 0...1, step: 0.01)
            if let toggle = toggle {
                DK2CheckBox(id: DK2SoundOptionsScreen.toggleID(id), checked: toggle)
            } else {
                Spacer().frame(width: 24)
            }
        }
        .onChange(of: value.wrappedValue) { onChange($0) }
    }

    static func toggleID(_ id: Int) -> String {
        switch id {
        case 1457: return "voiceEnabled"
        case 1459: return "musicEnabled"
        case 1458: return "sfxEnabled"
        default: return "sound.toggle.\(id)"
        }
    }

    private func save() {
        DK2Settings.setValue(DK2Settings.Key.voiceVolumeValue, Float(voice))
        DK2Settings.setFlag(DK2Settings.Key.voiceEnabled, voiceEnabled)
        DK2Settings.setFlag(DK2Settings.Key.musicEnabled, musicEnabled)
        DK2Settings.setFlag(DK2Settings.Key.sfxEnabled, sfxEnabled)
    }
}

struct DK2ControlOptionsScreen: View {
    @EnvironmentObject private var nav: DK2FrontEndModel

    @State private var mouse = Double(DK2Settings.value(DK2Settings.Key.mouseSensitivity, 0.7))
    @State private var gameSpeed = Double(DK2Settings.value(DK2Settings.Key.gameSpeed, 1.0))
    @State private var invertMouse = DK2Settings.flag(DK2Settings.Key.invertMouse, false)
    @State private var scrollSpeed = Double(DK2Settings.value(DK2Settings.Key.scrollSpeed, 1.0))

    private let keyboardColumns = [2845, 2846]

    var body: some View {
        DK2ScreenScaffold(.optionsControl) {
            VStack(spacing: 0) {
                DK2ScreenHeader(id: 98)
                DK2Panel("keyboardSetup") {
                    DK2TableView(spec: DK2TableSpec(
                        id: "keyboardSetup.table",
                        columns: keyboardColumns.map { DK2L10n.shared[$0] },
                        rows: (0..<5).map { _ in ["", ""] }))
                }
                .frame(height: 200)
                HStack(spacing: 16) {
                    VStack {
                        DK2L10nText(id: 1466, style: .small)
                        DK2SliderControl(id: "mouseSensitivity", value: $mouse, range: 0.5...2, step: 0.1)
                    }
                    VStack {
                        DK2L10nText(id: 1469, style: .small)
                        DK2SliderControl(id: "gameSpeed", value: $gameSpeed, range: 0.25...4, step: 0.25)
                    }
                }
                .padding(10)
                HStack(spacing: 16) {
                    HStack {
                        DK2CheckBox(id: "invertMouse", checked: $invertMouse)
                        DK2L10nText(id: 2840, style: .small)
                    }
                    VStack {
                        DK2L10nText(id: 1468, style: .small)
                        DK2SliderControl(id: "scrollSpeed", value: $scrollSpeed, range: 0.25...4, step: 0.25)
                    }
                }
                .padding(.horizontal, 10)
                Spacer()
                HStack(spacing: 16) {
                    DK2UIStyle.okButton { save(); nav.goToScreen(.optionsMain) }
                    DK2UIStyle.cancelButton { nav.goToScreen(.optionsMain) }
                }
                .padding(.bottom, 12)
            }
            .padding(.horizontal, 12)
        }
    }

    private func save() {
        DK2Settings.setValue(DK2Settings.Key.mouseSensitivity, Float(mouse))
        DK2Settings.setValue(DK2Settings.Key.gameSpeed, Float(gameSpeed))
        DK2Settings.setFlag(DK2Settings.Key.invertMouse, invertMouse)
        DK2Settings.setValue(DK2Settings.Key.scrollSpeed, Float(scrollSpeed))
    }
}

struct DK2ExtrasScreen: View {
    @EnvironmentObject private var nav: DK2FrontEndModel

    var body: some View {
        DK2ScreenScaffold(.extras) {
            VStack(spacing: 0) {
                DK2ScreenHeader(id: 73)
                VStack(spacing: 10) {
                    DK2MenuAction(id: 2802) { nav.goToScreen(.hiscores) }
                    DK2MenuAction(id: 93) { nav.goToScreen(.movies) }
                }
                .frame(maxWidth: .infinity, minHeight: 150)
                .accessibilityIdentifier("extras.top")
                Spacer().frame(height: 30).accessibilityIdentifier("extras.gap")
                VStack(spacing: 10) {
                    DK2MenuAction(id: 86) { nav.playMovie(.trailer) }
                    DK2MenuAction(id: 372) { nav.doTransition(265, .credits, nil) }
                }
                .frame(maxWidth: .infinity)
                .accessibilityIdentifier("extras.bottom")
                Spacer()
                HStack {
                    DK2UIStyle.okButton { nav.doTransition(260, .start, 274) }
                }
                .padding(.bottom, 12)
            }
        }
    }
}

struct DK2HiscoresScreen: View {
    @EnvironmentObject private var nav: DK2FrontEndModel

    var body: some View {
        DK2ScreenScaffold(.hiscores) {
            VStack(spacing: 0) {
                DK2ScreenHeader(id: 2802)
                ScrollView {
                    LazyVStack(spacing: 4) {
                        DK2L10nText(id: 2802, style: .hiscoreHead)
                    }
                }
                .frame(width: 900, maxWidth: .infinity)
                .accessibilityIdentifier("hiscoreList")
                HStack {
                    DK2UIStyle.okButton { nav.goToScreen(.extras) }
                }
                .padding(.bottom, 12)
            }
        }
    }
}

struct DK2MoviesScreen: View {
    @EnvironmentObject private var nav: DK2FrontEndModel

    private struct MovieEntry: Identifiable {
        let id: Int
        let kind: DK2CutsceneKind?
    }

    private let movies: [MovieEntry] = [
        MovieEntry(id: 77, kind: .intro),
        MovieEntry(id: 91, kind: .intro),
        MovieEntry(id: 2843, kind: .outro),
        MovieEntry(id: 86, kind: .trailer),
        MovieEntry(id: 87, kind: nil),
        MovieEntry(id: 88, kind: nil)
    ]

    var body: some View {
        DK2ScreenScaffold(.movies) {
            VStack(spacing: 0) {
                DK2ScreenHeader(id: 93)
                ScrollView {
                    LazyVStack(spacing: 8) {
                        ForEach(movies) { movie in
                            DK2MenuAction(id: movie.id) {
                                if let kind = movie.kind {
                                    nav.playMovie(kind)
                                } else {
                                    nav.goToScreen(.cutsceneLocked)
                                }
                            }
                        }
                    }
                    .padding()
                }
                .frame(width: 900, maxWidth: .infinity)
                .accessibilityIdentifier("moviePanel.movieList")
                HStack {
                    DK2UIStyle.okButton { nav.goToScreen(.extras) }
                }
                .padding(.bottom, 12)
            }
        }
    }
}

struct DK2CutsceneLockedScreen: View {
    @EnvironmentObject private var nav: DK2FrontEndModel

    var body: some View {
        DK2ScreenScaffold(.cutsceneLocked) {
            VStack(spacing: 0) {
                Spacer().frame(height: 84)
                Spacer()
                VStack(spacing: 10) {
                    DK2L10nText(id: 2805, style: .menuDisabled)
                        .multilineTextAlignment(.center)
                    DK2VSpace(height: 20, id: "cutsceneLocked.margin")
                    DK2UIStyle.okButton { nav.goToScreen(.movies) }
                }
                .frame(width: 900, maxWidth: .infinity)
                Spacer()
            }
        }
    }
}

struct DK2CreditsScreen: View {
    @EnvironmentObject private var nav: DK2FrontEndModel

    var body: some View {
        DK2ScreenScaffold(.credits) {
            VStack(spacing: 0) {
                DK2ScreenHeader(id: 372)
                ScrollView {
                    VStack(spacing: 12) {
                        DK2L10nText(id: 372, style: .subTitle)
                        DK2Text(text: "OpenKeeper", style: .small)
                        DK2Text(text: "Open Source Edition", style: .body)
                    }
                    .frame(maxWidth: .infinity)
                    .id(nav.creditsRevision)
                }
                .frame(width: 900, maxWidth: .infinity)
                .accessibilityIdentifier("creditsPanel")
                .onTapGesture { nav.restartCredits() }
                HStack {
                    DK2UIStyle.okButton { nav.doTransition(266, .extras, 261) }
                }
                .padding(.bottom, 12)
            }
        }
    }
}

struct DK2MultiplayerScreen: View {
    @EnvironmentObject private var nav: DK2FrontEndModel

    var body: some View {
        DK2ScreenScaffold(.multiplayer) {
            VStack(spacing: 0) {
                DK2ScreenHeader(id: 149)
                DK2Unavailable(id: 149)
                Spacer()
                VStack(spacing: 10) {
                    DK2MenuAction(id: 168) { nav.goToScreen(.multiplayerLocal) }
                    DK2MenuAction(id: 2729) { nav.goToScreen(.multiplayerWatch) }
                    DK2VSpace(height: 20, id: "multiplayer.cancel.margin")
                    DK2UIStyle.cancelButton { nav.doTransition(272, .start, 274) }
                }
                Spacer()
            }
        }
    }
}

struct DK2MultiplayerLocalScreen: View {
    @EnvironmentObject private var nav: DK2FrontEndModel

    @State private var playerName = "Player"
    @State private var gameName = "OpenKeeper"
    @State private var gamePort = "7575"

    private let gameColumns = [1393, 194, 533, 0]

    var body: some View {
        DK2ScreenScaffold(.multiplayerLocal) {
            VStack(spacing: 0) {
                DK2ScreenHeader(id: 168)
                DK2Unavailable(id: 168)
                HStack(alignment: .top) {
                    VStack {
                        DK2L10nText(id: 157, style: .subTitle)
                        DK2TableView(spec: DK2TableSpec(
                            id: "multiplayerGamesTable",
                            columns: gameColumns.map { $0 == 0 ? "" : DK2L10n.shared[$0] },
                            rows: [],
                            rowAction: { _ in nav.connectToServer() }))
                        DK2L10nText(id: 1557, style: .subTitle)
                    }
                    .frame(maxWidth: .infinity)
                    VStack {
                        DK2L10nText(id: 1688, style: .subTitle)
                        DK2TextFieldControl(id: "playerName", initial: "Player", text: $playerName)
                        DK2L10nText(id: 1393, style: .subTitle)
                        DK2TextFieldControl(id: "gameName", initial: "OpenKeeper", text: $gameName)
                        DK2L10nText(id: 236, style: .subTitle)
                        DK2TextFieldControl(id: "gamePort", initial: "7575", text: $gamePort)
                    }
                }
                .padding(10)
                HStack {
                    HStack(spacing: 24) {
                        DK2MenuAction(id: 216) { nav.multiplayerRefresh() }
                        DK2MenuAction(id: 1403) { nav.multiplayerConnect() }
                    }
                    Spacer()
                    DK2MenuAction(id: 158) { nav.multiplayerCreate() }
                }
                .padding(.horizontal, 20)
                Spacer()
                HStack {
                    DK2UIStyle.cancelButton { nav.goToScreen(.multiplayer) }
                }
                .padding(.bottom, 12)
            }
            .padding(.horizontal, 12)
        }
    }
}

struct DK2MultiplayerWatchScreen: View {
    @EnvironmentObject private var nav: DK2FrontEndModel

    @State private var playerName = ""
    @State private var hostAddress = ""

    var body: some View {
        DK2ScreenScaffold(.multiplayerWatch) {
            VStack(spacing: 0) {
                DK2ScreenHeader(id: 2729)
                DK2Unavailable(id: 2729)
                Spacer()
                VStack(spacing: 10) {
                    HStack {
                        DK2Text(text: "Name", style: .subTitle)
                        DK2TextFieldControl(id: "playerName", initial: "", text: $playerName)
                    }
                    HStack {
                        DK2Text(text: "Address", style: .subTitle)
                        DK2TextFieldControl(id: "hostAddress", initial: "", text: $hostAddress)
                    }
                    HStack {
                        Spacer()
                        DK2TextAction(text: "Connect") { nav.multiplayerConnect() }
                            .accessibilityIdentifier("connect.watch")
                    }
                    DK2VSpace(height: 20, id: "multiplayerWatch.margin")
                    DK2UIStyle.cancelButton { nav.goToScreen(.multiplayer) }
                }
                .frame(width: 600, maxWidth: .infinity)
                Spacer()
            }
        }
    }
}

struct DK2SkirmishLobbyScreen: View {
    @EnvironmentObject private var nav: DK2FrontEndModel

    var body: some View {
        DK2ScreenScaffold(.skirmishLobby) {
            VStack(spacing: 0) {
                DK2L10nText(id: nav.lobbyIsMultiplayer ? 149 : 141, style: .subTitle)
                    .frame(maxWidth: .infinity, minHeight: 84)
                    .accessibilityIdentifier("multiplayerTitle")
                if nav.lobbyIsMultiplayer { DK2Unavailable(id: 149) }
                HStack(alignment: .top) {
                    VStack(alignment: .leading) {
                        DK2Panel("ipPanel", background: Color(white: 0.5, opacity: 0.19)) {
                            DK2Text(text: "", style: .small).accessibilityIdentifier("ip")
                        }
                        DK2TableView(spec: DK2TableSpec(
                            id: "playersTable", columns: ["", ""], rows: []))
                    }
                    .frame(maxWidth: .infinity)
                    VStack {
                        DK2Panel("mapNameTitle.header", background: DK2UIStyle.gold.opacity(0.35)) {
                            DK2L10nText(id: 1544, style: .small).accessibilityIdentifier("mapNameTitle")
                        }
                        HStack {
                            DK2L10nText(id: 1492, style: .small)
                            DK2Text(text: "", style: .small).accessibilityIdentifier("playerCount")
                        }
                        ZStack {
                            if let image = DK2UIStyle.image("BG-Map-00.png") {
                                Image(uiImage: image).resizable().scaledToFit()
                            }
                        }
                        .accessibilityIdentifier("mapImage")
                    }
                    .frame(width: 320)
                }
                .frame(maxWidth: .infinity)
                HStack(alignment: .top) {
                    VStack {
                        HStack {
                            VStack {
                                DK2MenuAction(id: 270) { nav.kickPlayer() }
                            }
                            VStack {
                                DK2MenuAction(id: 2121) { nav.changeAI() }
                                    .accessibilityIdentifier("changeAi")
                                DK2MenuAction(id: 1547) { nav.addComputerPlayer() }
                            }
                        }
                        .accessibilityIdentifier("multiplayerPlayerControl")
                        DK2Panel("chatPanel") {
                            DK2ChatView(id: "multiplayerChat")
                        }
                    }
                    .frame(maxWidth: .infinity)
                    VStack(spacing: 10) {
                        DK2MenuAction(id: 1495) { nav.goToScreen(.skirmishMapSelect) }
                        DK2MenuAction(id: 1496) { nav.selectRandomMap() }
                        DK2L10nText(id: 2803, style: .menuDisabled)
                    }
                    .frame(width: 320)
                    .accessibilityIdentifier("multiplayerMapControl")
                }
                .frame(maxWidth: .infinity)
                .padding(.top, 10)
                Spacer()
                HStack(spacing: 16) {
                    DK2UIStyle.okButton { nav.setPlayerReady() }
                    DK2UIStyle.cancelButton { nav.cancelMultiplayer() }
                }
                .padding(.bottom, 12)
            }
            .padding(.horizontal, 12)
        }
    }
}

struct DK2MyPetDungeonScreen: View {
    @EnvironmentObject private var nav: DK2FrontEndModel

    var body: some View {
        DK2ScreenScaffold(.myPetDungeon) {
            VStack(spacing: 0) {
                DK2ScreenHeader(id: 1675)
                DK2Unavailable(id: 1675)
                ScrollView {
                    VStack(spacing: 8) {
                        DK2TextAction(text: dk2MPDName(1)) { nav.selectMPDLevel(1) }
                            .accessibilityIdentifier("mpd1")
                        ForEach([2, 3, 4, 5, 6], id: \.self) { level in
                            DK2Text(text: dk2MPDName(level), style: .menuDisabled)
                                .accessibilityIdentifier("mpd\(level)")
                        }
                        DK2MenuAction(id: 1618) { nav.goToScreen(.myPetDungeonMapSelect) }
                            .accessibilityIdentifier("mpdOther")
                    }
                }
                .accessibilityIdentifier("mpdList")
                HStack {
                    DK2UIStyle.cancelButton { nav.doTransition(263, .start, 274) }
                    Spacer().frame(width: 48).padding(5)
                }
                .padding(.bottom, 12)
            }
        }
    }
}

struct DK2MyPetDungeonMapSelectScreen: View {
    @EnvironmentObject private var nav: DK2FrontEndModel

    var body: some View {
        DK2ScreenScaffold(.myPetDungeonMapSelect) {
            VStack(spacing: 0) {
                DK2ScreenHeader(id: 1675)
                DK2Unavailable(id: 1675)
                DK2TableView(spec: DK2TableSpec(
                    id: "mpdPanel.mapsPanel.mapsTable",
                    columns: [DK2L10n.shared[1544], DK2L10n.shared[2960], DK2L10n.shared[1545]],
                    rows: []))
                    .frame(maxWidth: .infinity)
                Spacer()
                HStack {
                    DK2UIStyle.cancelButton { nav.goToScreen(.myPetDungeon) }
                    Spacer().frame(width: 48).padding(5)
                }
                .padding(.bottom, 12)
            }
            .padding(.horizontal, 12)
        }
    }
}

struct DK2SkirmishMapSelectScreen: View {
    @EnvironmentObject private var nav: DK2FrontEndModel

    var body: some View {
        DK2ScreenScaffold(.skirmishMapSelect) {
            VStack(spacing: 0) {
                DK2ScreenHeader(id: 2800)
                HStack(alignment: .top) {
                    ScrollView {
                        LazyVStack(spacing: 4) {
                            ForEach(nav.skirmish) { mission in
                                DK2MissionRow(mission: mission) {
                                    nav.chooseSkirmishMission(mission)
                                }
                            }
                        }
                        .padding(4)
                    }
                    .frame(maxWidth: .infinity)
                    .accessibilityIdentifier("skirmishPanel.mapsPanel.mapsTable")
                    VStack {
                        DK2Panel("mapNameTitle.header", background: DK2UIStyle.gold.opacity(0.35)) {
                            DK2Text(text: nav.selectedMapName, style: .small)
                                .accessibilityIdentifier("mapNameTitle")
                        }
                        HStack {
                            DK2L10nText(id: 1492, style: .small)
                            DK2Text(text: "", style: .small).accessibilityIdentifier("playerCount")
                        }
                        ZStack {
                            if let image = DK2UIStyle.image("BG-Map-01.png") {
                                Image(uiImage: image).resizable().scaledToFit()
                            }
                        }
                        .accessibilityIdentifier("mapImage")
                        DK2Text(text: nav.selectedMapSize, style: .small)
                            .accessibilityIdentifier("mapSize")
                    }
                    .frame(width: 320)
                }
                Spacer()
                HStack(spacing: 16) {
                    DK2UIStyle.okButton { nav.mapSelected() }
                    DK2UIStyle.cancelButton { nav.cancelMapSelection() }
                }
                .padding(.bottom, 12)
            }
            .padding(.horizontal, 12)
        }
    }
}

struct DK2PopupLayer: View {
    @EnvironmentObject private var nav: DK2FrontEndModel
    let popup: DK2Screen

    var body: some View {
        ZStack {
            Color.black.opacity(0.65).ignoresSafeArea()
            switch popup {
            case .connectingLayer:
                DK2L10nText(id: 2070, style: .subTitle)
                    .accessibilityIdentifier("connectingLayer")
            case .errorMessage:
                VStack(spacing: 12) {
                    DK2Text(text: nav.errorTitle, style: .subTitle).accessibilityIdentifier("title")
                    DK2Text(text: nav.errorMessage, style: .small).accessibilityIdentifier("message")
                    DK2UIStyle.okButton { nav.closePopup() }
                }
                .padding(24)
                .background(DK2UIStyle.panelDark)
                .overlay(Rectangle().stroke(DK2UIStyle.border, lineWidth: 2))
                .accessibilityIdentifier("errorMessage")
            case .confirmNewCampaign:
                VStack(spacing: 12) {
                    DK2L10nText(id: 140, style: .subTitle)
                    DK2L10nText(id: 2847, style: .small)
                    DK2L10nText(id: 15, style: .small)
                    HStack(spacing: 16) {
                        DK2UIStyle.okButton { nav.confirmNewCampaign() }
                        DK2UIStyle.cancelButton { nav.cancelNewCampaign() }
                    }
                }
                .padding(24)
                .background(DK2UIStyle.panelDark)
                .overlay(Rectangle().stroke(DK2UIStyle.border, lineWidth: 2))
                .accessibilityIdentifier("confirmNewCampaign")
            default:
                EmptyView()
            }
        }
    }
}
