import Foundation
import SwiftUI
import UIKit

private struct DK2Glyph {
    let x: Int
    let y: Int
    let width: Int
    let height: Int
    let xoffset: Int
    let yoffset: Int
    let xadvance: Int
    let page: Int
}

private struct DK2KerningPair: Hashable {
    let first: UInt32
    let second: UInt32
}

private struct DK2PlacedGlyph {
    let glyph: DK2Glyph?
    let x: Int
    let y: Int
    let advance: Int
}

private final class DK2ParsedFont {
    let lineHeight: Int
    let base: Int
    let pointSize: Int
    let chars: [UInt32: DK2Glyph]
    let kernings: [DK2KerningPair: Int]
    let pageFiles: [Int: String]
    var pages: [Int: UIImage] = [:]

    init?(name: String) {
        let fontDir = DK2AssetLoader.root.appendingPathComponent("interface/fonts")
        let url = fontDir.appendingPathComponent("\(name).fnt")
        guard let raw = try? String(contentsOf: url, encoding: .utf8) else { return nil }

        var lineHeight = 0
        var base = 0
        var pointSize = 0
        var chars: [UInt32: DK2Glyph] = [:]
        var kernings: [DK2KerningPair: Int] = [:]
        var pageFiles: [Int: String] = [:]

        for line in raw.split(whereSeparator: \.isNewline) {
            let tokens = Array(line.split(whereSeparator: { $0 == " " || $0 == "\t" }))
            guard let head = tokens.first else { continue }
            let attrs = DK2ParsedFont.attributes(tokens)
            switch head {
            case "info":
                pointSize = Int(attrs["size"] ?? "") ?? 0
            case "common":
                lineHeight = Int(attrs["lineHeight"] ?? "") ?? 0
                base = Int(attrs["base"] ?? "") ?? 0
            case "page":
                if let ident = Int(attrs["id"] ?? ""), let file = attrs["file"] {
                    pageFiles[ident] = file
                }
            case "char":
                guard let ident = UInt32(attrs["id"] ?? ""),
                      let x = Int(attrs["x"] ?? ""),
                      let y = Int(attrs["y"] ?? ""),
                      let width = Int(attrs["width"] ?? ""),
                      let height = Int(attrs["height"] ?? ""),
                      let xoffset = Int(attrs["xoffset"] ?? ""),
                      let yoffset = Int(attrs["yoffset"] ?? ""),
                      let xadvance = Int(attrs["xadvance"] ?? ""),
                      let page = Int(attrs["page"] ?? "") else { continue }
                chars[ident] = DK2Glyph(x: x, y: y, width: width, height: height,
                                        xoffset: xoffset, yoffset: yoffset,
                                        xadvance: xadvance, page: page)
            case "kerning":
                guard let first = UInt32(attrs["first"] ?? ""),
                      let second = UInt32(attrs["second"] ?? ""),
                      let amount = Int(attrs["amount"] ?? "") else { continue }
                kernings[DK2KerningPair(first: first, second: second)] = amount
            default:
                continue
            }
        }

        guard !chars.isEmpty else { return nil }
        self.lineHeight = lineHeight > 0 ? lineHeight : (pointSize > 0 ? pointSize : 16)
        self.base = base
        self.pointSize = pointSize
        self.chars = chars
        self.kernings = kernings
        self.pageFiles = pageFiles
    }

    private static func attributes(_ tokens: [Substring]) -> [String: String] {
        var result: [String: String] = [:]
        for token in tokens {
            guard let eq = token.firstIndex(of: "=") else { continue }
            let key = String(token[token.startIndex..<eq])
            var value = String(token[token.index(after: eq)...])
            if value.count >= 2, value.first == "\"", value.last == "\"" {
                value = String(value.dropFirst().dropLast())
            }
            result[key] = value
        }
        return result
    }

    func page(_ ident: Int) -> UIImage? {
        if let cached = pages[ident] { return cached }
        guard let file = pageFiles[ident] else { return nil }
        let fontDir = DK2AssetLoader.root.appendingPathComponent("interface/fonts")
        guard let image = UIImage(contentsOfFile: fontDir.appendingPathComponent(file).path) else {
            return nil
        }
        pages[ident] = image
        return image
    }
}

private struct DK2TextCacheKey: Hashable {
    let text: String
    let font: String
    let size: Int
    let color: UInt64
}

enum DK2FontRenderer {
    private static let lock = NSLock()
    private static var fonts: [String: DK2ParsedFont] = [:]
    private static var images: [DK2TextCacheKey: UIImage] = [:]

    static func image(_ text: String, font fontName: String, size: CGFloat?, color: Color) -> UIImage {
        var r: CGFloat = 0, g: CGFloat = 0, b: CGFloat = 0, a: CGFloat = 0
        UIColor(color).getRed(&r, green: &g, blue: &b, alpha: &a)
        let colorKey = (UInt64((r * 255).rounded()) << 24)
            | (UInt64((g * 255).rounded()) << 16)
            | (UInt64((b * 255).rounded()) << 8)
            | UInt64((a * 255).rounded())
        let key = DK2TextCacheKey(text: text, font: fontName,
                                  size: size.map { Int(($0 * 100).rounded()) } ?? Int.min,
                                  color: colorKey)
        lock.lock()
        if let cached = images[key] {
            lock.unlock()
            return cached
        }
        let parsed = font(for: fontName)
        let result: UIImage
        if let parsed = parsed {
            result = draw(text, font: parsed, size: size, color: color)
        } else {
            result = drawSystem(text, size: size, color: color)
        }
        images[key] = result
        lock.unlock()
        return result
    }

