import Foundation
#if canImport(ImageIO) && canImport(CryptoKit)
import ImageIO
import CryptoKit
#endif
#if canImport(Darwin)
import Darwin
#else
import Glibc
#endif

/// Native compiled storage owner for Swift apps and platform filesystem bridges.
public enum IisaccFileProvider {
    public struct StorageError: LocalizedError {
        public let message: String
        public var errorDescription: String? { message }
    }
    public static func write(_ data: Data, to url: URL, options: Data.WritingOptions = .atomic) throws {
        try data.write(to: url, options: options)
        presentLater(url)
    }
    public static func createFile(atPath path: String, contents: Data?, attributes: [FileAttributeKey: Any]? = nil) -> Bool {
        let created = FileManager.default.createFile(atPath: path, contents: contents, attributes: attributes)
        if created { presentLater(URL(fileURLWithPath: path)) }
        return created
    }
    public static func createDirectory(at url: URL, withIntermediateDirectories recursive: Bool, attributes: [FileAttributeKey: Any]? = nil) throws {
        try FileManager.default.createDirectory(at: url, withIntermediateDirectories: recursive, attributes: attributes)
    }
    public static func copyItem(at source: URL, to destination: URL) throws {
        try FileManager.default.copyItem(at: source, to: destination)
        presentLater(destination)
    }
#if canImport(Darwin)
    /// Finalization hook for descriptor-based native filesystem callbacks.
    @discardableResult public static func closeAfterWriting(_ descriptor: Int32) -> Int32 {
        var path = [CChar](repeating: 0, count: Int(MAXPATHLEN))
        let found = path.withUnsafeMutableBufferPointer { fcntl(descriptor, F_GETPATH, $0.baseAddress!) == 0 }
        let result = Darwin.close(descriptor)
        let savedError = errno
        if result == 0 && found { presentLater(URL(fileURLWithPath: String(cString: path))) }
        errno = savedError
        return result
    }
#endif
    public static func open(_ path: String, _ flags: Int32, _ mode: mode_t = 0o666) -> Int32 {
#if canImport(Darwin)
        Darwin.open(path, flags, mode)
#else
        Glibc.open(path, flags, mode)
#endif
    }
    public static func open(_ path: UnsafePointer<CChar>, _ flags: Int32, _ mode: mode_t = 0o666) -> Int32 {
#if canImport(Darwin)
        Darwin.open(path, flags, mode)
#else
        Glibc.open(path, flags, mode)
#endif
    }
    public static func openAt(_ directory: Int32, _ path: UnsafePointer<CChar>, _ flags: Int32, _ mode: mode_t = 0o666) -> Int32 {
        openat(directory, path, flags, mode)
    }
    public static func openAt(_ directory: Int32, _ path: String, _ flags: Int32, _ mode: mode_t = 0o666) -> Int32 {
        path.withCString { openat(directory, $0, flags, mode) }
    }
    public static func makeDirectoryAt(_ directory: Int32, _ path: UnsafePointer<CChar>, _ mode: mode_t) -> Int32 {
        mkdirat(directory, path, mode)
    }
#if canImport(Darwin)
    // Exclusive publication retains the granted-root/no-follow traversal used by
    // Finder document creation. The domain supplies titles, extensions and bytes.
    public static func createDocument(root: String, relative: String, title: String, fileExtension: String, content: Data) throws -> URL {
        let components = relative.split(separator: "/").map(String.init)
        guard root.hasPrefix("/"), !relative.hasPrefix("/"), !components.contains(".."),
              !title.isEmpty, !fileExtension.isEmpty,
              !title.contains("/"), !title.contains("\0"),
              !fileExtension.contains("/"), !fileExtension.contains("\0"),
              !root.contains("\0"), !relative.contains("\0") else {
            throw StorageError(message: "A granted root and a safe relative directory are required.")
        }
        var directory = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
        guard directory >= 0 else { throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO) }
        defer { close(directory) }
        for component in components where component != "." {
            let next = openat(directory, component, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
            guard next >= 0 else { throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO) }
            close(directory)
            directory = next
        }
        // Publish only a complete document. The hidden staging name is never a final file.
        let temporary = ".alright-" + UUID().uuidString + ".tmp"
        let file = openat(directory, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0o600)
        guard file >= 0 else { throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO) }
        defer { close(file); unlinkat(directory, temporary, 0) }
        try content.withUnsafeBytes { bytes in
            var offset = 0
            while offset < bytes.count {
                let count = Darwin.write(file, bytes.baseAddress!.advanced(by: offset), bytes.count - offset)
                if count < 0 && errno == EINTR { continue }
                guard count > 0 else { throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO) }
                offset += count
            }
        }
        guard fchmod(file, 0o644) == 0, fsync(file) == 0 else {
            throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO)
        }
        for index in 1...10_000 {
            let suffix = index == 1 ? "" : " \(index)"
            let name = "\(title)\(suffix).\(fileExtension)"
            if renameatx_np(directory, temporary, directory, name, UInt32(RENAME_EXCL)) == 0 {
                let result = URL(fileURLWithPath: root).appendingPathComponent(relative).appendingPathComponent(name).standardizedFileURL
                presentLater(result)
                return result
            }
            if errno != EEXIST { throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO) }
        }
        throw StorageError(message: "Too many files named \(title) in this folder.")
    }
