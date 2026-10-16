#pragma once
#include "iiFileProviderNativeExport.h"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace iiFileProvider::native {
struct FileDetails {
    std::string name, extension, mimeType;
    std::uint64_t size = 0;
    std::int64_t modifiedAtNs = 0;
    std::map<std::string, std::string> properties;
};
struct FilePreview {
    std::string mimeType, kind, bytes; // Binary bytes; kind: image/text/audio/information.
    unsigned width = 0, height = 0;
};
struct PresentationRecord {
    FileDetails details;
    FilePreview preview;
    std::string sourceVersion, document;
    bool embedded = false; // Native extended attribute / NTFS stream succeeded.
    bool cacheHit = false;
};
// Native C++23 file presentation. The source data fork is never rewritten.
// A single portable companion embeds details and bounded preview bytes; native
// attributes additionally carry the same document where supported. Unsupported
// media gets an information card, never a claimed content rendering.
class IIFILEPROVIDER_NATIVE_EXPORT Presentation final {
public:
    using Renderer = std::function<std::optional<FilePreview>(const std::filesystem::path &,
        std::map<std::string, std::string> &properties)>;
    static constexpr std::uint64_t maximumSourceBytes = 32ull * 1024 * 1024;
    static constexpr std::size_t maximumDocumentBytes = 2 * 1024 * 1024;
    static constexpr unsigned maximumPreviewExtent = 512;
    [[nodiscard]] static PresentationRecord prepare(const std::filesystem::path &);
    // A domain SDK registers its existing read-only decoder through this lower
    // interface. Empty unregisters; the provider never links that SDK upward.
    static void setRenderer(std::string extension, Renderer);
    // Document/renderer SDKs supply their own PNG through this lower interface.
    // The provider neither imports document SDKs nor opens documents for editing.
    [[nodiscard]] static PresentationRecord attach(const std::filesystem::path &, const FilePreview &,
        const std::map<std::string, std::string> &properties = {});
    [[nodiscard]] static std::optional<PresentationRecord> load(const std::filesystem::path &);
    [[nodiscard]] static std::filesystem::path companionPath(const std::filesystem::path &);
    [[nodiscard]] static bool isControlFile(const std::filesystem::path &) noexcept;
    // Native ctime/change-time, normalized over this process's own attribute
    // writes. GUI thumbnail keys remain stable while first previews are made.
    [[nodiscard]] static std::string cacheChangeToken(const std::filesystem::path &);
    // Normalize ctime observed through a pinned native stat/handle without path
    // lookup. Device/volume and the native 64-bit file index identify the file;
    // timestamps use Unix seconds plus nanoseconds. Only our own attribute
    // publications are normalized; external changes retain their native token.
    [[nodiscard]] static std::string normalizeChangeTime(std::uint64_t device, std::uint64_t fileIndex,
        std::int64_t seconds, std::int64_t nanoseconds);
    // Complete writes enqueue here. A single compiled worker coalesces requests,
    // joins at normal shutdown, and never makes successful file saves fail.
    static void enqueue(const std::filesystem::path &) noexcept;
    [[nodiscard]] static bool waitForIdle(std::chrono::milliseconds timeout);
    // Lifecycle barrier before deleting a file or a caller-owned temporary tree.
    // Cancels queued work and joins an in-flight write beneath this path.
    static void cancelPending(const std::filesystem::path &);
    static void removeCompanion(const std::filesystem::path &);
    // Recursive native intake: no symlinks, hidden/private trees or control
    // companions. Two stable observations precede external-file enrichment.
    // The root's native identity is retained and checked on every scan slice.
    static void watch(const std::filesystem::path &root,
        const std::vector<std::filesystem::path> &relativeDirectories,
        std::chrono::milliseconds interval = std::chrono::seconds(2),
        std::function<bool()> available = {});
    static void unwatch(const std::filesystem::path &root);
};
} // namespace iiFileProvider::native
