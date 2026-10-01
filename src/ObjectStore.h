#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace iiFileProvider {

// Exact, credential-free output of FileAuthor::toJson or Authorship::dump.
// An empty document means unknown provenance, never "the importer is creator".
struct ObjectMetadata {
    std::string format;
    std::string payload;
    bool operator==(const ObjectMetadata &) const = default;
};

// Persistent attribution, never a login token or an authentication claim.
struct ObjectAuthor {
    std::string origin;
    std::string subject;
    std::string displayName;
    ObjectMetadata profile;
    bool operator==(const ObjectAuthor &) const = default;
};

struct ObjectRecord {
    std::string key;
    std::int64_t indexKey = 0;
    std::uint64_t version = 0;
    std::string path;
    std::string sha256;
    std::uint64_t size = 0;
    ObjectAuthor author;
    std::string sessionKey;
    std::int64_t recordedAtNs = 0;
    std::string operation;
    bool deleted = false;
    std::string parentValidationKey;
    std::string validationKey;
    ObjectMetadata authorship;
    std::string sourceStamp;
    std::uint32_t schemaVersion = 2;
};

struct ObjectWriteOptions {
    // Omission preserves existing authorship; explicit empty clears to unknown.
    std::optional<ObjectMetadata> authorship;
    std::string sourceStamp;
    std::stop_token cancellation;
};

struct ObjectImport {
    std::filesystem::path source;
    std::string logicalPath;
    ObjectWriteOptions options;
};

// Compact persistent traversal projection, atomically updated with each head.
// Full author documents and journal/payload are fetched on demand; whole-index
// scans neither join history nor copy those potentially large data.
struct ObjectIndexEntry {
    std::string key;
    std::int64_t indexKey=0;
    std::uint64_t version=0;
    std::string path;
    std::string sha256;
    std::uint64_t size=0;
    std::string validationKey;
    std::string sessionKey;
    bool deleted=false;
    std::string authorOrigin;
    std::string authorSubject;
    bool operator==(const ObjectIndexEntry &) const = default;
};

// A reversible binary diff. Hashes address immutable chunk bytes in the package.
// An empty hash denotes absence (insertion/deletion); offset is a byte offset.
struct ObjectChange {
    std::uint64_t offset = 0;
    std::string beforeHash;
    std::string afterHash;
    std::uint64_t beforeSize = 0;
    std::uint64_t afterSize = 0;
};

struct ObjectSession {
    std::string key;
    ObjectAuthor author;
    std::string device;
    std::string description;
    std::int64_t startedAtNs = 0;
    std::int64_t endedAtNs = 0;
};

// Qt-independent C++23 storage owner. The directory contains a transactional
// SQLite object package; the source tree remains an ordinary, unmodified tree.
// One instance serializes its connection. Separate instances support WAL readers.
class ObjectStore final {
public:
    enum class Access { ReadOnly, ReadWrite, Create };
    static constexpr std::size_t chunkBytes = 1024 * 1024;
    static constexpr std::size_t maximumBatchFiles = 256;
    static constexpr std::uint64_t maximumBatchBytes = 16ull * 1024 * 1024;
    static constexpr std::size_t maximumBatchMetadataBytes = 16 * 1024 * 1024;
    static constexpr int defaultWalAutoCheckpointPages = 1000;
    explicit ObjectStore(const std::filesystem::path &directory,
                         std::string containerKey, bool create = false);
    ObjectStore(const std::filesystem::path &directory, std::string containerKey,
                bool create, int walAutoCheckpointPages);
    // ReadOnly accepts only the current schema and never migrates or configures
    // writer checkpoints. It cannot create a package or mutate database state.
    ObjectStore(const std::filesystem::path &directory, std::string containerKey,
                Access access, int walAutoCheckpointPages = defaultWalAutoCheckpointPages);
    ~ObjectStore();
    ObjectStore(const ObjectStore &) = delete;
    ObjectStore &operator=(const ObjectStore &) = delete;

    ObjectSession beginSession(ObjectAuthor author, std::string device, std::string description);
    void endSession(const std::string &sessionKey);
    ObjectSession session(const std::string &sessionKey) const;
    const std::filesystem::path &directory() const noexcept;

    ObjectRecord importFile(const std::filesystem::path &source, std::string logicalPath,
                            const std::string &sessionKey, const ObjectWriteOptions &options = {});
    // Small-file ingestion: at most 256 files / 16 MiB payload and 16 MiB
    // author/provenance JSON (including repeated actor profiles), one commit.
    // Bounded workers read payloads; the owning thread publishes in input order.
    // All records become visible together; any failure rolls back the batch.
    std::vector<ObjectRecord> importFiles(const std::vector<ObjectImport> &files,
                                          const std::string &sessionKey);
    ObjectRecord reviseFile(const std::string &key, const std::filesystem::path &source,
                            std::uint64_t expectedVersion, const std::string &sessionKey,
                            const ObjectWriteOptions &options = {});
    ObjectRecord move(const std::string &key, std::string logicalPath,
                      std::uint64_t expectedVersion, const std::string &sessionKey);
    ObjectRecord erase(const std::string &key, std::uint64_t expectedVersion,
                       const std::string &sessionKey);

    std::optional<ObjectRecord> lookup(const std::string &key) const;
    std::optional<ObjectRecord> lookupPath(const std::string &logicalPath) const;
    // Keyset pagination: no recursive filesystem scan and no OFFSET pagination.
    std::vector<ObjectRecord> scan(std::int64_t afterIndexKey = 0, std::size_t limit = 256,
                                  bool includeDeleted = false) const;
    std::vector<ObjectIndexEntry> index(std::int64_t afterIndexKey = 0, std::size_t limit = 1024,
                                       bool includeDeleted = false) const;
    std::vector<ObjectRecord> history(const std::string &key, std::uint64_t afterVersion = 0,
                                     std::size_t limit = 256) const;
    std::vector<ObjectChange> diff(const std::string &key, std::uint64_t version) const;
    // Schema/integrity evidence only; not a signature or authorization credential.
    bool validate(const std::string &key, std::uint64_t version, bool payload = true) const;
    // Exclusive creation: never overwrites an existing file or restores a tombstone.
    void extract(const std::string &key, std::uint64_t version,
                 const std::filesystem::path &destination) const;
    std::uint64_t count(bool includeDeleted = false) const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace iiFileProvider
