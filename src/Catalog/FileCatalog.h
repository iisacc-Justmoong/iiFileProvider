#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace iiFileProvider {

enum class ColorLabel { None, Red, Orange, Yellow, Green, Blue, Purple, Gray };

struct FileMetadata {
    std::string title;
    std::string description;
    std::vector<std::string> keywords;
    unsigned rating = 0; // 0 = unrated, 1..5 = stars.
    ColorLabel color = ColorLabel::None;
    std::map<std::string, std::string> fields;
    std::vector<std::string> collections; // Stable collection keys.
    bool operator==(const FileMetadata &) const = default;
};

struct CatalogFile {
    std::string key;
    std::int64_t index = 0;
    std::uint64_t revision = 0;
    std::string path; // Root-relative UTF-8 spelling; never a caller URL.
    std::uint64_t size = 0;
    std::int64_t modifiedAtNs = 0;
    std::string contentVersion;
    FileMetadata metadata;
    bool metadataPending = false;
};

struct FileFilter {
    std::string text; // Unicode word prefixes, AND semantics; never raw SQL/FTS.
    std::string pathPrefix;
    std::vector<std::string> allKeywords;
    std::vector<std::string> anyKeywords;
    std::vector<ColorLabel> colors;
    std::vector<std::string> collections; // Membership in any selected collection.
    std::optional<unsigned> minimumRating;
    std::optional<unsigned> maximumRating;
    std::optional<std::uint64_t> minimumSize;
    std::optional<std::uint64_t> maximumSize;
    std::map<std::string, std::string> fields; // Exact key/value pairs, AND semantics.
    bool operator==(const FileFilter &) const = default;
};

struct FileCursor {
    std::string catalog;
    std::uint64_t revision = 0;
    std::int64_t afterIndex = 0;
    std::string filter;
};

// Search projection: large descriptions, fields and membership documents are
// loaded only by lookup(). Listing cost is independent of payload/annotation size.
struct CatalogFileSummary {
    std::string key;
    std::int64_t index = 0;
    std::uint64_t revision = 0;
    std::string path;
    std::uint64_t size = 0;
    std::int64_t modifiedAtNs = 0;
    std::string contentVersion;
    std::string title;
    unsigned rating = 0;
    ColorLabel color = ColorLabel::None;
    bool metadataPending = false;
};

struct FilePage {
    std::vector<CatalogFileSummary> files;
    std::optional<FileCursor> next;
};

struct FileCollection {
    std::string key;
    std::string name;
    bool operator==(const FileCollection &) const = default;
};

struct SavedFileFilter {
    std::string key;
    std::string name;
    FileFilter filter;
};

struct MetadataWrite {
    CatalogFile file;
    bool metadataWritten = false;
    std::string metadataError;
};

struct FileRegistration {
    std::string path;
    // Omission imports an existing iiFileProvider companion, or preserves the
    // current annotation. Explicit metadata is a complete replacement.
    std::optional<FileMetadata> metadata;
};

struct FileChunk {
    std::vector<std::byte> bytes;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::string contentVersion;
    bool eof = false;
};

// Native C++23 file creation/provision and persistent classification. SQLite
// indexes metadata, not media contents. The companion <file>.iisacc.json travels
// with exported files; the catalog journals pending companion writes durably.
// One instance serializes calls. Independent instances/processes use WAL and
// optimistic revisions. No Qt, renderer, account, network or product dependency.
class FileCatalog final {
public:
    enum class Access { ReadOnly, ReadWrite, Create };
    static constexpr std::size_t maximumPage = 1024;
    static constexpr std::size_t maximumBatch = 1024;
    static constexpr std::size_t maximumChunk = 1024 * 1024;
    static constexpr std::size_t maximumMetadataBytes = 256 * 1024;

    explicit FileCatalog(const std::filesystem::path &root, Access access = Access::Create);
    ~FileCatalog();
    FileCatalog(const FileCatalog &) = delete;
    FileCatalog &operator=(const FileCatalog &) = delete;
    [[nodiscard]] const std::filesystem::path &root() const noexcept;

    [[nodiscard]] MetadataWrite registerFile(const std::string &relativePath,
        const std::optional<FileMetadata> &metadata = {});
    // One transaction, bounded memory, no payload reads. Companion publication
    // is explicit through publishMetadata(), so large inventories stay bounded.
    [[nodiscard]] std::vector<CatalogFile> registerFiles(const std::vector<FileRegistration> &files);
    [[nodiscard]] MetadataWrite createFile(const std::string &relativePath,
        std::span<const std::byte> bytes, const FileMetadata &metadata = {});
    [[nodiscard]] MetadataWrite createFileWith(const std::string &relativePath,
        const std::function<void(std::ostream &)> &encode, const FileMetadata &metadata = {});
    [[nodiscard]] MetadataWrite importFile(const std::filesystem::path &source,
        const std::string &relativePath, const std::optional<FileMetadata> &metadata = {});
    [[nodiscard]] MetadataWrite updateMetadata(const std::string &key,
        std::uint64_t expectedRevision, const FileMetadata &metadata);
    [[nodiscard]] std::optional<CatalogFile> lookup(const std::string &key) const;
    [[nodiscard]] std::optional<CatalogFile> lookupPath(const std::string &relativePath) const;
    [[nodiscard]] FileChunk read(const std::string &key, std::uint64_t offset = 0,
        std::size_t maximumBytes = 256 * 1024, const std::string &expectedContentVersion = {}) const;
    [[nodiscard]] FilePage search(const FileFilter &filter = {},
        const std::optional<FileCursor> &cursor = {}, std::size_t limit = 256) const;
    [[nodiscard]] std::uint64_t count() const;

    [[nodiscard]] FileCollection createCollection(const std::string &name);
    void renameCollection(const std::string &key, const std::string &name);
    void removeCollection(const std::string &key);
    [[nodiscard]] std::vector<FileCollection> collections() const;
    [[nodiscard]] SavedFileFilter saveFilter(const std::string &name, const FileFilter &filter);
    [[nodiscard]] std::optional<SavedFileFilter> savedFilter(const std::string &key) const;
    [[nodiscard]] std::vector<SavedFileFilter> savedFilters() const;
    void removeFilter(const std::string &key);
    [[nodiscard]] FilePage searchSaved(const std::string &key,
        const std::optional<FileCursor> &cursor = {}, std::size_t limit = 256) const;

    [[nodiscard]] std::size_t publishMetadata(std::size_t limit = 256);
    [[nodiscard]] std::uint64_t pendingMetadata() const;
    // Portable, credential-independent classification; does not rewrite payloads.
    [[nodiscard]] std::string metadataDocument(const std::string &key) const;
    // Verified native details + embedded preview. Empty means absent/stale or
    // a build with the native presentation component explicitly disabled.
    [[nodiscard]] std::string presentationDocument(const std::string &key) const;
    // Read-only portable classification for one existing native asset. No index
    // is opened or created and no payload is read. Empty means unclassified;
    // malformed, oversized, redirected or concurrently changed data throws.
    // Returns validated fields only, without the catalog-local path.
    [[nodiscard]] static std::string classificationDocument(const std::filesystem::path &asset);

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace iiFileProvider
