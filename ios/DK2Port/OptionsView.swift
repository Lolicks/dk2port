import Foundation
import SwiftUI

enum DK2Settings {
    enum Key {
        static let masterVolume = "dk2.settings.masterVolume"
        static let musicVolume = "dk2.settings.musicVolume"
        static let sfxVolume = "dk2.settings.sfxVolume"
        static let speechVolume = "dk2.settings.speechVolume"
        static let gamma = "dk2.settings.gamma"
        static let shadows = "dk2.settings.shadows"
        static let viewDistance = "dk2.settings.viewDistance"

        static let resolution = "dk2.settings.resolution"
        static let fullscreen = "dk2.settings.fullscreen"
        static let antialiasing = "dk2.settings.antialiasing"
        static let openGl = "dk2.settings.openGl"
        static let ssao = "dk2.settings.ssao"
        static let gammaCorrection = "dk2.settings.gammaCorrection"
        static let hdTextures = "dk2.settings.hdTextures"
        static let bitDepth = "dk2.settings.bitDepth"
        static let refreshRate = "dk2.settings.refreshRate"
        static let verticalSync = "dk2.settings.verticalSync"
        static let anisotropicFiltering = "dk2.settings.anisotropicFiltering"

        static let voiceVolumeValue = "dk2.settings.voiceVolume"
        static let voiceEnabled = "dk2.settings.voiceEnabled"
        static let musicEnabled = "dk2.settings.musicEnabled"
        static let sfxEnabled = "dk2.settings.sfxEnabled"

        static let mouseSensitivity = "dk2.settings.mouseSensitivity"
        static let gameSpeed = "dk2.settings.gameSpeed"
        static let invertMouse = "dk2.settings.invertMouse"
        static let scrollSpeed = "dk2.settings.scrollSpeed"

        static let all = [
            masterVolume, musicVolume, sfxVolume, speechVolume, gamma, shadows,
            viewDistance, resolution, fullscreen, antialiasing, openGl, ssao,
            gammaCorrection, hdTextures, bitDepth, refreshRate, verticalSync,
            anisotropicFiltering, voiceVolumeValue, voiceEnabled, musicEnabled,
            sfxEnabled, mouseSensitivity, gameSpeed, invertMouse, scrollSpeed
        ]
    }

    static let audioDidChange = Notification.Name("dk2.settings.audioDidChange")
    static let renderingDidChange = Notification.Name("dk2.settings.renderingDidChange")

    private static func number(_ key: String, _ fallback: Float) -> Float {
        let defaults = UserDefaults.standard
        guard defaults.object(forKey: key) != nil else { return fallback }
        return defaults.float(forKey: key)
    }

    private static func boolean(_ key: String, _ fallback: Bool) -> Bool {
        let defaults = UserDefaults.standard
        guard defaults.object(forKey: key) != nil else { return fallback }
        return defaults.bool(forKey: key)
    }

    static func value(_ key: String, _ fallback: Float) -> Float { number(key, fallback) }
    static func flag(_ key: String, _ fallback: Bool) -> Bool { boolean(key, fallback) }
    static func text(_ key: String, _ fallback: String) -> String {
        UserDefaults.standard.string(forKey: key) ?? fallback
    }

    static func setValue(_ key: String, _ value: Float) {
        UserDefaults.standard.set(value, forKey: key)
    }

    static func setFlag(_ key: String, _ value: Bool) {
        UserDefaults.standard.set(value, forKey: key)
    }

    static func setText(_ key: String, _ value: String) {
        UserDefaults.standard.set(value, forKey: key)
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
        get { boolean(Key.shadows, true) }
        set {
            UserDefaults.standard.set(newValue, forKey: Key.shadows)
            NotificationCenter.default.post(name: renderingDidChange, object: nil)
        }
    }

    static func reset() {
        let defaults = UserDefaults.standard
        for key in Key.all { defaults.removeObject(forKey: key) }
        NotificationCenter.default.post(name: audioDidChange, object: nil)
        NotificationCenter.default.post(name: renderingDidChange, object: nil)
    }
}

struct DK2OptionsView: View {
    var onDone: (() -> Void)? = nil
    @StateObject private var nav = DK2FrontEndModel()

    var body: some View {
        DK2OptionsMainScreen()
            .environmentObject(nav)
            .overlay(alignment: .topTrailing) {
                if let onDone = onDone {
                    Button("Done") { onDone() }
                        .foregroundColor(DK2UIStyle.gold)
                        .padding()
                }
            }
    }
}
