import Foundation
import SwiftUI
import UIKit

enum DK2UIStyle {
    static let parchment = Color(red: 0.85, green: 0.78, blue: 0.58)
    static let gold = Color(red: 0.95, green: 0.85, blue: 0.62)
    static let panelDark = Color(red: 0.12, green: 0.09, blue: 0.07)
    static let border = Color(red: 0.45, green: 0.32, blue: 0.16)

    static let frontEndSound = Notification.Name("dk2.audio.front_end")

    private static let imageLock = NSLock()
    private static var imageCache: [String: UIImage] = [:]

    static func image(_ name: String) -> UIImage? {
        imageLock.lock()
        defer { imageLock.unlock() }
        if let cached = imageCache[name] { return cached }

        let relative = name.contains("/") ? name : "textures/FrontEnd/\(name)"
        let root = DK2AssetLoader.root
        let direct = root.appendingPathComponent(relative)
        var result = UIImage(contentsOfFile: direct.path)

        if result == nil {
            let directory = direct.deletingLastPathComponent()
            let wanted = direct.lastPathComponent.lowercased()
            if let entries = try? FileManager.default.contentsOfDirectory(atPath: directory.path),
               let match = entries.first(where: { $0.lowercased() == wanted }) {
                result = UIImage(contentsOfFile: directory.appendingPathComponent(match).path)
            }
        }

        if let result = result { imageCache[name] = result }
        return result
    }

    static func font(_ name: String) -> String {
        if name == ".system" || name.lowercased() == "system" { return "Frontend14" }
        if name.hasSuffix(".fnt") { return String(name.dropLast(4)) }
        return name
    }

    static func okButton(_ title: String? = nil, action: @escaping () -> Void) -> AnyView {
        AnyView(DK2AssetButton(title: title ?? "OK",
                               bases: ["Confirm", "ConfirmButton", "Ok", "OK"],
                               fallbacks: ["Bigbutton.PNG", "Bigbutton.png"],
                               action: action))
    }

    static func cancelButton(_ title: String? = nil, action: @escaping () -> Void) -> AnyView {
        AnyView(DK2AssetButton(title: title ?? "Cancel",
                               bases: ["Cancel", "CancelButton"],
                               fallbacks: ["Bigbutton.PNG", "Bigbutton.png"],
                               action: action))
    }

    static func menuText(_ id: Int, action: @escaping () -> Void) -> AnyView {
        AnyView(DK2MenuTextButton(title: DK2L10n.shared[id], action: action))
    }

    static func menuTitle(_ id: Int) -> AnyView {
        AnyView(DK2BitmapText(DK2L10n.shared[id], font: "Frontend36", size: 36, color: gold)
            .shadow(color: .black, radius: 2, y: 2))
    }

    static func postFrontEndSound() {
        NotificationCenter.default.post(name: frontEndSound, object: nil)
    }

    fileprivate static func firstImage(_ names: [String]) -> UIImage? {
        for name in names {
            if let image = image(name) { return image }
        }
        return nil
    }
}

private struct DK2AssetButton: View {
    let title: String
    let bases: [String]
    let fallbacks: [String]
    let action: () -> Void

    private var images: (normal: UIImage?, over: UIImage?, click: UIImage?) {
        var normal: UIImage?
        var over: UIImage?
        var click: UIImage?
        for base in bases {
            normal = DK2UIStyle.firstImage(["\(base).png", "\(base).PNG", base])
            if normal != nil {
                over = DK2UIStyle.firstImage([
                    "\(base)-Over.png", "\(base)_Over.png", "\(base)Over.png",
                    "\(base)-Over.PNG", "\(base)_Over.PNG", "\(base)Over.PNG"
                ])
                click = DK2UIStyle.firstImage([
                    "\(base)-Click.png", "\(base)_Click.png", "\(base)Click.png",
                    "\(base)-Click.PNG", "\(base)_Click.PNG", "\(base)Click.PNG"
                ])
                break
            }
        }
        if normal == nil { normal = DK2UIStyle.firstImage(fallbacks) }
        return (normal, over, click)
    }

    var body: some View {
        let assets = images
        Button(action: {
            DK2UIStyle.postFrontEndSound()
            action()
        }) {
            DK2BitmapText(title, font: "Frontend20", size: 20, color: DK2UIStyle.gold)
                .shadow(color: .black, radius: 1, y: 1)
        }
        .buttonStyle(DK2AssetButtonStyle(normal: assets.normal, over: assets.over, click: assets.click))
    }
}

private struct DK2AssetButtonStyle: ButtonStyle {
    let normal: UIImage?
    let over: UIImage?
    let click: UIImage?

    func makeBody(configuration: Configuration) -> some View {
        ZStack {
            if let image = configuration.isPressed ? (click ?? normal) : (over ?? normal) {
                Image(uiImage: image)
                    .resizable()
                    .scaledToFit()
                    .frame(height: 52)
            } else {
                RoundedRectangle(cornerRadius: 6)
                    .fill(DK2UIStyle.panelDark)
                    .frame(width: 220, height: 48)
                    .overlay(RoundedRectangle(cornerRadius: 6)
                        .stroke(DK2UIStyle.border, lineWidth: 2))
            }
            configuration.label
        }
    }
}

private struct DK2MenuTextButton: View {
    let title: String
    let action: () -> Void
    @State private var hovering = false

    var body: some View {
        Button(action: {
            DK2UIStyle.postFrontEndSound()
            action()
        }) {
            DK2BitmapText(title, font: "Frontend20", size: 20,
                          color: hovering ? DK2UIStyle.gold : DK2UIStyle.parchment)
        }
        .buttonStyle(.plain)
        .background(DK2HoverDetector(hovering: $hovering))
    }
}

private struct DK2HoverDetector: UIViewRepresentable {
    @Binding var hovering: Bool

    func makeUIView(context: Context) -> UIView {
        let view = UIView()
        view.backgroundColor = .clear
        let recognizer = UIHoverGestureRecognizer(target: context.coordinator,
                                                  action: #selector(Coordinator.hover(_:)))
        view.addGestureRecognizer(recognizer)
        return view
    }

    func updateUIView(_ uiView: UIView, context: Context) {}

    func makeCoordinator() -> Coordinator { Coordinator(self) }

    final class Coordinator: NSObject {
        let parent: DK2HoverDetector

        init(_ parent: DK2HoverDetector) { self.parent = parent }

        @objc func hover(_ recognizer: UIHoverGestureRecognizer) {
            switch recognizer.state {
            case .began, .changed:
                parent.hovering = true
                DK2UIStyle.postFrontEndSound()
            default:
                parent.hovering = false
            }
        }
    }
}
