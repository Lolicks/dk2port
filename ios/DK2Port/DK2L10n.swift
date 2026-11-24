import Foundation

final class DK2L10n {
    static let shared = DK2L10n()

    private var tables: [String: [Int: String]] = [:]

    init() {
        for name in ["Text", "Speech"] {
            guard let url = DK2AssetLoader.root.appendingPathComponent("interface/\(name).properties") as URL?,
                  let raw = try? String(contentsOf: url, encoding: .utf8) else { continue }
            var table: [Int: String] = [:]
            for line in raw.split(separator: "\n", omittingEmptySubsequences: false) {
                guard let eq = line.firstIndex(of: "="),
                      let key = Int(line[line.startIndex..<eq]) else { continue }
                var value = String(line[line.index(after: eq)...])
                if value.hasSuffix("\r") { value.removeLast() }
                table[key] = Self.unescape(value)
            }
            tables[name] = table
        }
    }

    private static func unescape(_ value: String) -> String {
        var out = ""
        var iterator = value.makeIterator()
        while let ch = iterator.next() {
            if ch == "\\", let next = iterator.next() {
                switch next {
                case "n": out.append("\n")
                case "t": out.append("\t")
                case "r": out.append("\r")
                default: out.append(next)
                }
            } else {
                out.append(ch)
            }
        }
        return out
    }

    subscript(id: Int) -> String {
        tables["Text"]?[id] ?? "{\(id)}"
    }

    func string(_ id: Int) -> String { self[id] }

    func speech(_ id: Int) -> String {
        tables["Speech"]?[id] ?? "{\(id)}"
    }

    func format(_ id: Int, _ params: [Int: String]) -> String {
        var result = self[id]
        for (key, value) in params {
            result = result.replacingOccurrences(of: "%\(key)", with: value)
        }
        return result.replacingOccurrences(of: "%%", with: "%")
    }

    func resolve(_ text: String) -> String {
        let pattern = #"\$\{(?:menu|speech|mpd[1-6])\.(\d+)\}"#
        guard let regex = try? NSRegularExpression(pattern: pattern) else { return text }
        let ns = text as NSString
        var result = text
        let matches = regex.matches(in: text, range: NSRange(location: 0, length: ns.length))
        for match in matches.reversed() {
            guard match.numberOfRanges > 1,
                  let id = Int(ns.substring(with: match.range(at: 1))) else { continue }
            let replacement = self[id]
            let swiftRange = Range(match.range, in: result)!
            result.replaceSubrange(swiftRange, with: replacement)
        }
        return result
    }
}
