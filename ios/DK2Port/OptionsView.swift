import Foundation
import SwiftUI

/// User-facing settings persisted in UserDefaults. Rendering consumers may
/// observe `renderingDidChange`; audio observes `audioDidChange` directly.
enum DK2Settings {
    enum Key {
        static let masterVolume = "dk2.settings.masterVolume"
        static let musicVolume = "dk2.settings.musicVolume"
        static let sfxVolume = "dk2.settings.sfxVolume"
        static let speechVolume = "dk2.settings.speechVolume"
        static let gamma = "dk2.settings.gamma"
        static let shadows = "dk2.settings.shadows"
        static let viewDistance = "dk2.settings.viewDistance"
    }

    static let audioDidChange = Notification.Name("dk2.settings.audioDidChange")
    static let renderingDidChange = Notification.Name("dk2.settings.renderingDidChange")

    private static func number(_ key: String, _ fallback: Float) -> Float {
        let defaults = UserDefaults.standard
        guard defaults.object(forKey: key) != nil else { return fallback }
        return defaults.float(forKey: key)
    }

    static var masterVolume: Float {
        get { number(Key.masterVolume, 0.7) }
        set {
            UserDefaults.standard.set(newValue, forKey: Key.masterVolume)
            NotificationCenter.default.post(name: audioDidChange, object: nil)
        }
    }

    static var musicVolume: Float {
        get { number(Key.musicVolume, 0.6) }
        set {
            UserDefaults.standard.set(newValue, forKey: Key.musicVolume)
            NotificationCenter.default.post(name: audioDidChange, object: nil)
        }
    }

    static var sfxVolume: Float {
        get { number(Key.sfxVolume, 0.8) }
        set {
            UserDefaults.standard.set(newValue, forKey: Key.sfxVolume)
            NotificationCenter.default.post(name: audioDidChange, object: nil)
        }
    }

    static var speechVolume: Float {
        get { number(Key.speechVolume, 0.8) }
        set {
            UserDefaults.standard.set(newValue, forKey: Key.speechVolume)
            NotificationCenter.default.post(name: audioDidChange, object: nil)
        }
    }

    static var gamma: Float {
        get { number(Key.gamma, 1.0) }
        set {
            UserDefaults.standard.set(newValue, forKey: Key.gamma)
            NotificationCenter.default.post(name: renderingDidChange, object: nil)
        }
    }

    static var viewDistance: Float {
        get { number(Key.viewDistance, 1.0) }
        set {
            UserDefaults.standard.set(newValue, forKey: Key.viewDistance)
            NotificationCenter.default.post(name: renderingDidChange, object: nil)
        }
    }

    static var shadows: Bool {
        get {
            let defaults = UserDefaults.standard
            guard defaults.object(forKey: Key.shadows) != nil else { return true }
            return defaults.bool(forKey: Key.shadows)
        }
        set {
            UserDefaults.standard.set(newValue, forKey: Key.shadows)
            NotificationCenter.default.post(name: renderingDidChange, object: nil)
        }
    }

    static func reset() {
        let defaults = UserDefaults.standard
        for key in [Key.masterVolume, Key.musicVolume, Key.sfxVolume,
                    Key.speechVolume, Key.gamma, Key.shadows, Key.viewDistance] {
            defaults.removeObject(forKey: key)
        }
        NotificationCenter.default.post(name: audioDidChange, object: nil)
        NotificationCenter.default.post(name: renderingDidChange, object: nil)
    }
}

struct DK2OptionsView: View {
    var onDone: (() -> Void)? = nil

    @State private var master = DK2Settings.masterVolume
    @State private var music = DK2Settings.musicVolume
    @State private var sfx = DK2Settings.sfxVolume
    @State private var speech = DK2Settings.speechVolume
    @State private var gamma = DK2Settings.gamma
    @State private var viewDistance = DK2Settings.viewDistance
    @State private var shadows = DK2Settings.shadows

    var body: some View {
        VStack(spacing: 14) {
            HStack {
                Text("Options")
                    .font(.system(size: 22, weight: .bold, design: .serif))
                    .foregroundColor(Color(red: 0.95, green: 0.85, blue: 0.62))
                Spacer()
                if let onDone = onDone {
                    Button("Done") { onDone() }
                        .foregroundColor(Color(red: 0.95, green: 0.85, blue: 0.62))
                }
            }

            settingSlider(title: "Master volume", value: $master, range: 0...1) {
                DK2Settings.masterVolume = $0
            }
            settingSlider(title: "Music volume", value: $music, range: 0...1) {
                DK2Settings.musicVolume = $0
            }
            settingSlider(title: "SFX volume", value: $sfx, range: 0...1) {
                DK2Settings.sfxVolume = $0
            }
            settingSlider(title: "Speech volume", value: $speech, range: 0...1) {
                DK2Settings.speechVolume = $0
            }
            settingSlider(title: "Gamma", value: $gamma, range: 0.5...2.0) {
                DK2Settings.gamma = $0
            }
            settingSlider(title: "View distance", value: $viewDistance, range: 0.5...2.0) {
                DK2Settings.viewDistance = $0
            }

            Toggle(isOn: $shadows) {
                Text("Shadows").foregroundColor(.white)
            }
            .onChange(of: shadows) { DK2Settings.shadows = $0 }
            .toggleStyle(.switch)

            HStack(spacing: 16) {
                Button("Reset Defaults") {
                    DK2Settings.reset()
                    master = DK2Settings.masterVolume
                    music = DK2Settings.musicVolume
                    sfx = DK2Settings.sfxVolume
                    speech = DK2Settings.speechVolume
                    gamma = DK2Settings.gamma
                    viewDistance = DK2Settings.viewDistance
                    shadows = DK2Settings.shadows
                }
                .foregroundColor(.gray)
                Spacer()
            }
        }
        .padding(18)
        .background(Color(red: 0.12, green: 0.08, blue: 0.07).opacity(0.96))
        .cornerRadius(12)
        .overlay(RoundedRectangle(cornerRadius: 12)
            .stroke(Color(red: 0.45, green: 0.32, blue: 0.18), lineWidth: 2))
        .padding()
        .onAppear {
            master = DK2Settings.masterVolume
            music = DK2Settings.musicVolume
            sfx = DK2Settings.sfxVolume
            speech = DK2Settings.speechVolume
            gamma = DK2Settings.gamma
            viewDistance = DK2Settings.viewDistance
            shadows = DK2Settings.shadows
        }
    }

    @ViewBuilder
    private func settingSlider(title: String,
                               value: Binding<Float>,
                               range: ClosedRange<Float>,
                               onChange: @escaping (Float) -> Void) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack {
                Text(title).foregroundColor(.white)
                Spacer()
                Text(String(format: "%.2f", value.wrappedValue))
                    .font(.system(size: 11, design: .monospaced))
                    .foregroundColor(.gray)
            }
            Slider(value: value, in: range)
                .onChange(of: value.wrappedValue) { onChange($0) }
        }
    }
}