    private static func font(for name: String) -> DK2ParsedFont? {
        if let cached = fonts[name] { return cached }
        if let parsed = DK2ParsedFont(name: name) {
            fonts[name] = parsed
            return parsed
        }
        return nil
    }

    private static func draw(_ text: String, font: DK2ParsedFont, size: CGFloat?, color: Color) -> UIImage {
        let scale: CGFloat
        if let requested = size, requested > 0 {
            let native = CGFloat(font.pointSize > 0 ? font.pointSize : font.lineHeight)
            scale = requested / native
        } else {
            scale = 1
        }

        var placed: [DK2PlacedGlyph] = []
        var penX = 0
        var penY = 0
        var maxX = 0
        var maxY = 0
        var previous: UInt32? = nil
        let question = font.chars[63]
        let space = font.chars[32]

        for scalar in text.unicodeScalars {
            let ident = scalar.value
            if ident == 10 {
                penX = 0
                penY += font.lineHeight
                previous = nil
                continue
            }
            if ident == 13 { continue }

            let glyph = font.chars[ident] ?? question ?? space
            if let pairPrevious = previous,
               let amount = font.kernings[DK2KerningPair(first: pairPrevious, second: ident)] {
                penX += amount
            }
            let x = penX + (glyph?.xoffset ?? 0)
            let y = penY + (glyph?.yoffset ?? 0)
            let advance = glyph?.xadvance ?? (question?.xadvance ?? font.lineHeight / 2)
            placed.append(DK2PlacedGlyph(glyph: glyph, x: x, y: y, advance: advance))
            if let glyph = glyph {
                maxX = Swift.max(maxX, x + glyph.width)
                maxY = Swift.max(maxY, y + glyph.height)
            } else {
                maxX = Swift.max(maxX, x + advance)
                maxY = Swift.max(maxY, y + font.lineHeight - 2)
            }
            penX += advance
            maxX = Swift.max(maxX, penX)
            previous = ident
        }

        let pixelWidth = Swift.max(1, maxX)
        let pixelHeight = Swift.max(1, Swift.max(font.lineHeight, maxY))
        let output = CGSize(width: ceil(CGFloat(pixelWidth) * scale),
                            height: ceil(CGFloat(pixelHeight) * scale))
        let format = UIGraphicsImageRendererFormat.default()
        format.opaque = false
        format.scale = 1
        let uiColor = UIColor(color)

        return UIGraphicsImageRenderer(size: output, format: format).image { renderer in
            let context = renderer.cgContext
            context.scaleBy(x: scale, y: scale)
            for item in placed {
                guard let glyph = item.glyph else {
                    context.setStrokeColor(UIColor.white.cgColor)
                    context.setLineWidth(1)
                    context.stroke(CGRect(x: CGFloat(item.x), y: CGFloat(item.y + 1),
                                          width: CGFloat(Swift.max(1, item.advance - 1)),
                                          height: CGFloat(Swift.max(1, font.lineHeight - 3))))
                    continue
                }
                guard glyph.width > 0, glyph.height > 0,
                      let page = font.page(glyph.page),
                      let cgImage = page.cgImage else { continue }
                let sourceX = Swift.max(0, glyph.x)
                let sourceY = Swift.max(0, glyph.y)
                let sourceWidth = Swift.min(glyph.width, cgImage.width - sourceX)
                let sourceHeight = Swift.min(glyph.height, cgImage.height - sourceY)
                guard sourceWidth > 0, sourceHeight > 0,
                      let crop = cgImage.cropping(to: CGRect(x: CGFloat(sourceX), y: CGFloat(sourceY),
                                                             width: CGFloat(sourceWidth),
                                                             height: CGFloat(sourceHeight)))
                else { continue }
                context.draw(crop, in: CGRect(x: CGFloat(item.x), y: CGFloat(item.y),
                                              width: CGFloat(sourceWidth),
                                              height: CGFloat(sourceHeight)))
            }
            context.setBlendMode(.sourceIn)
            context.setFillColor(uiColor.cgColor)
            context.fill(CGRect(x: 0, y: 0,
                                width: CGFloat(pixelWidth), height: CGFloat(pixelHeight)))
            context.setBlendMode(.normal)
        }
    }

    private static func drawSystem(_ text: String, size: CGFloat?, color: Color) -> UIImage {
        let uiColor = UIColor(color)
        let attributes: [NSAttributedString.Key: Any] = [
            .font: UIFont.systemFont(ofSize: size ?? 16),
            .foregroundColor: uiColor
        ]
        let measured = (text as NSString).size(withAttributes: attributes)
        let output = CGSize(width: max(1, ceil(measured.width)),
                            height: max(1, ceil(measured.height)))
        let format = UIGraphicsImageRendererFormat.default()
        format.opaque = false
        format.scale = 1
        return UIGraphicsImageRenderer(size: output, format: format).image { _ in
            (text as NSString).draw(at: CGPoint(x: 0, y: 0), withAttributes: attributes)
        }
    }
}

struct DK2BitmapText: View {
    let text: String
    let font: String
    let size: CGFloat?
    let color: Color

    init(_ text: String,
         font: String = "Frontend14",
         size: CGFloat? = nil,
         color: Color = Color(red: 0.95, green: 0.85, blue: 0.62)) {
        self.text = text
        self.font = font
        self.size = size
        self.color = color
    }

    var body: some View {
        let image = DK2FontRenderer.image(text, font: font, size: size, color: color)
        Image(uiImage: image)
    }
}
