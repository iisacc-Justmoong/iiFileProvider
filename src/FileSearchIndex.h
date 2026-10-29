#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace iiFileProvider {

// A caller-supplied, root-relative identity and provider-derived metadata.
// The index performs no filesystem access and never reads or writes a payload.
struct FileSearchEntry {
    std::string path, title, fileKey, mimeType, classification, searchTerms;
    std::uint64_t size = 0;
    bool hasClassification = false, hasPresentation = false, metadataPending = false;
};

// Thread-safe, process-local snapshot index. A new instance starts empty.
// Duplicate paths merge presentation information with first classification
// taking precedence. Queries are literal, whitespace-separated FTS prefixes.
class FileSearchIndex final {
public:
    FileSearchIndex();
    ~FileSearchIndex();
    FileSearchIndex(const FileSearchIndex&) = delete;
    FileSearchIndex& operator=(const FileSearchIndex&) = delete;
    [[nodiscard]] bool upsert(const FileSearchEntry& entry);
    [[nodiscard]] bool contains(std::string_view path) const;
    [[nodiscard]] std::vector<FileSearchEntry> search(std::string_view query = {}, std::size_t limit = 100) const;
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace iiFileProvider