#endif

    private static let previewQueue = DispatchQueue(label: "com.iisacc.file-provider.presentation", qos: .utility)
    private static let previewGroup = DispatchGroup()
    private static let previewLock = NSLock()
    private static let presentationOwnership = NSRecursiveLock()
    // Every access is serialized by previewLock, including background removal.
    nonisolated(unsafe) private static var pendingPreviews = Set<String>()
    public static func isControlFile(_ name: String) -> Bool {
        let name = name.lowercased()
        return name == ".iisacc-temporary.lock" || name == ".iisacc-file-provider"
            || name.hasPrefix(".iifile-stage-") || name.hasSuffix(".iisacc-preview")
            || name.hasSuffix(".iisacc.json") || name.hasSuffix(".iisacc-lock")
    }
    /// The source-only Swift adapter is compiled natively, including in iOS
    /// extensions without a C++ dylib. It writes the same schema as Native.
    public static func presentLater(_ url: URL) {
        guard url.isFileURL else { return }
        let path = url.standardizedFileURL.path
        let leaf = url.lastPathComponent.lowercased()
        guard !url.pathComponents.contains(where: { $0.hasPrefix(".") }),
              ![".iisacc-preview", ".iisacc.json", ".lock", ".tmp", ".log", ".db", ".sqlite", ".sqlite3", "-wal", "-shm"].contains(where: { leaf.hasSuffix($0) }) else { return }
        previewLock.lock()
        guard !pendingPreviews.contains(path) else { previewLock.unlock(); return }
        if pendingPreviews.count >= 4096 {
            previewLock.unlock(); _ = try? preparePresentation(url); return
        }
        pendingPreviews.insert(path); previewLock.unlock()
        previewGroup.enter()
        previewQueue.async {
            previewLock.lock(); pendingPreviews.remove(path); previewLock.unlock()
            defer { previewGroup.leave() }
            _ = try? preparePresentation(URL(fileURLWithPath: path))
        }
    }
    public static func waitForPresentations(timeout: TimeInterval = 15) -> Bool {
        previewGroup.wait(timeout: .now() + max(0, timeout)) == .success
    }
    @discardableResult public static func preparePresentation(_ url: URL) throws -> [String: Any] {
        presentationOwnership.lock(); defer { presentationOwnership.unlock() }
#if canImport(Darwin) && canImport(ImageIO) && canImport(CryptoKit)
        let source = url.standardizedFileURL
        guard source.isFileURL, source.resolvingSymlinksInPath() == source,
              !source.lastPathComponent.hasPrefix("."), !source.lastPathComponent.hasSuffix(".iisacc-preview") else {
            throw StorageError(message: "Presentation requires an original local file.")
        }
        func state() throws -> stat {
            var result = stat()
            guard lstat(source.path, &result) == 0, (result.st_mode & S_IFMT) == S_IFREG else {
                throw StorageError(message: "The presentation source is unavailable or redirected.")
            }
            return result
        }
        func stamp(_ value: stat) -> String {
            "\(value.st_dev):\(value.st_ino):\(value.st_size):\(value.st_mtimespec.tv_sec):\(value.st_mtimespec.tv_nsec)"
        }
        func changeToken(_ value: stat) -> String { "\(value.st_dev):\(value.st_ino):\(value.st_ctimespec.tv_sec):\(value.st_ctimespec.tv_nsec)" }
        func fingerprint(_ value: stat) throws -> String {
            let file = try FileHandle(forReadingFrom: source); defer { try? file.close() }
            let size = UInt64(value.st_size)
            let head = Int(min(size, 32 * 1024))
            var sample = try file.read(upToCount: head) ?? Data()
            if size > UInt64(head) {
                let tail = min(size - UInt64(head), 32 * 1024)
                try file.seek(toOffset: size - tail)
                sample.append(try file.read(upToCount: Int(tail)) ?? Data())
            }
            guard stamp(try state()) == stamp(value) else { throw StorageError(message: "The source changed while reading its details.") }
            return "1:" + stamp(value) + ":" + SHA256.hash(data: sample).map { String(format: "%02x", $0) }.joined()
        }
        let before = try state(), sourceVersion = try fingerprint(try state())
        let companion = source.deletingLastPathComponent().appendingPathComponent("." + source.lastPathComponent + ".iisacc-preview")
        func readCompanion() throws -> Data {
            let descriptor = open(companion.path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC)
            guard descriptor >= 0 else { throw StorageError(message: "The companion is missing or redirected.") }
            let input = FileHandle(fileDescriptor: descriptor, closeOnDealloc: true)
            defer { try? input.close() }
            var info = stat()
            guard fstat(descriptor, &info) == 0, (info.st_mode & S_IFMT) == S_IFREG,
                  info.st_size >= 0, info.st_size <= 2 * 1024 * 1024 else {
                throw StorageError(message: "The companion exceeds its byte limit or is not a file.")
            }
            let data = try input.read(upToCount: Int(info.st_size) + 1) ?? Data()
            guard data.count == Int(info.st_size) else { throw StorageError(message: "The companion changed while reading.") }
            return data
        }
        func writeCompanion(_ data: Data) throws {
            // Foundation's anonymous .atomic staging names can be observed by
            // Files/FSKit enumeration. Keep all derived stages in the owner's
            // reserved namespace and expose only a complete companion.
            let lease = companion.deletingLastPathComponent().appendingPathComponent(".iisacc-temporary.lock")
            let leaseDescriptor = open(lease.path, O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0o600)
            guard leaseDescriptor >= 0 else { throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO) }
            defer { close(leaseDescriptor) }
            var leaseInfo = stat()
            guard fstat(leaseDescriptor, &leaseInfo) == 0, (leaseInfo.st_mode & S_IFMT) == S_IFREG,
                  leaseInfo.st_size == 0, flock(leaseDescriptor, LOCK_SH) == 0 else {
                throw StorageError(message: "Cannot acquire the provider's temporary-file lease.")
            }
            defer { flock(leaseDescriptor, LOCK_UN) }
            let temporary = companion.deletingLastPathComponent().appendingPathComponent(".iifile-stage-" + UUID().uuidString + ".iisacc-preview")
            let descriptor = open(temporary.path, O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW | O_CLOEXEC, 0o600)
            guard descriptor >= 0 else { throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO) }
            defer { close(descriptor); unlink(temporary.path) }
            try data.withUnsafeBytes { bytes in
                var offset = 0
                while offset < bytes.count {
                    let count = Darwin.write(descriptor, bytes.baseAddress!.advanced(by: offset), bytes.count - offset)
                    if count < 0 && errno == EINTR { continue }
                    guard count > 0 else { throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO) }
                    offset += count
                }
            }
            guard fsync(descriptor) == 0, rename(temporary.path, companion.path) == 0 else {
                throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO)
            }
        }
        if let old = try? readCompanion(),
           let document = try? JSONSerialization.jsonObject(with: old) as? [String: Any],
           document["format"] as? String == "iiFileProvider.Presentation", document["schema"] as? Int == 1,
           document["sourceVersion"] as? String == sourceVersion,
           document["sourceChangeToken"] as? String == changeToken(before) { return document }
        let file = try FileHandle(forReadingFrom: source); defer { try? file.close() }
        let head = try file.read(upToCount: 4096) ?? Data()
        let ext = source.pathExtension.lowercased()
        var mime = "application/octet-stream", properties = [String: String]()
        var preview = Data(), previewMime = "image/svg+xml", kind = "information", width = 512, height = 512
        if before.st_size <= 32 * 1024 * 1024, let image = CGImageSourceCreateWithURL(source as CFURL, nil) {
            let values = CGImageSourceCopyPropertiesAtIndex(image, 0, nil) as? [CFString: Any] ?? [:]
            if let value = values[kCGImagePropertyPixelWidth] as? NSNumber { properties["width"] = value.stringValue }
            if let value = values[kCGImagePropertyPixelHeight] as? NSNumber { properties["height"] = value.stringValue }
            if let imageType = CGImageSourceGetType(image) as String? {
                mime = ["public.png": "image/png", "public.jpeg": "image/jpeg", "public.tiff": "image/tiff", "public.heic": "image/heic", "com.compuserve.gif": "image/gif"][imageType] ?? "image/" + ext
            }
            let options: [CFString: Any] = [kCGImageSourceCreateThumbnailFromImageAlways: true,
                kCGImageSourceCreateThumbnailWithTransform: true, kCGImageSourceThumbnailMaxPixelSize: 512]
            if let thumbnail = CGImageSourceCreateThumbnailAtIndex(image, 0, options as CFDictionary) {
                let data = NSMutableData()
                if let destination = CGImageDestinationCreateWithData(data, "public.png" as CFString, 1, nil) {
                    CGImageDestinationAddImage(destination, thumbnail, nil)
                    if CGImageDestinationFinalize(destination), data.length <= 1024 * 1024 {
                        preview = data as Data; previewMime = "image/png"; kind = "image"
                        width = thumbnail.width; height = thumbnail.height
                    }
                }
            }
        }
        if preview.isEmpty {
            let content = String(data: head, encoding: .utf8)
            let readable = content != nil && !head.contains(where: { $0 < 32 && $0 != 9 && $0 != 10 && $0 != 13 })
            if readable { mime = ext == "json" ? "application/json" : "text/plain"; kind = "text"; properties["textEncoding"] = "UTF-8" }
            func escape(_ text: String) -> String {
                text.replacingOccurrences(of: "&", with: "&amp;").replacingOccurrences(of: "<", with: "&lt;")
                    .replacingOccurrences(of: ">", with: "&gt;").replacingOccurrences(of: "\"", with: "&quot;")
                    .replacingOccurrences(of: "'", with: "&apos;")
            }
            var svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"512\" height=\"512\" viewBox=\"0 0 512 512\"><rect width=\"512\" height=\"512\" fill=\"#f7f7f8\"/>"
            svg += "<text x=\"24\" y=\"38\" font-family=\"sans-serif\" font-size=\"18\" fill=\"#202124\">" + escape(String(source.lastPathComponent.prefix(100))) + "</text>"
            let lines = readable ? String((content ?? "").prefix(1500)).components(separatedBy: .newlines) : ["Content preview unavailable for this format."]
            for (index, line) in lines.prefix(23).enumerated() {
                svg += "<text x=\"24\" y=\"\(96 + index * 17)\" font-family=\"monospace\" font-size=\"12\" fill=\"#303134\">" + escape(String(line.prefix(66))) + "</text>"
            }
            preview = Data((svg + "</svg>").utf8)
        }
        let document: [String: Any] = ["format": "iiFileProvider.Presentation", "schema": 1, "generator": "iiFileProvider.Swift/0.7", "sourceVersion": sourceVersion,
            "details": ["name": source.lastPathComponent, "extension": ext.isEmpty ? "" : "." + ext, "mimeType": mime,
                "size": UInt64(before.st_size), "modifiedAtNs": Int64(before.st_mtimespec.tv_sec) * 1_000_000_000 + Int64(before.st_mtimespec.tv_nsec), "properties": properties],
            "preview": ["mimeType": previewMime, "kind": kind, "width": width, "height": height, "encoding": "base64", "data": preview.base64EncodedString(),
                "sha256": SHA256.hash(data: preview).map { String(format: "%02x", $0) }.joined()]]
        let encoded = try JSONSerialization.data(withJSONObject: document, options: [.sortedKeys])
        guard encoded.count <= 2 * 1024 * 1024, try fingerprint(try state()) == sourceVersion else {
            throw StorageError(message: "The source changed before preview publication.")
        }
        if FileManager.default.fileExists(atPath: companion.path) {
            let info = try FileManager.default.attributesOfItem(atPath: companion.path)
            guard info[.type] as? FileAttributeType == .typeRegular,
                  let previous = try JSONSerialization.jsonObject(with: readCompanion()) as? [String: Any],
                  previous["format"] as? String == "iiFileProvider.Presentation" else {
                throw StorageError(message: "An unrelated or redirected companion is preserved.")
            }
        }
        try writeCompanion(encoded)
        encoded.withUnsafeBytes { bytes in
            _ = source.path.withCString { setxattr($0, "com.iisacc.file.presentation", bytes.baseAddress, bytes.count, 0, XATTR_NOFOLLOW) }
        }
        var portable = document
        portable["sourceChangeToken"] = changeToken(try state())
        try writeCompanion(JSONSerialization.data(withJSONObject: portable, options: [.sortedKeys]))
        guard try fingerprint(try state()) == sourceVersion else { throw StorageError(message: "The source changed during preview publication.") }
        return portable
#else
        throw StorageError(message: "Use the native iiFileProvider presentation component on this platform.")
#endif
    }
}
