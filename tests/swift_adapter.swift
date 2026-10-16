import Foundation

@main struct ProviderSwiftTest {
    static func check(_ value: Bool) {
        precondition(value, "Native Swift provider contract failed")
    }
    static func main() throws {
        let root = URL(fileURLWithPath: CommandLine.arguments[1]).appendingPathComponent("swift-" + UUID().uuidString)
        try IisaccFileProvider.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }
        let first = try IisaccFileProvider.createDocument(root: root.path, relative: "", title: "문서", fileExtension: "txt", content: Data("first".utf8))
        let second = try IisaccFileProvider.createDocument(root: root.path, relative: "", title: "문서", fileExtension: "txt", content: Data("second".utf8))
        check(first != second)
        check(try Data(contentsOf: first) == Data("first".utf8))
        check(try Data(contentsOf: second) == Data("second".utf8))
        try IisaccFileProvider.write(Data("updated".utf8), to: first)
        check(try Data(contentsOf: first) == Data("updated".utf8))
        check(IisaccFileProvider.waitForPresentations())
        let details = try IisaccFileProvider.preparePresentation(first)
        check(details["format"] as? String == "iiFileProvider.Presentation")
        check((details["preview"] as? [String: Any])?["kind"] as? String == "text")
        do {
            _ = try IisaccFileProvider.createDocument(root: root.path, relative: "..", title: "bad", fileExtension: "txt", content: Data())
            preconditionFailure("Root traversal was accepted")
        } catch {}
        do {
            _ = try IisaccFileProvider.createDocument(root: root.path, relative: "", title: "../bad", fileExtension: "txt", content: Data())
            preconditionFailure("Filename traversal was accepted")
        } catch {}
    }
}
